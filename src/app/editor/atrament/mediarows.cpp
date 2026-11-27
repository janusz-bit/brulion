// PageView's pictures, files and embedded blocks (pageview.h "Pictures, files, embedded blocks and
// query results"; docs/ATRAMENT.md M2 step 5): a block's embeds under its text as EmbedView draws
// them — pictures at their natural size up to the column's width and 420 px high (the size from
// the file's header, so the row has its height before the picture is decoded), PDF thumbnails
// (PdfEmbedTile), files as chips — and its embedded pages, sections and blocks in
// TransclusionView's frames. Where BlockDelegate places them: below the text (none for a block of
// nothing but embedded blocks, nor for a document's paragraph of nothing but pictures, unless it
// is edited), from the text column's edge.
#include "mediarows_p.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"
#include "picturecache.h"
#include "queryresultmodel.h"
#include "renderedtextitem.h"

#include "markdown/renderedtext.h"
#include "ui/list.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::editor::atr {

namespace at = brulion::atrament;

namespace {

bool textView(const QString &view)
{
    return view == u"text" || view == u"heading" || view == u"quote" || view == u"callout";
}

enum class EmbedKind { Picture, PdfTile, Chip, Foreign };

// An SVG: a local file by its name, a picture from the internet by the format of its bytes.
bool scalableEmbed(const QVariantMap &e)
{
    if (e.value(u"web"_s).toBool()) {
        const QString format = e.value(u"format"_s).toString().toLower();
        return format == u"svg" || format == u"svgz";
    }
    const QString name = e.value(u"name"_s).toString().toLower();
    return name.endsWith(u".svg"_s) || name.endsWith(u".svgz"_s);
}

// What EmbedView would show for an embed, as far as Atrament draws it: a picture (a local one, a
// cached one from the internet, a format the device cannot decode — its frame says so), a PDF's
// thumbnail, a chip; Foreign for the rest (drawings — their previews come from an image provider —,
// players, bases, a picture from the internet not shown yet, a PDF's thumbnail being drawn).
EmbedKind embedKind(const QVariantMap &e)
{
    if (e.value(u"base"_s).toBool() || e.value(u"media"_s).toBool() || e.value(u"drawing"_s).toBool())
        return EmbedKind::Foreign;
    const QUrl url = e.value(u"url"_s).toUrl();
    if (e.value(u"pdf"_s).toBool()) {
        const QString state = e.value(u"state"_s).toString();
        if (!e.value(u"viewable"_s).toBool() || state == u"unsupported")
            return EmbedKind::Chip;
        return state == u"ready" && PictureCache::readable(url) ? EmbedKind::PdfTile : EmbedKind::Foreign;
    }
    const bool web = e.value(u"web"_s).toBool();
    if (!e.value(u"image"_s).toBool() || (!web && url.isEmpty()))
        return EmbedKind::Chip;
    const bool decodable = e.value(u"decodable"_s, true).toBool();
    if (web) {
        const QString format = e.value(u"format"_s).toString().toLower();
        return e.value(u"state"_s).toString() == u"ready" && decodable && PictureCache::readable(url)
                               && PictureCache::decodableName(u"picture."_s + format)
                       ? EmbedKind::Picture
                       : EmbedKind::Foreign;
    }
    if (!decodable)
        return EmbedKind::Picture; // (its frame names the format)
    return PictureCache::readable(url) && PictureCache::decodableName(e.value(u"name"_s).toString())
                   ? EmbedKind::Picture
                   : EmbedKind::Foreign;
}

// The cache's key of an embed's picture: short (a data: URL is the whole file).
QString pictureKey(const QVariantMap &e)
{
    if (e.value(u"web"_s).toBool())
        return u"web:"_s + e.value(u"target"_s).toString();
    if (e.value(u"pdf"_s).toBool())
        return u"pdf:"_s + e.value(u"path"_s).toString() + u'#' + QString::number(e.value(u"page"_s).toInt());
    const QString path = e.value(u"path"_s).toString();
    return path.isEmpty() ? e.value(u"url"_s).toUrl().toString().left(512) : u"file:"_s + path;
}

// A picture's size as EmbedView's Image shows it once loaded: a raster picture's pixels (at most
// 2048 a side), an SVG fitted into 1024 × 420 (rendered at the screen's ratio `dpr`).
QSizeF shownSize(QSize file, bool scalable, float dpr)
{
    const float boxW = scalable ? 1024 * dpr : 2048;
    const float boxH = scalable ? 420 * dpr : 2048;
    const float fit = scalable || float(file.width()) > boxW || float(file.height()) > boxH
                              ? std::min(boxW / float(file.width()), boxH / float(file.height()))
                              : 1.0f;
    const float d = scalable ? dpr : 1.0f;
    return {std::floor(float(file.width()) * fit) / d, std::floor(float(file.height()) * fit) / d};
}

// The file's decoded picture has another shape than its header said (EmbedView's otherShape).
bool otherShape(QSize seen, QSize header)
{
    if (seen.isEmpty() || header.isEmpty())
        return false;
    const double h = double(seen.width()) * header.height() / header.width();
    const double w = double(seen.height()) * header.width() / header.height();
    return std::abs(seen.height() - h) > std::max(1.5, 0.02 * h) && std::abs(seen.width() - w) > std::max(1.5, 0.02 * w);
}

// At most as wide as the column (CSS's max-width: 100%).
at::Div capWidth(at::Div &&d)
{
    d.styleRef().layout.maxWidth = at::pct(100);
    return std::move(d);
}

// `<pre>…</pre>` of an embedded code block (QString::toHtmlEscaped's escapes undone).
QString preText(const QString &html)
{
    QString s = html;
    if (s.startsWith(u"<pre>"_s))
        s.remove(0, 5);
    if (s.endsWith(u"</pre>"_s))
        s.chop(6);
    s.replace(u"&lt;"_s, u"<"_s).replace(u"&gt;"_s, u">"_s).replace(u"&quot;"_s, u"\""_s).replace(u"&amp;"_s, u"&"_s);
    return s;
}

} // namespace

const PageView::Media::Parsed &PageView::Media::parse(quint64 uid, const QVariantList &list)
{
    Parsed &p = transclusions[uid];
    if (p.parsed && p.source == list)
        return p;
    const std::uint64_t drawn = p.frame;
    p = Parsed{};
    p.source = list;
    p.parsed = true;
    p.frame = drawn;
    p.drawable = true;
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        Frame f;
        f.kind = m.value(u"kind"_s).toString();
        f.title = m.value(u"title"_s).toString();
        f.href = m.value(u"href"_s).toString();
        f.message = m.value(u"message"_s).toString();
        f.found = m.value(u"found"_s).toBool();
        f.more = m.value(u"more"_s).toInt();
        const QVariantList blocks = m.value(u"blocks"_s).toList();
        for (const QVariant &b : blocks) {
            const QVariantMap bm = b.toMap();
            Entry e;
            e.depth = bm.value(u"depth"_s).toInt();
            e.level = bm.value(u"headingLevel"_s).toInt();
            e.view = bm.value(u"view"_s).toString();
            e.task = bm.value(u"task"_s).toString();
            e.path = bm.value(u"path"_s).toString();
            e.line = bm.value(u"line"_s, -1).toInt();
            e.raw = bm.value(u"raw"_s).toString();
            e.title = bm.value(u"title"_s).toString();
            e.href = bm.value(u"href"_s).toString();
            e.message = bm.value(u"message"_s).toString();
            e.found = bm.value(u"found"_s).toBool();
            const QString html = bm.value(u"html"_s).toString();
            if (bm.value(u"kind"_s).toString() == u"embed") {
                e.kind = Entry::Kind::Nested;
            } else if (e.view == u"rule") {
                e.kind = Entry::Kind::Rule;
            } else if (e.view == u"code") {
                e.kind = Entry::Kind::Code;
                e.plain = preText(html);
            } else {
                auto text = std::make_shared<markdown::RenderedText>(markdown::renderedTextFromHtml(html));
                if (!text->valid || !text->drawable)
                    p.drawable = false; // (math, preformatted text: the QML view's)
                e.text = std::move(text);
            }
            f.entries.push_back(std::move(e));
        }
        p.frames.push_back(std::move(f));
    }
    return p;
}

void PageView::setDrawsMedia(unsigned kinds, const MediaLook &look, at::Context<PageView> &cx)
{
    if (!m_media || !m_media->cache || !m_media->app)
        setPictureCache(m_media && m_media->cache ? m_media->cache : PictureCache::shared(), cx);
    m_mediaKinds = kinds;
    m_mediaLook = look;
    m_extents.markAllEstimated();
    cx.notify();
}

void PageView::setPictureCache(std::shared_ptr<PictureCache> cache, at::Context<PageView> &cx)
{
    if (!m_media)
        m_media = std::make_shared<Media>();
    if (m_media->cache)
        m_media->cache->unsubscribe(m_media->listener);
    m_media->cache = std::move(cache);
    m_media->held.clear();
    m_media->heldBefore.clear();
    if (!m_media->cache)
        m_media->cache = PictureCache::shared();
    // A picture decoded: the page draws again (the frames keep their sizes: nothing moves).
    at::App *app = &cx.app();
    const at::EntityId self = cx.entityId();
    m_media->app = app;
    m_media->self = self;
    m_media->listener = m_media->cache->subscribe([app, self] {
        app->updateEntity<PageView>(self, [](PageView &, at::Context<PageView> &c) { c.notify(); });
    });
    cx.notify();
}

PictureCache &PageView::pictureCache()
{
    if (!m_media)
        m_media = std::make_shared<Media>();
    if (!m_media->cache)
        m_media->cache = PictureCache::shared();
    return *m_media->cache;
}

std::optional<bool> PageView::drawsMediaRow(int row) const
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return std::nullopt;
    const QModelIndex index = m_model->index(row, 0);
    const QString view = index.data(BlockListModel::ViewRole).toString();
    if (view == u"query") {
        // Its results (not a database view, not while its source is edited: QueryBlockView's
        // InlineEditor does that).
        if (!(m_mediaKinds & Queries) || !m_media)
            return false;
        const QVariantMap query = index.data(BlockListModel::QueryRole).toMap();
        return query.value(u"view"_s).toString().isEmpty() && !index.data(BlockListModel::IsEditingRole).toBool();
    }
    const QVariantList embeds =
            textView(view) ? index.data(BlockListModel::EmbedsRole).toList() : QVariantList();
    const QVariantList transclusions = index.data(BlockListModel::TransclusionsRole).toList();
    if (embeds.isEmpty() && transclusions.isEmpty())
        return std::nullopt;
    if (!textView(view) || !m_media || !m_mediaKinds)
        return false;
    const bool editing = index.data(BlockListModel::IsEditingRole).toBool();
    const bool only = index.data(BlockListModel::OnlyEmbedsRole).toBool();
    const bool listItem = index.data(BlockListModel::KindRole).toString() == u"listItem";
    const bool noText = !editing && only && view == u"text"
                        && ((!transclusions.isEmpty() && embeds.isEmpty())
                            || (m_metrics.documentMode && !embeds.isEmpty() && !listItem));
    if (!noText && !index.data(BlockListModel::RenderedTextRole).value<RenderedTextRef>().drawable())
        return false;
    for (const QVariant &v : embeds) {
        switch (embedKind(v.toMap())) {
        case EmbedKind::Picture:
        case EmbedKind::PdfTile:
            if (!(m_mediaKinds & Pictures))
                return false;
            break;
        case EmbedKind::Chip:
            if (!(m_mediaKinds & Files))
                return false;
            break;
        case EmbedKind::Foreign: return false;
        }
    }
    if (!transclusions.isEmpty()) {
        if (!(m_mediaKinds & Transclusions))
            return false;
        if (!m_media->parse(m_model->uidAt(row), transclusions).drawable)
            return false;
    }
    return true;
}

void PageView::mediaFrame()
{
    // Once a frame: the pictures held move one frame back, the state of rows not drawn for a while
    // goes (their query models too).
    if (m_pictureFrame == m_frame)
        return;
    m_pictureFrame = m_frame;
    m_pictureFrames.clear();
    if (!m_media)
        return;
    m_media->heldBefore = std::move(m_media->held);
    m_media->held.clear();
    for (auto it = m_media->transclusions.begin(); it != m_media->transclusions.end();)
        it = it->second.frame + 3 < m_frame ? m_media->transclusions.erase(it) : std::next(it);
    for (auto it = m_media->queries.begin(); it != m_media->queries.end();) {
        if (it->second.frame + 3 < m_frame) {
            for (const QMetaObject::Connection &c : it->second.connections)
                QObject::disconnect(c);
            // (Not inside a signal of its own: the frame runs from Atrament's loop.)
            it->second.model.release()->deleteLater();
            it = m_media->queries.erase(it);
        } else {
            ++it;
        }
    }
}

void PageView::mediaClicked(std::size_t row, const at::ClickEvent &click)
{
    // Beside a picture, on an embedded block's text: the block is edited (BlockDelegate.editOnTap(-1),
    // TransclusionView: startEditing(block, -1)) — or the editor takes the tap (Shift, the touch
    // selection mode).
    if (!m_controller || m_controller->readOnly() || click.isKeyboard || click.fromAccessibility)
        return;
    if (m_dragSelected) {
        m_dragSelected = false;
        return;
    }
    if (onEditRequest && onEditRequest(int(row), -1, click.up.modifiers))
        return;
    m_controller->startEditing(int(row), -1);
}

std::optional<at::AnyElement> PageView::mediaRow(std::size_t row, const QModelIndex &index)
{
    const std::optional<bool> draws = drawsMediaRow(int(row));
    if (!draws)
        return std::nullopt;
    mediaFrame();
    if (!*draws) {
        // The QML delegate draws it all (foreignrows.cpp).
        m_foreign.push_back(row);
        return foreignRow(row);
    }
    const PageMetrics &m = m_metrics;
    const QString view = index.data(BlockListModel::ViewRole).toString();
    const int depth = std::max(0, index.data(BlockListModel::DepthRole).toInt());
    const bool listItem = index.data(BlockListModel::KindRole).toString() == u"listItem";
    const QString task = index.data(BlockListModel::TaskStatusRole).toString();
    const bool marked = listItem || !task.isEmpty();
    // (renderRow's text column: PageItem::textX's geometry.)
    const float textX = m.documentMode ? m.gutter + (marked ? float(depth + 1) * m.listIndent : 0)
                                       : m.gutter + float(depth) * m.indentStep;
    const quint64 uid = index.data(BlockListModel::UidRole).toString().toULongLong();
    if (view == u"query") {
        m_drawn.push_back(row);
        return queryRow(row, index, textX);
    }

    const QVariantList embeds = index.data(BlockListModel::EmbedsRole).toList();
    const QVariantList transclusions = index.data(BlockListModel::TransclusionsRole).toList();
    const bool editing = index.data(BlockListModel::IsEditingRole).toBool();
    const bool only = index.data(BlockListModel::OnlyEmbedsRole).toBool();
    const bool noText = !editing && only && view == u"text"
                        && ((!transclusions.isEmpty() && embeds.isEmpty())
                            || (m.documentMode && !embeds.isEmpty() && !listItem));
    std::optional<at::AnyElement> text;
    if (!noText) {
        // The text as every text row's (its field while edited), the parts below it.
        m_mediaInner = true;
        text = renderRow(row);
        m_mediaInner = false;
    } else {
        m_drawn.push_back(row);
    }

    const MediaLook &look = m_mediaLook;
    const float gap = look.spacing;
    at::Div part = at::div()
                           .id(u"media")
                           .flexCol()
                           .itemsStart()
                           .gap(gap)
                           .pl(m.contentX + textX)
                           .pt(noText ? m.rowPad : (only ? 0 : std::max(0.0f, gap - m.rowPad)))
                           .pb(m.rowPad);
    if (m.contentWidth > 0)
        std::move(part).maxW(m.contentX + m.contentWidth);
    const QString edge = index.data(BlockListModel::SelectionEdgeRole).toString();
    if (!edge.isEmpty()) {
        // The band goes on under the parts (BlockPartSelectionBand over the whole block).
        const bool start = edge == u"single" || edge == u"first";
        const bool end = edge == u"single" || edge == u"last";
        const float r = m_selectionLook.radius;
        std::move(part).bg(m_selectionLook.band).roundedB(end ? r : 0);
        if (noText)
            std::move(part).roundedT(start ? r : 0);
        if (edge != u"single")
            std::move(part).borderL(2).borderColor(m_selectionLook.ring.fadedBy(0.4f));
    }
    if (!embeds.isEmpty())
        std::move(part).child(embedsPart(row, embeds, 0));
    if (!transclusions.isEmpty())
        std::move(part).child(transclusionsPart(row, uid, transclusions));
    const at::EntityId selfId = m_selfId;
    if (onBlockMenu)
        std::move(part).onMouseDown(at::MouseButton::Right,
                                    [selfId, row](const at::MouseDownEvent &e, at::Window &w, at::App &a) {
                                        a.updateEntity<PageView>(selfId, [&](PageView &v, at::Context<PageView> &) {
                                            if (v.onBlockMenu)
                                                v.onBlockMenu(int(row), e.position);
                                        });
                                        w.stopPropagation();
                                    });
    if (onLongPressRow)
        std::move(part).onLongPress([selfId, row](at::Point, at::Window &, at::App &a) {
            a.updateEntity<PageView>(selfId, [row](PageView &v, at::Context<PageView> &) {
                if (v.onLongPressRow)
                    v.onLongPressRow(int(row));
            });
        });
    if (m_controller && !m_controller->readOnly())
        std::move(part).role(at::Role::None).cursorText().onClick(
                [selfId, row](const at::ClickEvent &click, at::Window &, at::App &a) {
                    a.updateEntity<PageView>(selfId, [&](PageView &v, at::Context<PageView> &) { v.mediaClicked(row, click); });
                });
    at::Div whole = at::div().id(at::ElementId::number(uid)).flexCol();
    if (text)
        std::move(whole).child(std::move(*text));
    return at::AnyElement(std::move(whole).child(std::move(part)));
}

// --- Pictures and files (EmbedView, PdfEmbedTile) --------------------------------------------------

at::AnyElement PageView::embedsPart(std::size_t row, const QVariantList &embeds, float)
{
    const MediaLook &look = m_mediaLook;
    PageView *self = this;
    at::Div column = at::div().id(u"embeds").wFull().flexCol().itemsStart().gap(look.spacing);
    for (qsizetype k = 0; k < embeds.size(); ++k) {
        const QVariantMap e = embeds.at(k).toMap();
        const EmbedKind kind = embedKind(e);
        const QString name = e.value(u"name"_s).toString();
        const QString alt = e.value(u"alt"_s).toString();
        const bool web = e.value(u"web"_s).toBool();
        const int embed = int(k);
        auto click = [self, row, e](const at::ClickEvent &, at::Window &, at::App &) {
            if (self->onEmbedClick)
                self->onEmbedClick(int(row), e);
        };

        if (kind == EmbedKind::Chip) {
            // BChip, interactive and outlined (Theme.bgElevated, Theme.borderStrong): an icon of its
            // kind and the file's name.
            const bool pdf = e.value(u"pdf"_s).toBool();
            const Glyph glyph = pdf ? Glyph::FileText : e.value(u"image"_s).toBool() ? Glyph::Picture : Glyph::File;
            std::move(column).child(at::div()
                                            .id(at::ElementId::number(quint64(k) + 1))
                                            .flexRow()
                                            .itemsCenter()
                                            .gap(5)
                                            .h(m_metrics.touch ? 32 : 26)
                                            .pl(10)
                                            .pr(11)
                                            .roundedFull()
                                            .bg(look.queryBackground)
                                            .border(1)
                                            .borderColor(look.borderStrong)
                                            .textSize(look.smallSize)
                                            .fontMedium()
                                            .textColor(look.chipText)
                                            .whitespaceNowrap()
                                            .when(!look.uiFamily.empty(),
                                                  [&look](at::Div &&d) { return std::move(d).fontFamily(look.uiFamily); })
                                            .cursorPointer()
                                            .role(at::Role::Button)
                                            .child(glyphElement(look.smallSize + 2, look.chipText, glyph))
                                            .child(at::text(u16View(name)))
                                            .onClick(click));
            continue;
        }

        if (kind == EmbedKind::PdfTile) {
            // PdfEmbedTile: the page's thumbnail on white paper, its shape from the page's size, and a
            // caption — a page icon, the file's name, its page count, the page shown.
            const double pageW = e.value(u"pageWidth"_s).toDouble();
            const double pageH = e.value(u"pageHeight"_s).toDouble();
            const QSize thumb(e.value(u"width"_s).toInt(), e.value(u"height"_s).toInt());
            const double aspect = pageW > 0 && pageH > 0 ? std::clamp(pageH / pageW, 1.0 / 32, 32.0)
                                  : !thumb.isEmpty()     ? double(thumb.height()) / thumb.width()
                                                         : 1.414;
            float width = aspect >= 1 ? 297 : 480;
            if (std::round(width * aspect) > 420)
                width = float(std::floor(420 / aspect));
            const int pages = e.value(u"pages"_s).toInt();
            const int page = std::max(1, e.value(u"page"_s).toInt());
            QString details;
            if (pages > 0)
                details = translated("PdfEmbedTile", "%n page(s)", pages);
            if (page > 1 && (pages == 0 || page <= pages))
                details += (details.isEmpty() ? QString() : u" · "_s)
                           + translated("PdfEmbedTile", "p. %1").arg(page);
            if (!details.isEmpty())
                details = u"· "_s + details;
            const QString label = translated("PdfEmbedTile", "PDF %1, page %2 of %3")
                                          .arg(name)
                                          .arg(page)
                                          .arg(pages > 0 ? QString::number(pages) : u"?"_s);
            const QString key = pictureKey(e);
            const QUrl url = e.value(u"url"_s).toUrl();
            const at::Color frameColor = look.frame;
            const float r = look.radiusSm;
            at::Div frame = at::div()
                                    .relative()
                                    .w(width)
                                    .when(true, capWidth)
                                    .aspectRatio(float(1 / aspect))
                                    .bg(at::rgb(0xffffff)) // a page is white, also in the dark theme
                                    .border(1)
                                    .borderColor(frameColor)
                                    .rounded(r)
                                    .child(at::canvas([self, key, url, thumb, row, embed](at::Rect b, at::Window &w, at::App &) {
                                               Media &md = *self->m_media;
                                               md.scale = w.scaleFactor();
                                               // (Inside its border; the thumbnail 3 px in, as PdfEmbedTile's.)
                                               const at::Rect inner = at::Rect::fromLTRB(b.left() + 2, b.top() + 2,
                                                                                         b.right() - 2, b.bottom() - 2);
                                               const PictureCache::Result p = md.cache->picture(
                                                       key, url, PictureCache::sideFor(inner.width(), inner.height(), md.scale), thumb);
                                               if (p.image) {
                                                   md.held.push_back(p.image);
                                                   w.paintPicture(at::Img::fitRect({float(p.image->width), float(p.image->height)},
                                                                                   inner, at::ObjectFit::Contain),
                                                                  {}, p.image);
                                               }
                                               self->m_pictureFrames.push_back(
                                                       {int(row), embed,
                                                        at::Rect::fromLTRB(b.left() - 1, b.top() - 1, b.right() + 1,
                                                                           b.bottom() + 1),
                                                        bool(p.image)});
                                           }).absolute().inset(0));
            at::Div caption = at::div()
                                      .flexRow()
                                      .itemsCenter()
                                      .gap(4)
                                      .textSize(look.smallSize)
                                      .textColor(m_palette.muted)
                                      .whitespaceNowrap()
                                      .overflowHidden()
                                      .ariaHidden()
                                      .child(glyphElement(look.smallSize + 2, m_palette.muted, Glyph::FileText))
                                      .child(at::text(u16View(name)));
            if (!details.isEmpty())
                std::move(caption).child(at::text(u16View(details)));
            std::move(column).child(at::div()
                                            .id(at::ElementId::number(quint64(k) + 1))
                                            .flexCol()
                                            .gap(4)
                                            .w(width)
                                            .when(true, capWidth)
                                            .cursorPointer()
                                            .role(at::Role::Button)
                                            .ariaLabel(u16View(label))
                                            .child(std::move(frame))
                                            .child(std::move(caption))
                                            .onClick(click));
            continue;
        }

        // A picture: at its natural size, at most the column's width and 420 px high, its aspect
        // ratio kept. The size comes from the file's header (the model's), so the row is as tall
        // before the picture is decoded as after; a picture without one is 320 × 180 until it is
        // decoded, then takes what it measured (once). A raster picture is stretched to its frame
        // (they have its shape), an SVG fitted into it.
        const QString key = pictureKey(e);
        const QUrl url = e.value(u"url"_s).toUrl();
        const bool decodable = e.value(u"decodable"_s, true).toBool();
        const bool scalable = scalableEmbed(e);
        const QSize header(e.value(u"width"_s).toInt(), e.value(u"height"_s).toInt());
        const PictureCache::Result seen = m_media->cache->peek(key);
        const float dpr = scalable ? m_media->scale : 1.0f;
        const bool measured = header.isEmpty() || otherShape(seen.natural, header);
        QSizeF natural = !measured ? shownSize(header, scalable, dpr)
                         : !seen.natural.isEmpty() ? shownSize(seen.natural, scalable, dpr)
                                                   : QSizeF(320, 180);
        if (natural.isEmpty())
            natural = QSizeF(320, 180);
        const float aspect = float(natural.width() / std::max(1.0, natural.height()));
        const float width = float(std::min(natural.width(), 420 * natural.width() / std::max(1.0, natural.height())));
        const bool shown = decodable && seen.image && !seen.failed;
        QString message;
        if (!decodable)
            message = name + u'\n'
                      + translated("ContentPictureView", "Pictures in the %1 format cannot be shown on this device.")
                                .arg(e.value(u"format"_s).toString());
        else if (seen.failed)
            message = translated("EmbedView", "Cannot load %1").arg(name);
        const float radius = look.radius;
        const bool backing = look.lightBacking;
        const at::Color backingColor = look.backing;
        at::Div frame = at::div()
                                .id(at::ElementId::number(quint64(k) + 1))
                                .relative()
                                .w(width)
                                .when(true, capWidth)
                                .aspectRatio(aspect)
                                .rounded(radius)
                                .bg(m_palette.background)
                                .cursorPointer()
                                .onClick(click);
        // The hairline frame with the device's or the note's setting, or while there is no picture to
        // show (loading, a format the device cannot decode): it marks the place.
        const bool bordered = look.frames || !shown;
        if (bordered)
            std::move(frame).border(1).borderColor(look.frame);
        std::move(frame).child(
                at::canvas([self, key, url, header, scalable, decodable, row, embed, radius, backing, backingColor,
                            bordered](at::Rect b, at::Window &w, at::App &) {
                    Media &md = *self->m_media;
                    md.scale = w.scaleFactor();
                    bool decoded = false;
                    // (The canvas fills the frame's padding box: inside its border, when it has one.)
                    const at::Rect outer = bordered ? at::Rect::fromLTRB(b.left() - 1, b.top() - 1, b.right() + 1,
                                                                         b.bottom() + 1)
                                                    : b;
                    if (decodable) {
                        // The picture 1 px inside the frame (EmbedView's margins), with a border or not.
                        const at::Rect inner = at::Rect::fromLTRB(outer.left() + 1, outer.top() + 1,
                                                                  outer.right() - 1, outer.bottom() - 1);
                        // Decoded for its frame in device pixels; one without a known size at the
                        // largest side first (its frame follows what it measured).
                        const bool known = !header.isEmpty() || md.cache->peek(key).natural.isValid();
                        const int side = known ? PictureCache::sideFor(inner.width(), inner.height(), md.scale)
                                               : PictureCache::MaxSide;
                        const PictureCache::Result p = md.cache->picture(key, url, side, header, scalable);
                        if (p.image) {
                            decoded = true;
                            md.held.push_back(p.image);
                            const at::Rect target =
                                    scalable ? at::Img::fitRect({float(p.image->width), float(p.image->height)}, inner,
                                                                at::ObjectFit::Contain)
                                             : inner;
                            const at::Corners corners = at::Corners::all(std::max(0.0f, radius - 1));
                            // In a dark theme a light backing under the painted picture (a transparent
                            // PNG's dark lines would vanish into the page).
                            if (backing)
                                w.paintQuad(target, at::Background::solid(backingColor), {}, {}, corners);
                            w.paintPicture(target, corners, p.image);
                        }
                    }
                    self->m_pictureFrames.push_back({int(row), embed, outer, decoded});
                }).absolute().inset(0));
        // What the picture shows, for screen readers (EmbedView's Accessible.name).
        std::move(frame).child(at::img(nullptr).alt(u16View(alt.isEmpty() ? name : alt)).absolute().inset(0));
        if (!message.isEmpty())
            std::move(frame).child(at::div()
                                           .absolute()
                                           .inset(0)
                                           .flexCol()
                                           .justifyCenter()
                                           .px(12)
                                           .textSize(look.smallSize)
                                           .textColor(m_palette.muted)
                                           .textCenter()
                                           .ariaHidden()
                                           .child(at::text(u16View(message))));
        if (web) {
            // A picture from the internet: its menu (WebPictureMenu) on a right-click or a finger held.
            std::move(frame)
                    .onMouseDown(at::MouseButton::Right,
                                 [self, row, e](const at::MouseDownEvent &ev, at::Window &w, at::App &) {
                                     if (self->onEmbedMenu)
                                         self->onEmbedMenu(int(row), e, ev.position);
                                     w.stopPropagation();
                                 })
                    .onLongPress([self, row, e](at::Point p, at::Window &, at::App &) {
                        if (self->onEmbedMenu)
                            self->onEmbedMenu(int(row), e, p);
                    });
        }
        std::move(column).child(std::move(frame));
    }
    return column;
}

// --- Embedded pages, sections and blocks (TransclusionView) ----------------------------------------

at::AnyElement PageView::transclusionsPart(std::size_t row, quint64 uid, const QVariantList &transclusions)
{
    const MediaLook &look = m_mediaLook;
    const Media::Parsed &parsed = m_media->parse(uid, transclusions);
    m_media->transclusions[uid].frame = m_frame;
    PageView *self = this;
    at::Div column = at::div().id(u"transclusions").wFull().flexCol().gap(look.spacing);
    int frameIndex = 0;
    for (const Media::Frame &f : parsed.frames) {
        const QString href = f.href;
        auto open = [self, row, href](const at::ClickEvent &, at::Window &, at::App &) {
            if (self->onLink && !href.isEmpty())
                self->onLink(int(row), href);
        };
        // A quiet frame, its source's edge a thin line in the link's colour.
        at::Div frame = at::div()
                                .id(at::ElementId::number(quint64(++frameIndex)))
                                .relative()
                                .wFull()
                                .flexCol()
                                .bg(look.embedFill)
                                .border(1)
                                .borderColor(look.frame)
                                .rounded(look.radius)
                                .pl(12)
                                .pr(8)
                                .pb(8)
                                .role(at::Role::Group)
                                .ariaLabel(u16View(QString(look.embeddedName).arg(f.title)))
                                .child(at::div()
                                               .absolute()
                                               .left(0)
                                               .top(look.radius)
                                               .bottom(look.radius)
                                               .w(2)
                                               .rounded(1)
                                               .bg(f.found ? m_palette.link.fadedBy(0.55f) : look.borderStrong)
                                               .ariaHidden());
        // The header: the source as a link ("Budżet domowy › Październik"); a click opens it.
        const Glyph glyph = f.kind == u"heading" ? Glyph::Heading : f.kind == u"block" ? Glyph::Link : Glyph::FileText;
        const at::Color titleColor = f.found ? m_palette.link : look.linkMissing;
        std::move(frame).child(at::div()
                                       .id(u"header")
                                       .flexRow()
                                       .itemsCenter()
                                       .gap(6)
                                       .h(look.headerHeight)
                                       .cursorPointer()
                                       .role(at::Role::Link)
                                       .ariaLabel(u16View(f.title))
                                       .child(glyphElement(look.smallSize, titleColor, glyph))
                                       .child(at::div()
                                                      .flex1()
                                                      .minW(0)
                                                      .overflowHidden()
                                                      .whitespaceNowrap()
                                                      .textSize(look.smallSize)
                                                      .fontMedium()
                                                      .textColor(titleColor)
                                                      .when(!look.uiFamily.empty(),
                                                            [&look](at::Div &&d) { return std::move(d).fontFamily(look.uiFamily); })
                                                      .child(at::text(u16View(f.title))))
                                       .onClick(open));
        int entryIndex = 0;
        for (const Media::Entry &e : f.entries) {
            const float indent = float(std::min(6, e.depth)) * look.indentStep;
            at::Div entry = at::div().id(at::ElementId::number(quint64(++entryIndex))).relative().wFull();
            // Guide lines for nested blocks, as the outline's.
            for (int g = 0; g < std::min(6, e.depth); ++g)
                std::move(entry).child(at::div()
                                               .absolute()
                                               .left(float(g) * look.indentStep + 4)
                                               .top(0)
                                               .bottom(0)
                                               .w(1)
                                               .bg(look.frame)
                                               .ariaHidden());
            if (e.kind == Media::Entry::Kind::Rule) {
                std::move(frame).child(std::move(entry).h(16).flexCol().justifyCenter().pl(indent).child(
                        at::div().h(1).wFull().bg(look.border).ariaHidden()));
                continue;
            }
            if (e.kind == Media::Entry::Kind::Nested) {
                // A nested embed: its own source line, or why it is not shown.
                QString title = e.title;
                if (!e.message.isEmpty()) {
                    QString why;
                    if (e.message == u"cycle")
                        why = translated("TransclusionView", "Already shown above (it embeds itself).");
                    else if (e.message == u"depth")
                        why = translated("TransclusionView", "Embedded too deep to show here.");
                    else if (e.message == u"missingPage")
                        why = translated("TransclusionView", "“%1” does not exist yet.").arg(e.title);
                    else if (e.message == u"missingSection")
                        why = translated("TransclusionView", "There is no such section on this page.");
                    else if (e.message == u"missingBlock")
                        why = translated("TransclusionView", "This block was not found.");
                    if (!why.isEmpty())
                        title += u" — "_s + why;
                }
                const QString nestedHref = e.href;
                const at::Color ink = e.message.isEmpty() ? m_palette.link : look.faint;
                std::move(frame).child(
                        std::move(entry)
                                .flexRow()
                                .itemsCenter()
                                .gap(6)
                                .pl(indent)
                                .pt(2)
                                .pb(4)
                                .textSize(look.tinySize)
                                .textColor(ink)
                                .when(!e.message.isEmpty(), [](at::Div &&d) { return std::move(d).italic(); })
                                .child(glyphElement(look.tinySize, look.faint, Glyph::Link))
                                .child(at::div()
                                               .cursorPointer()
                                               .role(at::Role::Link)
                                               .child(at::text(u16View(title)))
                                               .onClick([self, row, nestedHref](const at::ClickEvent &, at::Window &,
                                                                                at::App &) {
                                                   if (self->onLink && !nestedHref.isEmpty())
                                                       self->onLink(int(row), nestedHref);
                                               })));
                continue;
            }
            // A block of the source, rendered read-only as the editor renders it.
            const bool closed = e.task == u"done" || e.task == u"cancelled";
            const bool isTask = !e.task.isEmpty();
            const bool code = e.kind == Media::Entry::Kind::Code;
            const float size = code ? look.smallSize
                               : e.level == 1 ? look.h1Size
                               : e.level == 2 ? look.h2Size
                                              : look.bodySize;
            const float lineHeight = std::round(size * (e.level > 0 ? 1.4f : 1.55f));
            const float textLeft = indent + (isTask ? look.taskBoxWidth + 2 : 0);
            at::Div body = at::div()
                                   .flex1()
                                   .minW(0)
                                   .textSize(size)
                                   .lineHeight(lineHeight)
                                   .textColor(closed ? look.faint : e.view == u"quote" ? m_palette.muted : m_palette.text)
                                   .fontFamily(code ? m_palette.codeFamily : m_palette.bodyFamily);
            if (e.level > 0)
                std::move(body).fontSemibold();
            if (e.task == u"cancelled")
                std::move(body).lineThrough();
            if (code) {
                std::move(body).child(at::text(u16View(e.plain)));
            } else if (e.text) {
                const markdown::RenderedText &rt = *e.text;
                at::Text t = at::text(u16View(rt.text)).spans(renderedSpans(rt, m_palette));
                const QString path = e.path;
                const int line = e.line;
                const QString raw = e.raw;
                for (qsizetype i = 0; i < rt.runs.size();) {
                    const QString &link = rt.runs[i].href;
                    qsizetype j = i + 1;
                    while (j < rt.runs.size() && rt.runs[j].href == link && rt.runs[j].from == rt.runs[j - 1].to)
                        ++j;
                    if (!link.isEmpty())
                        std::move(t).link(at::Range{int(rt.runs[i].from), int(rt.runs[j - 1].to)},
                                          [self, row, link, path, line, raw](const at::ClickEvent &, at::Window &, at::App &) {
                                              if (self->onEmbeddedLink)
                                                  self->onEmbeddedLink(int(row), link, path, line, raw);
                                              else if (self->onLink)
                                                  self->onLink(int(row), link);
                                          });
                    i = j;
                }
                std::move(body).child(std::move(t));
            }
            std::move(entry).flexRow().itemsStart().pl(textLeft).pt(1).pb(e.level > 0 ? look.spacing / 2 - 1 : 1);
            if (isTask) {
                // A working checkbox: it changes the task in its own file (PageController::toggleTaskAt).
                const bool enabled = m_controller
                                     && (m_controller->queriesAvailable() || e.path == m_controller->path());
                const QString path = e.path;
                const int line = e.line;
                const QString status = e.task;
                const float box = m_metrics.taskBox;
                at::Div check = at::div()
                                        .absolute()
                                        .left(indent + 2)
                                        .top(1 + std::round((lineHeight - box) / 2))
                                        .size(box)
                                        .rounded(3)
                                        .border(1.5f)
                                        .borderColor(m_palette.muted)
                                        .opacity(enabled ? 1.0f : 0.6f)
                                        .role(at::Role::CheckBox)
                                        .checked(closed)
                                        .ariaLabel(e.text ? u16View(e.text->text) : std::u16string_view())
                                        .id(u"task");
                if (closed)
                    std::move(check).bg(m_palette.muted);
                if (enabled)
                    std::move(check).cursorPointer().onClick(
                            [self, path, line, status](const at::ClickEvent &, at::Window &, at::App &) {
                                if (self->m_controller)
                                    self->m_controller->toggleTaskAt(path, line, status);
                            });
                std::move(entry).child(std::move(check));
            }
            // A click on the text (not on a link) edits the block with the embed.
            std::move(frame).child(std::move(entry).child(std::move(body)));
        }
        // Why nothing is shown, what is left out.
        QString message;
        if (f.message == u"missingPage")
            message = translated("TransclusionView", "“%1” does not exist yet.").arg(f.title);
        else if (f.message == u"missingSection")
            message = translated("TransclusionView", "There is no such section on this page.");
        else if (f.message == u"missingBlock")
            message = translated("TransclusionView", "This block was not found.");
        else if (f.message == u"empty")
            message = translated("TransclusionView", "Nothing here yet.");
        else if (f.message == u"cycle")
            message = translated("TransclusionView", "Already shown above (it embeds itself).");
        else if (f.message == u"depth")
            message = translated("TransclusionView", "Embedded too deep to show here.");
        if (!message.isEmpty())
            std::move(frame).child(at::div()
                                           .pb(4)
                                           .textSize(look.smallSize)
                                           .italic()
                                           .textColor(look.faint)
                                           .child(at::text(u16View(message))));
        if (f.more > 0)
            std::move(frame).child(at::div()
                                           .id(u"more")
                                           .py(4)
                                           .textSize(look.smallSize)
                                           .textColor(m_palette.link)
                                           .cursorPointer()
                                           .role(at::Role::Link)
                                           .child(at::text(u16View(
                                                   translated("TransclusionView", "%n more block(s) — open the page", f.more))))
                                           .onClick(open));
        std::move(column).child(std::move(frame));
    }
    return column;
}

} // namespace brulion::editor::atr
