// ThemePalette — the colours of Theme.qml that the appearance settings change (Ustawienia →
// Wygląd, docs/UI.md "Look & feel"): the surfaces (light "paper" | "white", dark "slate" |
// "black" for OLED screens), the accent colour (presets "red" — the logo's margin red, the
// default —, "blue", "green", "teal", "violet", "graphite", or "custom" with any "#rrggbb"),
// and the text colours, kept here so the tests check every one of them on every surface.
// QML singleton `ThemePalette` (registered by qmlforeign.h); Theme.qml reads
// `ThemePalette.colors(dark, lightSurface, darkSurface, accent, customAccent)`.
//
// Contrast (WCAG 2): a custom colour stays readable by itself — deriveAccent() moves its HSL
// lightness (darker on light surfaces, lighter on dark ones) until the accent as a mark reaches
// 3:1 on the page, cards and wells of the mode (bg, bgElevated, bgSunken), the accent as text
// 4.5:1 on each of them and on the accent's
// own tint (accentBg), and the solid fill 4.5:1 under white text. The presets go through the same
// derivation from a base colour; "red" keeps today's hand-picked values exactly (the default look
// never changes). Unknown names and malformed colours give the defaults: settings files come
// from anywhere.
//
// GUI-free (QtCore only; colours are "#rrggbb" strings), unit-tested in tests/app
// (tst_ui_themepalette). Also the font family filter of the font pickers (DeviceInfo).
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>

namespace brulion::ui {

/// An opaque sRGB colour.
struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
    friend bool operator==(const Rgb &, const Rgb &) = default;
};

/// "#rgb" or "#rrggbb" (any case, surrounding spaces allowed); nullopt for anything else.
std::optional<Rgb> parseColor(QStringView text);
/// "#rrggbb", lower case.
QString toHex(const Rgb &c);
/// WCAG 2 relative luminance, 0 … 1.
double relativeLuminance(const Rgb &c);
/// WCAG 2 contrast ratio, 1 … 21.
double contrast(const Rgb &a, const Rgb &b);
/// `fg` with opacity `alpha` composited over `bg` (rounded to 8 bits, as drawn).
Rgb over(const Rgb &fg, double alpha, const Rgb &bg);

/// The surface colours of one background variant.
struct Surface {
    Rgb bg, bgElevated, bgElevated2, bgSunken;
    Rgb border, borderSubtle, borderStrong, borderControl;
};
/// "paper" | "white" (light) or "slate" | "black" (dark); unknown → "paper" / "slate".
Surface surface(QStringView name, bool dark);
/// Known names of the light (`dark` false) or dark surfaces, the default first.
QStringList surfaceNames(bool dark);

/// The accent family (accentBg is the accent with accentBgAlpha()).
struct Accent {
    Rgb accent;      ///< marks: today's date, the tab underline, drop targets (≥ 3:1 on bg)
    Rgb accentHover;
    Rgb accentText;  ///< text on any surface and on accentBg (≥ 4.5:1)
    Rgb accentSolid; ///< fill under white text (≥ 4.5:1)
};
/// Opacity of accentBg over the surface.
constexpr double accentBgAlpha(bool dark)
{
    return dark ? 0.18 : 0.12;
}
/// The presets, the default ("red") first; "custom" is not among them.
QStringList accentNames();
/// The base colour of a preset (what deriveAccent() starts from); "red" → #e2574c.
Rgb accentBase(QStringView name);
/// A readable accent family from any colour on surface `s` of the mode.
Accent deriveAccent(const Rgb &base, bool dark, const Surface &s);
/// The accent family of preset `name` ("custom": `custom`, "#rrggbb"; malformed or unknown → "red").
Accent accent(QStringView name, QStringView custom, bool dark, const Surface &s);

/// The text colours (they do not depend on the surface; checked on each of them).
struct TextColors {
    Rgb fg, fgMuted, fgFaint, link, linkMissing, tagFg;
    Rgb dangerText, warningText, successText;
    Rgb chipDueFg, chipScheduledFg, chipReminderFg, chipRecurrenceFg;
};
TextColors textColors(bool dark);

/// Every token above by its Theme.qml name, as "#rrggbb".
QVariantMap paletteMap(bool dark, QStringView lightSurface, QStringView darkSurface, QStringView accentName,
                       QStringView customAccent);

/// The families a font picker offers: no private ones ("." first), none for emoji, symbols or
/// mathematics (no text in them — the app's own fallback fonts are among them), no duplicates,
/// sorted for Polish readers ("Łato" after "Lato", case-insensitive).
QStringList filterFontFamilies(const QStringList &families);

class ThemePalette : public QObject {
    Q_OBJECT

public:
    explicit ThemePalette(QObject *parent = nullptr);

    /// paletteMap(): { bg, bgElevated, …, accent, accentText, …, fg, fgMuted, … } as colour strings.
    Q_INVOKABLE QVariantMap colors(bool dark, const QString &lightSurface, const QString &darkSurface,
                                   const QString &accent, const QString &customAccent) const;
    /// accentNames().
    Q_INVOKABLE QStringList accentNames() const;
    /// The accent (marks) of preset `name` on `surfaceName` of the mode — a settings swatch.
    Q_INVOKABLE QString accentSwatch(const QString &name, const QString &customAccent, bool dark,
                                     const QString &surfaceName) const;
    /// "#rrggbb" of a typed colour ("#ABC" → "#aabbcc"), "" when it is not one.
    Q_INVOKABLE QString normalizeColor(const QString &text) const;
    /// Contrast ratio of two colours ("#rrggbb"); 0 when one is malformed.
    Q_INVOKABLE double contrastRatio(const QString &a, const QString &b) const;
};

} // namespace brulion::ui
