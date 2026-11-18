// The parity check of RenderedText (markdown/renderedtext.h), linked into a second build of the
// renderer's tests (tst_markdown_renderparity_*): every render() and renderBlock() those tests make
// is also rendered with renderRuns() / renderBlockRuns(), which must give exactly the same HTML
// back through runsToHtml(), or be marked not valid (HTML that is not well formed) and not drawable.
// The traced HTML must be the HTML, and the source map must stay inside the text and the display.
// A failure is printed and makes the test exit with 1 after its own run.
#include "markdown/inlinerenderer.h"
#include "markdown/renderedtext.h"

#include <QMutex>
#include <QStringList>

#include <cstdio>
#include <cstdlib>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

struct Stats {
    QMutex mutex;
    qsizetype checked = 0;
    qsizetype invalid = 0;
    qsizetype undrawable = 0;
    QStringList failures;
};

Stats &stats()
{
    static Stats s;
    return s;
}

QString quoted(QStringView s)
{
    QString out;
    for (const QChar c : s) {
        if (c == u'\n')
            out += u"\\n"_s;
        else if (c == u'\r')
            out += u"\\r"_s;
        else
            out += c;
    }
    return out;
}

void check(const InlineRenderer &renderer, QStringView text, std::optional<BlockKind> kind, const QString &html)
{
    const TracedHtml traced = kind ? renderer.renderBlockTraced(text, *kind) : renderer.renderTraced(text);
    const RenderedText runs = kind ? renderer.renderBlockRuns(text, *kind) : renderer.renderRuns(text);
    QStringList problems;
    if (traced.html != html)
        problems.append(u"traced HTML differs: "_s + traced.html);
    const QString back = runsToHtml(runs);
    if (runs.valid && back != html)
        problems.append(u"runsToHtml differs: "_s + back);
    if (!runs.valid && runs.drawable)
        problems.append(u"not valid but drawable"_s);
    if (renderedTextFromHtml(html).text != runs.text)
        problems.append(u"the source map changes the text"_s);
    qsizetype lastDisplay = 0;
    for (const RenderedSourceSegment &s : runs.source) {
        if (s.displayFrom < lastDisplay || s.displayTo > runs.text.size() || s.displayFrom >= s.displayTo
            || s.sourceFrom < 0 || s.sourceTo > text.size() || s.sourceFrom > s.sourceTo) {
            problems.append(u"bad source segment %1-%2 -> %3-%4"_s.arg(s.displayFrom)
                                    .arg(s.displayTo)
                                    .arg(s.sourceFrom)
                                    .arg(s.sourceTo));
            break;
        }
        lastDisplay = s.displayTo;
    }
    for (const RenderedRun &r : runs.runs) {
        if (r.from < 0 || r.to > runs.text.size() || r.from >= r.to) {
            problems.append(u"bad run"_s);
            break;
        }
    }
    Stats &s = stats();
    const QMutexLocker lock(&s.mutex);
    ++s.checked;
    if (!runs.valid)
        ++s.invalid;
    if (!runs.drawable)
        ++s.undrawable;
    if (!problems.isEmpty()) {
        s.failures.append(u"input (kind %1): \"%2\"\n  html: %3\n  %4"_s.arg(kind ? int(*kind) : -1)
                                  .arg(quoted(text), html, problems.join(u"\n  "_s)));
    }
}

struct Install {
    Install() { InlineRenderer::setRenderObserver(&check); }
    ~Install()
    {
        InlineRenderer::setRenderObserver(nullptr);
        Stats &s = stats();
        std::fprintf(stderr,
                     "render parity: %lld renderings checked, %lld not valid, %lld not drawable, %lld failures\n",
                     qlonglong(s.checked), qlonglong(s.invalid), qlonglong(s.undrawable), qlonglong(s.failures.size()));
        for (const QString &f : std::as_const(s.failures).first(std::min<qsizetype>(s.failures.size(), 40)))
            std::fprintf(stderr, "PARITY FAIL %s\n", qPrintable(f));
        if (!s.failures.isEmpty() || s.checked == 0)
            std::_Exit(1);
    }
};

const Install install;

} // namespace
