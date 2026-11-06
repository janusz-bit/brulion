// ThemePalette — the colours of Theme.qml that the appearance settings change (Settings →
// Appearance, docs/UI.md "Look & feel"): the surfaces (light "paper" | "white", dark "black" for
// OLED screens — the default, the "Nocny" look — | "slate"), the accent colour (presets "coral" —
// the default, #ff7a66 in dark and #c4422f in light —, "red" — the logo's margin red —, "blue",
// "green", "teal", "violet", "graphite", or "custom" with any "#rrggbb"),
// and the text colours, kept here so the tests check every one of them on every surface.
// QML singleton `ThemePalette` (registered by qmlforeign.h); Theme.qml reads the colours from a
// `ThemeColors` object (below: the same map as `ThemePalette.colors(dark, lightSurface, darkSurface,
// accent, customAccent)`, as typed properties for compiled QML).
//
// Contrast (WCAG 2): a custom colour stays readable by itself — deriveAccent() moves its HSL
// lightness (darker on light surfaces, lighter on dark ones) until the accent as a mark reaches
// 3:1 on the page, cards and wells of the mode (bg, bgElevated, bgSunken), the accent as text
// 4.5:1 on each of them and on the accent's own tint (accentBg), and the solid fill 4.5:1 under
// its text (onAccent: white, or dark ink on the bright dark-mode coral). The presets go through
// the same derivation from a base colour; "red" keeps its hand-picked values (the default look
// before "coral"). Unknown names and malformed colours give the defaults: settings files come
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
/// "paper" | "white" (light) or "black" | "slate" (dark); unknown → "paper" / "black".
Surface surface(QStringView name, bool dark);
/// Known names of the light (`dark` false) or dark surfaces, the default first.
QStringList surfaceNames(bool dark);

/// The accent family (accentBg is the accent with accentBgAlpha()).
struct Accent {
    Rgb accent;      ///< marks: today's date, the tab underline, drop targets (≥ 3:1 on bg)
    Rgb accentHover;
    Rgb accentText;  ///< text on any surface and on accentBg (≥ 4.5:1)
    Rgb accentSolid; ///< fill under onAccent text (≥ 4.5:1)
    Rgb onAccent = {255, 255, 255}; ///< text on accentSolid: white, dark ink on dark-mode coral
};
/// Opacity of accentBg over the surface.
constexpr double accentBgAlpha(bool dark)
{
    return dark ? 0.18 : 0.12;
}
/// The presets, the default ("coral") first; "custom" is not among them.
QStringList accentNames();
/// The base colour of a preset in a mode (what deriveAccent() starts from); "red" → #e2574c,
/// "coral" → #ff7a66 in dark, #c4422f in light (the other presets have one base for both).
Rgb accentBase(QStringView name, bool dark = false);
/// A readable accent family from any colour on surface `s` of the mode.
Accent deriveAccent(const Rgb &base, bool dark, const Surface &s);
/// The accent family of preset `name` ("custom": `custom`, "#rrggbb"; malformed or unknown →
/// "coral", the default).
Accent accent(QStringView name, QStringView custom, bool dark, const Surface &s);

/// The text colours (they do not depend on the surface; checked on each of them).
struct TextColors {
    Rgb fg, fgMuted, fgFaint, link, linkMissing, tagFg;
    Rgb dangerText, warningText, successText;
    Rgb chipDueFg, chipScheduledFg, chipReminderFg, chipRecurrenceFg;
};
TextColors textColors(bool dark);

/// A translucent colour (a scrim over the page).
struct Scrim {
    Rgb color;
    double alpha = 0;
};
/// The scrim behind centred modal surfaces (the floating command palette and quick switcher, a
/// centred dialog): the page recedes rather than darkens, so the panel holds the eye. Light: a wash
/// of the page colour tinted 22 % toward the text (fg) at 62 % — page text falls from about 7:1 to
/// about 2.5:1 while the panel's bgElevated stands out by its lighter fill (≥ 1.25:1); dark: the
/// ink of Theme.scrim at 72 % instead of 62 % (page text about 2:1). Bottom sheets, the full-screen
/// palette and the drawer keep Theme.scrim (an edge sheet dims the page).
Scrim modalScrim(bool dark, const Surface &s, const TextColors &t);
/// "#aarrggbb" (QML's colour string with alpha), lower case.
QString toHexArgb(const Rgb &c, double alpha);

/// Every token above by its Theme.qml name, as "#rrggbb" (scrimModal: "#aarrggbb"; onAccent as
/// "accentOn").
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
    /// String(c) of a QML colour from its channels (c.r, c.g, c.b, c.a, 0 … 1): "#rrggbb", or
    /// "#aarrggbb" when it is translucent — for compiled QML, where a colour does not convert to
    /// text.
    Q_INVOKABLE QString colorText(double r, double g, double b, double a) const;
};

/// The colours of paletteMap() for the inputs below, as FINAL properties that compiled QML can read
/// (Theme.qml: `readonly property ThemeColors palette: ThemeColors { dark: theme.dark; … }` and
/// `readonly property color bg: palette.bg`). QML type `ThemeColors` (qmlforeign.h).
class ThemeColors : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY inputsChanged FINAL)
    Q_PROPERTY(QString lightSurface READ lightSurface WRITE setLightSurface NOTIFY inputsChanged FINAL)
    Q_PROPERTY(QString darkSurface READ darkSurface WRITE setDarkSurface NOTIFY inputsChanged FINAL)
    Q_PROPERTY(QString accentName READ accentName WRITE setAccentName NOTIFY inputsChanged FINAL)
    Q_PROPERTY(QString customAccent READ customAccent WRITE setCustomAccent NOTIFY inputsChanged FINAL)
    /// paletteMap() of the inputs (what ThemePalette.colors() returns).
    Q_PROPERTY(QVariantMap map READ map NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString bg READ bg NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString bgElevated READ bgElevated NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString bgElevated2 READ bgElevated2 NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString bgSunken READ bgSunken NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString border READ border NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString borderSubtle READ borderSubtle NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString borderStrong READ borderStrong NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString borderControl READ borderControl NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString accent READ accent NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString accentHover READ accentHover NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString accentText READ accentText NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString accentSolid READ accentSolid NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString accentOn READ accentOn NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString fg READ fg NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString fgMuted READ fgMuted NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString fgFaint READ fgFaint NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString link READ link NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString linkMissing READ linkMissing NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString tagFg READ tagFg NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString dangerText READ dangerText NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString warningText READ warningText NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString successText READ successText NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString chipDueFg READ chipDueFg NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString chipScheduledFg READ chipScheduledFg NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString chipReminderFg READ chipReminderFg NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString chipRecurrenceFg READ chipRecurrenceFg NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QString scrimModal READ scrimModal NOTIFY colorsChanged FINAL)

public:
    explicit ThemeColors(QObject *parent = nullptr);

    bool dark() const { return m_dark; }
    void setDark(bool dark);
    QString lightSurface() const { return m_lightSurface; }
    void setLightSurface(const QString &name);
    QString darkSurface() const { return m_darkSurface; }
    void setDarkSurface(const QString &name);
    QString accentName() const { return m_accentName; }
    void setAccentName(const QString &name);
    QString customAccent() const { return m_customAccent; }
    void setCustomAccent(const QString &color);

    QVariantMap map() const { return m_map; }
    QString bg() const { return token(u"bg"); }
    QString bgElevated() const { return token(u"bgElevated"); }
    QString bgElevated2() const { return token(u"bgElevated2"); }
    QString bgSunken() const { return token(u"bgSunken"); }
    QString border() const { return token(u"border"); }
    QString borderSubtle() const { return token(u"borderSubtle"); }
    QString borderStrong() const { return token(u"borderStrong"); }
    QString borderControl() const { return token(u"borderControl"); }
    QString accent() const { return token(u"accent"); }
    QString accentHover() const { return token(u"accentHover"); }
    QString accentText() const { return token(u"accentText"); }
    QString accentSolid() const { return token(u"accentSolid"); }
    QString accentOn() const { return token(u"accentOn"); }
    QString fg() const { return token(u"fg"); }
    QString fgMuted() const { return token(u"fgMuted"); }
    QString fgFaint() const { return token(u"fgFaint"); }
    QString link() const { return token(u"link"); }
    QString linkMissing() const { return token(u"linkMissing"); }
    QString tagFg() const { return token(u"tagFg"); }
    QString dangerText() const { return token(u"dangerText"); }
    QString warningText() const { return token(u"warningText"); }
    QString successText() const { return token(u"successText"); }
    QString chipDueFg() const { return token(u"chipDueFg"); }
    QString chipScheduledFg() const { return token(u"chipScheduledFg"); }
    QString chipReminderFg() const { return token(u"chipReminderFg"); }
    QString chipRecurrenceFg() const { return token(u"chipRecurrenceFg"); }
    QString scrimModal() const { return token(u"scrimModal"); }

Q_SIGNALS:
    void inputsChanged();
    void colorsChanged();

private:
    QString token(QStringView name) const;
    void update();

    bool m_dark = false;
    QString m_lightSurface;
    QString m_darkSurface;
    QString m_accentName;
    QString m_customAccent;
    QVariantMap m_map;
};

} // namespace brulion::ui
