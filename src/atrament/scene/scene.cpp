#include "scene/scene.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace brulion::atrament {

std::shared_ptr<ImageData> ImageData::make(int width, int height, std::vector<Pixel> pixels)
{
    static std::atomic<std::uint64_t> s_next { 1 };
    auto image = std::make_shared<ImageData>();
    image->width = width;
    image->height = height;
    image->pixels = std::move(pixels);
    image->pixels.resize(std::size_t(std::max(0, width)) * std::size_t(std::max(0, height)));
    image->id = s_next.fetch_add(1, std::memory_order_relaxed);
    return image;
}

void Scene::clear()
{
    m_primitives.clear();
}

void Scene::insert(Primitive p)
{
    m_primitives.push_back(std::move(p));
}

Rect primitiveBounds(const Primitive &p)
{
    return std::visit(
            [](const auto &prim) -> Rect {
                using T = std::decay_t<decltype(prim)>;
                if constexpr (std::is_same_v<T, Shadow>)
                    return prim.bounds.dilated(prim.blurRadius * 1.5f + 1);
                else
                    return prim.bounds;
            },
            p);
}

Rect Scene::bounds() const
{
    Rect total;
    for (const Primitive &p : m_primitives) {
        const ContentMask &mask = std::visit([](const auto &prim) -> const ContentMask & { return prim.mask; }, p);
        total = total.united(primitiveBounds(p).intersected(mask.bounds));
    }
    return total;
}

void PathBuilder::moveTo(Point p)
{
    close();
    m_points.push_back(p);
    m_current = p;
    m_open = true;
}

void PathBuilder::lineTo(Point p)
{
    if (!m_open) {
        moveTo(p);
        return;
    }
    m_points.push_back(p);
    m_current = p;
}

void PathBuilder::quadTo(Point c, Point p)
{
    const Point p0 = m_current;
    const Point d = p0 - c * 2 + p;
    const int n = std::clamp(int(std::ceil(std::sqrt(std::sqrt(d.x * d.x + d.y * d.y) / 0.8f))), 1, 64);
    for (int i = 1; i <= n; ++i) {
        const float t = float(i) / float(n);
        const float mt = 1 - t;
        lineTo(p0 * (mt * mt) + c * (2 * mt * t) + p * (t * t));
    }
}

void PathBuilder::cubicTo(Point c1, Point c2, Point p)
{
    const Point p0 = m_current;
    const Point d1 = p0 - c1 * 2 + c2;
    const Point d2 = c1 - c2 * 2 + p;
    const float dd = std::max(std::sqrt(d1.x * d1.x + d1.y * d1.y), std::sqrt(d2.x * d2.x + d2.y * d2.y));
    const int n = std::clamp(int(std::ceil(std::sqrt(dd * 3 / 0.8f))), 1, 96);
    for (int i = 1; i <= n; ++i) {
        const float t = float(i) / float(n);
        const float mt = 1 - t;
        lineTo(p0 * (mt * mt * mt) + c1 * (3 * mt * mt * t) + c2 * (3 * mt * t * t) + p * (t * t * t));
    }
}

void PathBuilder::arcTo(Point center, float radius, float a0, float a1)
{
    const float sweep = a1 - a0;
    const int n = std::clamp(int(std::ceil(std::abs(sweep) * std::sqrt(std::max(radius, 1.0f)) * 1.2f)), 2, 256);
    for (int i = 0; i <= n; ++i) {
        const float a = a0 + sweep * float(i) / float(n);
        const Point p { center.x + radius * std::cos(a), center.y + radius * std::sin(a) };
        if (i == 0 && !m_open)
            moveTo(p);
        else
            lineTo(p);
    }
}

void PathBuilder::close()
{
    if (m_open) {
        m_ends.push_back(std::uint32_t(m_points.size()));
        m_open = false;
    }
}

PathPrimitive PathBuilder::build(Color color, ContentMask mask) &&
{
    close();
    PathPrimitive path;
    path.color = color;
    path.mask = mask;
    if (!m_points.empty()) {
        float l = m_points[0].x, t = m_points[0].y, r = l, b = t;
        for (const Point &p : m_points) {
            l = std::min(l, p.x);
            t = std::min(t, p.y);
            r = std::max(r, p.x);
            b = std::max(b, p.y);
        }
        path.bounds = Rect::fromLTRB(l, t, r, b);
    }
    path.points = std::move(m_points);
    path.contourEnds = std::move(m_ends);
    return path;
}

} // namespace brulion::atrament
