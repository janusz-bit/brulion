#pragma once

// Fonts (docs/ATRAMENT.md "Text", D7): a collection of faces loaded from font files' bytes —
// Brulion's bundled fonts (Inter, Literata, JetBrains Mono, Brulion Emoji, Brulion Math) and,
// later, the system's — read with HarfBuzz. Faces are matched by family, weight and style as CSS
// does, and a character a face lacks is found in the fallback families.

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct hb_font_t;
struct hb_face_t;

namespace brulion::atrament {

using FaceId = std::uint32_t;
constexpr FaceId kNoFace = std::numeric_limits<FaceId>::max();

enum class FontStyle : std::uint8_t { Normal, Italic };

struct FontWeight {
    std::uint16_t value = 400;
    static constexpr std::uint16_t Thin = 100, Light = 300, Normal = 400, Medium = 500, SemiBold = 600, Bold = 700,
                                   Black = 900;
    constexpr bool operator==(const FontWeight &) const = default;
};

struct FaceInfo {
    std::u16string family; // the typographic family (name ID 16) if present, else name ID 1
    std::u16string postscriptName;
    FontWeight weight; // from OS/2 usWeightClass
    FontStyle style = FontStyle::Normal; // from OS/2 fsSelection / the subfamily name
    bool monospace = false; // post.isFixedPitch
};

// In ems (multiply by the font size in pixels). Ascent and descent are positive distances above
// and below the baseline (OS/2 typographic metrics when USE_TYPO_METRICS is set, else hhea).
struct FontMetrics {
    float ascent = 0;
    float descent = 0;
    float lineGap = 0;
    float underlinePosition = 0; // below the baseline, positive down
    float underlineThickness = 0;
    float strikeoutPosition = 0; // above the baseline, positive up
    float strikeoutThickness = 0;
    float xHeight = 0;
    float capHeight = 0;
};

class FontCollection {
public:
    FontCollection();
    ~FontCollection();
    FontCollection(const FontCollection &) = delete;
    FontCollection &operator=(const FontCollection &) = delete;

    // Every face of a font file (a .ttc gives several). Empty when the bytes are not a font.
    std::vector<FaceId> addFontData(std::shared_ptr<const std::vector<std::uint8_t>> bytes);
    std::vector<FaceId> addFontFile(const std::string &utf8Path);

    std::size_t faceCount() const;
    const FaceInfo &faceInfo(FaceId face) const;

    // CSS font matching (https://www.w3.org/TR/css-fonts-4/#font-style-matching) within a family
    // (compared case-insensitively): the style first, then the nearest weight. kNoFace when the
    // family is unknown.
    FaceId match(std::u16string_view family, FontWeight weight = {}, FontStyle style = FontStyle::Normal) const;

    // Families tried, in order, for a character the requested face lacks.
    void setFallbackFamilies(std::vector<std::u16string> families);
    // A face with a glyph for `codepoint`: `preferred` if it has one, else the best match of
    // `preferred`'s weight and style in the fallback families, else any face that has it; kNoFace
    // when none does. Results are cached.
    FaceId faceFor(char32_t codepoint, FaceId preferred) const;
    bool hasGlyph(FaceId face, char32_t codepoint) const;

    FontMetrics metrics(FaceId face) const;
    int unitsPerEm(FaceId face) const;

    // HarfBuzz's objects for the shaper and the glyph rasterizer; the font's scale is its units
    // per em (positions are scaled by size / upem by the callers), no hinting.
    hb_font_t *hbFont(FaceId face) const;
    hb_face_t *hbFace(FaceId face) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace brulion::atrament
