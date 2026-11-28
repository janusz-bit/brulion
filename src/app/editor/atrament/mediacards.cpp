// Sounds and videos embedded in blocks drawn by Atrament (docs/ATRAMENT.md step 5, "Sounds and
// videos"): MediaEmbedTile's card — the file's name, the player's bar (MediaControls: play / pause,
// where it is and how long, the seek bar, the speed, full screen for a video, "More"), a video's
// picture above it (its poster with a play button until it plays, then the player's frames) — over
// the app's one player (MediaSource). Where nothing plays, the card names the file and offers
// another app.
#include "blocklistmodel.h"
#include "editorhost.h"
#include "mediarows_p.h"
#include "pagecontroller.h"
#include "mediasource.h"
#include "pageicons.h"
#include "pageview.h"
#include "picturecache.h"

#include <QCoreApplication>

#include <algorithm>

namespace brulion::editor::atr {

namespace at = brulion::atrament;
using namespace Qt::StringLiterals;

namespace {

QString trMedia(const char *context, const char *text)
{
    return QCoreApplication::translate(context, text);
}

} // namespace

void PageView::setMediaSource(std::shared_ptr<MediaSource> source, at::Context<PageView> &cx)
{
    if (m_mediaSource)
        m_mediaSource->unlisten(this);
    m_mediaSource = std::move(source);
    if (m_mediaSource) {
        at::App *app = &cx.app();
        const at::EntityId id = cx.entityId();
        m_mediaSource->listen(this, [app, id] {
            app->updateEntity<PageView>(id, [](PageView &, at::Context<PageView> &c) { c.notify(); });
        });
    }
    cx.notify();
}

// What a card shows of its embed and of the player.
PageView::MediaCard PageView::mediaCard(const QVariantMap &e) const
{
    MediaCard c;
    c.path = e.value(u"path"_s).toString();
    c.name = e.value(u"name"_s).toString();
    c.videoKind = e.value(u"kind"_s).toString() == u"video";
    c.startMs = e.value(u"start"_s, -1).toDouble();
    c.endMs = e.value(u"end"_s, -1).toDouble();
    MediaSource *src = m_mediaSource.get();
    c.available = src && src->available();
    if (c.available && !c.path.isEmpty())
        c.withPlayer(*src);
    return c;
}

// The player's part: the file's info, whether it is the one loaded, where it is, how long.
void PageView::MediaCard::withPlayer(MediaSource &src)
{
    info = src.info(path);
    const MediaSource::State player = src.state();
    current = player.path == path;
    playing = current && player.playing;
    loading = current && player.loading;
    started = current && (player.playing || player.position > 0);
    duration = current && player.duration > 0 ? player.duration : info.duration;
    const double left = src.positionOf(path);
    position = current ? player.position : left > 0 ? left : std::max(0.0, startMs);
    rate = player.rate;
    // A video until the file says it has no picture (a .webm of sound alone: the audio card).
    video = videoKind && (info.hasVideo || info.state != u"ready");
    aspect = info.width > 0 && info.height > 0 ? float(info.width) / float(info.height) : 16.0f / 9;
}

// MediaEmbedTile: the card, as wide as the column (at most 560 px, a video's 640).
at::AnyElement PageView::mediaTile(std::size_t row, const QVariantMap &e, int embed)
{
    const MediaLook &look = m_mediaLook;
    const MediaCard c = mediaCard(e);
    if (c.available && !c.path.isEmpty())
        m_mediaShown.push_back(c.path);
    at::Div column = at::div().wFull().flexCol().gap(c.video ? 0 : 4);
    if (c.video && c.available)
        std::move(column).child(mediaScreen(row, embed, c));
    if (!c.video || !c.available)
        std::move(column).child(mediaName(c));
    std::move(column).child(c.available ? mediaControls(row, e, embed, c) : mediaElsewhere(c));
    return at::div()
            .id(at::ElementId::number(quint64(embed)))
            .wFull()
            .maxW(c.video ? 640 : 560)
            .flexCol()
            .rounded(look.radius)
            .bg(look.embedFill)
            .border(1)
            .borderColor(look.frame)
            .overflowHidden()
            .when(!c.video || !c.available, [](at::Div &&d) { return std::move(d).px(12).py(8); })
            .role(at::Role::Group)
            .ariaLabel(u16View(c.name))
            .child(std::move(column));
}

// The file's name with its kind's icon; "Cannot be played here" when the player could not read it.
at::AnyElement PageView::mediaName(const MediaCard &c)
{
    const MediaLook &look = m_mediaLook;
    at::Div name = at::div()
                           .flexRow()
                           .itemsCenter()
                           .gap(8)
                           .textSize(look.smallSize)
                           .textColor(m_palette.muted)
                           .child(iconElement(c.videoKind ? u"film"_s : u"audio-lines"_s, 16, m_palette.muted))
                           .child(at::div().flex1().minW(0).child(at::text(u16View(c.name)).maxLines(1)));
    if (c.available && c.info.state == u"failed")
        std::move(name).child(at::div().textColor(look.danger).child(
                at::text(u16View(trMedia("MediaEmbedTile", "Cannot be played here")))));
    return std::move(name);
}

// A video's picture: its poster with a play button until it plays, then the player's frames; a
// click plays or pauses it.
at::AnyElement PageView::mediaScreen(std::size_t row, int embed, const MediaCard &c)
{
    PageView *self = this;
    const QString path = c.path;
    const double start = c.startMs;
    const double end = c.endMs;
    return at::div()
            .id(u"screen")
            .relative()
            .wFull()
            .aspectRatio(c.aspect)
            .maxH(360)
            .bg(at::rgb(0x000000))
            .cursorPointer()
            .onClick([self, path, start, end](const at::ClickEvent &, at::Window &, at::App &) {
                if (self->m_mediaSource)
                    self->m_mediaSource->toggle(path, start, end);
            })
            .child(at::canvas([self, c, row, embed](at::Rect b, at::Window &w, at::App &) {
                       self->paintMediaScreen(b, w, c);
                       self->m_mediaFrames.push_back({int(row), embed, b, c.started, c.loading});
                   }).absolute().inset(0));
}

// The poster (or the frame playing) fitted into the screen, the play button over a poster, the
// spinner while the player loads.
void PageView::paintMediaScreen(at::Rect b, at::Window &w, const MediaCard &c)
{
    std::shared_ptr<const at::ImageData> picture;
    if (c.started && m_mediaSource) {
        m_mediaSource->showVideo(c.path, true);
        picture = m_mediaSource->videoFrame();
    } else if (!c.info.poster.isEmpty()) {
        const PictureCache::Result p = pictureCache().picture(
                u"poster:"_s + c.path, QUrl(c.info.poster), PictureCache::sideFor(b.width(), b.height(), w.scaleFactor()), {});
        picture = p.image;
    }
    if (picture)
        w.paintPicture(at::Img::fitRect({float(picture->width), float(picture->height)}, b, at::ObjectFit::Contain), {},
                       picture);
    if (c.loading) {
        paintSpinner(w, b.center(), 28, at::rgb(0xffffff), std::chrono::steady_clock::now());
        return;
    }
    if (c.started)
        return;
    const at::Rect button = at::Rect::fromXYWH(b.center().x - 26, b.center().y - 26, 52, 52);
    w.paintQuad(button, at::Background::solid(at::rgba(0x0000008c)), {}, at::transparent(), at::Corners::all(26));
    if (const auto play = pageIcon(u"play"_s))
        w.paintIcon(*play, at::Rect::fromXYWH(button.left() + 15, button.top() + 13, 26, 26), at::rgb(0xffffff));
}

// MediaControls: play / pause, the position, the seek bar, the length, the speed, full screen (a
// video), "More".
at::AnyElement PageView::mediaControls(std::size_t row, const QVariantMap &e, int embed, const MediaCard &c)
{
    PageView *self = this;
    const QString path = c.path;
    const double start = c.startMs;
    const double end = c.endMs;
    MediaSource &src = *m_mediaSource;
    const QString positionText = src.timeText(c.position, c.duration);
    const QString durationText = c.duration > 0 ? src.timeText(std::round(c.duration / 1000) * 1000, c.duration) : QString();
    at::Div bar = at::div()
                          .flexRow()
                          .itemsCenter()
                          .gap(4)
                          .textSize(m_mediaLook.smallSize)
                          .textColor(m_palette.muted)
                          .when(c.video, [](at::Div &&d) { return std::move(d).px(8).py(4); })
                          .child(mediaButton(c.playing ? u"pause"_s : u"play"_s,
                                             c.playing ? trMedia("MediaControls", "Pause") : trMedia("MediaControls", "Play"),
                                             [self, path, start, end](at::Point) {
                                                 if (self->m_mediaSource)
                                                     self->m_mediaSource->toggle(path, start, end);
                                             }))
                          .child(at::text(u16View(positionText)))
                          .child(mediaSlider(row, embed, c, positionText));
    if (!durationText.isEmpty())
        std::move(bar).child(at::text(u16View(durationText)));
    std::move(bar).child(at::div()
                                 .id(u"rate")
                                 .h(26)
                                 .px(8)
                                 .flexRow()
                                 .itemsCenter()
                                 .rounded(m_mediaLook.radiusSm)
                                 .cursorPointer()
                                 .hover([hover = m_mediaLook.hover](at::Style &s) { s.background = at::Background::solid(hover); })
                                 .role(at::Role::Button)
                                 .ariaLabel(u16View(trMedia("MediaControls", "Speed %1").arg(src.rateText(c.rate))))
                                 .onClick([self](const at::ClickEvent &, at::Window &, at::App &) {
                                     if (self->m_mediaSource)
                                         self->m_mediaSource->cycleRate();
                                 })
                                 .child(at::text(u16View(src.rateText(c.rate)))));
    if (c.video)
        std::move(bar).child(mediaButton(u"maximize"_s, trMedia("MediaControls", "Full screen"), [self, row, e](at::Point) {
            if (self->onMediaFullscreen)
                self->onMediaFullscreen(int(row), e);
        }));
    // "More": the owner's menu (MediaEmbedMenu: the player's view, transcribe, a link to the
    // moment, another app) where it was clicked.
    std::move(bar).child(mediaButton(u"ellipsis"_s, trMedia("MediaControls", "More"), [self, row, e](at::Point at) {
        if (self->onMediaMenu)
            self->onMediaMenu(int(row), e, at);
    }));
    return std::move(bar);
}

// An icon button of the bar (BIconButton: 32 px, round while hovered).
at::AnyElement PageView::mediaButton(const QString &icon, const QString &label, std::function<void(at::Point)> clicked)
{
    return at::div()
            .id(at::ElementId(u16View(icon)))
            .size(32)
            .flexShrink0()
            .flexRow()
            .itemsCenter()
            .justifyCenter()
            .roundedFull()
            .cursorPointer()
            .hover([hover = m_mediaLook.hover](at::Style &s) { s.background = at::Background::solid(hover); })
            .role(at::Role::Button)
            .ariaLabel(u16View(label))
            .onClick([clicked](const at::ClickEvent &click, at::Window &, at::App &) { clicked(click.up.position); })
            .child(iconElement(icon, 18, m_palette.text));
}

// The seek bar (MediaSlider): a track, the part played, a knob; a press and a drag seek there (a
// file not loaded starts there).
at::AnyElement PageView::mediaSlider(std::size_t row, int embed, const MediaCard &c, const QString &positionText)
{
    PageView *self = this;
    const float value = c.duration > 0 ? float(std::clamp(c.position / c.duration, 0.0, 1.0)) : 0;
    const QString path = c.path;
    const double duration = c.duration;
    const double end = c.endMs;
    const at::Color track = m_mediaLook.border;
    const at::Color played = m_mediaLook.focusRing;
    auto seekAt = [self, row, embed, path, duration, end](at::Point p) {
        if (duration > 0 && self->m_mediaSource)
            if (const std::optional<at::Rect> b = self->mediaSliderBounds(int(row), embed))
                self->m_mediaSource->seek(path, std::clamp((p.x - b->left()) / b->width(), 0.0f, 1.0f) * duration, end);
    };
    return at::div()
            .id(u"seek")
            .relative()
            .flex1()
            .minW(48)
            .h(24)
            .cursorPointer()
            .role(at::Role::Slider)
            .ariaLabel(u16View(trMedia("MediaControls", "Position")))
            .ariaValue(u16View(positionText))
            .rangeValue(value, 0, 1, 0.01f)
            .onMouseDown(at::MouseButton::Left,
                         [seekAt](const at::MouseDownEvent &ev, at::Window &, at::App &) { seekAt(ev.position); })
            .onMouseMove([seekAt](const at::MouseMoveEvent &ev, at::Window &, at::App &) {
                if (ev.pressedButton == at::MouseButton::Left)
                    seekAt(ev.position);
            })
            .child(at::canvas([self, row, embed, value, track, played](at::Rect b, at::Window &w, at::App &) {
                       const float y = b.center().y;
                       w.paintQuad(at::Rect::fromXYWH(b.left(), y - 2, b.width(), 4), at::Background::solid(track), {},
                                   at::transparent(), at::Corners::all(2));
                       w.paintQuad(at::Rect::fromXYWH(b.left(), y - 2, b.width() * value, 4), at::Background::solid(played),
                                   {}, at::transparent(), at::Corners::all(2));
                       w.paintQuad(at::Rect::fromXYWH(b.left() + b.width() * value - 6, y - 6, 12, 12),
                                   at::Background::solid(played), {}, at::transparent(), at::Corners::all(6));
                       self->m_mediaSliders.push_back({int(row), embed, b});
                   }).absolute().inset(0));
}

std::optional<at::Rect> PageView::mediaSliderBounds(int row, int embed) const
{
    for (const MediaSlider &s : m_mediaSliders)
        if (s.row == row && s.embed == embed)
            return s.bounds;
    return std::nullopt;
}

// Nothing plays here: why, and the system's app.
at::AnyElement PageView::mediaElsewhere(const MediaCard &c)
{
    PageView *self = this;
    const QString path = c.path;
    return at::div()
            .flexRow()
            .itemsCenter()
            .gap(8)
            .textSize(m_mediaLook.smallSize)
            .textColor(m_palette.muted)
            .child(at::div().flex1().minW(0).child(
                    at::text(u16View(trMedia("MediaEmbedTile", "This version of Brulion plays no audio or video.")))))
            .child(at::div()
                           .id(u"elsewhere")
                           .h(26)
                           .px(10)
                           .flexRow()
                           .itemsCenter()
                           .gap(5)
                           .rounded(m_mediaLook.radiusSm)
                           .border(1)
                           .borderColor(m_mediaLook.borderStrong)
                           .cursorPointer()
                           .role(at::Role::Button)
                           .ariaLabel(u16View(trMedia("MediaEmbedTile", "Open in another app")))
                           .onClick([self, path](const at::ClickEvent &, at::Window &, at::App &) {
                               if (self->onOpenElsewhere)
                                   self->onOpenElsewhere(path);
                           })
                           .child(iconElement(u"external-link"_s, 14, m_palette.text))
                           .child(at::text(u16View(trMedia("MediaEmbedTile", "Open in another app")))));
}

// After a frame: the cards shown told to the source (addView / removeView), a video no longer
// shown hidden (it pauses).
void PageView::mediaCardsFrame()
{
    if (!m_mediaSource)
        return;
    std::sort(m_mediaShown.begin(), m_mediaShown.end());
    m_mediaShown.erase(std::unique(m_mediaShown.begin(), m_mediaShown.end()), m_mediaShown.end());
    for (const QString &p : m_mediaShown)
        if (!std::binary_search(m_mediaShownLast.begin(), m_mediaShownLast.end(), p))
            m_mediaSource->addView(p);
    for (const QString &p : m_mediaShownLast)
        if (!std::binary_search(m_mediaShown.begin(), m_mediaShown.end(), p)) {
            m_mediaSource->removeView(p);
            m_mediaSource->showVideo(p, false);
        }
    m_mediaShownLast = std::move(m_mediaShown);
    m_mediaShown.clear();
}

} // namespace brulion::editor::atr
