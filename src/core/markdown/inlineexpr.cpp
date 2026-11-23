// Inline expressions of a note (inlineexpr.h).
#include "markdown/inlineexpr.h"
#include "markdown/lines_p.h"

namespace brulion::markdown {

QList<InlineExpression> findInlineExpressions(QStringView content)
{
    QList<InlineExpression> out;
    QChar fence;
    qsizetype fenceLength = 0;
    qsizetype start = 0;
    int number = 0;
    while (start <= content.size()) {
        qsizetype end = content.indexOf(u'\n', start);
        if (end < 0)
            end = content.size();
        const QStringView line = content.sliced(start, end - start);
        // Fenced code: from a line of three or more backticks or tildes to one of the same
        // character at least as long (a list item's indented fence too), as findMacrosInNote().
        const QStringView trimmed = line.trimmed();
        const QStringView marker =
                trimmed.startsWith(u"- ") || trimmed.startsWith(u"* ") ? trimmed.sliced(2).trimmed() : trimmed;
        qsizetype run = 0;
        const QChar first = marker.isEmpty() ? QChar() : marker.front();
        if (first == u'`' || first == u'~') {
            while (run < marker.size() && marker[run] == first)
                ++run;
        }
        if (fenceLength > 0) {
            if (first == fence && run >= fenceLength && marker.sliced(run).trimmed().isEmpty())
                fenceLength = 0;
        } else if (run >= 3) {
            fence = first;
            fenceLength = run;
        } else if (line.contains(u'`')) {
            for (const detail::InlineSpan &span : detail::inlineSpans(line)) {
                if (span.kind != detail::InlineSpan::Kind::Code)
                    continue;
                const QStringView code =
                        line.sliced(span.from + span.fence, span.to - span.from - 2 * span.fence).trimmed();
                if (!code.startsWith(u'='))
                    continue;
                const QString expression = code.sliced(1).trimmed().toString();
                if (expression.isEmpty())
                    continue;
                out.append({start + span.from, start + span.to, number, expression});
            }
        }
        if (end >= content.size())
            break;
        start = end + 1;
        ++number;
    }
    return out;
}

QString replaceInlineExpressions(QStringView content,
                                 const std::function<std::optional<QString>(const InlineExpression &)> &value)
{
    QString out;
    qsizetype last = 0;
    for (const InlineExpression &e : findInlineExpressions(content)) {
        const std::optional<QString> text = value ? value(e) : std::nullopt;
        if (!text)
            continue;
        out += content.sliced(last, e.from - last);
        out += *text;
        last = e.to;
    }
    out += content.sliced(last);
    return out;
}

QString escapedMarkdownText(QStringView text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (QStringView(u"\\`*_[]#<>~=|$!{}%").contains(c))
            out.append(u'\\');
        out.append(c == u'\n' || c == u'\r' ? u' ' : c);
    }
    return out;
}

} // namespace brulion::markdown
