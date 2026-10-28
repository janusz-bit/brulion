// The colours a drawing is shown with in the light and the dark theme (docs/SYNTAX.md §2
// "Drawings", docs/UI.md "Drawing editor"). A drawing stores its colours as they were drawn, in
// light-theme terms (as Excalidraw does); the dark theme maps them when it shows them, exactly as
// Excalidraw's dark mode does (packages/common/src/colors.ts `applyDarkModeFilter`, MIT: the CSS
// filter `invert(93%) hue-rotate(180deg)` on each colour) — the default ink #1f2430 shows as
// #caced8, white paper as #121212. Highlighter strokes keep their colour in the dark (mapped,
// a pastel would sink into the dark paper: #ffd43b → #5f3a00, 1.9:1) and show a little stronger.
//
//   const Ink ink = displayInk(stroke, rendersDark(drawing.theme, appIsDark));
//   paint(path, ink.color, ink.opacity);
//
// Pure functions; no Qt GUI types (the app turns an Ink into a QColor).
#pragma once

#include "drawing/drawing.h"

#include <QByteArray>
#include <QString>
#include <QStringView>

#include <array>
#include <optional>

namespace brulion::drawing {

/// A colour: 0–255 a channel, `a` the alpha (255 opaque).
struct Rgba {
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 255;

    friend bool operator==(const Rgba &, const Rgba &) = default;
};

/// "#rgb", "#rrggbb" or "#rrggbbaa" (CSS order: the alpha last; case and surrounding spaces do not
/// matter) — the colours safeColor() lets into an SVG; anything else: nullopt.
std::optional<Rgba> parseColor(QStringView hex);
/// "#rrggbb", or "#rrggbbaa" when the colour is not opaque (lower case).
QString colorName(Rgba c);

/// How much the dark theme inverts a colour (Excalidraw's invert(93 %)).
inline constexpr double DarkInvert = 0.93;
/// A highlighter stroke's opacity in the dark theme: × 1.25, at most 100 (40 % → 50 %).
inline constexpr double HighlighterDarkOpacityFactor = 1.25;
/// What ink with an unreadable colour is drawn with (Excalidraw's default stroke colour).
inline constexpr QStringView FallbackInk = u"#1e1e1e";

/// Excalidraw's applyDarkModeFilter: invert(93 %), then hue-rotate(180°) with the CSS filter
/// matrix, every channel rounded as JavaScript's Math.round and clamped; the alpha kept.
Rgba darkRgba(Rgba c);
/// darkRgba()'s two steps, for code that maps many pixels with tables (the app's darkImage()): a
/// channel inverted (93 %, rounded) and the hue-rotate(180°) matrix (row-major); darkRgba()
/// gives round(clamp(Σ (inverted / 255) × matrix entry, 0, 1) × 255) per channel.
int darkInvertChannel(int v);
std::array<double, 9> darkHueMatrix();
/// `hex` as the dark theme shows it ("#rrggbb", "#rrggbbaa" with an alpha); a malformed colour
/// gives `fallback` as it is.
QString darkColor(QStringView hex, const QString &fallback = QString());

/// The drawing is shown dark: its theme is Dark, or Auto while the app is dark.
bool rendersDark(PaperTheme theme, bool appDark);

/// What a stroke is drawn with: a colour ("#rrggbb" or "#rrggbbaa") and an opacity (0–100).
struct Ink {
    QString color;
    int opacity = 100;

    friend bool operator==(const Ink &, const Ink &) = default;
};
/// The stroke's ink as shown: light — its colour (FallbackInk when unreadable) and opacity; dark —
/// a pen's colour darkColor()'d, a highlighter's colour as stored with its opacity ×
/// HighlighterDarkOpacityFactor (it stays a highlighter whatever its colour).
Ink displayInk(const Stroke &stroke, bool dark);
/// The highlighter's opacity in the dark theme (0–100).
int highlighterDarkOpacity(int opacity);
/// The paper's colour as shown, always opaque "#rrggbb" ("transparent" or malformed: white; a
/// see-through colour such as Excalidraw's "#ffffff80": as it shows over white; dark: darkColor()
/// of that — white gives #121212, Excalidraw's dark canvas).
QString displayPaper(QStringView paper, bool dark);
/// The colour of the lines and the grid as shown (RuleColor; dark #323a45).
QString displayRule(bool dark);

/// An SVG picture with its colours as the dark theme shows them — what Excalidraw's dark mode does
/// to a scene, colour by colour: every colour parseColor() reads in the attributes fill, stroke,
/// stop-color, flood-color, lighting-color and color, in `style` attributes and in style sheets
/// (`fill: #…` and the like) darkColor()'d; everything else as it is — pictures (<image>: a
/// pasted photo is no negative), the glyphs of colour emoji, "none", url(#…), named colours, the
/// comments (the embedded scene). For pictures Brulion does not draw itself (an Excalidraw drawing
/// with shapes or text). nullopt when the bytes are no well-formed XML.
std::optional<QByteArray> darkSvg(const QByteArray &svg);

/// WCAG 2 relative luminance (0–1) of an opaque colour, and the contrast ratio of two (1–21).
double luminance(Rgba c);
double contrast(Rgba a, Rgba b);
/// `fg` (its alpha too) at `opacity` (0–100) over the opaque `bg`: the opaque colour seen.
Rgba over(Rgba fg, int opacity, Rgba bg);

} // namespace brulion::drawing
