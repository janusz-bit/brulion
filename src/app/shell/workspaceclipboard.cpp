// Workspace: the clipboard formats of the block editor's copy and paste (EditorHost::renderHtml,
// EditorHost::htmlToMarkdown; docs/UI.md "Selecting and copying").
#include "workspace.h"

#include "index/vaultindex.h"
#include "io/clipboardhtml.h"
#include "io/htmlmarkdown.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QUrl>

namespace brulion::shell {

QString Workspace::renderHtml(const QString &markdown, const QString &fromPath) const
{
    io::ClipboardHtmlOptions options;
    if (m_vault)
        options.config = m_vault->config();
    const QString from = paths::normalize(fromPath);
    options.attachmentUrl = [this, from](const QString &target) -> QString {
        // A link to the file on disk; the browser's assets are data: URLs (never on the clipboard).
        if (!m_vault || !m_index || m_dataUrlAssets)
            return {};
        const auto path = m_index->resolveLink(target, from);
        if (!path || !m_vault->exists(*path))
            return {};
        return QUrl::fromLocalFile(m_vault->absolutePath(*path)).toString();
    };
    options.blockText = [this](const QString &id) { return blockText(id); };
    return io::clipboardHtml(markdown, options);
}

QString Workspace::htmlToMarkdown(const QString &html) const
{
    return io::htmlToMarkdown(html).markdown;
}

} // namespace brulion::shell
