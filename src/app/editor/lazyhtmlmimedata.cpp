#include "lazyhtmlmimedata.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {
const QString HtmlMime = u"text/html"_s;
} // namespace

LazyHtmlMimeData::LazyHtmlMimeData(const QString &text, Render render)
    : m_render(std::move(render))
{
    setText(text);
    // Shutdown reads every format (X11's hand-over to a clipboard manager): rendered while the
    // notebook is still open. Only while this is still what the clipboard holds.
    if (m_render && QCoreApplication::instance()) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
            if (qGuiApp && QGuiApplication::clipboard() && QGuiApplication::clipboard()->mimeData() == this)
                materialize();
        });
    }
}

QStringList LazyHtmlMimeData::formats() const
{
    QStringList out = QMimeData::formats();
    if (m_render && !out.contains(HtmlMime))
        out.append(HtmlMime);
    return out;
}

bool LazyHtmlMimeData::hasFormat(const QString &mimeType) const
{
    if (mimeType == HtmlMime && m_render)
        return true;
    return QMimeData::hasFormat(mimeType);
}

void LazyHtmlMimeData::materialize() const
{
    if (m_html || !m_render)
        return;
    m_html = m_render();
}

QVariant LazyHtmlMimeData::retrieveData(const QString &mimeType, QMetaType type) const
{
    if (mimeType != HtmlMime || !m_render)
        return QMimeData::retrieveData(mimeType, type);
    materialize();
    if (m_html->isEmpty())
        return {};
    return *m_html; // a QString: QMimeData::data() gives it as UTF-8
}

bool clipboardReadsAtOnce()
{
    const QString platform = QGuiApplication::platformName();
    // Windows: OleSetClipboard() enumerates the formats, and Qt's CF_HTML converter offers its
    // format only for non-empty html() — the render runs inside setMimeData() there too.
    return platform == u"android" || platform == u"wasm" || platform == u"windows";
}

} // namespace brulion::editor
