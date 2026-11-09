// Query parser (docs/SYNTAX.md §10) — see index/query.h for the language — and the names in query
// blocks that renames and moves rewrite (findQueryNames(), retargetQueryNames()), read by the same
// tokenizer.
#include "index/query.h"

#include "dates/expr.h"
#include "index/querymessages_p.h"
#include "index/textutil_p.h"
#include "index/vaultindex_p.h"
#include "markdown/parse.h"
#include "text/text.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <optional>

namespace brulion {

using namespace Qt::StringLiterals;
using namespace index_detail;

namespace {

enum class Key {
    Type,
    Status,
    Priority,
    Tag,
    Page,
    Path,
    Ref,
    Text,
    Due,
    Scheduled,
    Start,
    Done,
    Created,
    Remind,
    Date,
    Is,
    Sort,
    Group,
    Limit,
};

const QString &publicKeys()
{
    static const QString keys = u"type, status, tag, page, path, ref, due, scheduled, start, done, created, "
                                u"remind, date, priority, is, text, sort, group, limit"_s;
    return keys;
}

/// Key spellings (folded) -> canonical key.
const QHash<QString, Key> &keyAliases()
{
    static const QHash<QString, Key> table = {
            {u"type"_s, Key::Type},
            {u"typ"_s, Key::Type},
            {u"status"_s, Key::Status},
            {u"stan"_s, Key::Status},
            {u"priority"_s, Key::Priority},
            {u"priorytet"_s, Key::Priority},
            {u"prio"_s, Key::Priority},
            {u"tag"_s, Key::Tag},
            {u"tags"_s, Key::Tag},
            {u"tagi"_s, Key::Tag},
            {u"page"_s, Key::Page},
            {u"strona"_s, Key::Page},
            {u"path"_s, Key::Path},
            {u"sciezka"_s, Key::Path},
            {u"folder"_s, Key::Path},
            {u"ref"_s, Key::Ref},
            {u"link"_s, Key::Ref},
            {u"links"_s, Key::Ref},
            {u"linki"_s, Key::Ref},
            {u"odnosnik"_s, Key::Ref},
            {u"text"_s, Key::Text},
            {u"tekst"_s, Key::Text},
            {u"due"_s, Key::Due},
            {u"termin"_s, Key::Due},
            {u"deadline"_s, Key::Due},
            {u"scheduled"_s, Key::Scheduled},
            {u"zaplanowane"_s, Key::Scheduled},
            {u"plan"_s, Key::Scheduled},
            {u"start"_s, Key::Start},
            {u"poczatek"_s, Key::Start},
            {u"done"_s, Key::Done},
            {u"zrobione"_s, Key::Done},
            {u"wykonane"_s, Key::Done},
            {u"created"_s, Key::Created},
            {u"utworzone"_s, Key::Created},
            {u"remind"_s, Key::Remind},
            {u"reminder"_s, Key::Remind},
            {u"przypomnienie"_s, Key::Remind},
            {u"date"_s, Key::Date},
            {u"data"_s, Key::Date},
            {u"dzien"_s, Key::Date},
            {u"sort"_s, Key::Sort},
            {u"sortuj"_s, Key::Sort},
            {u"group"_s, Key::Group},
            {u"grupuj"_s, Key::Group},
            {u"limit"_s, Key::Limit},
            {u"is"_s, Key::Is},
            {u"jest"_s, Key::Is},
    };
    return table;
}

const QHash<QString, QueryType> &typeAliases()
{
    static const QHash<QString, QueryType> table = {
            {u"tasks"_s, QueryType::Tasks},
            {u"task"_s, QueryType::Tasks},
            {u"zadania"_s, QueryType::Tasks},
            {u"zadanie"_s, QueryType::Tasks},
            {u"blocks"_s, QueryType::Blocks},
            {u"block"_s, QueryType::Blocks},
            {u"bloki"_s, QueryType::Blocks},
            {u"blok"_s, QueryType::Blocks},
            {u"pages"_s, QueryType::Pages},
            {u"page"_s, QueryType::Pages},
            {u"strony"_s, QueryType::Pages},
            {u"strona"_s, QueryType::Pages},
            {u"notes"_s, QueryType::Pages},
            {u"notatki"_s, QueryType::Pages},
            {u"reminders"_s, QueryType::Reminders},
            {u"reminder"_s, QueryType::Reminders},
            {u"przypomnienia"_s, QueryType::Reminders},
            {u"przypomnienie"_s, QueryType::Reminders},
    };
    return table;
}

/// `is:` values (folded) -> canonical value (QueryFilter::values).
const QHash<QString, QString> &isAliases()
{
    static const QHash<QString, QString> table = {
            {u"blocked"_s, u"blocked"_s},    {u"zablokowane"_s, u"blocked"_s}, {u"zablokowany"_s, u"blocked"_s},
            {u"blocking"_s, u"blocking"_s},  {u"blokujace"_s, u"blocking"_s},  {u"blokuje"_s, u"blocking"_s},
            {u"running"_s, u"running"_s},    {u"timing"_s, u"running"_s},      {u"mierzone"_s, u"running"_s},
            {u"w-pomiarze"_s, u"running"_s}, {u"tracked"_s, u"tracked"_s},     {u"timed"_s, u"tracked"_s},
            {u"z-czasem"_s, u"tracked"_s},
    };
    return table;
}

const QHash<QString, QList<TaskStatus>> &statusAliases()
{
    using S = TaskStatus;
    static const QList<S> all = {S::Todo, S::Doing, S::Done, S::Cancelled, S::Deferred};
    static const QHash<QString, QList<S>> table = {
            {u"todo"_s, {S::Todo}},
            {u"do-zrobienia"_s, {S::Todo}},
            {u"dozrobienia"_s, {S::Todo}},
            {u"doing"_s, {S::Doing}},
            {u"in-progress"_s, {S::Doing}},
            {u"w-toku"_s, {S::Doing}},
            {u"wtoku"_s, {S::Doing}},
            {u"done"_s, {S::Done}},
            {u"zrobione"_s, {S::Done}},
            {u"wykonane"_s, {S::Done}},
            {u"cancelled"_s, {S::Cancelled}},
            {u"canceled"_s, {S::Cancelled}},
            {u"anulowane"_s, {S::Cancelled}},
            {u"deferred"_s, {S::Deferred}},
            {u"waiting"_s, {S::Deferred}},
            {u"odlozone"_s, {S::Deferred}},
            {u"open"_s, {S::Todo, S::Doing}},
            {u"otwarte"_s, {S::Todo, S::Doing}},
            {u"closed"_s, {S::Done, S::Cancelled}},
            {u"zamkniete"_s, {S::Done, S::Cancelled}},
            {u"all"_s, all},
            {u"any"_s, all},
            {u"wszystkie"_s, all},
    };
    return table;
}

const QHash<QString, Priority> &priorityAliases()
{
    static const QHash<QString, Priority> table = {
            {u"highest"_s, Priority::Highest},  {u"najwyzszy"_s, Priority::Highest}, {u"high"_s, Priority::High},
            {u"wysoki"_s, Priority::High},      {u"medium"_s, Priority::Medium},     {u"sredni"_s, Priority::Medium},
            {u"low"_s, Priority::Low},          {u"niski"_s, Priority::Low},         {u"lowest"_s, Priority::Lowest},
            {u"najnizszy"_s, Priority::Lowest}, {u"none"_s, Priority::None},         {u"normal"_s, Priority::None},
            {u"brak"_s, Priority::None},        {u"normalny"_s, Priority::None},
    };
    return table;
}

const QHash<QString, QuerySortKey> &sortAliases()
{
    using K = QuerySortKey;
    static const QHash<QString, K> table = {
            {u"due"_s, K::Due},
            {u"termin"_s, K::Due},
            {u"scheduled"_s, K::Scheduled},
            {u"zaplanowane"_s, K::Scheduled},
            {u"start"_s, K::Start},
            {u"done"_s, K::Done},
            {u"zrobione"_s, K::Done},
            {u"created"_s, K::Created},
            {u"utworzone"_s, K::Created},
            {u"remind"_s, K::Remind},
            {u"przypomnienie"_s, K::Remind},
            {u"date"_s, K::Date},
            {u"data"_s, K::Date},
            {u"priority"_s, K::Priority},
            {u"priorytet"_s, K::Priority},
            {u"status"_s, K::Status},
            {u"page"_s, K::Page},
            {u"title"_s, K::Page},
            {u"strona"_s, K::Page},
            {u"tytul"_s, K::Page},
            {u"path"_s, K::Path},
            {u"sciezka"_s, K::Path},
    };
    return table;
}

const QHash<QString, QueryGroupKey> &groupAliases()
{
    using G = QueryGroupKey;
    static const QHash<QString, G> table = {
            {u"page"_s, G::Page},     {u"strona"_s, G::Page},         {u"due"_s, G::Due},
            {u"termin"_s, G::Due},    {u"scheduled"_s, G::Scheduled}, {u"zaplanowane"_s, G::Scheduled},
            {u"date"_s, G::Date},     {u"data"_s, G::Date},           {u"tag"_s, G::Tag},
            {u"status"_s, G::Status}, {u"priority"_s, G::Priority},   {u"priorytet"_s, G::Priority},
    };
    return table;
}

bool isTaskKey(Key k)
{
    switch (k) {
    case Key::Status:
    case Key::Priority:
    case Key::Due:
    case Key::Scheduled:
    case Key::Start:
    case Key::Done:
    case Key::Created:
    case Key::Is: return true;
    default: return false;
    }
}

bool isTaskSort(QuerySortKey k)
{
    switch (k) {
    case QuerySortKey::Due:
    case QuerySortKey::Scheduled:
    case QuerySortKey::Start:
    case QuerySortKey::Done:
    case QuerySortKey::Created:
    case QuerySortKey::Priority:
    case QuerySortKey::Status: return true;
    default: return false;
    }
}

bool isTaskGroup(QueryGroupKey g)
{
    return g == QueryGroupKey::Due || g == QueryGroupKey::Scheduled || g == QueryGroupKey::Status
           || g == QueryGroupKey::Priority;
}

QString keyName(Key k)
{
    switch (k) {
    case Key::Type: return u"type"_s;
    case Key::Status: return u"status"_s;
    case Key::Priority: return u"priority"_s;
    case Key::Tag: return u"tag"_s;
    case Key::Page: return u"page"_s;
    case Key::Path: return u"path"_s;
    case Key::Ref: return u"ref"_s;
    case Key::Text: return u"text"_s;
    case Key::Due: return u"due"_s;
    case Key::Scheduled: return u"scheduled"_s;
    case Key::Start: return u"start"_s;
    case Key::Done: return u"done"_s;
    case Key::Created: return u"created"_s;
    case Key::Remind: return u"remind"_s;
    case Key::Date: return u"date"_s;
    case Key::Is: return u"is"_s;
    case Key::Sort: return u"sort"_s;
    case Key::Group: return u"group"_s;
    case Key::Limit: return u"limit"_s;
    }
    return {};
}

QString exampleFor(Key k)
{
    switch (k) {
    case Key::Type: return u"type:tasks"_s;
    case Key::Status: return u"status:open"_s;
    case Key::Priority: return u"priority:high"_s;
    case Key::Tag: return u"tag:praca"_s;
    case Key::Page: return u"page:Projekt"_s;
    case Key::Path: return u"path:journals/"_s;
    case Key::Ref: return u"ref:[[Jan Kowalski]]"_s;
    case Key::Text: return u"text:\"lista zakupów\""_s;
    case Key::Sort: return u"sort:due"_s;
    case Key::Group: return u"group:page"_s;
    case Key::Limit: return u"limit:20"_s;
    case Key::Is: return u"is:blocked"_s;
    default: return keyName(k) + u":today"_s;
    }
}

QString sortName(QuerySortKey k)
{
    switch (k) {
    case QuerySortKey::Due: return u"due"_s;
    case QuerySortKey::Scheduled: return u"scheduled"_s;
    case QuerySortKey::Start: return u"start"_s;
    case QuerySortKey::Done: return u"done"_s;
    case QuerySortKey::Created: return u"created"_s;
    case QuerySortKey::Remind: return u"remind"_s;
    case QuerySortKey::Date: return u"date"_s;
    case QuerySortKey::Priority: return u"priority"_s;
    case QuerySortKey::Status: return u"status"_s;
    case QuerySortKey::Page: return u"page"_s;
    case QuerySortKey::Path: return u"path"_s;
    }
    return {};
}

QString groupName(QueryGroupKey g)
{
    switch (g) {
    case QueryGroupKey::None: return {};
    case QueryGroupKey::Page: return u"page"_s;
    case QueryGroupKey::Due: return u"due"_s;
    case QueryGroupKey::Scheduled: return u"scheduled"_s;
    case QueryGroupKey::Date: return u"date"_s;
    case QueryGroupKey::Tag: return u"tag"_s;
    case QueryGroupKey::Status: return u"status"_s;
    case QueryGroupKey::Priority: return u"priority"_s;
    }
    return {};
}

QueryDateField dateField(Key k)
{
    switch (k) {
    case Key::Scheduled: return QueryDateField::Scheduled;
    case Key::Start: return QueryDateField::Start;
    case Key::Done: return QueryDateField::Done;
    case Key::Created: return QueryDateField::Created;
    case Key::Remind: return QueryDateField::Remind;
    case Key::Date: return QueryDateField::Date;
    default: return QueryDateField::Due;
    }
}

// ---- tokens -------------------------------------------------------------------------------------

/// Closing quotes accepted for an opening quote; empty when `c` opens no quote.
QStringView closersFor(QChar c)
{
    switch (c.unicode()) {
    case u'"': return u"\"";
    case u'„': return u"”“\"";
    case u'“': return u"”\"";
    case u'«': return u"»";
    default: return {};
    }
}

bool opensQuote(QChar c)
{
    return !closersFor(c).isEmpty();
}

/// End (exclusive) of the quoted run starting at `i`, or -1 when unclosed.
qsizetype quoteEnd(QStringView s, qsizetype i)
{
    const QStringView closers = closersFor(s[i]);
    for (qsizetype j = i + 1; j < s.size(); ++j) {
        if (closers.contains(s[j]))
            return j + 1;
    }
    return -1;
}

/// Offset of the first quote that no closer follows (`s.size()` when every quote closes), found
/// per kind of quote (those closersFor() knows): quoteEnd() from every quote is quadratic.
qsizetype firstUnclosedQuote(QStringView s)
{
    qsizetype first = s.size();
    for (const QChar quote : {u'"', u'„', u'“', u'«'}) {
        qsizetype lastCloser = -1;
        for (const QChar c : closersFor(quote))
            lastCloser = std::max(lastCloser, s.lastIndexOf(c));
        if (const qsizetype at = s.indexOf(quote, std::max<qsizetype>(lastCloser, 0)); at >= 0)
            first = std::min(first, at);
    }
    return first;
}

/// End (exclusive) of the quoted run or `[[…]]` starting at `i` in a value (the value's end when
/// unclosed); -1 when none starts there.
qsizetype groupEnd(QStringView value, qsizetype i)
{
    if (opensQuote(value[i])) {
        const qsizetype end = quoteEnd(value, i);
        return end < 0 ? value.size() : end;
    }
    if (value.sliced(i).startsWith(u"[[")) {
        const qsizetype close = value.indexOf(u"]]", i + 2);
        return close < 0 ? value.size() : close + 2;
    }
    return -1;
}

/// Call `f(part, offset)` for each part of a comma list (not split inside quotes and `[[…]]`).
template<typename F>
void forEachPart(QStringView value, F &&f)
{
    qsizetype start = 0;
    for (qsizetype i = 0; i <= value.size(); ++i) {
        if (i == value.size() || value[i] == u',') {
            f(value.sliced(start, i - start), start);
            start = i + 1;
        } else if (const qsizetype end = groupEnd(value, i); end >= 0) {
            i = end - 1;
        }
    }
}

struct Token {
    QString raw;
    qsizetype from = 0; ///< offset in the source
};

/// Does a `#` at `i` start a comment: followed by whitespace or the end (`# Moje zadania`, as in
/// Brulion 1 and Obsidian Tasks queries)? Only first on a line; `#tag` is a tag filter.
bool isHashComment(QStringView src, qsizetype i)
{
    return src[i] == u'#' && (i + 1 == src.size() || src[i + 1].isSpace());
}

bool isLineBreak(QChar c)
{
    return c == u'\n' || c == u'\r';
}

/// Split the source into tokens (quotes and `[[…]]` may contain spaces); false on an unclosed
/// quote. `//` starts a comment (to the end of the line), and so does a `# ` line.
bool tokenize(QStringView src, QList<Token> &out)
{
    qsizetype i = 0;
    const qsizetype n = src.size();
    bool lineStart = true;
    bool bracketsClose = true; // false once a `[[` has no `]]` after it (nor has a later one then)
    while (i < n) {
        if (src[i].isSpace()) {
            lineStart = lineStart || isLineBreak(src[i]);
            ++i;
            continue;
        }
        if (src.sliced(i).startsWith(u"//") || (lineStart && isHashComment(src, i))) {
            while (i < n && !isLineBreak(src[i]))
                ++i;
            continue;
        }
        lineStart = false;
        const qsizetype start = i;
        while (i < n && !src[i].isSpace()) {
            if (opensQuote(src[i])) {
                const qsizetype end = quoteEnd(src, i);
                if (end < 0)
                    return false;
                i = end;
            } else if (src.sliced(i).startsWith(u"[[")) {
                const qsizetype close = bracketsClose ? src.indexOf(u"]]", i + 2) : -1;
                bracketsClose = close >= 0;
                i = close < 0 ? i + 2 : close + 2;
            } else {
                ++i;
            }
        }
        out.append(Token{src.sliced(start, i - start).toString(), start});
    }
    return true;
}

/// Remove one level of surrounding quotes.
QString unquote(QStringView s)
{
    const QStringView t = s.trimmed();
    if (t.size() >= 2 && opensQuote(t.front()) && closersFor(t.front()).contains(t.back()))
        return t.sliced(1, t.size() - 2).toString();
    return t.toString();
}

/// Remove surrounding `[[ ]]`.
QString unbracket(const QString &s)
{
    const QString t = s.trimmed();
    if (t.size() >= 4 && t.startsWith(u"[[") && t.endsWith(u"]]"))
        return t.sliced(2, t.size() - 4).trimmed();
    return t;
}

/// Split a value on commas outside quotes and `[[…]]`; unquote; drop empty parts.
QStringList splitValues(QStringView value)
{
    QStringList out;
    forEachPart(value, [&out](QStringView part, qsizetype) {
        if (QString v = unquote(part).trimmed(); !v.isEmpty())
            out.append(std::move(v));
    });
    return out;
}

/// Tag value: no '#', no `[[ ]]`, NFC, lower-case.
QString tagValue(const QString &v)
{
    QString t = v.trimmed();
    if (t.startsWith(u'#'))
        t.remove(0, 1);
    return nfc(unbracket(t)).toLower();
}

/// Lookup key of a written value: folded, whitespace/underscore runs as '-'.
QString valueKey(const QString &v)
{
    const QString folded = text::foldKey(QStringView(v).trimmed());
    QString out;
    bool dash = false;
    for (const QChar c : folded) {
        if (c.isSpace() || c == u'_') {
            dash = true;
            continue;
        }
        if (dash)
            out += u'-';
        dash = false;
        out += c;
    }
    return out;
}

/// `key` of a `key:value` token: a letter or '_' followed by letters, digits, '_' or '-'.
qsizetype keyLength(QStringView raw)
{
    if (raw.isEmpty() || !(raw[0].isLetter() || raw[0] == u'_'))
        return -1;
    qsizetype k = 1;
    while (k < raw.size() && (raw[k].isLetterOrNumber() || raw[k] == u'_' || raw[k] == u'-' || raw[k].isMark()))
        ++k;
    return k < raw.size() && raw[k] == u':' ? k : -1;
}

// ---- parser -------------------------------------------------------------------------------------

struct Used {
    Key key = Key::Type;
    QString value;
};

/// The filter kind of a key that names pages, folders or tags (tag:, page:, path:, ref:).
std::optional<QueryFilterKind> nameKind(Key k)
{
    switch (k) {
    case Key::Tag: return QueryFilterKind::Tag;
    case Key::Page: return QueryFilterKind::Page;
    case Key::Path: return QueryFilterKind::Path;
    case Key::Ref: return QueryFilterKind::Ref;
    default: return std::nullopt;
    }
}

/// Append `value` unless `list` holds it.
template<typename T>
void appendNew(QList<T> &list, const T &value)
{
    if (!list.contains(value))
        list.append(value);
}

/// Append the `values` that `list` does not hold.
template<typename T>
void appendNew(QList<T> &list, const QList<T> &values)
{
    for (const T &v : values)
        appendNew(list, v);
}

class Parser {
public:
    std::variant<Query, QString> parse(const QString &source);

private:
    bool applyToken(const QString &token);
    bool applyKey(Key key, const QString &written, const QString &rawValue, bool negate);
    bool applyDate(Key key, const QString &written, const QStringList &values, bool negate);
    /// The canonical value of a written one (`table`: by valueKey()); null after fail().
    template<typename T>
    const T *lookup(const QString &written, const QString &value, const QHash<QString, T> &table,
                    const QString &allowed);
    /// The canonical values of `values` appended to `out`, each once; false after fail().
    template<typename T, typename V>
    bool mapValues(const QString &written, const QStringList &values, const QHash<QString, V> &table,
                   const QString &allowed, QList<T> &out);
    /// A filter of one value: a word, a phrase, `#tag`, `[[Page]]`.
    void addFilter(QueryFilterKind kind, const QString &value, bool negate);
    bool fail(QString message)
    {
        m_error = std::move(message);
        return false;
    }
    QueryType inferType() const;
    bool validatePages();

    Query m_query;
    std::optional<QueryType> m_type;
    QList<Used> m_used;
    QString m_error;
};

template<typename T>
const T *Parser::lookup(const QString &written, const QString &value, const QHash<QString, T> &table,
                        const QString &allowed)
{
    const auto it = table.constFind(valueKey(value));
    if (it != table.constEnd())
        return &*it;
    fail(messages::badValue(written, value, allowed));
    return nullptr;
}

template<typename T, typename V>
bool Parser::mapValues(const QString &written, const QStringList &values, const QHash<QString, V> &table,
                       const QString &allowed, QList<T> &out)
{
    for (const QString &v : values) {
        const V *value = lookup(written, v, table, allowed);
        if (!value)
            return false;
        appendNew(out, *value);
    }
    return true;
}

void Parser::addFilter(QueryFilterKind kind, const QString &value, bool negate)
{
    QueryFilter f;
    f.kind = kind;
    f.negate = negate;
    f.values = {value};
    m_query.filters.append(std::move(f));
}

bool Parser::applyDate(Key key, const QString &written, const QStringList &values, bool negate)
{
    // A fixed reference day: only validates the expressions (runQuery resolves them).
    static const QDate reference(2026, 1, 15);
    static constexpr std::pair<QStringView, QueryDateOp> operators[] = {
            {u"<=", QueryDateOp::OnOrBefore}, {u">=", QueryDateOp::OnOrAfter}, {u"<", QueryDateOp::Before},
            {u">", QueryDateOp::After},       {u"=", QueryDateOp::Within},
    };
    QueryFilter f;
    f.kind = QueryFilterKind::Date;
    f.field = dateField(key);
    f.negate = negate;
    for (const QString &v : values) {
        QueryDateCondition cond;
        QStringView rest(v);
        QStringView op;
        for (const auto &[spelling, dateOp] : operators) {
            if (rest.startsWith(spelling)) {
                op = spelling;
                cond.op = dateOp;
                rest = rest.sliced(spelling.size());
                break;
            }
        }
        cond.expr = rest.trimmed().toString();
        if (cond.expr.isEmpty())
            return fail(messages::emptyValue(written, exampleFor(key)));
        const auto resolved = dates::resolveDateExpr(cond.expr, reference, Qt::Monday);
        if (!resolved)
            return fail(messages::badDate(written, cond.expr));
        if (std::get_if<dates::SpecialDate>(&*resolved) && cond.op != QueryDateOp::Within)
            return fail(messages::operatorWithKeyword(written, op.toString(), cond.expr));
        f.dates.append(cond);
    }
    m_query.filters.append(f);
    return true;
}

bool Parser::applyKey(Key key, const QString &written, const QString &rawValue, bool negate)
{
    const QStringList values = splitValues(rawValue);
    if (values.isEmpty())
        return fail(messages::emptyValue(written, exampleFor(key)));
    m_used.append(Used{key, values.join(u',')});
    const bool once = key == Key::Type || key == Key::Sort || key == Key::Group || key == Key::Limit;
    if (once && negate)
        return fail(messages::notNegatable(written));
    QueryFilter f;
    f.negate = negate;
    switch (key) {
    case Key::Type: {
        if (m_type)
            return fail(messages::duplicate(written));
        const QString allowed = u"tasks, blocks, pages, reminders"_s;
        if (values.size() > 1)
            return fail(messages::badValue(written, values.join(u','), allowed));
        const QueryType *type = lookup(written, values.first(), typeAliases(), allowed);
        if (!type)
            return false;
        m_type = *type;
        return true;
    }
    case Key::Status:
        f.kind = QueryFilterKind::Status;
        if (!mapValues(written, values, statusAliases(), u"todo, doing, done, cancelled, deferred, open, closed"_s,
                       f.statuses))
            return false;
        break;
    case Key::Is:
        f.kind = QueryFilterKind::Is;
        if (!mapValues(written, values, isAliases(), u"blocked, blocking, running, tracked"_s, f.values))
            return false;
        break;
    case Key::Priority:
        f.kind = QueryFilterKind::Priority;
        if (!mapValues(written, values, priorityAliases(), u"highest, high, medium, low, lowest, none"_s, f.priorities))
            return false;
        break;
    case Key::Tag:
    case Key::Page:
    case Key::Path:
    case Key::Ref:
        f.kind = nameKind(key).value();
        for (const QString &v : values) {
            if (const QString clean = key == Key::Tag ? tagValue(v) : unbracket(v); !clean.isEmpty())
                f.values.append(clean);
        }
        if (f.values.isEmpty())
            return fail(messages::emptyValue(written, exampleFor(key)));
        break;
    case Key::Text:
        f.kind = QueryFilterKind::Text;
        f.values = values;
        break;
    case Key::Sort: {
        const QString allowed = u"due, scheduled, start, done, created, remind, date, priority, status, page, path"_s;
        for (const QString &v : values) {
            const bool desc = v.startsWith(u'-');
            const QString name = desc || v.startsWith(u'+') ? v.sliced(1) : v;
            const QuerySortKey *sortKey = lookup(written, name, sortAliases(), allowed);
            if (!sortKey)
                return false;
            m_query.sort.append(QuerySort{*sortKey, desc});
        }
        return true;
    }
    case Key::Group: {
        if (m_query.group != QueryGroupKey::None)
            return fail(messages::duplicate(written));
        const QString allowed = u"page, due, scheduled, date, tag, status, priority"_s;
        if (values.size() > 1)
            return fail(messages::badValue(written, values.join(u','), allowed));
        const QueryGroupKey *group = lookup(written, values.first(), groupAliases(), allowed);
        if (!group)
            return false;
        m_query.group = *group;
        return true;
    }
    case Key::Limit: {
        const QString &v = values.first();
        bool ok = values.size() == 1 && std::ranges::all_of(v, [](QChar c) { return c >= u'0' && c <= u'9'; });
        const qlonglong n = ok ? v.toLongLong(&ok) : 0;
        if (!ok || n < 1 || n > MaxQueryLimit)
            return fail(messages::badLimit(values.join(u','), MaxQueryLimit));
        m_query.limit = int(n);
        return true;
    }
    default: return applyDate(key, written, values, negate);
    }
    m_query.filters.append(f);
    return true;
}

bool Parser::applyToken(const QString &token)
{
    if (token == u"-")
        return true;
    const bool negate = token.startsWith(u'-');
    const QString raw = negate ? token.sliced(1) : token;
    if (opensQuote(raw.front())) {
        if (const QString phrase = unquote(raw); !phrase.trimmed().isEmpty())
            addFilter(QueryFilterKind::Text, phrase, negate);
        return true;
    }
    if (raw.size() > 1 && raw.startsWith(u'#') && !raw[1].isSpace()) {
        if (const QString tag = tagValue(raw.sliced(1)); !tag.isEmpty())
            addFilter(QueryFilterKind::Tag, tag, negate);
        return true;
    }
    if (raw.startsWith(u"[[") && raw.endsWith(u"]]")) {
        if (const QString name = unbracket(raw); !name.isEmpty())
            addFilter(QueryFilterKind::Ref, name, negate);
        return true;
    }
    const qsizetype k = keyLength(raw);
    // `https://…` and other URLs are text, not filters.
    if (k > 0 && !QStringView(raw).sliced(k + 1).startsWith(u"//")) {
        const QString written = raw.first(k);
        const auto it = keyAliases().constFind(text::foldKey(written));
        if (it == keyAliases().constEnd())
            return fail(messages::unknownKey(written, publicKeys()));
        return applyKey(*it, written, raw.sliced(k + 1), negate);
    }
    addFilter(QueryFilterKind::Text, raw, negate);
    return true;
}

QueryType Parser::inferType() const
{
    const bool taskish = std::ranges::any_of(m_used, isTaskKey, &Used::key)
                         || std::ranges::any_of(m_query.sort, isTaskSort, &QuerySort::key)
                         || isTaskGroup(m_query.group);
    if (taskish)
        return QueryType::Tasks;
    const bool remind = std::ranges::any_of(m_used, [](const Used &u) { return u.key == Key::Remind; })
                        || std::ranges::any_of(m_query.sort, [](QuerySort s) { return s.key == QuerySortKey::Remind; });
    return remind ? QueryType::Reminders : QueryType::Blocks;
}

bool Parser::validatePages()
{
    const QString type = u"pages"_s;
    for (const Used &u : std::as_const(m_used)) {
        if (isTaskKey(u.key) || u.key == Key::Remind)
            return fail(messages::typeConflict(keyName(u.key), u.value, type));
    }
    for (const QuerySort &s : std::as_const(m_query.sort)) {
        if (s.key != QuerySortKey::Page && s.key != QuerySortKey::Path && s.key != QuerySortKey::Date)
            return fail(messages::typeConflict(u"sort"_s, sortName(s.key), type));
    }
    const QueryGroupKey g = m_query.group;
    if (g != QueryGroupKey::None && g != QueryGroupKey::Page && g != QueryGroupKey::Tag && g != QueryGroupKey::Date)
        return fail(messages::typeConflict(u"group"_s, groupName(g), type));
    return true;
}

std::variant<Query, QString> Parser::parse(const QString &source)
{
    m_query.source = source;
    QList<Token> tokens;
    if (!tokenize(source, tokens))
        return messages::unclosedQuote();
    for (const Token &t : std::as_const(tokens)) {
        if (!applyToken(t.raw))
            return m_error;
    }
    m_query.explicitType = m_type.has_value();
    m_query.type = m_type.value_or(inferType());
    if (m_query.type == QueryType::Pages && !validatePages())
        return m_error;
    return m_query;
}

} // namespace

std::variant<Query, QString> parseQuery(const QString &source)
{
    return Parser().parse(source);
}

// ---- Names in query blocks -----------------------------------------------------------------------

namespace {

/// A fence line alone: three or more backticks or tildes, after indentation and quote markers.
bool isFenceOnly(QStringView line)
{
    qsizetype i = 0;
    while (i < line.size() && (line[i].isSpace() || line[i] == u'>'))
        ++i;
    const QStringView rest = line.sliced(i).trimmed();
    if (rest.size() < 3 || (rest.front() != u'`' && rest.front() != u'~'))
        return false;
    return std::ranges::all_of(rest, [&](QChar c) { return c == rest.front(); });
}

/// The language of the fence `text` opens (up to three spaces before three or more backticks or
/// tildes, lower-cased: "query"); "" when it opens none.
QString fenceLanguage(QStringView text)
{
    qsizetype i = 0;
    while (i < text.size() && i < 3 && text[i] == u' ')
        ++i;
    if (i >= text.size() || (text[i] != u'`' && text[i] != u'~'))
        return {};
    const QChar marker = text[i];
    qsizetype n = 0;
    while (i + n < text.size() && text[i + n] == marker)
        ++n;
    if (n < 3)
        return {};
    QStringView info = text.sliced(i + n).trimmed();
    if (marker == u'`' && info.contains(u'`'))
        return {};
    while (!info.isEmpty() && (info.front() == u'{' || info.front().isSpace()))
        info = info.sliced(1);
    qsizetype end = 0;
    while (end < info.size() && !info[end].isSpace() && info[end] != u'{' && info[end] != u'}')
        ++end;
    return end > 0 ? info.first(end).toString().toLower() : u"-"_s; // "-": a fence without a language
}

/// A list item's line without its marker ("- ```query" -> "```query"); other lines as they are.
QStringView afterListMarker(QStringView line)
{
    qsizetype i = 0;
    while (i < line.size() && line[i].isSpace())
        ++i;
    qsizetype j = i;
    if (j < line.size() && (line[j] == u'-' || line[j] == u'*' || line[j] == u'+')) {
        ++j;
    } else {
        while (j < line.size() && line[j].isDigit())
            ++j;
        if (j == i || j >= line.size() || (line[j] != u'.' && line[j] != u')'))
            return line;
        ++j;
    }
    if (j < line.size() && !line[j].isSpace())
        return line;
    while (j < line.size() && line[j].isSpace())
        ++j;
    return line.sliced(j);
}

/// The ranges [from, to) of the bodies of the ```query blocks of `content` (their own lines, not
/// the fences): fenced code blocks, and fences inside list items (Logseq's "- ```query", the
/// outline's query blocks).
QList<std::pair<qsizetype, qsizetype>> queryBodies(const QString &content, const ParsedNote *parsed = nullptr)
{
    QList<std::pair<qsizetype, qsizetype>> out;
    // Most notes have no fence at all: the (fast) case-sensitive search for one first, the
    // case-insensitive search (slow: case folding per character) only then.
    if ((!content.contains(u"```") && !content.contains(u"~~~")) || !content.contains(u"query", Qt::CaseInsensitive))
        return out;
    std::optional<ParsedNote> own;
    if (!parsed)
        own = markdown::parseNote(QString(), content);
    const ParsedNote &note = parsed ? *parsed : *own;
    QList<text::Line> lines;
    const auto body = [&](int first, int last) {
        if (last >= first)
            out.append({lines.at(first).start, lines.at(last).start + lines.at(last).text.size()});
    };
    for (const Block &b : note.blocks) {
        if (b.kind != BlockKind::ListItem && (b.kind != BlockKind::Code || b.lang != u"query"))
            continue;
        if (lines.isEmpty())
            lines = text::splitLines(content);
        const int end = std::min<int>(b.ownEndLine, int(lines.size()) - 1);
        if (b.kind == BlockKind::Code) {
            const int last = end > b.line && isFenceOnly(lines.at(end).text) ? end - 1 : end;
            body(b.line + 1, last);
            continue;
        }
        // A list item: its own lines may hold fences.
        for (int ln = b.line; ln <= end; ++ln) {
            const QStringView text =
                    ln == b.line ? afterListMarker(lines.at(ln).text) : QStringView(lines.at(ln).text).trimmed();
            const QString lang = fenceLanguage(text);
            if (lang.isEmpty())
                continue;
            int close = ln + 1;
            while (close <= end && !isFenceOnly(lines.at(close).text))
                ++close;
            if (lang == u"query")
                body(ln + 1, std::min(close, end + 1) - 1);
            ln = close; // past the closing fence (code is not looked into)
        }
    }
    std::ranges::sort(out);
    return out;
}

/// One value of a filter as written (`part`, at `at`): bare, quoted or in `[[ ]]`; a tag's may
/// start with '#'.
void addName(QueryFilterKind kind, QStringView part, qsizetype at, QList<QueryNameRef> &out)
{
    QueryNameRef r;
    r.kind = kind;
    qsizetype a = 0;
    qsizetype b = part.size();
    if (kind == QueryFilterKind::Tag && b > a && part[a] == u'#')
        ++a;
    const QStringView v = part.sliced(a, b - a);
    if (v.size() >= 2 && opensQuote(v.front()) && closersFor(v.front()).contains(v.back())) {
        r.quote = v.front();
        ++a;
        --b;
    } else if (v.size() >= 4 && v.startsWith(u"[[") && v.endsWith(u"]]")) {
        r.bracketed = true;
        a += 2;
        b -= 2;
    }
    while (a < b && part[a].isSpace())
        ++a;
    while (b > a && part[b - 1].isSpace())
        --b;
    if (a >= b)
        return;
    r.value = part.sliced(a, b - a).toString();
    r.from = at + a;
    r.to = at + b;
    out.append(r);
}

/// The names in one token (at `at`), as Parser::applyToken() reads it.
void tokenNames(QStringView token, qsizetype at, QList<QueryNameRef> &out)
{
    if (token == u"-")
        return;
    const qsizetype neg = token.startsWith(u'-') ? 1 : 0;
    const QStringView raw = token.sliced(neg);
    at += neg;
    if (raw.isEmpty() || opensQuote(raw.front()))
        return; // a phrase
    if (raw.size() > 1 && raw.front() == u'#' && !raw[1].isSpace()) {
        addName(QueryFilterKind::Tag, raw.sliced(1), at + 1, out);
        return;
    }
    if (raw.startsWith(u"[[") && raw.endsWith(u"]]")) {
        addName(QueryFilterKind::Ref, raw, at, out);
        return;
    }
    const qsizetype k = keyLength(raw);
    if (k <= 0 || raw.sliced(k + 1).startsWith(u"//"))
        return;
    const auto it = keyAliases().constFind(text::foldKey(raw.first(k)));
    if (it == keyAliases().constEnd())
        return;
    const std::optional<QueryFilterKind> kind = nameKind(*it);
    if (!kind)
        return;
    // The comma list, split as splitValues() does.
    const qsizetype base = at + k + 1;
    forEachPart(raw.sliced(k + 1),
                [&](QStringView part, qsizetype offset) { addName(*kind, part, base + offset, out); });
}

/// Can `value` stand bare as a filter value (one token, one value of a comma list)?
bool bareValue(QStringView value)
{
    return !value.startsWith(u"//") && !value.contains(u"[[") && !value.contains(u"]]")
           && std::ranges::none_of(value, [](QChar c) { return c.isSpace() || c == u',' || opensQuote(c); });
}

} // namespace

bool index_detail::hasQueryBlocks(const QString &content, const ParsedNote &note)
{
    return !queryBodies(content, &note).isEmpty();
}

QList<QueryNameRef> findQueryNames(const QString &content, const ParsedNote *parsed)
{
    QList<QueryNameRef> out;
    const QList<std::pair<qsizetype, qsizetype>> bodies = queryBodies(content, parsed);
    for (const auto &[from, to] : bodies) {
        const QStringView body = QStringView(content).sliced(from, to - from);
        QList<Token> tokens;
        if (!tokenize(body, tokens)) {
            // An unclosed quote: the query does not run; the tokens before it are still names.
            tokens.clear();
            tokenize(body.first(firstUnclosedQuote(body)), tokens);
        }
        for (const Token &t : std::as_const(tokens))
            tokenNames(t.raw, from + t.from, out);
    }
    return out;
}

QString formatQueryName(const QueryNameRef &name, const QString &value)
{
    if (value.trimmed().isEmpty() || value.contains(u'\n') || value.contains(u'\r'))
        return {};
    const bool brackets = value.contains(u"[[") || value.contains(u"]]");
    if (name.bracketed)
        return brackets ? QString() : value;
    if (!name.quote.isNull()) {
        const bool closes = std::ranges::any_of(closersFor(name.quote), [&](QChar c) { return value.contains(c); });
        return closes ? QString() : value;
    }
    if (bareValue(value))
        return value;
    return brackets ? QString() : u"[["_s + value + u"]]"_s;
}

QString retargetQueryNames(const QString &content, const QList<QueryNameEdit> &edits, int *applied)
{
    if (applied)
        *applied = 0;
    if (edits.isEmpty())
        return content;
    const QList<QueryNameRef> names = findQueryNames(content);
    struct Replacement {
        qsizetype from = 0;
        qsizetype to = 0;
        QString text;
    };
    QList<Replacement> replacements;
    for (const QueryNameEdit &e : edits) {
        const auto it =
                std::ranges::find_if(names, [&](const QueryNameRef &n) { return n.from == e.from && n.to == e.to; });
        if (it == names.cend())
            continue;
        const QString written = formatQueryName(*it, e.value);
        if (written.isEmpty() || written == QStringView(content).sliced(e.from, e.to - e.from))
            continue;
        replacements.append({e.from, e.to, written});
    }
    std::ranges::sort(replacements, {}, &Replacement::from);
    QString out;
    qsizetype pos = 0;
    int count = 0;
    for (const Replacement &r : std::as_const(replacements)) {
        if (r.from < pos)
            continue; // overlaps an earlier edit
        out += QStringView(content).sliced(pos, r.from - pos);
        out += r.text;
        pos = r.to;
        ++count;
    }
    if (count == 0)
        return content;
    out += QStringView(content).sliced(pos);
    if (applied)
        *applied = count;
    return out;
}

} // namespace brulion
