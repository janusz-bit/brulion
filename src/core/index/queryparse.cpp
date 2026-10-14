// Query parser (docs/SYNTAX.md §10) — see index/query.h for the language.
#include "index/query.h"

#include "dates/expr.h"
#include "index/querymessages_p.h"
#include "index/textutil_p.h"
#include "text/text.h"

#include <QHash>
#include <QSet>

#include <algorithm>

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
    Sort,
    Group,
    Limit,
};

const QString &publicKeys()
{
    static const QString keys = u"type, status, tag, page, path, ref, due, scheduled, start, done, created, "
                                u"remind, date, priority, text, sort, group, limit"_s;
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
    case Key::Created: return true;
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

struct Token {
    QString raw;
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
                const qsizetype close = src.indexOf(u"]]", i + 2);
                i = close < 0 ? i + 2 : close + 2;
            } else {
                ++i;
            }
        }
        out.append(Token{src.sliced(start, i - start).toString()});
    }
    return true;
}

/// Remove one level of surrounding quotes.
QString unquote(const QString &s)
{
    const QString t = s.trimmed();
    if (t.size() >= 2 && opensQuote(t.front()) && closersFor(t.front()).contains(t.back()))
        return t.sliced(1, t.size() - 2);
    return t;
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
    QStringList parts;
    QString cur;
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar ch = value[i];
        if (opensQuote(ch)) {
            const qsizetype end = quoteEnd(value, i);
            const qsizetype stop = end < 0 ? value.size() : end;
            cur += value.sliced(i, stop - i);
            i = stop - 1;
        } else if (value.sliced(i).startsWith(u"[[")) {
            const qsizetype close = value.indexOf(u"]]", i + 2);
            const qsizetype stop = close < 0 ? value.size() : close + 2;
            cur += value.sliced(i, stop - i);
            i = stop - 1;
        } else if (ch == u',') {
            parts.append(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    parts.append(cur);
    QStringList out;
    for (const QString &p : std::as_const(parts)) {
        const QString v = unquote(p).trimmed();
        if (!v.isEmpty())
            out.append(v);
    }
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
    QString out;
    bool dash = false;
    for (const QChar c : text::foldKey(v.trimmed())) {
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
    Key key;
    QString value;
};

class Parser {
public:
    std::variant<Query, QString> parse(const QString &source);

private:
    bool applyToken(const QString &token);
    bool applyKey(Key key, const QString &written, const QString &rawValue, bool negate);
    bool applyDate(Key key, const QString &written, const QStringList &values, bool negate);
    template<typename T>
    bool mapValues(const QString &written, const QStringList &values, const QHash<QString, T> &table,
                   const QString &allowed, QList<T> &out);
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
bool Parser::mapValues(const QString &written, const QStringList &values, const QHash<QString, T> &table,
                       const QString &allowed, QList<T> &out)
{
    for (const QString &v : values) {
        const auto it = table.constFind(valueKey(v));
        if (it == table.constEnd())
            return fail(messages::badValue(written, v, allowed));
        out.append(*it);
    }
    return true;
}

bool Parser::applyDate(Key key, const QString &written, const QStringList &values, bool negate)
{
    // A fixed reference day: only validates the expressions (runQuery resolves them).
    static const QDate reference(2026, 1, 15);
    QueryFilter f;
    f.kind = QueryFilterKind::Date;
    f.field = dateField(key);
    f.negate = negate;
    for (const QString &v : values) {
        QueryDateCondition cond;
        QStringView rest(v);
        QString op;
        for (const QStringView candidate : {u"<=", u">=", u"<", u">", u"="}) {
            if (rest.startsWith(candidate)) {
                op = candidate.toString();
                rest = rest.sliced(candidate.size());
                break;
            }
        }
        if (op == u"<")
            cond.op = QueryDateOp::Before;
        else if (op == u"<=")
            cond.op = QueryDateOp::OnOrBefore;
        else if (op == u">")
            cond.op = QueryDateOp::After;
        else if (op == u">=")
            cond.op = QueryDateOp::OnOrAfter;
        cond.expr = rest.trimmed().toString();
        if (cond.expr.isEmpty())
            return fail(messages::emptyValue(written, exampleFor(key)));
        const auto resolved = dates::resolveDateExpr(cond.expr, reference, Qt::Monday);
        if (!resolved)
            return fail(messages::badDate(written, cond.expr));
        if (std::holds_alternative<dates::SpecialDate>(*resolved) && cond.op != QueryDateOp::Within)
            return fail(messages::operatorWithKeyword(written, op, cond.expr));
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
        QList<QueryType> types;
        if (!mapValues(written, values, typeAliases(), allowed, types))
            return false;
        m_type = types.first();
        return true;
    }
    case Key::Status: {
        QList<QList<TaskStatus>> lists;
        if (!mapValues(written, values, statusAliases(), u"todo, doing, done, cancelled, deferred, open, closed"_s,
                       lists))
            return false;
        f.kind = QueryFilterKind::Status;
        for (const QList<TaskStatus> &l : std::as_const(lists)) {
            for (const TaskStatus s : l) {
                if (!f.statuses.contains(s))
                    f.statuses.append(s);
            }
        }
        break;
    }
    case Key::Priority: {
        QList<Priority> list;
        if (!mapValues(written, values, priorityAliases(), u"highest, high, medium, low, lowest, none"_s, list))
            return false;
        f.kind = QueryFilterKind::Priority;
        for (const Priority p : std::as_const(list)) {
            if (!f.priorities.contains(p))
                f.priorities.append(p);
        }
        break;
    }
    case Key::Tag:
    case Key::Page:
    case Key::Path:
    case Key::Ref: {
        for (const QString &v : values) {
            const QString clean = key == Key::Tag ? tagValue(v) : unbracket(v);
            if (!clean.isEmpty())
                f.values.append(clean);
        }
        if (f.values.isEmpty())
            return fail(messages::emptyValue(written, exampleFor(key)));
        f.kind = key == Key::Tag    ? QueryFilterKind::Tag
                 : key == Key::Page ? QueryFilterKind::Page
                 : key == Key::Path ? QueryFilterKind::Path
                                    : QueryFilterKind::Ref;
        break;
    }
    case Key::Text:
        f.kind = QueryFilterKind::Text;
        f.values = values;
        break;
    case Key::Sort:
        for (const QString &v : values) {
            const bool desc = v.startsWith(u'-');
            const QString name = desc || v.startsWith(u'+') ? v.sliced(1) : v;
            QList<QuerySortKey> keys;
            if (!mapValues(written, {name}, sortAliases(),
                           u"due, scheduled, start, done, created, remind, date, priority, status, page, path"_s, keys))
                return false;
            m_query.sort.append(QuerySort{keys.first(), desc});
        }
        return true;
    case Key::Group: {
        if (m_query.group != QueryGroupKey::None)
            return fail(messages::duplicate(written));
        const QString allowed = u"page, due, scheduled, date, tag, status, priority"_s;
        if (values.size() > 1)
            return fail(messages::badValue(written, values.join(u','), allowed));
        QList<QueryGroupKey> groups;
        if (!mapValues(written, values, groupAliases(), allowed, groups))
            return false;
        m_query.group = groups.first();
        return true;
    }
    case Key::Limit: {
        const QString &v = values.first();
        bool ok = values.size() == 1 && !v.isEmpty()
                  && std::all_of(v.cbegin(), v.cend(), [](QChar c) { return c >= u'0' && c <= u'9'; });
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
    QString raw = token;
    bool negate = false;
    if (raw == u"-")
        return true;
    if (raw.startsWith(u'-')) {
        negate = true;
        raw.remove(0, 1);
    }
    QueryFilter f;
    f.negate = negate;
    if (opensQuote(raw.front())) {
        const QString phrase = unquote(raw);
        if (phrase.trimmed().isEmpty())
            return true;
        f.kind = QueryFilterKind::Text;
        f.values = {phrase};
        m_query.filters.append(f);
        return true;
    }
    if (raw.size() > 1 && raw.startsWith(u'#') && !raw[1].isSpace()) {
        const QString tag = tagValue(raw.sliced(1));
        if (!tag.isEmpty()) {
            f.kind = QueryFilterKind::Tag;
            f.values = {tag};
            m_query.filters.append(f);
        }
        return true;
    }
    if (raw.startsWith(u"[[") && raw.endsWith(u"]]")) {
        const QString name = unbracket(raw);
        if (!name.isEmpty()) {
            f.kind = QueryFilterKind::Ref;
            f.values = {name};
            m_query.filters.append(f);
        }
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
    f.kind = QueryFilterKind::Text;
    f.values = {raw};
    m_query.filters.append(f);
    return true;
}

QueryType Parser::inferType() const
{
    const bool taskish = std::any_of(m_used.cbegin(), m_used.cend(), [](const Used &u) { return isTaskKey(u.key); })
                         || std::any_of(m_query.sort.cbegin(), m_query.sort.cend(),
                                        [](const QuerySort &s) { return isTaskSort(s.key); })
                         || isTaskGroup(m_query.group);
    if (taskish)
        return QueryType::Tasks;
    const bool remind = std::any_of(m_used.cbegin(), m_used.cend(), [](const Used &u) { return u.key == Key::Remind; })
                        || std::any_of(m_query.sort.cbegin(), m_query.sort.cend(),
                                       [](const QuerySort &s) { return s.key == QuerySortKey::Remind; });
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

} // namespace brulion
