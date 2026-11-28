// PartRecorder — where a view drawn by Atrament drew the parts of its rows in the last frame (a
// row, its box, its title…): a tracker element over a part records its bounds in prepaint; the
// view forgets them as it renders the next frame. Self-tests and tests click there.
#pragma once

#include "ui/atrament.h"

#include <optional>
#include <vector>

namespace brulion::views::atr {

template<class Part>
class PartRecorder {
public:
    // A new frame: what the last one recorded goes.
    void clear()
    {
        m_drawn.clear();
        m_rows = 0;
    }
    // An element over its parent (absolute, the parent's size) recording the parent's bounds as
    // `part` of `row`; `wholeRow`: the part is the row itself (counted by rows()).
    atrament::AnyElement tracker(int row, Part part, bool wholeRow)
    {
        PartRecorder *self = this;
        return atrament::canvas({})
                .onPrepaint([self, row, part, wholeRow](atrament::Rect b, atrament::Window &, atrament::App &) {
                    self->m_drawn.push_back({row, part, b});
                    self->m_rows += wholeRow ? 1 : 0;
                })
                .absolute()
                .inset(0);
    }
    std::optional<atrament::Rect> bounds(int row, Part part) const
    {
        for (const Drawn &d : m_drawn)
            if (d.row == row && d.part == part)
                return d.bounds;
        return std::nullopt;
    }
    // The rows the last frame drew.
    int rows() const { return m_rows; }

private:
    struct Drawn {
        int row;
        Part part;
        atrament::Rect bounds;
    };
    std::vector<Drawn> m_drawn;
    int m_rows = 0;
};

} // namespace brulion::views::atr
