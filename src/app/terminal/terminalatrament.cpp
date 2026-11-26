#include "terminalatrament.h"

#include "qthost.h"
#include "text/textlayout.h"
#include "text/textsystem.h"

#include "term/cell.h"
#include "term/charwidth.h"
#include "term/screen.h"

#include <QQuickItem>
#include <QtQuick/private/qquickitem_p.h>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;
namespace at = brulion::atrament;

namespace brulion::terminal {

namespace {

/// The bundled monospace font: what a family Atrament's collection lacks is drawn with.
constexpr char16_t BundledMono[] = u"Brulion JetBrains Mono";

constexpr quint16 Decorations = term::Underline | term::DoubleUnderline | term::Strikethrough | term::Overline;

TerminalAtrament::AppProvider s_provider = nullptr;

at::Color color(QRgb c)
{
    return {float(qRed(c)) / 255.0f, float(qGreen(c)) / 255.0f, float(qBlue(c)) / 255.0f, float(qAlpha(c)) / 255.0f};
}

QRgb mixRgb(QRgb a, QRgb b, double t)
{
    auto channel = [t](int x, int y) { return int(std::lround(x + (y - x) * t)); };
    return qRgb(channel(qRed(a), qRed(b)), channel(qGreen(a), qGreen(b)), channel(qBlue(a), qBlue(b)));
}

QRgb xtermRgb(int index)
{
    if (index >= 16 && index < 232) {
        const int i = index - 16;
        auto level = [](int v) { return v == 0 ? 0 : 55 + v * 40; };
        return qRgb(level(i / 36), level((i / 6) % 6), level(i % 6));
    }
    const int grey = 8 + (index - 232) * 10;
    return qRgb(grey, grey, grey);
}

std::u16string_view u16(const QString &s)
{
    return {reinterpret_cast<const char16_t *>(s.utf16()), std::size_t(s.size())};
}

void fill(at::Window &window, at::Rect rect, QRgb c)
{
    if (qAlpha(c) == 0 || rect.size.width <= 0 || rect.size.height <= 0)
        return;
    window.paintQuad(rect, at::Background::solid(color(c)), {}, at::transparent(), {});
}

/// The element of a TerminalScreen: fills what it is given and paints the picture.
class TerminalElement final : public at::Element {
public:
    explicit TerminalElement(TerminalScreen *screen) : m_screen(screen) { }

    at::NodeId requestLayout(at::Window &window, at::App &) override
    {
        at::LayoutStyle style;
        style.width = at::Length::percent(100);
        style.height = at::Length::percent(100);
        return window.requestLayout(style, {});
    }
    void paint(at::Rect bounds, at::Window &window, at::App &app) override { m_screen->paint(bounds, window, app); }

private:
    TerminalScreen *m_screen;
};

} // namespace

// ---- TerminalScreen -----------------------------------------------------------------------------

TerminalScreen::TerminalScreen(at::Context<TerminalScreen> &, const TerminalPictureSource *source)
    : m_source(source) { }

at::AnyElement TerminalScreen::render(at::Window &, at::Context<TerminalScreen> &)
{
    return TerminalElement(this);
}

std::array<at::FaceId, 4> TerminalScreen::faces(const at::FontCollection &fonts, const QString &family)
{
    std::u16string_view name = u16(family);
    if (name.empty() || fonts.match(name) == at::kNoFace)
        name = BundledMono;
    std::array<at::FaceId, 4> out{};
    for (int style = 0; style < 4; ++style) {
        const at::FontWeight weight{(style & 1) ? at::FontWeight::Bold : at::FontWeight::Normal};
        const at::FontStyle slant = (style & 2) ? at::FontStyle::Italic : at::FontStyle::Normal;
        at::FaceId face = fonts.match(name, weight, slant);
        if (face == at::kNoFace)
            face = fonts.faceCount() > 0 ? 0 : at::kNoFace;
        out[std::size_t(style)] = face;
    }
    return out;
}

TerminalCellMetrics TerminalScreen::metrics(const at::FontCollection &fonts, const QString &family, qreal pixelSize,
                                            qreal devicePixelRatio)
{
    TerminalCellMetrics m;
    const at::FaceId face = faces(fonts, family)[0];
    if (face == at::kNoFace || pixelSize <= 0)
        return m;
    const qreal dpr = devicePixelRatio > 0 ? devicePixelRatio : 1;
    const at::FontMetrics fm = fonts.metrics(face);
    const qreal ascent = fm.ascent * pixelSize;
    const qreal height = (fm.ascent + fm.descent) * pixelSize;
    const at::FontRun run{1, face, float(pixelSize), 0, {}, {}};
    const at::TextLayout layout = at::layoutParagraph(fonts, u"M", std::span(&run, 1), {});
    qreal advance = layout.maxContentWidth();
    if (advance <= 0)
        advance = pixelSize * 0.6;
    m.cellWidth = std::max<qreal>(1, std::round(advance * dpr) / dpr);
    m.cellHeight = std::max<qreal>(1, std::ceil(height * dpr) / dpr);
    m.ascent = std::round((ascent + (m.cellHeight - height) / 2) * dpr) / dpr;
    // Lines on whole device pixels: crisp, and as thick at every column.
    auto snap = [dpr](qreal v) { return std::round(v * dpr) / dpr; };
    m.underlinePos = std::max<qreal>(1, snap(fm.underlinePosition * pixelSize));
    m.lineWidth = std::max<qreal>(1, snap(fm.underlineThickness * pixelSize));
    m.strikePos = snap(fm.strikeoutPosition * pixelSize);
    return m;
}

const TerminalScreen::Glyphs &TerminalScreen::glyphsFor(const at::FontCollection &fonts, char32_t ch,
                                                        const QString &cluster, int style)
{
    // Each cell's text shaped once (a character, or a character with its combining marks) — the
    // face that has it found by the collection's fallback — and its glyphs kept, relative to the
    // cell's left edge and the baseline.
    auto shape = [&](const QString &text) {
        Glyphs out;
        const at::FontRun run{int(text.size()), m_faces[std::size_t(style)], float(m_pixelSize), 0, {}, {}};
        const at::TextLayout layout = at::layoutParagraph(fonts, u16(text), std::span(&run, 1), {});
        const float baseline = layout.lines().empty() ? 0 : layout.lines().front().baseline;
        for (const at::PositionedGlyph &g : layout.glyphs())
            out.push_back({g.face, g.glyph, g.position.x, g.position.y - baseline});
        return out;
    };
    // (Bounded: a program printing every character there is cannot grow it for ever.)
    if (m_simple.size() + m_clusters.size() > 8192) {
        m_simple.clear();
        m_clusters.clear();
    }
    if (cluster.isEmpty()) {
        const std::uint64_t key = (std::uint64_t(ch) << 2) | std::uint64_t(style);
        auto it = m_simple.find(key);
        if (it == m_simple.end()) {
            char32_t cp[1] = {ch};
            it = m_simple.emplace(key, shape(QString::fromUcs4(cp, 1))).first;
        }
        return it->second;
    }
    std::u16string key(1, char16_t(u'0' + style));
    key += u16(cluster);
    auto it = m_clusters.find(key);
    if (it == m_clusters.end())
        it = m_clusters.emplace(std::move(key), shape(cluster)).first;
    return it->second;
}

void TerminalScreen::paintText(at::Window &window, const at::FontCollection &fonts, float x, float baseline,
                               char32_t ch, const QString &cluster, int style, at::Color c)
{
    for (const Glyph &g : glyphsFor(fonts, ch, cluster, style))
        window.paintGlyph(g.face, g.glyph, float(m_pixelSize), {x + g.dx, baseline + g.dy}, c);
}

void TerminalScreen::paint(at::Rect bounds, at::Window &window, at::App &app)
{
    TerminalPicture &p = m_picture;
    p = TerminalPicture();
    if (m_source)
        m_source->terminalPicture(p);
    fill(window, bounds, p.background);
    const term::Screen *s = p.screen;
    if (!s)
        return;
    const at::FontCollection &fonts = app.textSystem().fonts();
    if (p.fontFamily != m_family || p.fontPixelSize != m_pixelSize) {
        m_family = p.fontFamily;
        m_pixelSize = p.fontPixelSize;
        m_faces = faces(fonts, m_family);
        m_simple.clear();
        m_clusters.clear();
    }
    const TerminalCellMetrics &m = p.metrics;
    const float ox = bounds.origin.x;
    const float oy = bounds.origin.y;
    const float pad = float(p.padding);
    const float cw = float(m.cellWidth);
    const float ch = float(m.cellHeight);
    const float lineWidth = float(m.lineWidth);
    const bool swapped = s->reverseVideo();
    auto resolve = [&](quint32 raw, bool isForeground) -> QRgb {
        const quint32 kind = raw >> 24;
        if (kind == term::Color::Indexed) {
            const int index = int(raw & 0xff);
            return index < 16 ? p.palette[std::size_t(index)] : xtermRgb(index);
        }
        if (kind == term::Color::Rgb)
            return 0xff000000u | (raw & 0xffffff);
        return isForeground != swapped ? p.foreground : p.background;
    };
    auto colorsOf = [&](const term::Cell &cell, QRgb *fg, QRgb *bg) {
        *fg = resolve(cell.fg.raw(), true);
        *bg = resolve(cell.bg.raw(), false);
        if (cell.attrs & term::Inverse)
            std::swap(*fg, *bg);
        if (cell.attrs & term::Dim)
            *fg = mixRgb(*fg, *bg, 0.45);
        if (cell.attrs & term::Invisible)
            *fg = *bg;
    };
    auto cellRect = [&](int column, float top, int cells) {
        return at::Rect::fromXYWH(ox + pad + float(column) * cw, top, float(cells) * cw, ch);
    };

    const int rows = s->rows();
    const int screenColumns = s->columns();
    // The rows the bounds show (an item taller than the grid: the rest is background).
    for (int viewRow = 0; viewRow < rows; ++viewRow) {
        const int lineIndex = viewRow - p.scrollOffset;
        if (lineIndex < -s->historySize())
            continue;
        const term::Line &line = s->lineAt(lineIndex);
        const float top = oy + pad + float(viewRow) * ch;
        if (top >= bounds.origin.y + bounds.size.height)
            break;
        const float baseline = top + float(m.ascent);
        const int columns = std::min(int(line.cells.size()), screenColumns);
        const QRgb rowBackground = resolve(0, false);
        if (rowBackground != p.background)
            fill(window, cellRect(0, top, screenColumns), rowBackground);

        // Backgrounds, a run of equal ones at a time.
        for (int column = 0; column < columns;) {
            QRgb fg, bg;
            colorsOf(line.cells.at(column), &fg, &bg);
            int end = column + 1;
            while (end < columns) {
                QRgb nextFg, nextBg;
                colorsOf(line.cells.at(end), &nextFg, &nextBg);
                if (nextBg != bg)
                    break;
                ++end;
            }
            if (bg != rowBackground)
                fill(window, cellRect(column, top, end - column), bg);
            column = end;
        }

        // The find bar's matches, then the selection over them.
        if (p.matches && !p.matches->isEmpty()) {
            const QList<term::TextRange> &matches = *p.matches;
            const auto first = std::lower_bound(matches.cbegin(), matches.cend(), lineIndex,
                                                [](const term::TextRange &r, int l) { return r.end.line < l; });
            for (auto it = first; it != matches.cend() && it->start.line <= lineIndex; ++it) {
                const int from = lineIndex == it->start.line ? it->start.column : 0;
                const int to = lineIndex == it->end.line ? it->end.column : screenColumns;
                if (to > from)
                    fill(window, cellRect(from, top, to - from),
                         it - matches.cbegin() == p.currentMatch ? p.currentMatchColor : p.matchColor);
            }
        }
        if (p.hasSelection && lineIndex >= p.selectionStart.line && lineIndex <= p.selectionStop.line) {
            const int from = lineIndex == p.selectionStart.line ? p.selectionStart.column : 0;
            const int to = lineIndex == p.selectionStop.line ? p.selectionStop.column : screenColumns;
            if (to > from)
                fill(window, cellRect(from, top, to - from), p.selection);
        }

        // Text, a cell at a time at its own column, and its decorations.
        for (int column = 0; column < columns; ++column) {
            const term::Cell &cell = line.cells.at(column);
            if (cell.isWideTail())
                continue;
            QRgb fg, bg;
            colorsOf(cell, &fg, &bg);
            const int style = ((cell.attrs & term::Bold) ? 1 : 0) | ((cell.attrs & term::Italic) ? 2 : 0);
            const float x = ox + pad + float(column) * cw;
            const at::Color ink = color(fg);
            if (cell.ch != U' ' && cell.ch != 0 && fg != bg) {
                const bool cluster = cell.attrs & term::ClusterFlag;
                paintText(window, fonts, x, baseline, cell.ch, cluster ? line.cellText(column) : QString(), style, ink);
            }
            const quint16 attrs = cell.attrs & Decorations;
            if (!attrs)
                continue;
            const float width = cw * float(cell.isWide() ? 2 : 1);
            if (attrs & (term::Underline | term::DoubleUnderline))
                fill(window, at::Rect::fromXYWH(x, baseline + float(m.underlinePos), width, lineWidth), fg);
            if (attrs & term::DoubleUnderline)
                fill(window, at::Rect::fromXYWH(x, baseline + float(m.underlinePos) + 2 * lineWidth, width, lineWidth),
                     fg);
            if (attrs & term::Strikethrough)
                fill(window, at::Rect::fromXYWH(x, baseline - float(m.strikePos), width, lineWidth), fg);
            if (attrs & term::Overline)
                fill(window, at::Rect::fromXYWH(x, top, width, lineWidth), fg);
        }

        // The link under the pointer (Ctrl held), underlined.
        if (p.hasLink && lineIndex >= p.link.start.line && lineIndex <= p.link.end.line) {
            const int from = lineIndex == p.link.start.line ? p.link.start.column : 0;
            const int to = lineIndex == p.link.end.line ? p.link.end.column : screenColumns;
            if (to > from)
                fill(window,
                     at::Rect::fromXYWH(ox + pad + float(from) * cw, baseline + float(m.underlinePos),
                                        float(to - from) * cw, lineWidth),
                     p.cursor);
        }
    }

    // The cursor (or the input method's preedit at its place).
    const int cursorRow = s->cursorRow() + p.scrollOffset;
    if (cursorRow >= rows)
        return;
    const int column = s->cursorColumn();
    const term::Line &cursorLine = s->line(s->cursorRow());
    const bool wide = column < cursorLine.cells.size() && cursorLine.cells.at(column).isWide();
    const float top = oy + pad + float(cursorRow) * ch;
    const at::Rect rect = cellRect(column, top, wide ? 2 : 1);
    if (!p.preedit.isEmpty()) {
        int cells = 0;
        const QList<uint> codePoints = p.preedit.toUcs4();
        for (const uint cp : codePoints)
            cells += term::charWidth(char32_t(cp));
        const at::Rect area = cellRect(column, top, std::max(1, cells));
        fill(window, area, p.background);
        float x = area.origin.x;
        const float baseline = top + float(m.ascent);
        for (const uint cp : codePoints) {
            const int width = term::charWidth(char32_t(cp));
            if (width == 0)
                continue; // (a combining mark: drawn with nothing; rare in a composition)
            paintText(window, fonts, x, baseline, char32_t(cp), QString(), 0, color(p.foreground));
            x += float(width) * cw;
        }
        fill(window, at::Rect::fromXYWH(area.origin.x, baseline + float(m.underlinePos), area.size.width, lineWidth),
             p.foreground);
        return;
    }
    // No cursor once the program ended (nothing reads the keys any more) or before it started.
    if (!s->cursorVisible() || !p.running)
        return;
    if (!p.focused) {
        window.paintQuad(rect, std::nullopt, at::Edges::all(1), color(p.cursor), {});
        return;
    }
    if (s->cursorBlinking() && !p.blinkOn)
        return;
    switch (s->cursorShape()) {
    case term::CursorShape::Block:
        fill(window, rect, p.cursor);
        if (column < cursorLine.cells.size()) {
            const term::Cell &cell = cursorLine.cells.at(column);
            if (cell.ch != U' ' && cell.ch != 0) {
                const int style = ((cell.attrs & term::Bold) ? 1 : 0) | ((cell.attrs & term::Italic) ? 2 : 0);
                const bool cluster = cell.attrs & term::ClusterFlag;
                paintText(window, fonts, rect.origin.x, top + float(m.ascent), cell.ch,
                          cluster ? cursorLine.cellText(column) : QString(), style, color(p.background));
            }
        }
        break;
    case term::CursorShape::Underline:
        fill(window, at::Rect::fromXYWH(rect.origin.x, rect.origin.y + rect.size.height - 2, rect.size.width, 2),
             p.cursor);
        break;
    case term::CursorShape::Bar:
        fill(window, at::Rect::fromXYWH(rect.origin.x, rect.origin.y, 2, rect.size.height), p.cursor);
        break;
    }
}

// ---- TerminalAtrament ---------------------------------------------------------------------------

struct TerminalAtrament::Impl {
    at::App *app = nullptr;
    at::AtramentItem *item = nullptr;
    at::Entity<TerminalScreen> view;
};

void TerminalAtrament::setAppProvider(AppProvider provider)
{
    s_provider = provider;
}

bool TerminalAtrament::available()
{
    return s_provider != nullptr;
}

TerminalAtrament::TerminalAtrament(QQuickItem *view, const TerminalPictureSource *source) : d(std::make_unique<Impl>())
{
    d->app = s_provider ? s_provider() : nullptr;
    if (!d->app)
        return;
    d->item = new at::AtramentItem(view);
    d->item->setObjectName(u"terminalAtrament"_s);
    // Input stays the view's own: the item below takes no mouse, hover, focus or input method.
    d->item->setAcceptedMouseButtons(Qt::NoButton);
    d->item->setAcceptHoverEvents(false);
    d->item->setAcceptTouchEvents(false);
    d->item->setActiveFocusOnTab(false);
    d->item->setFlag(QQuickItem::ItemAcceptsInputMethod, false);
    d->item->setEnabled(false);
    // Nothing of its own for assistive technology: the view's accessibleText is the screen's text
    // (Atrament's bridge marks its items accessible; this one would be an empty node in the view).
    QQuickItemPrivate::get(d->item)->isAccessible = false;
    d->item->setSize(view->size());
    d->view = d->app->newEntity<TerminalScreen>(source);
    d->item->attach(*d->app, at::AnyView{d->view.id(), &at::renderView<TerminalScreen>});
}

TerminalAtrament::~TerminalAtrament()
{
    // The item (and its window) before the view it shows.
    delete d->item;
    d->item = nullptr;
    d->view = {};
}

void TerminalAtrament::refresh()
{
    if (d->item && d->item->atramentWindow())
        d->item->atramentWindow()->refresh();
}

void TerminalAtrament::setSize(qreal width, qreal height)
{
    if (d->item)
        d->item->setSize(QSizeF(width, height));
}

TerminalCellMetrics TerminalAtrament::metrics(const QString &family, qreal pixelSize, qreal devicePixelRatio) const
{
    if (!d->app)
        return {};
    return TerminalScreen::metrics(d->app->textSystem().fonts(), family, pixelSize, devicePixelRatio);
}

at::AtramentItem *TerminalAtrament::item() const
{
    return d->item;
}

} // namespace brulion::terminal
