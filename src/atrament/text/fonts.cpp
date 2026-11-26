#include "text/fonts.h"

#include <hb-ot.h>
#include <hb.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace brulion::atrament {

namespace {

std::uint16_t readU16(const char *p)
{
    return std::uint16_t((unsigned(std::uint8_t(p[0])) << 8) | unsigned(std::uint8_t(p[1])));
}

std::uint32_t readU32(const char *p)
{
    return (std::uint32_t(readU16(p)) << 16) | readU16(p + 2);
}

char16_t asciiLower(char16_t c)
{
    return c >= u'A' && c <= u'Z' ? char16_t(c - u'A' + u'a') : c;
}

// Family names compare case-insensitively in ASCII (enough for the font names Brulion meets).
std::u16string familyKey(std::u16string_view family)
{
    std::u16string key(family);
    std::transform(key.begin(), key.end(), key.begin(), asciiLower);
    return key;
}

bool containsWord(std::u16string_view haystack, std::u16string_view needle)
{
    const std::u16string h = familyKey(haystack);
    const std::u16string n = familyKey(needle);
    return h.find(n) != std::u16string::npos;
}

std::u16string readName(hb_face_t *face, hb_ot_name_id_t id)
{
    // HB_LANGUAGE_INVALID: the English name, else the first one.
    const unsigned length = hb_ot_name_get_utf16(face, id, HB_LANGUAGE_INVALID, nullptr, nullptr);
    if (length == 0)
        return {};
    std::vector<std::uint16_t> buffer(length + 1);
    unsigned size = length + 1;
    hb_ot_name_get_utf16(face, id, HB_LANGUAGE_INVALID, &size, buffer.data());
    return std::u16string(buffer.begin(), buffer.begin() + std::ptrdiff_t(std::min(size, length)));
}

// A table's bytes, or empty (the blob is destroyed by the caller's guard).
struct Table {
    hb_blob_t *blob = nullptr;
    const char *data = nullptr;
    unsigned length = 0;
    Table(hb_face_t *face, hb_tag_t tag) : blob(hb_face_reference_table(face, tag))
    {
        data = hb_blob_get_data(blob, &length);
    }
    ~Table() { hb_blob_destroy(blob); }
    Table(const Table &) = delete;
    Table &operator=(const Table &) = delete;
};

std::uint16_t weightFromSubfamily(std::u16string_view subfamily)
{
    struct Keyword {
        std::u16string_view word;
        std::uint16_t weight;
    };
    // Longest first: "SemiBold" before "Bold", "ExtraLight" before "Light".
    static constexpr Keyword kKeywords[] = {
            {u"thin", 100},     {u"extralight", 200}, {u"ultralight", 200}, {u"semibold", 600},
            {u"demibold", 600}, {u"extrabold", 800},  {u"ultrabold", 800},  {u"light", 300},
            {u"medium", 500},   {u"bold", 700},       {u"black", 900},      {u"heavy", 900},
    };
    std::u16string key = familyKey(subfamily);
    key.erase(std::remove(key.begin(), key.end(), u' '), key.end());
    key.erase(std::remove(key.begin(), key.end(), u'-'), key.end());
    for (const Keyword &k : kKeywords) {
        if (key.find(k.word) != std::u16string::npos)
            return k.weight;
    }
    return FontWeight::Normal;
}

float metric(hb_font_t *font, hb_ot_metrics_tag_t tag)
{
    hb_position_t value = 0;
    hb_ot_metrics_get_position_with_fallback(font, tag, &value);
    return float(value);
}

} // namespace

struct FontCollection::Impl {
    struct Face {
        std::shared_ptr<const std::vector<std::uint8_t>> bytes;
        hb_face_t *face = nullptr;
        hb_font_t *font = nullptr;
        FaceInfo info;
        std::u16string key; // the family, lower-cased
        FontMetrics metrics;
        int upem = 1000;

        Face() = default;
        Face(const Face &) = delete;
        Face &operator=(const Face &) = delete;
        ~Face()
        {
            hb_font_destroy(font);
            hb_face_destroy(face);
        }
    };

    // unique_ptr: faceInfo() hands out references that outlive later additions.
    std::vector<std::unique_ptr<Face>> faces;
    std::vector<std::u16string> fallbackKeys;
    mutable std::unordered_map<std::uint64_t, FaceId> fallbackCache;

    const Face *get(FaceId id) const { return id < faces.size() ? faces[id].get() : nullptr; }

    // CSS font matching among `candidates` (which all have the same family).
    static FaceId bestMatch(const std::vector<std::pair<FaceId, const Face *>> &candidates, FontWeight weight,
                            FontStyle style);
    std::vector<std::pair<FaceId, const Face *>> family(std::u16string_view key) const;
};

FaceId FontCollection::Impl::bestMatch(const std::vector<std::pair<FaceId, const Face *>> &candidates,
                                       FontWeight weight, FontStyle style)
{
    if (candidates.empty())
        return kNoFace;
    // The style first: faces of the requested style if there are any, else the others (CSS: italic
    // falls back to oblique, then normal; Brulion's faces are either normal or italic).
    const bool anyOfStyle = std::any_of(candidates.begin(), candidates.end(),
                                        [style](const auto &c) { return c.second->info.style == style; });
    const int desired = weight.value;
    FaceId best = kNoFace;
    int bestTier = 0;
    int bestDistance = 0;
    for (const auto &[id, face] : candidates) {
        if (anyOfStyle && face->info.style != style)
            continue;
        const int w = face->info.weight.value;
        // The nearest weight as CSS orders them: for 400–500 the heavier ones up to 500 first,
        // then the lighter ones, then the heavier ones above 500; below 400 lighter first; above
        // 500 heavier first.
        int tier;
        if (desired >= 400 && desired <= 500)
            tier = (w >= desired && w <= 500) ? 0 : w < desired ? 1 : 2;
        else if (desired < 400)
            tier = w <= desired ? 0 : 1;
        else
            tier = w >= desired ? 0 : 1;
        const int distance = std::abs(w - desired);
        if (best == kNoFace || tier < bestTier || (tier == bestTier && distance < bestDistance)) {
            best = id;
            bestTier = tier;
            bestDistance = distance;
        }
    }
    return best;
}

std::vector<std::pair<FaceId, const FontCollection::Impl::Face *>>
FontCollection::Impl::family(std::u16string_view key) const
{
    std::vector<std::pair<FaceId, const Face *>> out;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        if (faces[i]->key == key)
            out.emplace_back(FaceId(i), faces[i].get());
    }
    return out;
}

FontCollection::FontCollection() : d(std::make_unique<Impl>()) { }

FontCollection::~FontCollection() = default;

std::vector<FaceId> FontCollection::addFontData(std::shared_ptr<const std::vector<std::uint8_t>> bytes)
{
    std::vector<FaceId> added;
    if (!bytes || bytes->empty() || bytes->size() > std::size_t(std::numeric_limits<unsigned>::max()))
        return added;
    // The blob keeps a reference to the bytes for as long as HarfBuzz uses them.
    auto *keep = new std::shared_ptr<const std::vector<std::uint8_t>>(bytes);
    hb_blob_t *blob = hb_blob_create(
            reinterpret_cast<const char *>(bytes->data()), unsigned(bytes->size()), HB_MEMORY_MODE_READONLY, keep,
            [](void *p) { delete static_cast<std::shared_ptr<const std::vector<std::uint8_t>> *>(p); });
    const unsigned count = hb_face_count(blob);
    for (unsigned index = 0; index < count; ++index) {
        hb_face_t *face = hb_face_create(blob, index);
        // Not a usable font without glyphs or a character map (HarfBuzz gives an empty face for
        // bytes it cannot read).
        const bool usable = hb_face_get_glyph_count(face) > 0 && Table(face, HB_TAG('c', 'm', 'a', 'p')).length >= 4;
        if (!usable) {
            hb_face_destroy(face);
            continue;
        }
        hb_face_make_immutable(face);
        auto entry = std::make_unique<Impl::Face>();
        entry->bytes = bytes;
        entry->face = face;
        entry->font = hb_font_create(face);
        entry->upem = int(std::max(1u, hb_face_get_upem(face)));
        // hb_font_create's scale is the units per em already; set it explicitly as the contract.
        hb_font_set_scale(entry->font, entry->upem, entry->upem);
        hb_font_make_immutable(entry->font);

        FaceInfo &info = entry->info;
        info.family = readName(face, HB_OT_NAME_ID_TYPOGRAPHIC_FAMILY);
        if (info.family.empty())
            info.family = readName(face, HB_OT_NAME_ID_FONT_FAMILY);
        info.postscriptName = readName(face, HB_OT_NAME_ID_POSTSCRIPT_NAME);
        std::u16string subfamily = readName(face, HB_OT_NAME_ID_TYPOGRAPHIC_SUBFAMILY);
        if (subfamily.empty())
            subfamily = readName(face, HB_OT_NAME_ID_FONT_SUBFAMILY);

        bool italic = containsWord(subfamily, u"italic") || containsWord(subfamily, u"oblique");
        std::uint16_t weight = weightFromSubfamily(subfamily);
        const Table os2(face, HB_TAG('O', 'S', '/', '2'));
        if (os2.length >= 64) {
            std::uint16_t w = readU16(os2.data + 4); // usWeightClass
            if (w >= 1 && w <= 9)
                w = std::uint16_t(w * 100); // the old 1–9 scale
            if (w >= 1 && w <= 1000)
                weight = w;
            const std::uint16_t fsSelection = readU16(os2.data + 62);
            italic = (fsSelection & 0x0001) || (fsSelection & 0x0200); // ITALIC, OBLIQUE
        }
        info.weight = FontWeight{weight};
        info.style = italic ? FontStyle::Italic : FontStyle::Normal;
        const Table post(face, HB_TAG('p', 'o', 's', 't'));
        if (post.length >= 16)
            info.monospace = readU32(post.data + 12) != 0; // isFixedPitch
        entry->key = familyKey(info.family);

        // HarfBuzz's metrics follow the rule of the contract: OS/2's typographic ascender,
        // descender and line gap when USE_TYPO_METRICS is set, else hhea's; underline from post,
        // strikeout and x/cap heights from OS/2 (with fallbacks measured from the glyphs).
        const float em = float(entry->upem);
        hb_font_t *font = entry->font;
        FontMetrics &m = entry->metrics;
        m.ascent = metric(font, HB_OT_METRICS_TAG_HORIZONTAL_ASCENDER) / em;
        m.descent = -metric(font, HB_OT_METRICS_TAG_HORIZONTAL_DESCENDER) / em;
        m.lineGap = std::max(0.0f, metric(font, HB_OT_METRICS_TAG_HORIZONTAL_LINE_GAP) / em);
        m.underlinePosition = -metric(font, HB_OT_METRICS_TAG_UNDERLINE_OFFSET) / em;
        m.underlineThickness = metric(font, HB_OT_METRICS_TAG_UNDERLINE_SIZE) / em;
        m.strikeoutPosition = metric(font, HB_OT_METRICS_TAG_STRIKEOUT_OFFSET) / em;
        m.strikeoutThickness = metric(font, HB_OT_METRICS_TAG_STRIKEOUT_SIZE) / em;
        m.xHeight = metric(font, HB_OT_METRICS_TAG_X_HEIGHT) / em;
        m.capHeight = metric(font, HB_OT_METRICS_TAG_CAP_HEIGHT) / em;
        if (m.ascent <= 0 && m.descent <= 0) {
            m.ascent = 0.8f; // a font without vertical metrics: something sensible
            m.descent = 0.2f;
        }
        if (m.underlineThickness <= 0)
            m.underlineThickness = 0.05f;
        // Some fonts put the underline at or above the baseline (Brulion Emoji's post table says
        // +1244 units): half the descent instead; a strikeout without a position goes through the
        // middle of the x-height.
        if (m.underlinePosition <= 0)
            m.underlinePosition = std::max(m.descent / 2, m.underlineThickness);
        if (m.strikeoutPosition <= 0)
            m.strikeoutPosition = m.xHeight > 0 ? m.xHeight / 2 : m.ascent / 3;
        if (m.strikeoutThickness <= 0)
            m.strikeoutThickness = m.underlineThickness;

        added.push_back(FaceId(d->faces.size()));
        d->faces.push_back(std::move(entry));
    }
    hb_blob_destroy(blob);
    if (!added.empty())
        d->fallbackCache.clear();
    return added;
}

std::vector<FaceId> FontCollection::addFontFile(const std::string &utf8Path)
{
    // A UTF-8 path on every platform; istream::read reports errors as flags, never by throwing
    // (istreambuf_iterator would let libstdc++'s read errors, e.g. on a directory, through).
    const std::filesystem::path path(
            std::u8string_view(reinterpret_cast<const char8_t *>(utf8Path.data()), utf8Path.size()));
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
        return {};
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > std::uintmax_t(std::numeric_limits<unsigned>::max()))
        return {};
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(std::size_t(size));
    file.read(reinterpret_cast<char *>(bytes->data()), std::streamsize(size));
    if (std::uintmax_t(file.gcount()) != size)
        return {};
    return addFontData(std::move(bytes));
}

std::size_t FontCollection::faceCount() const
{
    return d->faces.size();
}

const FaceInfo &FontCollection::faceInfo(FaceId face) const
{
    static const FaceInfo kNone;
    const Impl::Face *f = d->get(face);
    return f ? f->info : kNone;
}

FaceId FontCollection::match(std::u16string_view family, FontWeight weight, FontStyle style) const
{
    return Impl::bestMatch(d->family(familyKey(family)), weight, style);
}

void FontCollection::setFallbackFamilies(std::vector<std::u16string> families)
{
    d->fallbackKeys.clear();
    for (const std::u16string &family : families)
        d->fallbackKeys.push_back(familyKey(family));
    d->fallbackCache.clear();
}

FaceId FontCollection::faceFor(char32_t codepoint, FaceId preferred) const
{
    const Impl::Face *pref = d->get(preferred);
    if (pref && hasGlyph(preferred, codepoint))
        return preferred;
    if (!pref)
        preferred = kNoFace;
    const std::uint64_t key = (std::uint64_t(codepoint) << 32) | preferred;
    if (auto it = d->fallbackCache.find(key); it != d->fallbackCache.end())
        return it->second;
    const FontWeight weight = pref ? pref->info.weight : FontWeight{};
    const FontStyle style = pref ? pref->info.style : FontStyle::Normal;
    FaceId found = kNoFace;
    for (const std::u16string &familyName : d->fallbackKeys) {
        std::vector<std::pair<FaceId, const Impl::Face *>> candidates = d->family(familyName);
        std::erase_if(candidates, [&](const auto &c) { return !hasGlyph(c.first, codepoint); });
        found = Impl::bestMatch(candidates, weight, style);
        if (found != kNoFace)
            break;
    }
    if (found == kNoFace) {
        for (std::size_t i = 0; i < d->faces.size(); ++i) {
            if (hasGlyph(FaceId(i), codepoint)) {
                found = FaceId(i);
                break;
            }
        }
    }
    d->fallbackCache.emplace(key, found);
    return found;
}

bool FontCollection::hasGlyph(FaceId face, char32_t codepoint) const
{
    const Impl::Face *f = d->get(face);
    if (!f)
        return false;
    hb_codepoint_t glyph = 0;
    return hb_font_get_nominal_glyph(f->font, codepoint, &glyph) && glyph != 0;
}

FontMetrics FontCollection::metrics(FaceId face) const
{
    const Impl::Face *f = d->get(face);
    return f ? f->metrics : FontMetrics{};
}

int FontCollection::unitsPerEm(FaceId face) const
{
    const Impl::Face *f = d->get(face);
    return f ? f->upem : 0;
}

hb_font_t *FontCollection::hbFont(FaceId face) const
{
    const Impl::Face *f = d->get(face);
    return f ? f->font : nullptr;
}

hb_face_t *FontCollection::hbFace(FaceId face) const
{
    const Impl::Face *f = d->get(face);
    return f ? f->face : nullptr;
}

} // namespace brulion::atrament
