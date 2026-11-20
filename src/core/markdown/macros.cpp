#include "markdown/macros.h"

using namespace Qt::StringLiterals;

namespace brulion::markdown {

namespace {

constexpr int MaxArguments = 32;
constexpr qsizetype MaxArgumentLength = 500;

bool isBlank(QChar c) { return c == u' ' || c == u'\t'; }
bool isLineBreak(QChar c) { return c == u'\n' || c == u'\r'; }
bool isIdChar(QChar c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'-' || c == u'_';
}
bool isNameChar(QChar c) { return c.isLetterOrNumber() || c == u'-' || c == u'_' || c == u'.'; }

} // namespace

QList<MacroSpan> findMacros(QStringView text)
{
    QList<MacroSpan> out;
    qsizetype searchFrom = 0;
    while (true) {
        const qsizetype open = text.indexOf(u"{{", searchFrom);
        if (open < 0)
            break;
        searchFrom = open + 1;
        if (open > 0 && text[open - 1] == u'\\')
            continue; // \{{ext:…}}: text
        qsizetype i = open + 2;
        while (i < text.size() && isBlank(text[i]))
            ++i;
        if (text.sliced(i).size() < 4 || text.sliced(i, 4).compare(u"ext:", Qt::CaseInsensitive) != 0)
            continue;
        i += 4;
        const qsizetype idStart = i;
        while (i < text.size() && isIdChar(text[i]))
            ++i;
        if (i == idStart || i >= text.size() || text[i] != u'/')
            continue;
        const QString extension = text.sliced(idStart, i - idStart).toString().toLower();
        ++i;
        const qsizetype nameStart = i;
        while (i < text.size() && isNameChar(text[i]))
            ++i;
        if (i == nameStart)
            continue;
        const QString name = text.sliced(nameStart, i - nameStart).toString();
        const qsizetype nameEnd = i;
        while (i < text.size() && isBlank(text[i]))
            ++i;
        qsizetype close = -1;
        QString arguments;
        if (text.sliced(i).startsWith(u"}}")) {
            close = i;
        } else if (i > nameEnd) {
            // The arguments: up to the first `}}` of the line, with no `{{` before it.
            for (qsizetype k = i; k + 1 < text.size() && k - open <= MaxMacroLength; ++k) {
                const QChar c = text[k];
                if (isLineBreak(c))
                    break;
                if (c == u'{' && text[k + 1] == u'{')
                    break;
                if (c == u'}' && text[k + 1] == u'}') {
                    close = k;
                    break;
                }
            }
            if (close >= 0)
                arguments = text.sliced(i, close - i).trimmed().toString();
        }
        if (close < 0 || close + 2 - open > MaxMacroLength)
            continue;
        out.append({open, close + 2, extension, name, arguments});
        searchFrom = close + 2;
    }
    return out;
}

QStringList macroArguments(QStringView arguments)
{
    QStringList out;
    if (arguments.trimmed().isEmpty())
        return out;
    QString current;
    bool quoted = false;
    const auto finish = [&] {
        QString arg = current.trimmed();
        if (arg.size() >= 2 && arg.startsWith(u'"') && arg.endsWith(u'"'))
            arg = arg.sliced(1, arg.size() - 2);
        out.append(arg.left(MaxArgumentLength));
        current.clear();
    };
    for (const QChar c : arguments) {
        if (c == u'"')
            quoted = !quoted;
        if (c == u',' && !quoted && out.size() < MaxArguments - 1) {
            finish();
            continue;
        }
        if (current.size() <= MaxArgumentLength + 2)
            current.append(c);
    }
    finish();
    return out;
}

QString macroSource(const QString &extension, const QString &name, const QString &arguments)
{
    return u"{{ext:"_s + extension + u'/' + name + (arguments.isEmpty() ? QString() : u' ' + arguments) + u"}}"_s;
}

bool isMacroName(QStringView name)
{
    if (name.isEmpty() || name.size() > 64 || !name.front().isLetterOrNumber())
        return false;
    for (const QChar c : name) {
        if (!isNameChar(c))
            return false;
    }
    return true;
}

ViewBlock parseViewBlock(QStringView source)
{
    ViewBlock out;
    constexpr qsizetype MaxSource = 64 * 1024;
    constexpr int MaxParams = 64;
    if (source.size() > MaxSource) {
        out.problem = u"the block is longer than 64 KB"_s;
        return out;
    }
    const QList<QStringView> lines = source.split(u'\n');
    int number = 0;
    for (QStringView raw : lines) {
        ++number;
        QStringView line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(u'#') || line.startsWith(u"//"))
            continue;
        if (out.extension.isEmpty()) {
            if (line.startsWith(u"view:", Qt::CaseInsensitive))
                line = line.sliced(5).trimmed();
            const qsizetype slash = line.indexOf(u'/');
            const QStringView id = slash > 0 ? line.first(slash) : QStringView();
            const QStringView view = slash > 0 ? line.sliced(slash + 1).trimmed() : QStringView();
            bool idOk = !id.isEmpty() && id.size() <= 64;
            for (const QChar c : id)
                idOk = idOk && (c.isLetterOrNumber() && c.unicode() < 128 ? true : c == u'-' || c == u'_');
            if (!idOk || !isMacroName(view)) {
                out.problem = u"line %1: the first line names the view, <extension>/<view>"_s.arg(number);
                return out;
            }
            out.extension = id.toString().toLower();
            out.view = view.toString();
            continue;
        }
        const qsizetype colon = line.indexOf(u':');
        QStringView key = colon > 0 ? line.first(colon).trimmed() : QStringView();
        bool keyOk = !key.isEmpty() && key.size() <= 64;
        for (const QChar c : key)
            keyOk = keyOk && (c.isLetterOrNumber() || c == u'-' || c == u'_');
        if (!keyOk) {
            if (out.problem.isEmpty())
                out.problem = u"line %1: not key: value"_s.arg(number);
            continue;
        }
        if (out.params.size() >= MaxParams && !out.params.contains(key.toString())) {
            if (out.problem.isEmpty())
                out.problem = u"more than %1 params"_s.arg(MaxParams);
            continue;
        }
        out.params.insert(key.toString(), line.sliced(colon + 1).trimmed().toString());
    }
    if (out.extension.isEmpty() && out.problem.isEmpty())
        out.problem = u"the first line names the view, <extension>/<view>"_s;
    return out;
}

QString macroViewText(const MacroView &view)
{
    switch (view.state) {
    case MacroView::State::Ok: break;
    case MacroView::State::Pending: return u"pending"_s;
    case MacroView::State::Unapproved: return u"unapproved"_s;
    case MacroView::State::Off: return u"off"_s;
    case MacroView::State::Unknown: return u"unknown"_s;
    case MacroView::State::Error: return u"error: "_s + view.message;
    case MacroView::State::TooMany: return u"too many: "_s + view.message;
    }
    QString out;
    for (const MacroPiece &p : view.pieces) {
        switch (p.kind) {
        case MacroPiece::Kind::Text: out += p.text; break;
        case MacroPiece::Kind::Link: out += u"[["_s + p.page + (p.text.isEmpty() || p.text == p.page ? QString() : u'|' + p.text) + u"]]"_s; break;
        case MacroPiece::Kind::Code: out += u'`' + p.text + u'`'; break;
        }
    }
    return out;
}

} // namespace brulion::markdown
