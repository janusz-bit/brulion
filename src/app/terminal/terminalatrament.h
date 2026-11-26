// The terminal drawn by Atrament (docs/CODE-TERMINAL.md "Drawn by Atrament", docs/ATRAMENT.md M3):
// what a TerminalView shows — the grid's backgrounds, glyphs, decorations, the find bar's matches,
// the selection, the link under the pointer, the cursor and the input method's preedit — painted
// as Atrament primitives (quads and glyph sprites from the shared atlas) instead of QPainter into
// the item's own texture. Built where Atrament is (BRULION_ATRAMENT); TerminalView keeps its keys,
// mouse, selection and input method, and only hands each frame's TerminalPicture over.
//
//   TerminalScreen      the Atrament view: render() gives an element that paints the picture;
//                       a cache of the cells' glyphs (shaped once per character and style)
//   TerminalAtrament    an AtramentItem over the TerminalView (its size, no input of its own)
//                       showing a TerminalScreen; the grid's metrics from Atrament's own fonts
//
// Glyphs come from Atrament's font collection — the app's bundled fonts: a family it does not
// have (a system font chosen as the code font) is drawn with Brulion JetBrains Mono. Each cell is
// placed at its own column (wide characters over two), so fallback glyphs (emoji, CJK, symbols)
// never shift the grid.
#pragma once

#include "terminalpicture.h"

#include "ui/atrament.h"

#include <QString>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class QQuickItem;

namespace brulion::atrament {
class AtramentItem;
}

namespace brulion::terminal {

class TerminalScreen {
public:
    TerminalScreen(atrament::Context<TerminalScreen> &cx, const TerminalPictureSource *source);

    atrament::AnyElement render(atrament::Window &window, atrament::Context<TerminalScreen> &cx);
    /// Paints the source's picture within `bounds` (the element's paint).
    void paint(atrament::Rect bounds, atrament::Window &window, atrament::App &app);

    /// The grid's metrics for `family` at `pixelSize` (logical px), snapped to `devicePixelRatio`.
    static TerminalCellMetrics metrics(const atrament::FontCollection &fonts, const QString &family, qreal pixelSize,
                                       qreal devicePixelRatio);
    /// The faces a terminal draws `family` with — regular, bold, italic, bold italic — the bundled
    /// monospace font's when the collection lacks it.
    static std::array<atrament::FaceId, 4> faces(const atrament::FontCollection &fonts, const QString &family);

    /// Glyphs cached (tests).
    std::size_t cachedGlyphs() const { return m_simple.size() + m_clusters.size(); }

private:
    struct Glyph {
        atrament::FaceId face = atrament::kNoFace;
        std::uint32_t glyph = 0;
        float dx = 0; // from the cell's left edge
        float dy = 0; // from the baseline
    };
    using Glyphs = std::vector<Glyph>;

    /// The glyphs of a cell's text in style `style` (bold 1 | italic 2).
    const Glyphs &glyphsFor(const atrament::FontCollection &fonts, char32_t ch, const QString &cluster, int style);
    void paintText(atrament::Window &window, const atrament::FontCollection &fonts, float x, float baseline,
                   char32_t ch, const QString &cluster, int style, atrament::Color color);

    const TerminalPictureSource *m_source;
    TerminalPicture m_picture;
    // The glyph cache, for the family and size it was made for.
    QString m_family;
    qreal m_pixelSize = 0;
    std::array<atrament::FaceId, 4> m_faces{};
    std::unordered_map<std::uint64_t, Glyphs> m_simple;    // (code point << 2) | style
    std::unordered_map<std::u16string, Glyphs> m_clusters; // style + the cluster's text
};

class TerminalAtrament {
public:
    using AppProvider = atrament::App *(*)();
    /// Where Atrament's App comes from: the process's (the app sets AtramentView::atramentApp()).
    static void setAppProvider(AppProvider provider);
    /// An App can be had: a TerminalView can be drawn by Atrament.
    static bool available();

    /// An item over `view` (a child filling it, taking no input) drawing what `source` gives.
    TerminalAtrament(QQuickItem *view, const TerminalPictureSource *source);
    ~TerminalAtrament();
    TerminalAtrament(const TerminalAtrament &) = delete;
    TerminalAtrament &operator=(const TerminalAtrament &) = delete;

    /// Draws another frame (the picture changed).
    void refresh();
    /// Follows the view's size.
    void setSize(qreal width, qreal height);
    TerminalCellMetrics metrics(const QString &family, qreal pixelSize, qreal devicePixelRatio) const;
    atrament::AtramentItem *item() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace brulion::terminal
