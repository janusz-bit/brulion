#include "base/coverage.h"

#include <algorithm>
#include <cmath>

namespace brulion::atrament {

void CoverageRasterizer::reset(int width, int height)
{
    m_width = std::max(0, width);
    m_height = std::max(0, height);
    m_stride = m_width + 2;
    m_cells.assign(std::size_t(m_stride) * std::size_t(m_height), 0.0f);
    m_open = false;
}

void CoverageRasterizer::moveTo(Point p)
{
    close();
    m_start = m_current = p;
    m_open = true;
}

void CoverageRasterizer::lineTo(Point p)
{
    if (!m_open) {
        moveTo(p);
        return;
    }
    addLine(m_current, p);
    m_current = p;
}

void CoverageRasterizer::quadTo(Point c, Point p)
{
    if (!m_open)
        moveTo(m_current);
    const Point p0 = m_current;
    // Segments from the curve's deviation: dd = |p0 - 2c + p| bounds the distance to the chord.
    const Point d = p0 - c * 2 + p;
    const float dd = std::sqrt(d.x * d.x + d.y * d.y);
    const int n = std::clamp(int(std::ceil(std::sqrt(dd / (4 * m_tolerance)))), 1, 64);
    for (int i = 1; i <= n; ++i) {
        const float t = float(i) / float(n);
        const float mt = 1 - t;
        lineTo(p0 * (mt * mt) + c * (2 * mt * t) + p * (t * t));
    }
}

void CoverageRasterizer::cubicTo(Point c1, Point c2, Point p)
{
    if (!m_open)
        moveTo(m_current);
    const Point p0 = m_current;
    const Point d1 = p0 - c1 * 2 + c2;
    const Point d2 = c1 - c2 * 2 + p;
    const float dd = std::max(std::sqrt(d1.x * d1.x + d1.y * d1.y), std::sqrt(d2.x * d2.x + d2.y * d2.y));
    const int n = std::clamp(int(std::ceil(std::sqrt(dd * 3 / (4 * m_tolerance)))), 1, 96);
    for (int i = 1; i <= n; ++i) {
        const float t = float(i) / float(n);
        const float mt = 1 - t;
        lineTo(p0 * (mt * mt * mt) + c1 * (3 * mt * mt * t) + c2 * (3 * mt * t * t) + p * (t * t * t));
    }
}

void CoverageRasterizer::close()
{
    if (m_open && !(m_current == m_start))
        addLine(m_current, m_start);
    m_current = m_start;
    m_open = false;
}

void CoverageRasterizer::addLine(Point p0, Point p1)
{
    if (m_height == 0 || std::abs(p0.y - p1.y) <= 1e-6f)
        return;
    float dir = 1;
    if (p0.y > p1.y) {
        std::swap(p0, p1);
        dir = -1;
    }
    if (p1.y <= 0 || p0.y >= float(m_height))
        return;
    const float dxdy = (p1.x - p0.x) / (p1.y - p0.y);
    float x = p0.x;
    float yTop = p0.y;
    if (yTop < 0) {
        x -= yTop * dxdy;
        yTop = 0;
    }
    const float yBottom = std::min(p1.y, float(m_height));
    const float maxX = float(m_width);
    const int yStart = int(std::floor(yTop));
    const int yEnd = int(std::ceil(yBottom));
    for (int y = yStart; y < yEnd; ++y) {
        float *row = m_cells.data() + std::size_t(y) * std::size_t(m_stride);
        const float dy = std::min(float(y + 1), yBottom) - std::max(float(y), yTop);
        const float xNext = x + dxdy * dy;
        const float d = dy * dir;
        // Clipped to [0, width]: what lies left of 0 covers the whole row to its right anyway.
        float x0 = std::clamp(std::min(x, xNext), 0.0f, maxX);
        float x1 = std::clamp(std::max(x, xNext), 0.0f, maxX);
        const float x0floor = std::floor(x0);
        const int x0i = int(x0floor);
        const float x1ceil = std::ceil(x1);
        const int x1i = int(x1ceil);
        if (x1i <= x0i + 1) {
            const float xmf = 0.5f * (x0 + x1) - x0floor;
            row[x0i] += d - d * xmf;
            row[x0i + 1] += d * xmf;
        } else {
            const float s = 1 / (x1 - x0);
            const float x0f = x0 - x0floor;
            const float a0 = 0.5f * s * (1 - x0f) * (1 - x0f);
            const float x1f = x1 - x1ceil + 1;
            const float am = 0.5f * s * x1f * x1f;
            row[x0i] += d * a0;
            if (x1i == x0i + 2) {
                row[x0i + 1] += d * (1 - a0 - am);
            } else {
                const float a1 = s * (1.5f - x0f);
                row[x0i + 1] += d * (a1 - a0);
                for (int xi = x0i + 2; xi < x1i - 1; ++xi)
                    row[xi] += d * s;
                const float a2 = a1 + float(x1i - x0i - 3) * s;
                row[x1i - 1] += d * (1 - a2 - am);
            }
            row[x1i] += d * am;
        }
        x = xNext;
    }
}

void CoverageRasterizer::accumulate(std::uint8_t *out, int stride) const
{
    for (int y = 0; y < m_height; ++y) {
        const float *row = m_cells.data() + std::size_t(y) * std::size_t(m_stride);
        std::uint8_t *o = out + std::ptrdiff_t(y) * stride;
        float acc = 0;
        for (int x = 0; x < m_width; ++x) {
            acc += row[x];
            const float c = std::min(std::abs(acc), 1.0f);
            o[x] = std::uint8_t(c * 255.0f + 0.5f);
        }
    }
}

void CoverageRasterizer::accumulate(std::vector<float> &out) const
{
    out.resize(std::size_t(m_width) * std::size_t(m_height));
    for (int y = 0; y < m_height; ++y) {
        const float *row = m_cells.data() + std::size_t(y) * std::size_t(m_stride);
        float *o = out.data() + std::size_t(y) * std::size_t(m_width);
        float acc = 0;
        for (int x = 0; x < m_width; ++x) {
            acc += row[x];
            o[x] = std::min(std::abs(acc), 1.0f);
        }
    }
}

} // namespace brulion::atrament
