#include "io/clipboardhtml.h"

#include "io/htmlrender_p.h"
#include "markdown/parse.h"

using namespace Qt::StringLiterals;

namespace brulion::io {

QString clipboardHtml(const QString &markdown, const ClipboardHtmlOptions &options)
{
    if (markdown.trimmed().isEmpty())
        return {};
    const ParsedNote note = markdown::parseNote(QString(), markdown, options.config);
    detail::HtmlLinks links;
    links.mediaAsLinks = true;
    // Notes and tags: their text (no link leads anywhere outside the notebook).
    links.attachment = [&](const QString &target) -> std::optional<QString> {
        const QString url = options.attachmentUrl ? options.attachmentUrl(target) : QString();
        if (url.isEmpty() || url.startsWith(u"data:"_s, Qt::CaseInsensitive))
            return std::nullopt;
        return url;
    };
    links.block = [&](const QString &id) -> std::optional<std::pair<QString, QString>> {
        const QString text = options.blockText ? options.blockText(id) : QString();
        if (text.isEmpty())
            return std::nullopt;
        return std::make_pair(QString(), text);
    };
    const detail::HtmlRenderer renderer(links);
    return u"<meta charset=\"utf-8\">"_s + QString(ClipboardHtmlMarker) + renderer.renderNote(note);
}

} // namespace brulion::io
