#include "clipper/cliptemplate.h"

#include "clipper/request.h"
#include "io/htmlmarkdown.h"
#include "markdown/templates.h"

#include <QLocale>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace brulion::clipper {

namespace {

const QRegularExpression &placeholderRe()
{
    // The same placeholders as markdown::applyTemplate().
    static const QRegularExpression re(u"\\{\\{[ \\t]*([A-Za-z]+)[ \\t]*(?::([^}\\r\\n]*))?\\}\\}"_s);
    return re;
}

/// Where a placeholder stands.
enum class Context { Body, Yaml, Property };

/// A variable's value for one placeholder.
struct Value {
    QString text;
    QStringList list; ///< tags
    bool isList = false;
    bool markdown = false; ///< Markdown already (content, selection, pictures): never escaped
    bool url = false;      ///< {{url}}: kept as a URL
    bool isEmpty() const { return isList ? list.isEmpty() : text.isEmpty(); }
};

QLocale localeOf(dates::UiLocale locale)
{
    return locale == dates::UiLocale::Polish ? QLocale(QLocale::Polish, QLocale::Poland)
                                             : QLocale(QLocale::English, QLocale::UnitedStates);
}

/// The value of `{{name:arg}}`, or nullopt for a variable no template knows (kept as written).
std::optional<Value> resolve(const QString &placeholder, const QString &name, const QString &arg,
                             const ClipValues &v, const TemplateFill &fill)
{
    const auto plain = [](const QString &text) { return Value{text, {}, false, false, false}; };
    const auto md = [](const QString &text) { return Value{text, {}, false, true, false}; };
    const auto textForm = [&](const QString &markdown, const QString &text) -> std::optional<Value> {
        if (arg.isEmpty())
            return md(markdown);
        if (arg.compare(u"quote", Qt::CaseInsensitive) == 0)
            return md(asQuote(markdown));
        if (arg.compare(u"text", Qt::CaseInsensitive) == 0)
            return plain(text);
        return std::nullopt;
    };
    if (name == u"title")
        return plain(v.title);
    if (name == u"url")
        return Value{v.url, {}, false, false, true};
    if (name == u"domain")
        return plain(v.domain);
    if (name == u"site")
        return plain(v.site.isEmpty() ? v.domain : v.site);
    if (name == u"author")
        return plain(v.author);
    if (name == u"description")
        return plain(v.description);
    if (name == u"published") {
        const QDateTime when = parsePublished(v.published);
        if (!when.isValid())
            return plain(v.published);
        const QLocale locale = localeOf(fill.locale);
        return plain(locale.toString(when, arg.isEmpty() ? u"yyyy-MM-dd"_s : arg));
    }
    if (name == u"content")
        return textForm(v.content, v.contentText);
    if (name == u"selection")
        return textForm(v.selection, v.selectionText);
    if (name == u"tags")
        return Value{QString(), v.tags, true, false, false};
    if (name == u"image")
        return md(v.image);
    if (name == u"screenshot")
        return md(v.screenshot);
    // The variables of every template: {{date}}, {{time}}, {{weekday}}, … ({{cursor}} is removed).
    markdown::TemplateVars vars;
    vars.now = fill.now;
    vars.title = v.title;
    vars.locale = fill.locale;
    const QString filled = markdown::applyTemplate(placeholder, vars).text;
    if (filled == placeholder)
        return std::nullopt;
    return plain(filled);
}

QString oneLine(QString text)
{
    static const QRegularExpression breaks(u"[\\r\\n\\t]+"_s);
    text.replace(breaks, u" "_s);
    return text.trimmed();
}

/// A double-quoted YAML (JSON) string.
QString yamlQuoted(QStringView s)
{
    QString out = u"\""_s;
    for (const QChar c : s) {
        switch (c.unicode()) {
        case u'"': out += u"\\\""_s; break;
        case u'\\': out += u"\\\\"_s; break;
        case u'\n': out += u"\\n"_s; break;
        case u'\r': out += u"\\r"_s; break;
        case u'\t': out += u"\\t"_s; break;
        default:
            if (c.unicode() < 0x20)
                out += u"\\u%1"_s.arg(uint(c.unicode()), 4, 16, u'0');
            else
                out += c;
        }
    }
    return out + u'"';
}

/// Would a YAML reader (also YAML 1.1) read the plain scalar `s` as anything but this text?
bool needsYamlQuotes(const QString &s, bool flowItem)
{
    if (s.isEmpty())
        return false;
    if (s != s.trimmed())
        return true;
    if (QStringView(u"-?:,[]{}#&*!|>'\"%@`").contains(s.front()))
        return true;
    if (s.contains(u": ") || s.endsWith(u':') || s.contains(u" #"))
        return true;
    if (flowItem && (s.contains(u',') || s.contains(u'[') || s.contains(u']') || s.contains(u'{') || s.contains(u'}')))
        return true;
    static const QRegularExpression special(u"^(?:true|false|yes|no|on|off|y|n|null|~)$"_s,
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression number(
            u"^(?:[-+]?(?:\\d[\\d_]*(?:\\.\\d*)?|\\.\\d+)(?:[eE][-+]?\\d+)?|0x[0-9a-fA-F_]+|0o?[0-7_]+|[-+]?\\.(?:inf|nan)"
            "|[-+]?\\d+(?::[0-5]?\\d)+(?:\\.\\d*)?)$"_s,
            QRegularExpression::CaseInsensitiveOption);
    return special.match(s).hasMatch() || number.match(s).hasMatch();
}

QString yamlScalar(const QString &s, bool flowItem = false)
{
    return needsYamlQuotes(s, flowItem) ? yamlQuoted(s) : s;
}

/// `text` escaped for the inside of quotes of `quote` ('"' or '\'').
QString escapedForQuotes(const QString &text, QChar quote)
{
    if (quote == u'\'') {
        QString out = text;
        return out.replace(u'\'', u"''"_s);
    }
    const QString quoted = yamlQuoted(text);
    return quoted.sliced(1, quoted.size() - 2);
}

/// `url` for Markdown text: what would end a link's destination or start other syntax around it
/// is percent-encoded (a URL stays the same address).
QString markdownUrl(const QString &url)
{
    QString out;
    out.reserve(url.size());
    for (const QChar c : url) {
        switch (c.unicode()) {
        case u'(': out += u"%28"_s; break;
        case u')': out += u"%29"_s; break;
        case u'<': out += u"%3C"_s; break;
        case u'>': out += u"%3E"_s; break;
        case u'[': out += u"%5B"_s; break;
        case u']': out += u"%5D"_s; break;
        case u'`': out += u"%60"_s; break;
        case u'"': out += u"%22"_s; break;
        case u'\\': out += u"%5C"_s; break;
        case u'|': out += u"%7C"_s; break;
        case u' ': out += u"%20"_s; break;
        default: out += c;
        }
    }
    return out;
}

/// One piece of a template line.
struct Piece {
    QString literal;     ///< text before the placeholder (or the rest of the line)
    QString placeholder; ///< "" for the last piece
    QString name;
    QString arg;
};

QList<Piece> splitLine(const QString &line)
{
    QList<Piece> pieces;
    qsizetype last = 0;
    auto it = placeholderRe().globalMatch(line);
    while (it.hasNext()) {
        const auto m = it.next();
        pieces.append(Piece{line.sliced(last, m.capturedStart(0) - last), m.captured(0), m.captured(1).toLower(),
                            m.captured(2).trimmed()});
        last = m.capturedEnd(0);
    }
    pieces.append(Piece{line.sliced(last), QString(), QString(), QString()});
    return pieces;
}

bool hasPlaceholder(const QList<Piece> &pieces)
{
    return pieces.size() > 1;
}

/// The literal text of a line apart from its placeholders.
QString literalsOf(const QList<Piece> &pieces)
{
    QString out;
    for (const Piece &p : pieces)
        out += p.literal;
    return out;
}

struct LineOut {
    QStringList lines; ///< what the template line became (several for a multi-line value or tags)
    bool removed = false;
};

// ---- the text (Markdown) ------------------------------------------------------------------------

LineOut fillBody(const QList<Piece> &pieces, const ClipValues &v, const TemplateFill &fill)
{
    LineOut out;
    QString line;
    bool anyValue = false;
    bool allEmpty = true;
    bool lastEmpty = false; ///< the line's last variable gave nothing
    for (const Piece &p : pieces) {
        line += p.literal;
        if (p.placeholder.isEmpty())
            break;
        const auto value = resolve(p.placeholder, p.name, p.arg, v, fill);
        if (!value) {
            line += p.placeholder;
            allEmpty = false;
            lastEmpty = false;
            continue;
        }
        anyValue = true;
        if (!value->isEmpty())
            allEmpty = false;
        lastEmpty = value->isEmpty();
        const bool atLineStart = line.trimmed().isEmpty();
        QString text;
        if (value->isList) {
            QStringList tags;
            for (const QString &tag : value->list)
                tags.append(tagText(tag));
            text = tags.join(u' ');
        } else if (value->url) {
            text = markdownUrl(value->text);
        } else if (value->markdown) {
            text = value->text;
        } else {
            text = io::escapeMarkdownText(value->text, atLineStart, true);
        }
        // A value of several lines continues with the indentation its line has.
        if (text.contains(u'\n')) {
            QString indent;
            if (atLineStart)
                indent = line;
            QStringList parts = text.split(u'\n');
            for (qsizetype i = 1; i < parts.size(); ++i) {
                if (!parts.at(i).isEmpty())
                    parts[i].prepend(indent);
            }
            text = parts.join(u'\n');
        }
        line += text;
    }
    if (anyValue && allEmpty && literalsOf(pieces).trimmed().isEmpty()) {
        out.removed = true;
        return out;
    }
    // "[{{title}}]({{url}}) {{tags}}" without tags: no blank at the end.
    if (lastEmpty && pieces.last().literal.trimmed().isEmpty()) {
        while (line.endsWith(u' ') || line.endsWith(u'\t'))
            line.chop(1);
    }
    out.lines = line.split(u'\n');
    return out;
}

// ---- `key:: value` lines ------------------------------------------------------------------------

LineOut fillProperty(const QList<Piece> &pieces, const ClipValues &v, const TemplateFill &fill)
{
    LineOut out;
    QString line;
    bool allEmpty = true;
    for (const Piece &p : pieces) {
        line += p.literal;
        if (p.placeholder.isEmpty())
            break;
        const auto value = resolve(p.placeholder, p.name, p.arg, v, fill);
        if (!value) {
            line += p.placeholder;
            allEmpty = false;
            continue;
        }
        if (!value->isEmpty())
            allEmpty = false;
        if (value->isList) {
            QStringList tags;
            for (const QString &tag : value->list)
                tags.append(tagText(tag));
            line += tags.join(u", "_s);
        } else {
            line += oneLine(value->text);
        }
    }
    // "source:: {{url}}" without a URL: no property at all.
    static const QRegularExpression keyOnly(u"^\\s*[A-Za-z0-9_-]+::\\s*$"_s);
    if (allEmpty && keyOnly.match(line).hasMatch()) {
        out.removed = true;
        return out;
    }
    out.lines.append(line);
    return out;
}

// ---- YAML frontmatter ---------------------------------------------------------------------------

/// The quote ('"', '\'' or none) each placeholder of a YAML line stands in.
QList<QChar> quotesOf(const QList<Piece> &pieces)
{
    QList<QChar> quotes;
    QChar open;
    for (const Piece &p : pieces) {
        for (qsizetype i = 0; i < p.literal.size(); ++i) {
            const QChar c = p.literal.at(i);
            if (open.isNull()) {
                if (c == u'"' || c == u'\'')
                    open = c;
            } else if (open == u'"' && c == u'\\') {
                ++i;
            } else if (c == open) {
                if (open == u'\'' && i + 1 < p.literal.size() && p.literal.at(i + 1) == u'\'')
                    ++i;
                else
                    open = QChar();
            }
        }
        if (!p.placeholder.isEmpty())
            quotes.append(open);
    }
    return quotes;
}

LineOut fillYaml(const QList<Piece> &pieces, const ClipValues &v, const TemplateFill &fill)
{
    LineOut out;
    static const QRegularExpression itemStart(u"^(\\s*-\\s+)"_s);
    static const QRegularExpression keyStart(u"^(\\s*[^\\s#:\"'\\-][^:]*:)(?:\\s+|$)"_s);
    const QList<QChar> quotes = quotesOf(pieces);
    const QString literals = literalsOf(pieces);

    // A `- {{tags}}` item (quoted or not): one item per tag.
    const int placeholders = int(pieces.size()) - 1;
    if (placeholders == 1 && pieces.first().name == u"tags" && itemStart.match(pieces.first().literal).hasMatch()) {
        const QString before = pieces.first().literal;
        const QString after = pieces.last().literal;
        const QString rest = before.sliced(itemStart.match(before).capturedLength(1)) + after;
        static const QRegularExpression onlyQuotes(u"^[\"'\\s]*$"_s);
        if (onlyQuotes.match(rest).hasMatch()) {
            const QChar quote = quotes.first();
            for (const QString &tag : v.tags) {
                out.lines.append(quote.isNull() ? before + yamlScalar(tag) + after
                                                : before + escapedForQuotes(tag, quote) + after);
            }
            out.removed = out.lines.isEmpty();
            return out;
        }
    }

    QString line;
    bool anyBare = false;
    bool allEmpty = true;
    bool anyValue = false;
    for (qsizetype i = 0; i < pieces.size(); ++i) {
        const Piece &p = pieces.at(i);
        line += p.literal;
        if (p.placeholder.isEmpty())
            break;
        const auto value = resolve(p.placeholder, p.name, p.arg, v, fill);
        if (!value) {
            line += p.placeholder;
            allEmpty = false;
            continue;
        }
        anyValue = true;
        if (!value->isEmpty())
            allEmpty = false;
        const QChar quote = quotes.at(i);
        if (value->isList) {
            if (quote.isNull()) {
                // `tags: {{tags}}`: a flow list.
                QStringList items;
                for (const QString &tag : value->list)
                    items.append(yamlScalar(tag, true));
                line += u'[' + items.join(u", "_s) + u']';
                // Already YAML: never quoted as a whole.
            } else {
                line += escapedForQuotes(value->list.join(u", "_s), quote);
            }
            continue;
        }
        const QString text = oneLine(value->text);
        if (quote.isNull()) {
            anyBare = true;
            line += text;
        } else {
            line += escapedForQuotes(text, quote);
        }
    }

    // What the line's value is: after "key:" or "- ".
    qsizetype valueStart = -1;
    if (const auto m = itemStart.match(line); m.hasMatch())
        valueStart = m.capturedLength(1);
    else if (const auto k = keyStart.match(line); k.hasMatch())
        valueStart = k.capturedLength(0);

    if (anyValue && allEmpty) {
        // "author: {{author}}", `- "[[{{author}}]]"` without an author: no entry at all.
        static const QRegularExpression emptyValue(u"^[\\s\"'\\[\\]#-]*$"_s);
        const QString literalValue =
                valueStart >= 0 ? literals.sliced(std::min(valueStart, literals.size())) : literals;
        if (valueStart >= 0 && emptyValue.match(literalValue).hasMatch()) {
            out.removed = true;
            return out;
        }
    }
    // A bare value that YAML would read as something else is quoted as a whole — when the
    // template wrote no quotes of its own there.
    const bool literalQuotes = literals.contains(u'"') || literals.contains(u'\'');
    if (anyBare && !literalQuotes && valueStart >= 0 && valueStart <= line.size()) {
        const QString value = line.sliced(valueStart).trimmed();
        if (!value.startsWith(u'[') || !value.endsWith(u']')) {
            if (needsYamlQuotes(value, false))
                line = line.left(valueStart) + yamlQuoted(value);
        }
    }
    out.lines.append(line);
    return out;
}

bool isDelimiter(const QString &line, bool closing)
{
    const QString t = line.trimmed();
    return t == u"---" || (closing && t == u"...");
}

} // namespace

const QList<BuiltinTemplate> &builtinTemplates()
{
    static const QList<BuiltinTemplate> templates = {
            {u"brulion"_s, "Brulion",
             u"---\n"
             "source: {{url}}\n"
             "author: {{author}}\n"
             "published: {{published}}\n"
             "clipped: {{date}} {{time}}\n"
             "tags: {{tags}}\n"
             "---\n"
             "\n"
             "{{content}}\n"_s},
            {u"obsidian"_s, "Obsidian Web Clipper",
             u"---\n"
             "title: \"{{title}}\"\n"
             "source: \"{{url}}\"\n"
             "author:\n"
             "  - \"[[{{author}}]]\"\n"
             "published: {{published}}\n"
             "created: {{date}}\n"
             "description: \"{{description}}\"\n"
             "tags:\n"
             "  - \"clippings\"\n"
             "  - \"{{tags}}\"\n"
             "---\n"
             "{{content}}\n"_s},
            {u"joplin"_s, "Joplin",
             u"---\n"
             "title: {{title}}\n"
             "source: {{url}}\n"
             "author: {{author}}\n"
             "created: {{date:yyyy-MM-dd HH:mm}}\n"
             "tags: {{tags}}\n"
             "---\n"
             "\n"
             "{{content}}\n"_s},
            {u"link"_s, "Link and selection",
             u"[{{title}}]({{url}}) {{tags}}\n"
             "{{selection:quote}}\n"_s},
    };
    return templates;
}

const BuiltinTemplate *builtinTemplate(QStringView id)
{
    for (const BuiltinTemplate &t : builtinTemplates()) {
        if (id.compare(t.id, Qt::CaseInsensitive) == 0)
            return &t;
    }
    return nullptr;
}

QString defaultTemplateFor(bool newPage)
{
    return newPage ? u"brulion"_s : u"link"_s;
}

QString asQuote(const QString &markdown)
{
    if (markdown.isEmpty())
        return {};
    QStringList lines = markdown.split(u'\n');
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    for (QString &line : lines)
        line = line.isEmpty() ? u">"_s : u"> "_s + line;
    return lines.join(u'\n');
}

QDateTime parsePublished(QStringView raw)
{
    const QString text = raw.trimmed().toString();
    if (text.isEmpty())
        return {};
    for (const Qt::DateFormat format : {Qt::ISODateWithMs, Qt::ISODate, Qt::RFC2822Date}) {
        const QDateTime dt = QDateTime::fromString(text, format);
        if (dt.isValid())
            return dt; // in its own offset: the time as the page wrote it
    }
    // "2026-11-19 08:00", "2026-11-19T08:00:00.000+0100" (no colon in the offset), a date alone.
    static const QRegularExpression isoStart(u"^(\\d{4}-\\d{2}-\\d{2})(?:[T ](\\d{2}:\\d{2}(?::\\d{2})?))?"_s);
    if (const auto m = isoStart.match(text); m.hasMatch()) {
        const QDate date = QDate::fromString(m.captured(1), Qt::ISODate);
        if (!date.isValid())
            return {};
        const QTime time = m.captured(2).isEmpty() ? QTime(0, 0) : QTime::fromString(m.captured(2), Qt::ISODate);
        return QDateTime(date, time.isValid() ? time : QTime(0, 0));
    }
    return {};
}

QString fillClipTemplate(QStringView tplView, const ClipValues &values, const TemplateFill &fill)
{
    QString tpl = tplView.toString();
    tpl.remove(u'\r');
    if (tpl.startsWith(QChar(0xfeff)))
        tpl.remove(0, 1);
    const QStringList lines = tpl.split(u'\n');
    // The frontmatter: "---" on the first line to the next "---" (or "...").
    qsizetype fmEnd = -1;
    if (!lines.isEmpty() && isDelimiter(lines.first(), false)) {
        for (qsizetype i = 1; i < lines.size(); ++i) {
            if (isDelimiter(lines.at(i), true)) {
                fmEnd = i;
                break;
            }
        }
    }
    static const QRegularExpression property(u"^\\s*[A-Za-z0-9_-]+::(?:\\s|$)"_s);
    static const QRegularExpression keyNoValue(u"^([^\\s#:\"'\\-][^:]*):\\s*$"_s);

    QStringList out;
    QList<bool> removedItems; // per output line in the frontmatter: items under it were removed
    bool lastWasBlank = false;
    bool skipBlank = false;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        const bool inFrontmatter = fmEnd > 0 && i > 0 && i < fmEnd;
        const QList<Piece> pieces = splitLine(line);
        if (!hasPlaceholder(pieces)) {
            if (line.trimmed().isEmpty() && skipBlank) {
                skipBlank = false;
                continue;
            }
            skipBlank = false;
            out.append(line);
            removedItems.append(false);
            lastWasBlank = line.trimmed().isEmpty();
            continue;
        }
        skipBlank = false;
        const Context context = inFrontmatter                        ? Context::Yaml
                                : property.match(line).hasMatch() ? Context::Property
                                                                     : Context::Body;
        const LineOut filled = context == Context::Yaml       ? fillYaml(pieces, values, fill)
                               : context == Context::Property ? fillProperty(pieces, values, fill)
                                                              : fillBody(pieces, values, fill);
        if (filled.removed) {
            if (inFrontmatter && !out.isEmpty())
                removedItems.last() = true;
            // One blank line of the two around a line left out.
            if (lastWasBlank || out.isEmpty())
                skipBlank = true;
            continue;
        }
        for (const QString &l : filled.lines) {
            out.append(l);
            removedItems.append(false);
        }
        lastWasBlank = !out.isEmpty() && out.last().trimmed().isEmpty();
    }

    // A frontmatter key whose items were all left out ("author:" without an author) goes too.
    if (fmEnd > 0) {
        qsizetype end = -1;
        for (qsizetype i = 1; i < out.size(); ++i) {
            if (isDelimiter(out.at(i), true)) {
                end = i;
                break;
            }
        }
        for (qsizetype i = end - 1; i >= 1; --i) {
            if (!keyNoValue.match(out.at(i)).hasMatch())
                continue;
            const bool hasItems = i + 1 < end && out.at(i + 1).startsWith(u' ');
            // Removed items were recorded on the key line or on items before them.
            bool lostItems = removedItems.at(i);
            for (qsizetype j = i + 1; j < end && !lostItems && out.at(j).startsWith(u' '); ++j)
                lostItems = removedItems.at(j);
            if (!hasItems && lostItems) {
                out.removeAt(i);
                removedItems.removeAt(i);
                --end;
            }
        }
    }
    while (!out.isEmpty() && out.first().trimmed().isEmpty())
        out.removeFirst();
    QString result = out.join(u'\n');
    while (result.endsWith(u'\n') || result.endsWith(u' '))
        result.chop(1);
    return result.isEmpty() ? result : result + u'\n';
}

QString frontmatterAsBlockProperties(const QString &markdown)
{
    const QStringList lines = markdown.split(u'\n');
    if (lines.isEmpty() || !isDelimiter(lines.first(), false))
        return markdown;
    qsizetype end = -1;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        if (isDelimiter(lines.at(i), true)) {
            end = i;
            break;
        }
    }
    if (end < 0)
        return markdown;
    static const QRegularExpression keyLine(u"^([A-Za-z0-9_-]+)\\s*:(?:\\s+(.*))?$"_s);
    static const QRegularExpression itemLine(u"^\\s+-\\s+(.*)$"_s);
    const auto unquote = [](QString s) {
        s = s.trimmed();
        if (s.size() >= 2 && s.startsWith(u'"') && s.endsWith(u'"')) {
            s = s.sliced(1, s.size() - 2);
            s.replace(u"\\\""_s, u"\""_s).replace(u"\\\\"_s, u"\\"_s);
        } else if (s.size() >= 2 && s.startsWith(u'\'') && s.endsWith(u'\'')) {
            s = s.sliced(1, s.size() - 2).replace(u"''"_s, u"'"_s);
        }
        return s;
    };
    QStringList properties;
    QString key;
    QStringList items;
    const auto flush = [&] {
        if (key.isEmpty())
            return;
        const bool tags = key.compare(u"tags", Qt::CaseInsensitive) == 0 || key.compare(u"tag", Qt::CaseInsensitive) == 0;
        QStringList values;
        for (const QString &item : std::as_const(items)) {
            const QString value = oneLine(unquote(item));
            if (!value.isEmpty())
                values.append(tags ? tagText(value) : value);
        }
        if (!values.isEmpty())
            properties.append(key.toLower() + u":: "_s + values.join(u", "_s));
        key.clear();
        items.clear();
    };
    for (qsizetype i = 1; i < end; ++i) {
        const QString &line = lines.at(i);
        if (const auto item = itemLine.match(line); item.hasMatch() && !key.isEmpty()) {
            items.append(item.captured(1));
            continue;
        }
        flush();
        const auto m = keyLine.match(line);
        if (!m.hasMatch())
            continue;
        key = m.captured(1);
        QString value = m.captured(2).trimmed();
        if (value.startsWith(u'[') && value.endsWith(u']') && !value.startsWith(u"[[")) {
            // A flow list: [a, "b c"] (items never hold a comma: yamlScalar() quotes those).
            const QStringList parts = value.sliced(1, value.size() - 2).split(u',');
            for (const QString &part : parts)
                items.append(part);
        } else if (!value.isEmpty()) {
            items.append(value);
        }
    }
    flush();
    QStringList body = lines.mid(end + 1);
    while (!body.isEmpty() && body.first().trimmed().isEmpty())
        body.removeFirst();
    if (properties.isEmpty())
        return body.join(u'\n');
    if (body.isEmpty())
        return properties.join(u'\n') + u'\n';
    QStringList result{body.first()};
    result += properties;
    result += body.mid(1);
    return result.join(u'\n');
}

} // namespace brulion::clipper
