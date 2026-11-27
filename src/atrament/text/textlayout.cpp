#include "text/textlayout.h"

#include "text/segmentation.h"
#include "text/segmentation_p.h"
#include "text/unicode.h"

#include <hb.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace brulion::atrament {

namespace layoutdetail {

// Two widths this close are equal: a layout at its own min- or max-content width wraps the same.
constexpr float kWidthEpsilon = 1e-3f;
// A tab is as wide as four spaces (no tab stops yet).
constexpr float kTabSpaces = 4;

// A run against the text: clipped to it, with a face that exists (or kNoFace in an empty
// collection) and a size that is a number ≥ 0.
struct ResolvedRun {
    int start = 0;
    int end = 0;
    FaceId face = kNoFace;
    float fontSize = 0;
    std::uint32_t styleIndex = 0;
    const FontRun *source = nullptr; // features and language; null for text past the runs
    std::optional<InlineBox> box;    // an inline box (sizes finite and ≥ 0)
    float shift = 0;                 // FontRun::baselineShift (finite)
};

float usableSize(float size)
{
    return std::isfinite(size) && size > 0 ? size : 0.0f;
}

std::optional<InlineBox> usableBox(const std::optional<InlineBox> &box)
{
    if (!box)
        return std::nullopt;
    return InlineBox{usableSize(box->width), usableSize(box->ascent), usableSize(box->descent)};
}

std::vector<ResolvedRun> resolveRuns(const FontCollection &fonts, int length, std::span<const FontRun> runs)
{
    const FaceId first = fonts.faceCount() > 0 ? 0 : kNoFace;
    auto usableFace = [&](FaceId face) { return face < fonts.faceCount() ? face : first; };
    std::vector<ResolvedRun> out;
    int pos = 0;
    for (const FontRun &run : runs) {
        if (pos >= length)
            break;
        if (run.length <= 0)
            continue;
        ResolvedRun r;
        r.start = pos;
        r.end = run.length >= length - pos ? length : pos + run.length;
        r.face = usableFace(run.face);
        r.fontSize = usableSize(run.fontSize);
        r.styleIndex = run.styleIndex;
        r.source = &run;
        r.box = usableBox(run.inlineBox);
        r.shift = std::isfinite(run.baselineShift) ? run.baselineShift : 0.0f;
        out.push_back(r);
        pos = r.end;
    }
    if (pos < length || out.empty()) {
        // Text past the runs (or an empty text, whose one line needs a font's height).
        ResolvedRun r;
        r.start = pos;
        r.end = length;
        if (length == 0 && !runs.empty()) {
            r.face = usableFace(runs.front().face);
            r.fontSize = usableSize(runs.front().fontSize);
            r.styleIndex = runs.front().styleIndex;
            r.source = &runs.front();
        } else {
            r.face = first;
            r.fontSize = runs.empty() ? FontRun{}.fontSize : usableSize(runs.back().fontSize);
            r.styleIndex = runs.empty() ? 0 : runs.back().styleIndex;
        }
        out.push_back(r);
    }
    return out;
}

std::vector<hb_feature_t> parseFeatures(std::u16string_view spec)
{
    std::vector<hb_feature_t> out;
    std::string token;
    bool valid = true;
    auto flush = [&] {
        hb_feature_t feature;
        if (valid && !token.empty() && hb_feature_from_string(token.data(), int(token.size()), &feature))
            out.push_back(feature);
        token.clear();
        valid = true;
    };
    for (const char16_t c : spec) {
        if (c == u',' || c == u' ' || c == u'\t')
            flush();
        else if (c < 0x80)
            token.push_back(char(c));
        else
            valid = false;
    }
    flush();
    return out;
}

hb_language_t languageOf(const ResolvedRun &run)
{
    static const hb_language_t undetermined = hb_language_from_string("und", -1);
    if (!run.source || run.source->language.empty())
        return undetermined;
    std::string tag;
    for (const char16_t c : run.source->language) {
        if (c >= 0x80)
            return undetermined;
        tag.push_back(char(c));
    }
    return hb_language_from_string(tag.data(), int(tag.size()));
}

// One buffer for every shaping call of the thread.
struct ThreadBuffer {
    hb_buffer_t *buffer = hb_buffer_create();
    ThreadBuffer() { hb_buffer_set_cluster_level(buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES); }
    ~ThreadBuffer() { hb_buffer_destroy(buffer); }
    ThreadBuffer(const ThreadBuffer &) = delete;
    ThreadBuffer &operator=(const ThreadBuffer &) = delete;
};

hb_buffer_t *shapingBuffer()
{
    thread_local ThreadBuffer buffer;
    return buffer.buffer;
}

bool isRealScript(hb_script_t s)
{
    return s != HB_SCRIPT_COMMON && s != HB_SCRIPT_INHERITED && s != HB_SCRIPT_UNKNOWN && s != HB_SCRIPT_INVALID;
}

// Characters that take the face of what they follow instead of looking for one with a glyph:
// controls, format characters (ZWJ, ZWSP, soft hyphen), separators, variation selectors.
bool followsPreviousFace(char32_t c)
{
    if (c < 0x80)
        return c < 0x20 || c == 0x7F;
    if ((c >= 0xFE00 && c <= 0xFE0F) || (c >= 0xE0100 && c <= 0xE01EF))
        return true;
    switch (unicode::generalCategory(c)) {
    case HB_UNICODE_GENERAL_CATEGORY_CONTROL:
    case HB_UNICODE_GENERAL_CATEGORY_FORMAT:
    case HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR:
    case HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR:
    case HB_UNICODE_GENERAL_CATEGORY_SURROGATE: return true;
    default: return false;
    }
}

hb_script_t scriptOf(char32_t c)
{
    if (c < 0x80)
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ? HB_SCRIPT_LATIN : HB_SCRIPT_COMMON;
    return unicode::script(c);
}

// A run of one face and script within one font run: what HarfBuzz shapes at once (or an inline
// box, which is not shaped).
struct Item {
    int start = 0;
    int end = 0;
    std::size_t run = 0;
    FaceId face = kNoFace;
    hb_script_t script = HB_SCRIPT_COMMON;
    bool box = false;
};

// An inline box is one cluster with no break inside: its own ends become grapheme boundaries, the
// boundaries and break opportunities inside it go.
void applyInlineBoxes(const std::vector<ResolvedRun> &runs, std::vector<int> &graphemes, std::vector<BreakKind> &breaks)
{
    for (const ResolvedRun &run : runs) {
        if (!run.box || run.end <= run.start)
            continue;
        auto from = std::lower_bound(graphemes.begin(), graphemes.end(), run.start);
        auto to = std::upper_bound(from, graphemes.end(), run.end);
        from = graphemes.insert(graphemes.erase(from, to), {run.start, run.end});
        for (int k = run.start + 1; k < run.end; ++k)
            breaks[std::size_t(k)] = BreakKind::None;
    }
}

} // namespace layoutdetail

using namespace layoutdetail;

struct TextLayout::Builder {
    static TextLayout build(const FontCollection &fonts, std::u16string_view text, std::span<const FontRun> runs,
                            const ParagraphStyle &style);
    static TextLayout rewrap(const FontCollection &fonts, const TextLayout &layout, std::span<const FontRun> runs,
                             const ParagraphStyle &style);
    // Shapes the text: glyphs with their unwrapped pen x (position.x) and their offset from the
    // baseline (position.y, down), and the caret x of every offset.
    static void shape(const FontCollection &fonts, std::u16string_view text, const std::vector<ResolvedRun> &runs,
                      const std::vector<int> &graphemes, std::vector<PositionedGlyph> &glyphs,
                      std::vector<float> &offsetX);
    // Breaks the shaped text into lines and places the glyphs on them.
    static void wrap(TextLayout &layout, const FontCollection &fonts, const std::vector<ResolvedRun> &runs,
                     std::vector<PositionedGlyph> glyphs, const std::vector<int> &graphemes,
                     const std::vector<BreakKind> &breaks, const ParagraphStyle &style);
    // The grapheme boundary of a line nearest to x (from the line's start); the line's end only
    // when `softEnd` allows it or the line does not end at a wrap.
    static int nearest(const TextLayout &layout, std::size_t line, float x, bool softEnd);
    static float x(const TextLayout &layout, int offset)
    {
        return offset >= 0 && std::size_t(offset) < layout.m_offsetX.size() ? layout.m_offsetX[std::size_t(offset)]
                                                                            : 0.0f;
    }
};

namespace {

// Offsets [from, to) of one shaped cluster at x with `advance`: the graphemes in it share the
// advance equally (carets inside a ligature), offsets inside a grapheme take its start's x.
void spreadCluster(std::vector<float> &offsetX, int &filled, int from, int to, float x, float advance,
                   const std::vector<int> &graphemes, std::size_t &g)
{
    for (; filled < from; ++filled)
        offsetX[std::size_t(filled)] = x; // text the shaper left without glyphs
    if (to <= from)
        return;
    while (g < graphemes.size() && graphemes[g] <= from)
        ++g;
    std::size_t last = g;
    while (last < graphemes.size() && graphemes[last] < to)
        ++last;
    const std::size_t count = 1 + (last - g);
    int start = from;
    for (std::size_t k = 0; k < count; ++k) {
        const int end = g + k < last ? graphemes[g + k] : to;
        const float gx = x + advance * float(k) / float(count);
        for (int o = start; o < end; ++o)
            offsetX[std::size_t(o)] = gx;
        start = end;
    }
    g = last;
    filled = to;
}

} // namespace

void TextLayout::Builder::shape(const FontCollection &fonts, std::u16string_view text,
                                const std::vector<ResolvedRun> &runs, const std::vector<int> &graphemes,
                                std::vector<PositionedGlyph> &glyphs, std::vector<float> &offsetX)
{
    const int n = int(text.size());
    glyphs.clear();
    glyphs.reserve(text.size());
    offsetX.assign(std::size_t(n) + 1, 0.0f);

    // Controls and the tab are shaped as spaces (fonts have no glyphs for them) and then drawn as
    // nothing: the copy has the same length, so clusters still index the text.
    std::u16string copy;
    std::u16string_view shaping = text;
    if (std::any_of(text.begin(), text.end(),
                    [](char16_t c) { return c == u'\t' || unicode::isInvisibleControl(c); })) {
        copy.assign(text);
        for (char16_t &c : copy) {
            if (c == u'\t' || unicode::isInvisibleControl(c))
                c = u' ';
        }
        shaping = copy;
    }

    // Itemization: whole grapheme clusters at a time, the face of the cluster's first character
    // (the run's face where it has a glyph, else the fallback's), the script of the run of text
    // (common characters such as spaces and punctuation join their neighbours).
    std::vector<Item> items;
    std::size_t g = 0;
    for (std::size_t r = 0; r < runs.size(); ++r) {
        const ResolvedRun &run = runs[r];
        if (run.box) {
            if (run.end > run.start)
                items.push_back(Item{run.start, run.end, r, run.face, HB_SCRIPT_COMMON, true});
            continue;
        }
        int pos = run.start;
        while (pos < run.end) {
            while (g < graphemes.size() && graphemes[std::size_t(g)] <= pos)
                ++g;
            const int end = g < graphemes.size() ? std::min(graphemes[g], run.end) : run.end;
            int k = pos;
            const char32_t c = readCodepoint(text, k);
            const bool sameRun = !items.empty() && items.back().run == r;
            FaceId face;
            if (followsPreviousFace(c)) {
                face = sameRun ? items.back().face : run.face;
            } else {
                face = fonts.faceFor(c, run.face);
                if (face == kNoFace)
                    face = run.face; // nobody has it: the run's .notdef
            }
            const hb_script_t script = scriptOf(c);
            const bool real = isRealScript(script);
            if (sameRun && items.back().face == face
                && (!real || items.back().script == HB_SCRIPT_COMMON || items.back().script == script)) {
                items.back().end = end;
                if (real)
                    items.back().script = script;
            } else {
                items.push_back(Item{pos, end, r, face, real ? script : HB_SCRIPT_COMMON});
            }
            pos = end;
        }
    }

    hb_buffer_t *buffer = shapingBuffer();
    float pen = 0;
    int filled = 0;
    std::size_t spreadIndex = 0;
    std::vector<hb_feature_t> features;
    std::size_t featuresRun = std::size_t(-1);
    for (const Item &item : items) {
        const ResolvedRun &run = runs[item.run];
        if (item.box) {
            // One cluster as wide as the box, every offset inside it at its start.
            for (; filled < item.end; ++filled)
                offsetX[std::size_t(filled)] = pen;
            pen += run.box->width;
            continue;
        }
        hb_font_t *font = fonts.hbFont(item.face);
        if (!font) {
            spreadCluster(offsetX, filled, item.start, item.end, pen, 0, graphemes, spreadIndex);
            continue;
        }
        if (featuresRun != item.run) {
            features = run.source ? parseFeatures(run.source->features) : std::vector<hb_feature_t>{};
            featuresRun = item.run;
        }
        hb_buffer_clear_contents(buffer);
        // The whole paragraph as context, so shaping sees the neighbours of the item.
        hb_buffer_add_utf16(buffer, reinterpret_cast<const std::uint16_t *>(shaping.data()), n, unsigned(item.start),
                            item.end - item.start);
        hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
        if (item.script != HB_SCRIPT_COMMON)
            hb_buffer_set_script(buffer, item.script);
        hb_buffer_set_language(buffer, languageOf(run));
        hb_buffer_guess_segment_properties(buffer);
        unsigned flags = HB_BUFFER_FLAG_DEFAULT;
        if (item.start == 0)
            flags |= HB_BUFFER_FLAG_BOT;
        if (item.end == n)
            flags |= HB_BUFFER_FLAG_EOT;
        hb_buffer_set_flags(buffer, hb_buffer_flags_t(flags));
        hb_shape(font, buffer, features.data(), unsigned(features.size()));

        unsigned count = 0;
        const hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buffer, &count);
        const hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, &count);
        const int upem = fonts.unitsPerEm(item.face);
        const float scale = upem > 0 ? run.fontSize / float(upem) : 0.0f;
        for (unsigned i = 0; i < count;) {
            const int cluster = std::clamp(int(info[i].cluster), item.start, item.end);
            const char16_t first = text[std::size_t(std::min(cluster, n - 1))];
            const bool invisible = unicode::isInvisibleControl(first);
            float advance = 0;
            unsigned j = i;
            for (; j < count && int(info[j].cluster) == int(info[i].cluster); ++j) {
                if (invisible)
                    continue; // a line break or control: no glyph, no width
                float glyphAdvance = float(positions[j].x_advance) * scale;
                if (first == u'\t')
                    glyphAdvance *= kTabSpaces;
                PositionedGlyph glyph;
                glyph.face = item.face;
                glyph.fontSize = run.fontSize;
                glyph.glyph = info[j].codepoint;
                glyph.position = {pen + advance + float(positions[j].x_offset) * scale,
                                  -float(positions[j].y_offset) * scale - run.shift};
                glyph.styleIndex = run.styleIndex;
                glyph.cluster = cluster;
                glyphs.push_back(glyph);
                advance += glyphAdvance;
            }
            const int clusterEnd = j < count ? std::clamp(int(info[j].cluster), cluster, item.end) : item.end;
            spreadCluster(offsetX, filled, cluster, clusterEnd, pen, advance, graphemes, spreadIndex);
            pen += advance;
            i = j;
        }
        spreadCluster(offsetX, filled, item.end, item.end, pen, 0, graphemes, spreadIndex);
    }
    for (; filled <= n; ++filled)
        offsetX[std::size_t(filled)] = pen;
}

void TextLayout::Builder::wrap(TextLayout &layout, const FontCollection &fonts, const std::vector<ResolvedRun> &runs,
                               std::vector<PositionedGlyph> glyphs, const std::vector<int> &graphemes,
                               const std::vector<BreakKind> &breaks, const ParagraphStyle &style)
{
    const std::u16string &text = layout.m_text;
    const int n = int(text.size());
    const std::vector<float> &X = layout.m_offsetX;
    std::optional<float> wrapWidth = style.wrapWidth;
    if (wrapWidth && std::isnan(*wrapWidth))
        wrapWidth.reset();
    if (wrapWidth && *wrapWidth < 0)
        wrapWidth = 0.0f;
    layout.m_wrapWidth = wrapWidth;

    // Where a mandatory break's characters start (LF, CR LF, U+2028…), or `b` without them.
    auto breakStart = [&](int b) {
        if (b <= 0 || !unicode::isMandatoryBreak(text[std::size_t(b - 1)]))
            return b;
        if (b >= 2 && text[std::size_t(b - 1)] == u'\n' && text[std::size_t(b - 2)] == u'\r')
            return b - 2;
        return b - 1;
    };
    // The end without the spaces that hang there.
    auto visibleEnd = [&](int start, int end) {
        while (end > start && unicode::isHangingSpace(text[std::size_t(end - 1)]))
            --end;
        return end;
    };
    auto widthOf = [&](int start, int end) { return X[std::size_t(end)] - X[std::size_t(start)]; };

    // Min- and max-content widths.
    float minContent = 0;
    float maxContent = 0;
    for (int b = 1, segment = 0, line = 0; b <= n; ++b) {
        if (breaks[std::size_t(b)] == BreakKind::None)
            continue;
        const int shown = breaks[std::size_t(b)] == BreakKind::Mandatory ? breakStart(b) : b;
        minContent = std::max(minContent, widthOf(segment, visibleEnd(segment, std::max(segment, shown))));
        segment = b;
        if (breaks[std::size_t(b)] == BreakKind::Mandatory) {
            maxContent = std::max(maxContent, widthOf(line, visibleEnd(line, std::max(line, shown))));
            line = b;
        }
    }
    layout.m_minContentWidth = minContent;
    layout.m_maxContentWidth = maxContent;

    // Greedy wrapping: the last allowed break that fits; a word wider than the line breaks at the
    // last grapheme that fits (at least one per line).
    std::vector<TextLine> &lines = layout.m_lines;
    lines.clear();
    const float limit = wrapWidth ? *wrapWidth + kWidthEpsilon : 0.0f;
    int start = 0;
    while (n > 0) {
        int lineEnd = n;
        bool mandatory = true;
        int lastFit = -1;
        for (int b = start + 1; b <= n; ++b) {
            const BreakKind kind = breaks[std::size_t(b)];
            if (kind == BreakKind::None)
                continue;
            const int shown = kind == BreakKind::Mandatory ? breakStart(b) : b;
            const int visible = visibleEnd(start, std::max(start, shown));
            if (wrapWidth && widthOf(start, visible) > limit) {
                if (lastFit >= 0) {
                    lineEnd = lastFit;
                    mandatory = false;
                    break;
                }
                auto it = std::upper_bound(graphemes.begin(), graphemes.end(), start);
                int fit = it != graphemes.end() ? *it : n;
                for (; it != graphemes.end() && *it < visible; ++it) {
                    if (widthOf(start, *it) > limit)
                        break;
                    fit = *it;
                }
                if (fit < visible) {
                    lineEnd = fit;
                    mandatory = false;
                    break;
                }
                // One grapheme wider than the line: it stays whole, with its spaces.
            }
            if (kind == BreakKind::Mandatory) {
                lineEnd = b;
                mandatory = true;
                break;
            }
            lastFit = b;
        }
        TextLine line;
        line.start = start;
        line.next = lineEnd;
        line.end = mandatory ? std::max(start, breakStart(lineEnd)) : lineEnd;
        line.width = widthOf(start, visibleEnd(start, line.end));
        lines.push_back(line);
        if (lineEnd >= n) {
            if (line.end < n) // the text ends with a line break: an empty line after it
                lines.push_back(TextLine{n, n, n});
            break;
        }
        start = lineEnd;
    }
    if (n == 0)
        lines.push_back(TextLine{});

    // Line metrics from the runs' own fonts (not their fallbacks', so an emoji never makes a line
    // taller), CSS's half-leading, and the glyphs placed on their lines.
    struct RunMetrics {
        float ascent = 0;
        float descent = 0;
        float gap = 0;
        float space = 0;
    };
    std::vector<RunMetrics> metrics(runs.size());
    layout.m_runs.clear();
    layout.m_runs.reserve(runs.size());
    for (std::size_t r = 0; r < runs.size(); ++r) {
        const FontMetrics m = fonts.metrics(runs[r].face);
        const float size = runs[r].fontSize;
        metrics[r] = {m.ascent * size, m.descent * size, m.lineGap * size, 0};
        if (const std::optional<InlineBox> &box = runs[r].box) {
            metrics[r] = {box->ascent, box->descent, 0, 0}; // the box in place of its text's font
        } else if (const float shift = runs[r].shift; shift != 0) {
            // A raised or lowered run reaches that much higher and less low (or the other way).
            metrics[r].ascent = std::max(0.0f, metrics[r].ascent + shift);
            metrics[r].descent = std::max(0.0f, metrics[r].descent - shift);
        }
        layout.m_runs.push_back(LaidOutRun{runs[r].start, runs[r].end, runs[r].styleIndex, metrics[r].ascent,
                                           metrics[r].descent, runs[r].box.has_value(),
                                           runs[r].box ? 0.0f : runs[r].shift});
        if (hb_font_t *font = fonts.hbFont(runs[r].face)) {
            hb_codepoint_t space = 0;
            if (hb_font_get_nominal_glyph(font, ' ', &space))
                metrics[r].space = float(hb_font_get_glyph_h_advance(font, space)) * size
                                   / float(std::max(1, fonts.unitsPerEm(runs[r].face)));
        }
    }
    auto runAt = [&](int offset) {
        auto it = std::upper_bound(runs.begin(), runs.end(), offset,
                                   [](int o, const ResolvedRun &run) { return o < run.end; });
        return it == runs.end() ? runs.size() - 1 : std::size_t(it - runs.begin());
    };
    layout.m_breakWidths.assign(lines.size(), 0.0f);
    layout.m_glyphs = std::move(glyphs);
    std::vector<PositionedGlyph> &placed = layout.m_glyphs;
    std::size_t gi = 0;
    float top = 0;
    float width = 0;
    for (std::size_t li = 0; li < lines.size(); ++li) {
        TextLine &line = lines[li];
        float ascent = 0;
        float descent = 0;
        float tallest = 0;
        if (line.end > line.start) {
            for (std::size_t r = runAt(line.start); r < runs.size() && runs[r].start < line.end; ++r) {
                const RunMetrics &m = metrics[r];
                ascent = std::max(ascent, m.ascent);
                descent = std::max(descent, m.descent);
                tallest = std::max(tallest, m.ascent + m.descent + m.gap);
            }
        } else {
            const RunMetrics &m = metrics[runAt(line.start)];
            ascent = m.ascent;
            descent = m.descent;
            tallest = m.ascent + m.descent + m.gap;
        }
        const float height = style.lineHeight > 0 ? style.lineHeight : std::max(tallest, ascent + descent);
        line.top = top;
        line.height = height;
        line.ascent = ascent;
        line.descent = descent;
        line.baseline = top + (height - (ascent + descent)) / 2 + ascent;
        layout.m_breakWidths[li] = metrics[runAt(line.end)].space;
        top += height;
        width = std::max(width, line.width);

        const bool last = li + 1 == lines.size();
        const float originX = X[std::size_t(line.start)];
        line.firstGlyph = std::uint32_t(gi);
        while (gi < placed.size() && (last || placed[gi].cluster < line.next)) {
            placed[gi].position.x -= originX;
            placed[gi].position.y += line.baseline;
            ++gi;
        }
        line.glyphCount = std::uint32_t(gi) - line.firstGlyph;
    }
    layout.m_size = {width, top};

    // The inline boxes on their lines.
    layout.m_inlineBoxes.clear();
    for (const ResolvedRun &run : runs) {
        if (!run.box || run.end <= run.start)
            continue;
        const int li = layout.lineIndex(run.start);
        const TextLine &line = lines[std::size_t(li)];
        const float x = X[std::size_t(run.start)] - X[std::size_t(line.start)];
        layout.m_inlineBoxes.push_back(
                PlacedInlineBox{run.start, run.end, li,
                                Rect::fromLTRB(x, line.baseline - run.box->ascent, x + run.box->width,
                                               line.baseline + run.box->descent),
                                run.styleIndex});
    }
}

int TextLayout::Builder::nearest(const TextLayout &layout, std::size_t li, float px, bool softEnd)
{
    const TextLine &line = layout.m_lines[li];
    const bool wraps = line.end == line.next && li + 1 < layout.m_lines.size() && line.end > line.start;
    const int end = wraps && !softEnd ? layout.previousCaretStop(line.end) : line.end;
    const float origin = x(layout, line.start);
    int best = line.start;
    float bestDistance = std::abs(px);
    for (int o = line.start; o < end;) {
        o = std::min(layout.nextCaretStop(o), end);
        const float ox = x(layout, o) - origin;
        const float distance = std::abs(ox - px);
        if (distance < bestDistance) {
            best = o;
            bestDistance = distance;
        } else if (ox > px) {
            break;
        }
    }
    return best;
}

int TextLayout::lineIndex(int offset, CaretAffinity affinity) const
{
    if (m_lines.empty())
        return 0;
    offset = std::clamp(offset, 0, length());
    auto it = std::upper_bound(m_lines.begin(), m_lines.end(), offset,
                               [](int o, const TextLine &line) { return o < line.start; });
    const int index = it == m_lines.begin() ? 0 : int(it - m_lines.begin()) - 1;
    if (affinity == CaretAffinity::Upstream && index > 0) {
        const TextLine &previous = m_lines[std::size_t(index - 1)];
        if (previous.next == offset && previous.end == previous.next)
            return index - 1; // the end of a wrapped line
    }
    return index;
}

float TextLayout::caretX(int offset, CaretAffinity affinity) const
{
    if (m_lines.empty())
        return 0;
    const TextLine &line = m_lines[std::size_t(lineIndex(offset, affinity))];
    const int o = std::clamp(offset, line.start, line.end);
    return Builder::x(*this, o) - Builder::x(*this, line.start);
}

Rect TextLayout::caretRect(int offset, CaretAffinity affinity) const
{
    if (m_lines.empty())
        return {};
    const TextLine &line = m_lines[std::size_t(lineIndex(offset, affinity))];
    return Rect::fromXYWH(caretX(offset, affinity), line.top, 0, line.height);
}

int TextLayout::offsetAt(Point p, CaretAffinity *affinity) const
{
    if (affinity)
        *affinity = CaretAffinity::Downstream;
    if (m_lines.empty())
        return 0;
    auto it = std::upper_bound(m_lines.begin(), m_lines.end(), p.y,
                               [](float y, const TextLine &line) { return y < line.top + line.height; });
    const std::size_t li = it == m_lines.end() ? m_lines.size() - 1 : std::size_t(it - m_lines.begin());
    const int offset = Builder::nearest(*this, li, p.x, true);
    const TextLine &line = m_lines[li];
    if (affinity && offset == line.end && line.end == line.next && line.end > line.start && li + 1 < m_lines.size())
        *affinity = CaretAffinity::Upstream;
    return offset;
}

std::vector<Rect> TextLayout::selectionRects(Range range) const
{
    std::vector<Rect> rects;
    const int n = length();
    int a = std::clamp(std::min(range.start, range.end), 0, n);
    int b = std::clamp(std::max(range.start, range.end), 0, n);
    if (a >= b)
        return rects;
    for (std::size_t li = 0; li < m_lines.size(); ++li) {
        const TextLine &line = m_lines[li];
        const bool last = li + 1 == m_lines.size();
        if (b <= line.start)
            break;
        if (!last && a >= line.next)
            continue;
        const float origin = Builder::x(*this, line.start);
        const float x0 = Builder::x(*this, std::clamp(a, line.start, line.end)) - origin;
        float x1;
        if (b > line.end)
            x1 = Builder::x(*this, line.end) - origin + (line.next > line.end ? m_breakWidths[li] : 0.0f);
        else
            x1 = Builder::x(*this, b) - origin;
        rects.push_back(Rect::fromLTRB(x0, line.top, std::max(x0, x1), line.top + line.height));
    }
    return rects;
}

std::vector<LineRect> TextLayout::glyphRects(Range range) const
{
    std::vector<LineRect> rects;
    const int n = length();
    const int a = std::clamp(std::min(range.start, range.end), 0, n);
    const int b = std::clamp(std::max(range.start, range.end), 0, n);
    if (a >= b)
        return rects;
    for (std::size_t li = 0; li < m_lines.size(); ++li) {
        const TextLine &line = m_lines[li];
        if (b <= line.start)
            break;
        const int from = std::max(a, line.start);
        const int to = std::min(b, line.end);
        if (from >= to)
            continue;
        const float origin = Builder::x(*this, line.start);
        const float x0 = Builder::x(*this, from) - origin;
        // Hanging spaces at a wrap are not shown: the piece ends at the line's visible end.
        const float x1 = std::min(Builder::x(*this, to) - origin, line.width);
        if (x1 - x0 < 0.5f)
            continue;
        float ascent = 0;
        float descent = 0;
        auto it = std::upper_bound(m_runs.begin(), m_runs.end(), from,
                                   [](int o, const LaidOutRun &run) { return o < run.end; });
        for (; it != m_runs.end() && it->start < to; ++it) {
            ascent = std::max(ascent, it->ascent);
            descent = std::max(descent, it->descent);
        }
        rects.push_back(LineRect{int(li), Rect::fromLTRB(x0, line.baseline - ascent, x1, line.baseline + descent)});
    }
    return rects;
}

int TextLayout::nextCaretStop(int offset) const
{
    const int n = length();
    if (offset >= n)
        return n;
    offset = std::max(0, offset);
    // The first box that ends after the offset: the offset is in it (its end is the stop), or the
    // next grapheme boundary stops at its start.
    auto box = std::upper_bound(m_inlineBoxes.begin(), m_inlineBoxes.end(), offset,
                                [](int o, const PlacedInlineBox &b) { return o < b.end; });
    if (box != m_inlineBoxes.end() && box->start <= offset)
        return box->end;
    const int next = nextGraphemeBoundary(m_text, offset);
    return box != m_inlineBoxes.end() ? std::min(next, box->start) : next;
}

int TextLayout::previousCaretStop(int offset) const
{
    offset = std::min(offset, length());
    if (offset <= 0)
        return 0;
    // The last box that starts before the offset: the offset is in it or at its end (its start is
    // the stop), or the previous grapheme boundary stops at its end.
    auto box = std::lower_bound(m_inlineBoxes.begin(), m_inlineBoxes.end(), offset,
                                [](const PlacedInlineBox &b, int o) { return b.start < o; });
    if (box != m_inlineBoxes.begin()) {
        --box;
        if (box->end >= offset)
            return box->start;
        return std::max(previousGraphemeBoundary(m_text, offset), box->end);
    }
    return previousGraphemeBoundary(m_text, offset);
}

int TextLayout::offsetAbove(int offset, float x) const
{
    const int li = lineIndex(offset);
    if (li <= 0)
        return -1;
    return Builder::nearest(*this, std::size_t(li - 1), x, false);
}

int TextLayout::offsetBelow(int offset, float x) const
{
    const int li = lineIndex(offset);
    if (li < 0 || std::size_t(li) + 1 >= m_lines.size())
        return -1;
    return Builder::nearest(*this, std::size_t(li + 1), x, false);
}

TextLayout TextLayout::Builder::build(const FontCollection &fonts, std::u16string_view text,
                                      std::span<const FontRun> runs, const ParagraphStyle &style)
{
    if (text.size() > std::size_t(std::numeric_limits<int>::max() / 2))
        text = text.substr(0, std::size_t(std::numeric_limits<int>::max() / 2));
    TextLayout layout;
    layout.m_text.assign(text);
    const std::vector<ResolvedRun> resolved = resolveRuns(fonts, int(text.size()), runs);
    std::vector<int> graphemes;
    detail::graphemeBoundaries(text, graphemes);
    std::vector<BreakKind> breaks;
    detail::lineBreaks(text, graphemes, breaks);
    applyInlineBoxes(resolved, graphemes, breaks);
    std::vector<PositionedGlyph> glyphs;
    shape(fonts, text, resolved, graphemes, glyphs, layout.m_offsetX);
    wrap(layout, fonts, resolved, std::move(glyphs), graphemes, breaks, style);
    return layout;
}

TextLayout TextLayout::Builder::rewrap(const FontCollection &fonts, const TextLayout &layout,
                                       std::span<const FontRun> runs, const ParagraphStyle &style)
{
    const std::u16string &text = layout.m_text;
    if (layout.m_offsetX.size() != text.size() + 1 || layout.m_lines.empty())
        return build(fonts, text, runs, style);
    TextLayout out;
    out.m_text = text;
    out.m_offsetX = layout.m_offsetX;
    // Back to the unwrapped positions the shaper gave.
    std::vector<PositionedGlyph> glyphs = layout.m_glyphs;
    for (const TextLine &line : layout.m_lines) {
        const float originX = layout.m_offsetX[std::size_t(line.start)];
        for (std::uint32_t i = line.firstGlyph; i < line.firstGlyph + line.glyphCount && i < glyphs.size(); ++i) {
            glyphs[i].position.x += originX;
            glyphs[i].position.y -= line.baseline;
        }
    }
    std::vector<int> graphemes;
    detail::graphemeBoundaries(text, graphemes);
    std::vector<BreakKind> breaks;
    detail::lineBreaks(text, graphemes, breaks);
    const std::vector<ResolvedRun> resolved = resolveRuns(fonts, int(text.size()), runs);
    applyInlineBoxes(resolved, graphemes, breaks);
    wrap(out, fonts, resolved, std::move(glyphs), graphemes, breaks, style);
    return out;
}

TextLayout layoutParagraph(const FontCollection &fonts, std::u16string_view text, std::span<const FontRun> runs,
                           const ParagraphStyle &style)
{
    return TextLayout::Builder::build(fonts, text, runs, style);
}

TextLayout rewrapParagraph(const FontCollection &fonts, const TextLayout &layout, std::span<const FontRun> runs,
                           const ParagraphStyle &style)
{
    return TextLayout::Builder::rewrap(fonts, layout, runs, style);
}

} // namespace brulion::atrament
