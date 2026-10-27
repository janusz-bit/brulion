// LazyHtmlMimeData — what a block copy puts on the clipboard (docs/UI.md "Selecting and copying"):
// the Markdown as text/plain, and a text/html flavour that is advertised at once but rendered
// only when an application asks for it (a paste into a mail or office app reads it; a paste into
// a code editor, a terminal or Brulion itself never does). A whole 5000-block page copies in
// ~4 instead of ~40 ms of key handling on Linux (X11, Wayland; docs/PERFORMANCE.md).
//
//   auto *mime = new LazyHtmlMimeData(markdown, [host, markdown, path] { return host->renderHtml(markdown, path); });
//   QGuiApplication::clipboard()->setMimeData(mime);
//
// The render function runs at most once (its result is kept) on the GUI thread — inside the
// platform's request for the data: X11's SelectionRequest, Wayland's send. It must
// capture copies (the Markdown is a snapshot) and must not rely on objects that may be gone by then
// (EditorHost through a QPointer: "" without one). When the application quits while this is still
// the clipboard's content, the HTML is rendered first (materialize()): X11's hand-over to a
// clipboard manager reads every format during shutdown, when the notebook may already be closed.
// An empty render gives no data for text/html.
//
// Platforms that read the data inside setMimeData() (Android builds its ClipData there, the
// browser writes navigator.clipboard in the same gesture, Windows' OleSetClipboard enumerates the
// formats and Qt offers CF_HTML only for a non-empty html()) gain nothing: clipboardReadsAtOnce()
// tells the editor to render there before it decides what goes on the clipboard.
#pragma once

#include <QMimeData>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <functional>
#include <optional>

namespace brulion::editor {

class LazyHtmlMimeData : public QMimeData {
    Q_OBJECT
public:
    using Render = std::function<QString()>;

    /// `text` as text/plain at once; text/html from `render` when asked (no `render`: no HTML).
    LazyHtmlMimeData(const QString &text, Render render);

    QStringList formats() const override;
    bool hasFormat(const QString &mimeType) const override;

    /// Render the HTML now (once; kept): shutdown, platforms that read the data at once.
    void materialize() const;
    /// The HTML was rendered already (tests, diagnostics).
    bool rendered() const { return m_html.has_value(); }

protected:
    QVariant retrieveData(const QString &mimeType, QMetaType type) const override;

private:
    Render m_render;
    mutable std::optional<QString> m_html;
};

/// The platform's clipboard takes every format inside QClipboard::setMimeData() (Android, the
/// browser, Windows): a lazy flavour would be rendered right there anyway, so the editor renders first and
/// keeps the size rule (PageController::ClipboardLimit for both flavours together).
bool clipboardReadsAtOnce();

} // namespace brulion::editor
