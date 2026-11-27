#include "diagrampaint.h"

#include "renderedspans.h"
#include "text/textsystem.h"

#include <QHash>

#include <algorithm>
#include <cmath>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

std::optional<at::Color> diagramColor(QStringView css)
{
    if ((css.size() != 7 && css.size() != 9) || css.front() != u'#')
        return std::nullopt;
    bool ok = false;
    const uint v = css.sliced(1).toUInt(&ok, 16);
    if (!ok)
        return std::nullopt;
    if (css.size() == 7)
        return at::rgb(v);
    return at::rgba(v); // "#rrggbbaa": rgba()'s order
}

DiagramColors DiagramColors::from(const diagram::Palette &palette)
{
    DiagramColors c;
    for (int r = 0; r < diagram::RoleCount; ++r)
        c.roles[r] = diagramColor(palette.colors[r]).value_or(at::transparent());
    c.roles[int(diagram::Role::None)] = at::transparent();
    return c;
}

at::Color DiagramColors::of(const diagram::Paint &paint) const
{
    if (!paint.color.isEmpty())
        return diagramColor(paint.color).value_or(at::transparent());
    const int r = int(paint.role);
    return r > 0 && r < diagram::RoleCount ? roles[r] : at::transparent();
}

namespace {

std::u16string_view view(QStringView s)
{
    return {reinterpret_cast<const char16_t *>(s.utf16()), std::size_t(s.size())};
}

// One line of a label laid out in `face` at `px` (a whole number of pixels, as MermaidView's fonts).
std::shared_ptr<const at::TextLayout> lineLayout(at::App &app, QStringView line, at::FaceId face, float px,
                                                 float lineHeight)
{
    at::FontRun run;
    run.length = int(line.size());
    run.face = face;
    run.fontSize = px;
    at::ParagraphStyle style;
    style.lineHeight = lineHeight;
    return app.textSystem().layout(view(line), std::span<const at::FontRun>(&run, 1), style);
}

at::FaceId faceOf(at::App &app, std::u16string_view family, bool bold)
{
    at::FontCollection &fonts = app.textSystem().fonts();
    at::FaceId face = fonts.match(family, at::FontWeight{bold ? at::FontWeight::Bold : at::FontWeight::Normal});
    if (face == at::kNoFace)
        face = fonts.faceCount() > 0 ? 0 : at::kNoFace;
    return face;
}

float wholePixels(qreal px)
{
    return float(std::max<qreal>(1, std::round(px)));
}

} // namespace

diagram::Metrics atramentMetrics(at::App &app, std::u16string family, qreal fontSize)
{
    diagram::Metrics m;
    m.fontSize = fontSize;
    at::App *a = &app;
    // The faces and the fonts' heights by size and weight, found when first needed.
    auto heights = std::make_shared<QHash<qint64, float>>();
    m.measure = [a, family = std::move(family), heights](QStringView line, qreal px, bool bold) {
        const float size = wholePixels(px);
        const at::FaceId face = faceOf(*a, family, bold);
        const qint64 key = (qint64(std::lround(size * 100)) << 1) | (bold ? 1 : 0);
        auto it = heights->find(key);
        if (it == heights->end()) {
            const at::FontMetrics fm = face == at::kNoFace ? at::FontMetrics{} : a->textSystem().fonts().metrics(face);
            it = heights->insert(key, (fm.ascent + fm.descent) * size);
        }
        const std::shared_ptr<const at::TextLayout> layout = lineLayout(*a, line, face, size, 0);
        return QSizeF(layout ? layout->maxContentWidth() : 0, *it);
    };
    return m;
}

void paintDiagram(at::Window &window, const diagram::Scene &scene, const DiagramColors &colors,
                  std::u16string_view family, at::Point origin, float scale)
{
    at::App &app = window.app();
    const at::PathTransform t{scale, origin};
    for (const diagram::Item &it : scene.items) {
        if (it.type == diagram::Item::Type::Path) {
            at::VectorPath path;
            for (const diagram::PathOp &op : it.path) {
                const at::Point p{float(op.p.x()), float(op.p.y())};
                switch (op.type) {
                case diagram::PathOp::Type::Move: path.moveTo(p); break;
                case diagram::PathOp::Type::Line: path.lineTo(p); break;
                case diagram::PathOp::Type::Cubic:
                    path.cubicTo({float(op.c1.x()), float(op.c1.y())}, {float(op.c2.x()), float(op.c2.y())}, p);
                    break;
                case diagram::PathOp::Type::Close: path.close(); break;
                }
            }
            const at::Color fill = colors.of(it.fill);
            if (!fill.isTransparent())
                window.fillPath(path, t, fill);
            const at::Color stroke = colors.of(it.stroke);
            if (!stroke.isTransparent() && it.strokeWidth > 0) {
                // MermaidView's pen: round caps and joins; dotted and dashed with flat caps, the
                // pattern 2 / 3 or 6 / 4 px of the scene.
                at::StrokeStyle style;
                style.width = float(it.strokeWidth);
                style.cap = at::LineCap::Round;
                style.join = at::LineJoin::Round;
                if (it.dash != diagram::Dash::Solid) {
                    style.cap = at::LineCap::Butt;
                    style.dashes = it.dash == diagram::Dash::Dotted ? std::vector<float>{2, 3} : std::vector<float>{6, 4};
                }
                window.strokePath(path, t, style, stroke);
            }
            continue;
        }
        const at::Rect box = at::Rect::fromXYWH(float(it.box.x()) * scale + origin.x, float(it.box.y()) * scale + origin.y,
                                                float(it.box.width()) * scale, float(it.box.height()) * scale);
        const at::Color background = colors.of(it.background);
        if (!background.isTransparent())
            window.paintQuad(box, at::Background::solid(background), {}, at::transparent(), at::Corners::all(3 * scale));
        const at::Color color = colors.of(it.color);
        if (color.isTransparent() || it.lines.isEmpty())
            continue;
        const at::FaceId face = faceOf(app, family, it.bold);
        const float px = wholePixels(it.fontSize) * scale;
        const float lineHeight = float(it.lineHeight) * scale;
        const float top = float(it.box.center().y()) * scale + origin.y - lineHeight * float(it.lines.size()) / 2;
        const Qt::Alignment h = it.align & Qt::AlignHorizontal_Mask ? it.align & Qt::AlignHorizontal_Mask
                                                                     : Qt::Alignment(Qt::AlignHCenter);
        const at::TextPaintStyle style{color, std::nullopt, false, std::nullopt, false};
        for (qsizetype i = 0; i < it.lines.size(); ++i) {
            const std::shared_ptr<const at::TextLayout> layout = lineLayout(app, it.lines[i], face, px, lineHeight);
            if (!layout)
                continue;
            const float width = layout->maxContentWidth();
            float x = box.left() + (box.width() - width) / 2;
            if (h & Qt::AlignLeft)
                x = box.left();
            else if (h & Qt::AlignRight)
                x = box.right() - width;
            window.paintText(*layout, {x, top + float(i) * lineHeight}, std::span<const at::TextPaintStyle>(&style, 1));
        }
    }
}

// --- DiagramElement -----------------------------------------------------------------------------

DiagramElement::DiagramElement(SceneAt scene, std::shared_ptr<const DiagramColors> colors, std::u16string family,
                               float minScale)
    : m_sceneAt(std::move(scene)), m_colors(std::move(colors)), m_family(std::move(family)), m_minScale(minScale)
{ }

at::Size DiagramElement::sizeAt(const diagram::Scene &scene, float width, float minScale, float *scale)
{
    const float sw = float(scene.size.width());
    const float sh = float(scene.size.height());
    if (!(sw > 0) || !(sh > 0)) {
        if (scale)
            *scale = 1;
        return {std::max(0.0f, width), 0};
    }
    // Scaled down to the width (never up), to minScale at most: then wider than the width.
    float viewWidth = std::max(0.0f, width);
    if (minScale > 0)
        viewWidth = std::max(viewWidth, std::min(sw * minScale, kDiagramMaxExtent));
    const float k = std::min({1.0f, std::min(viewWidth, kDiagramMaxExtent) / sw, kDiagramMaxExtent / sh});
    if (scale)
        *scale = k;
    return {viewWidth, std::ceil(sh * k)};
}

at::NodeId DiagramElement::requestLayout(at::Window &window, at::App &)
{
    at::LayoutStyle style;
    // Not stretched across a scroll container: a diagram wider than its column keeps its width.
    if (m_minScale > 0)
        style.alignSelf = at::AlignItems::FlexStart;
    DiagramElement *self = this; // in the frame arena while the layout runs
    return window.requestMeasuredLayout(style, [self](std::optional<float> knownWidth, std::optional<float> knownHeight,
                                                      at::AvailableSpace availableWidth, at::AvailableSpace) -> at::Size {
        float width = 0;
        if (knownWidth)
            width = *knownWidth;
        else if (availableWidth.kind == at::AvailableSpace::Kind::Definite)
            width = std::max(0.0f, availableWidth.value);
        else if (self->m_scene && availableWidth.kind == at::AvailableSpace::Kind::MaxContent)
            width = float(self->m_scene->size.width());
        if (self->m_sceneAt && (!self->m_scene || availableWidth.kind == at::AvailableSpace::Kind::Definite || knownWidth))
            self->m_scene = self->m_sceneAt(width);
        if (!self->m_scene)
            return {width, knownHeight.value_or(0)};
        at::Size size = sizeAt(*self->m_scene, width, self->m_minScale);
        if (knownWidth)
            size.width = *knownWidth;
        if (knownHeight)
            size.height = *knownHeight;
        return size;
    });
}

void DiagramElement::prepaint(at::Rect bounds, at::Window &, at::App &)
{
    // The scene of the width the element got (the last measure may have been another).
    if (m_sceneAt)
        m_scene = m_sceneAt(bounds.width());
}

void DiagramElement::paint(at::Rect bounds, at::Window &window, at::App &)
{
    if (!m_scene || !m_colors)
        return;
    float k = 1;
    (void)sizeAt(*m_scene, bounds.width(), m_minScale, &k);
    const float drawnWidth = float(m_scene->size.width()) * k;
    paintDiagram(window, *m_scene, *m_colors, m_family, {bounds.left() + (bounds.width() - drawnWidth) / 2, bounds.top()}, k);
}

} // namespace brulion::editor::atr
