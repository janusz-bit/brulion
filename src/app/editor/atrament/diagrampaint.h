// Diagram scenes painted by Atrament (docs/ATRAMENT.md "The block editor in Atrament (M2)", step 5,
// charts and Mermaid): the core diagram::Scene — Mermaid's flowcharts, sequence diagrams and pies
// (diagram::render), a ```chart block's bars, lines and slices (chart::layoutChart) — that
// MermaidView and ChartView paint with QPainter, drawn through Atrament's vector paths and text
// instead (scene/vectorpath.h): the same items in the same order, the same colours
// (diagram::themedPalette: Obsidian-like, light and dark), paths filled then stroked with round
// caps and joins (dotted and dashed lines flat, 2/3 and 6/4 px), text lines in the app's font
// aligned in their boxes over their rounded backgrounds. Labels are measured with Atrament's own
// fonts (atramentMetrics), so a scene laid out with them fits what Atrament draws.
//
// DiagramElement is a scene as an element: as tall as the scene drawn at the width it gets (scaled
// down when wider, never up — MermaidView / ChartView's rule), centred.
#pragma once

#include "diagram/diagram.h"
#include "ui/atrament.h"

#include <QStringView>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace brulion::editor::atr {

// A diagram colour, "#rrggbb" or "#rrggbbaa" (the CSS order diagram:: writes, not QColor's
// "#aarrggbb"); nothing for anything else.
std::optional<atrament::Color> diagramColor(QStringView css);

// The roles' colours of a palette, resolved once.
struct DiagramColors {
    atrament::Color roles[diagram::RoleCount] = {};
    static DiagramColors from(const diagram::Palette &palette);
    // A paint's own colour (a `style`'s, validated by the parser) or its role's; transparent for none.
    atrament::Color of(const diagram::Paint &paint) const;
};

// Text measured with Atrament's fonts: `family`'s face (bold or not) at the pixel size rounded to a
// whole pixel (as MermaidView's fonts), the width of the shaped line and the font's ascent + descent.
diagram::Metrics atramentMetrics(atrament::App &app, std::u16string family, qreal fontSize);

// Paints `scene` with its top-left at `origin` (window logical px), scaled by `scale`.
void paintDiagram(atrament::Window &window, const diagram::Scene &scene, const DiagramColors &colors,
                  std::u16string_view family, atrament::Point origin, float scale);

// The largest side a diagram is drawn at (MermaidView::MaxPaintExtent): larger ones are scaled down.
constexpr float kDiagramMaxExtent = 4096;

class DiagramElement final : public atrament::Element {
public:
    // The scene for the width the element is given (a chart is laid out at it; a Mermaid diagram
    // has one size whatever the width). May return null: nothing drawn, no height.
    using SceneAt = std::function<std::shared_ptr<const diagram::Scene>(float width)>;
    // `minScale` > 0: a scene wider than the width is scaled down to this at most, and then the
    // element is wider than its width (MermaidBlockView's 0.7: a scroll container around it
    // scrolls sideways); 0: always scaled to the width (ChartBlockView).
    DiagramElement(SceneAt scene, std::shared_ptr<const DiagramColors> colors, std::u16string family,
                   float minScale = 0);

    atrament::NodeId requestLayout(atrament::Window &window, atrament::App &app) override;
    void prepaint(atrament::Rect bounds, atrament::Window &window, atrament::App &app) override;
    void paint(atrament::Rect bounds, atrament::Window &window, atrament::App &app) override;

    // The size the element takes at `width` for `scene`, and the scale it is drawn at.
    static atrament::Size sizeAt(const diagram::Scene &scene, float width, float minScale, float *scale = nullptr);

private:
    SceneAt m_sceneAt;
    std::shared_ptr<const DiagramColors> m_colors;
    std::u16string m_family;
    float m_minScale = 0;
    std::shared_ptr<const diagram::Scene> m_scene;
};

} // namespace brulion::editor::atr
