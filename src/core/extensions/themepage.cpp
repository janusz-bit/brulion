#include "extensions/themepage.h"

#include "extensions/notebookhost.h"
#include "extensions/pageextension.h"

#include <algorithm>
#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace brulion::extensions {

namespace {

qsizetype indentOf(QStringView line)
{
    qsizetype n = 0;
    for (const QChar c : line) {
        if (c == u' ')
            ++n;
        else if (c == u'\t')
            n += 4;
        else
            break;
    }
    return n;
}

/// "light" / "dark" of a mode's header word in either language, "" for anything else.
QString modeOf(QStringView word)
{
    const QString w = word.toString().toLower();
    if (w == u"light" || w == u"jasny")
        return u"light"_s;
    if (w == u"dark" || w == u"ciemny")
        return u"dark"_s;
    return {};
}

/// The token named `name` in any letter case, nullptr when none.
const ThemeToken *tokenNamed(QStringView name)
{
    if (const ThemeToken *exact = themeToken(name))
        return exact;
    for (const ThemeToken &t : themeTokens())
        if (t.name.compare(name, Qt::CaseInsensitive) == 0)
            return &t;
    return nullptr;
}

/// A value as written: quotes taken off, a trailing ` // comment` dropped (outside the quotes).
QString valueOf(QStringView raw)
{
    QStringView v = raw.trimmed();
    if (v.size() >= 2 && (v.front() == u'"' || v.front() == u'\'')) {
        const qsizetype close = v.indexOf(v.front(), 1);
        if (close > 0)
            return v.sliced(1, close - 1).toString();
    }
    if (const qsizetype comment = v.indexOf(u"//"); comment == 0 || (comment > 0 && v[comment - 1].isSpace()))
        v = v.first(comment).trimmed();
    return v.toString();
}

/// A size as written: "9", "9.5", "9px" (the "px" in any case); nullopt otherwise.
std::optional<double> numberOf(const QString &text)
{
    QString t = text.trimmed();
    if (t.endsWith(u"px"_s, Qt::CaseInsensitive))
        t.chop(2);
    t = t.trimmed();
    bool ok = false;
    const double d = t.toDouble(&ok);
    if (!ok || !std::isfinite(d))
        return std::nullopt;
    return d;
}

QString cut(const QString &text)
{
    return text.size() > 60 ? text.left(59) + u'…' : text;
}

QString numberText(double d)
{
    return d == std::floor(d) ? QString::number(qint64(d)) : QString::number(d);
}

QString valueText(const ThemeToken &token, const QVariant &value)
{
    if (token.kind == ThemeTokenKind::Number)
        return numberText(value.toDouble());
    const QString text = value.toString();
    // A family's name with characters a reader would take for the syntax keeps its quotes.
    if (token.kind == ThemeTokenKind::Font && (text.contains(u"//"_s) || text.startsWith(u'"') || text.startsWith(u'\'')))
        return u'"' + text + u'"';
    return text;
}

void appendTokens(QStringList &out, const QVariantMap &tokens, const QString &indent)
{
    for (const ThemeToken &t : themeTokens()) {
        const auto it = tokens.constFind(t.name);
        if (it != tokens.constEnd())
            out << indent + t.name + u": "_s + valueText(t, *it);
    }
}

} // namespace

bool isThemePage(const ParsedNote &note)
{
    // note.tags has every tag of the page (blocks too): the cheap test first.
    if (std::ranges::none_of(themeTags(), [&](const QString &t) { return note.tags.contains(t); }))
        return false;
    const QStringList tags = pageTags(note);
    return std::ranges::any_of(themeTags(), [&](const QString &t) { return tags.contains(t); });
}

QString themeDescription(const ParsedNote &note)
{
    for (const QVariantMap *map : {&note.properties, &note.frontmatter}) {
        for (auto it = map->cbegin(); it != map->cend(); ++it) {
            if (it.key().compare(u"opis", Qt::CaseInsensitive) == 0
                || it.key().compare(u"description", Qt::CaseInsensitive) == 0)
                return it.value().toString().simplified().left(200);
        }
    }
    return {};
}

ThemePage parseThemeBlock(const QStringList &lines, int firstLine, const FontCheck &fontInstalled)
{
    ThemePage out;
    QString mode;               // "" both, "light", "dark"
    qsizetype modeIndent = -1;  // the header's indentation
    int count = 0;              // tokens kept (a token set again counts once)
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const int line = firstLine + int(i);
        QStringView text = lines[i];
        if (text.endsWith(u'\r'))
            text.chop(1);
        const QStringView trimmed = text.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(u'#') || trimmed.startsWith(u"//"))
            continue;
        const qsizetype indent = indentOf(text);
        if (!mode.isEmpty() && indent <= modeIndent) {
            mode.clear(); // a line no deeper than the header ends its mode
            modeIndent = -1;
        }
        const qsizetype colon = trimmed.indexOf(u':');
        if (colon <= 0) {
            out.issues.append({line, ThemeIssueKind::Syntax, QString(), cut(trimmed.toString()), {}});
            continue;
        }
        const QString name = trimmed.first(colon).trimmed().toString();
        const QString rawValue = trimmed.sliced(colon + 1).trimmed().toString();
        // A name is one word of letters and digits: anything else is prose, not a token.
        if (name.isEmpty() || !std::ranges::all_of(name, [](QChar c) { return c.isLetterOrNumber(); })) {
            out.issues.append({line, ThemeIssueKind::Syntax, QString(), cut(trimmed.toString()), {}});
            continue;
        }
        if (const QString header = modeOf(name); !header.isEmpty()) {
            if (!mode.isEmpty()) {
                out.issues.append({line, ThemeIssueKind::ModeInMode, name, {}, mode});
                continue;
            }
            if (!valueOf(rawValue).isEmpty()) {
                out.issues.append({line, ThemeIssueKind::ModeValue, name, cut(rawValue), {}});
                continue;
            }
            mode = header;
            modeIndent = indent;
            continue;
        }
        const ThemeToken *token = tokenNamed(name);
        if (!token) {
            out.issues.append({line, ThemeIssueKind::UnknownToken, name, cut(rawValue), nearestThemeToken(name)});
            continue;
        }
        const QString value = valueOf(rawValue);
        QVariant normalized;
        QString why;
        bool ok = false;
        ThemeIssueKind kind = ThemeIssueKind::BadColor;
        switch (token->kind) {
        case ThemeTokenKind::Color:
            ok = themeTokenValue(*token, value, &normalized, &why);
            kind = ThemeIssueKind::BadColor;
            break;
        case ThemeTokenKind::Number:
            if (const std::optional<double> d = numberOf(value))
                ok = themeTokenValue(*token, *d, &normalized, &why);
            kind = ThemeIssueKind::BadNumber;
            break;
        case ThemeTokenKind::Font:
            ok = themeTokenValue(*token, value, &normalized, &why);
            kind = ThemeIssueKind::BadFont;
            if (ok && !normalized.toString().isEmpty() && fontInstalled && !fontInstalled(normalized.toString())) {
                out.issues.append({line, ThemeIssueKind::MissingFont, token->name, cut(value), {}});
                continue;
            }
            break;
        }
        if (!ok) {
            const QString detail = token->kind == ThemeTokenKind::Number
                                           ? numberText(token->min) + u" … "_s + numberText(token->max)
                                           : QString();
            out.issues.append({line, kind, token->name, cut(value), detail});
            continue;
        }
        QVariantMap &into = mode == u"light" ? out.layer.light : mode == u"dark" ? out.layer.dark : out.layer.both;
        if (!into.contains(token->name)) {
            if (count >= MaxThemeTokens) {
                out.issues.append({line, ThemeIssueKind::TooMany, token->name, cut(value),
                                   QString::number(MaxThemeTokens)});
                continue;
            }
            ++count;
        }
        into.insert(token->name, normalized);
    }
    return out;
}

ThemePage parseThemePage(QStringView content, const FontCheck &fontInstalled)
{
    ThemePage out;
    int count = 0;
    for (const FencedBlock &block : fencedBlocks(content, u"theme"_s)) {
        ++out.blocks;
        if (out.firstLine < 0)
            out.firstLine = block.open;
        const ThemePage part = parseThemeBlock(block.lines, block.open + 1, fontInstalled);
        out.issues += part.issues;
        // The blocks add up in order (a later one wins a token), within the page's limit.
        for (const auto &[from, into] : {std::pair{&part.layer.both, &out.layer.both},
                                         std::pair{&part.layer.light, &out.layer.light},
                                         std::pair{&part.layer.dark, &out.layer.dark}}) {
            for (auto it = from->cbegin(); it != from->cend(); ++it) {
                if (!into->contains(it.key())) {
                    if (count >= MaxThemeTokens) {
                        out.issues.append({block.open + 1, ThemeIssueKind::TooMany, it.key(), {},
                                           QString::number(MaxThemeTokens)});
                        continue;
                    }
                    ++count;
                }
                into->insert(it.key(), it.value());
            }
        }
    }
    std::ranges::stable_sort(out.issues, {}, &ThemeIssue::line);
    return out;
}

QStringList themeBlockLines(const ThemeLayer &layer)
{
    QStringList out;
    appendTokens(out, layer.both, QString());
    for (const auto &[header, tokens] : {std::pair{u"light"_s, &layer.light}, std::pair{u"dark"_s, &layer.dark}}) {
        if (tokens->isEmpty())
            continue;
        out << header + u':';
        appendTokens(out, *tokens, u"  "_s);
    }
    return out;
}

QString themeBlockText(const ThemeLayer &layer)
{
    QString text = u"```theme\n"_s;
    for (const QString &line : themeBlockLines(layer))
        text += line + u'\n';
    return text + u"```\n"_s;
}

ThemeLayer foldedThemeLayer(const QVariantMap &light, const QVariantMap &dark)
{
    ThemeLayer out;
    for (auto it = light.cbegin(); it != light.cend(); ++it) {
        const auto there = dark.constFind(it.key());
        if (there != dark.constEnd() && *there == it.value())
            out.both.insert(it.key(), it.value());
        else
            out.light.insert(it.key(), it.value());
    }
    for (auto it = dark.cbegin(); it != dark.cend(); ++it)
        if (!out.both.contains(it.key()))
            out.dark.insert(it.key(), it.value());
    return out;
}

} // namespace brulion::extensions
