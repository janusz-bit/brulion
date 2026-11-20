#include "markdown/cloze.h"

namespace brulion::markdown {

QList<ClozeSpan> findClozes(QStringView text)
{
    QList<ClozeSpan> out;
    qsizetype i = 0;
    while (true) {
        const qsizetype open = text.indexOf(u"{{", i);
        if (open < 0)
            break;
        qsizetype p = open + 2;
        ClozeSpan span;
        span.from = open;
        bool head = false;
        if (p < text.size() && (text[p] == u'c' || text[p] == u'C')) {
            qsizetype d = p + 1;
            int number = 0;
            while (d < text.size() && d - p - 1 < 3 && text[d] >= u'0' && text[d] <= u'9') {
                number = number * 10 + (text[d].unicode() - u'0');
                ++d;
            }
            if (d > p + 1 && text.sliced(d).startsWith(u"::")) {
                span.number = number;
                span.textFrom = d + 2;
                head = true;
            } else if (text.sliced(p).startsWith(u"cloze") && p + 5 < text.size()
                       && (text[p + 5] == u' ' || text[p + 5] == u':' || text[p + 5] == u'\t')) {
                qsizetype t = p + 6;
                while (t < text.size() && (text[t] == u' ' || text[t] == u'\t'))
                    ++t;
                span.textFrom = t;
                head = true;
            }
        }
        if (!head) {
            i = open + 2;
            continue;
        }
        // The answer runs to the first "}}" of the line; another "{{" before it ends this one.
        qsizetype close = -1;
        bool broken = false;
        for (qsizetype k = span.textFrom; k + 1 < text.size(); ++k) {
            const QChar c = text[k];
            if (c == u'\n' || c == u'\r') {
                broken = true;
                break;
            }
            if (c == u'}' && text[k + 1] == u'}') {
                close = k;
                break;
            }
            if (c == u'{' && text[k + 1] == u'{') {
                broken = true;
                break;
            }
        }
        if (broken || close < 0) {
            i = open + 2;
            continue;
        }
        span.to = close + 2;
        span.textTo = close;
        if (span.number > 0) {
            const qsizetype hint = text.sliced(span.textFrom, close - span.textFrom).indexOf(u"::");
            if (hint >= 0) {
                span.textTo = span.textFrom + hint;
                span.hintFrom = span.textTo + 2;
                span.hintTo = close;
            }
        }
        if (text.sliced(span.textFrom, span.textTo - span.textFrom).trimmed().isEmpty()) {
            i = open + 2;
            continue;
        }
        out.append(span);
        i = span.to;
    }
    return out;
}

} // namespace brulion::markdown
