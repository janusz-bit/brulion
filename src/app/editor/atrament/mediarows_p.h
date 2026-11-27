// PageView's media rows' private parts (mediarows.cpp, queryrows.cpp): the frames' state — the
// pictures held, the embedded blocks read from their HTML, the query models — and the few icons
// Atrament draws itself until it has BIcon's.
#pragma once

#include "editorhost.h"
#include "pageview.h"
#include "picturecache.h"
#include "queryresultmodel.h"

#include "markdown/renderedtext.h"

#include <QCoreApplication>

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

// A string of a QML view's translation context (its Polish words, one translation for both).
inline QString translated(const char *context, const char *text, int n = -1)
{
    return QCoreApplication::translate(context, text, nullptr, n);
}

// An icon of Atrament's own until it draws BIcon's: a page (with lines for "file-text"), a
// picture (a frame with a hill), a link (two rings) — outlines in `color`, centred in `b`.
enum class Glyph { File, FileText, Picture, Heading, Link, Filter, Info };

inline void paintGlyph(at::Window &w, at::Rect b, at::Color color, Glyph glyph)
{
    const float cx = b.left() + b.width() / 2;
    const float cy = b.top() + b.height() / 2;
    const float s = std::min(b.width(), b.height());
    const float stroke = std::max(1.2f, s / 10);
    switch (glyph) {
    case Glyph::File:
    case Glyph::FileText: {
        const at::Rect page = at::Rect::fromXYWH(cx - s * 0.34f, cy - s * 0.44f, s * 0.68f, s * 0.88f);
        w.paintQuad(page, std::nullopt, at::Edges::all(stroke), color, at::Corners::all(s * 0.12f));
        if (glyph == Glyph::FileText)
            for (int i = 0; i < 2; ++i)
                w.paintQuad(at::Rect::fromXYWH(page.left() + s * 0.16f, page.top() + s * (0.34f + 0.2f * float(i)),
                                               page.width() - s * 0.32f, stroke),
                            at::Background::solid(color), {}, {}, {});
        break;
    }
    case Glyph::Picture: {
        const at::Rect frame = at::Rect::fromXYWH(cx - s * 0.44f, cy - s * 0.36f, s * 0.88f, s * 0.72f);
        w.paintQuad(frame, std::nullopt, at::Edges::all(stroke), color, at::Corners::all(s * 0.12f));
        w.paintPath(color, [&](at::Window::PathPainter &p) {
            p.moveTo({frame.left() + stroke, frame.bottom() - stroke});
            p.lineTo({frame.left() + frame.width() * 0.38f, frame.top() + frame.height() * 0.45f});
            p.lineTo({frame.left() + frame.width() * 0.62f, frame.bottom() - frame.height() * 0.3f});
            p.lineTo({frame.left() + frame.width() * 0.75f, frame.top() + frame.height() * 0.55f});
            p.lineTo({frame.right() - stroke, frame.bottom() - stroke});
            p.close();
        });
        break;
    }
    case Glyph::Heading:
        for (const float x : {cx - s * 0.3f, cx + s * 0.3f - stroke})
            w.paintQuad(at::Rect::fromXYWH(x, cy - s * 0.38f, stroke, s * 0.76f), at::Background::solid(color), {}, {}, {});
        w.paintQuad(at::Rect::fromXYWH(cx - s * 0.3f, cy - stroke / 2, s * 0.6f, stroke), at::Background::solid(color), {},
                    {}, {});
        break;
    case Glyph::Filter:
        // A funnel.
        w.paintPath(color, [&](at::Window::PathPainter &p) {
            p.moveTo({cx - s * 0.42f, cy - s * 0.36f});
            p.lineTo({cx + s * 0.42f, cy - s * 0.36f});
            p.lineTo({cx + s * 0.08f, cy + s * 0.04f});
            p.lineTo({cx + s * 0.08f, cy + s * 0.4f});
            p.lineTo({cx - s * 0.08f, cy + s * 0.3f});
            p.lineTo({cx - s * 0.08f, cy + s * 0.04f});
            p.close();
        });
        break;
    case Glyph::Info: {
        // A ring with a dot and a bar.
        const float r = s * 0.42f;
        w.paintQuad(at::Rect::fromXYWH(cx - r, cy - r, 2 * r, 2 * r), std::nullopt, at::Edges::all(stroke), color,
                    at::Corners::all(r));
        w.paintQuad(at::Rect::fromXYWH(cx - stroke / 2, cy - r * 0.1f, stroke, r * 0.6f), at::Background::solid(color), {},
                    {}, {});
        w.paintQuad(at::Rect::fromXYWH(cx - stroke / 2, cy - r * 0.5f, stroke, stroke), at::Background::solid(color), {},
                    {}, {});
        break;
    }
    case Glyph::Link: {
        const float r = s * 0.24f;
        for (const float dx : {-r * 0.7f, r * 0.7f})
            w.paintQuad(at::Rect::fromXYWH(cx + dx - r, cy - r * 0.75f, 2 * r, 1.5f * r), std::nullopt, at::Edges::all(stroke),
                        color, at::Corners::all(r * 0.75f));
        break;
    }
    }
}

inline at::AnyElement glyphElement(float size, at::Color color, Glyph glyph)
{
    return at::canvas([color, glyph](at::Rect b, at::Window &w, at::App &) { paintGlyph(w, b, color, glyph); })
            .size(size)
            .flexShrink0();
}

struct PageView::Media {
    std::shared_ptr<PictureCache> cache;
    int listener = 0;
    // The page's entity (a query's new results draw it again).
    at::App *app = nullptr;
    at::EntityId self = 0;
    // The pictures the last two frames drew: the cache evicts nothing held.
    std::vector<std::shared_ptr<const at::ImageData>> held;
    std::vector<std::shared_ptr<const at::ImageData>> heldBefore;
    float scale = 1; // the window's, as the last frame painted
    // Embedded blocks read from their HTML, by the block's uid (read again when the list changes).
    struct Entry {
        enum class Kind { Text, Code, Rule, Nested } kind = Kind::Text;
        std::shared_ptr<const markdown::RenderedText> text;
        QString plain; // a code block's text
        int depth = 0;
        int level = 0;
        int line = -1;
        bool found = false;
        QString view, task, path, raw, title, href, message;
    };
    struct Frame {
        QString kind, title, href, message;
        bool found = false;
        int more = 0;
        std::vector<Entry> entries;
    };
    struct Parsed {
        QVariantList source;
        bool drawable = false;
        std::vector<Frame> frames;
        bool parsed = false;
        std::uint64_t frame = 0; // last drawn
    };
    std::unordered_map<quint64, Parsed> transclusions;
    // Query blocks' results by uid, kept while drawn (queryrows.cpp).
    struct Query {
        std::unique_ptr<QueryResultModel> model;
        std::vector<QMetaObject::Connection> connections;
        std::uint64_t frame = 0;
        QString language;
        // The items' rendered text by row, read from their HTML once.
        std::vector<std::pair<QString, std::shared_ptr<const markdown::RenderedText>>> texts;
        std::vector<std::vector<std::pair<QString, std::shared_ptr<const markdown::RenderedText>>>> cells;
    };
    std::unordered_map<quint64, Query> queries;

    ~Media()
    {
        if (cache)
            cache->unsubscribe(listener);
        for (auto &[uid, q] : queries)
            for (const QMetaObject::Connection &c : q.connections)
                QObject::disconnect(c);
    }

    const Parsed &parse(quint64 uid, const QVariantList &list);
};


} // namespace brulion::editor::atr
