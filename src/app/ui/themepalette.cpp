#include "themepalette.h"

#include <QCollator>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

constexpr Rgb hex(unsigned v)
{
    return {int((v >> 16) & 0xff), int((v >> 8) & 0xff), int(v & 0xff)};
}

constexpr Rgb kWhite = hex(0xffffff);

int hexDigit(QChar c)
{
    if (c >= u'0' && c <= u'9')
        return c.unicode() - u'0';
    if (c >= u'a' && c <= u'f')
        return c.unicode() - u'a' + 10;
    if (c >= u'A' && c <= u'F')
        return c.unicode() - u'A' + 10;
    return -1;
}

double linear(int channel)
{
    const double v = channel / 255.0;
    return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

struct Hsl {
    double h = 0; // 0 … 1
    double s = 0;
    double l = 0;
};

Hsl toHsl(const Rgb &c)
{
    const double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
    const double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    Hsl out;
    out.l = (mx + mn) / 2;
    const double d = mx - mn;
    if (d <= 0)
        return out;
    out.s = out.l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
    if (mx == r)
        out.h = (g - b) / d + (g < b ? 6 : 0);
    else if (mx == g)
        out.h = (b - r) / d + 2;
    else
        out.h = (r - g) / d + 4;
    out.h /= 6;
    return out;
}

double hueChannel(double p, double q, double t)
{
    if (t < 0)
        t += 1;
    if (t > 1)
        t -= 1;
    if (t < 1.0 / 6)
        return p + (q - p) * 6 * t;
    if (t < 0.5)
        return q;
    if (t < 2.0 / 3)
        return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
}

int channel(double v)
{
    return std::clamp(int(std::lround(v * 255)), 0, 255);
}

Rgb fromHsl(const Hsl &c)
{
    const double l = std::clamp(c.l, 0.0, 1.0);
    if (c.s <= 0)
        return {channel(l), channel(l), channel(l)};
    const double q = l < 0.5 ? l * (1 + c.s) : l + c.s - l * c.s;
    const double p = 2 * l - q;
    return {channel(hueChannel(p, q, c.h + 1.0 / 3)), channel(hueChannel(p, q, c.h)),
            channel(hueChannel(p, q, c.h - 1.0 / 3))};
}

/// `c` with its HSL lightness moved by `delta`.
Rgb shifted(const Rgb &c, double delta)
{
    Hsl h = toHsl(c);
    h.l = std::clamp(h.l + delta, 0.0, 1.0);
    return fromHsl(h);
}

/// `start`, made darker (`darker`) or lighter step by step until `ok` holds (at the latest
/// black or white, which pass every check the callers make).
template<typename Ok>
Rgb adjustUntil(const Rgb &start, bool darker, Ok ok)
{
    if (ok(start))
        return start;
    Hsl h = toHsl(start);
    for (int i = 0; i < 200; ++i) {
        h.l = std::clamp(h.l + (darker ? -0.005 : 0.005), 0.0, 1.0);
        const Rgb c = fromHsl(h);
        if (ok(c))
            return c;
        if (h.l <= 0 || h.l >= 1)
            break;
    }
    return darker ? hex(0x000000) : kWhite;
}

/// The surfaces text must be readable on: the page, cards and dialogs, wells (as Theme.qml
/// promises; popups over cards, bgElevated2, carry fg).
QList<Rgb> textSurfaces(const Surface &s)
{
    return {s.bg, s.bgElevated, s.bgSunken};
}

} // namespace

std::optional<Rgb> parseColor(QStringView text)
{
    const QStringView t = text.trimmed();
    if (!t.startsWith(u'#') || (t.size() != 4 && t.size() != 7))
        return std::nullopt;
    int v[6] = {};
    for (qsizetype i = 1; i < t.size(); ++i) {
        v[i - 1] = hexDigit(t.at(i));
        if (v[i - 1] < 0)
            return std::nullopt;
    }
    if (t.size() == 4)
        return Rgb{v[0] * 17, v[1] * 17, v[2] * 17};
    return Rgb{v[0] * 16 + v[1], v[2] * 16 + v[3], v[4] * 16 + v[5]};
}

QString toHex(const Rgb &c)
{
    return u"#%1%2%3"_s.arg(c.r, 2, 16, QChar(u'0')).arg(c.g, 2, 16, QChar(u'0')).arg(c.b, 2, 16, QChar(u'0'));
}

QString toHexArgb(const Rgb &c, double alpha)
{
    const int a = std::clamp(int(std::lround(alpha * 255)), 0, 255);
    return u"#%1"_s.arg(a, 2, 16, QChar(u'0')) + toHex(c).mid(1);
}

double relativeLuminance(const Rgb &c)
{
    return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b);
}

double contrast(const Rgb &a, const Rgb &b)
{
    const double la = relativeLuminance(a), lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

Rgb over(const Rgb &fg, double alpha, const Rgb &bg)
{
    const auto mix = [alpha](int f, int b) {
        return std::clamp(int(std::lround(f * alpha + b * (1 - alpha))), 0, 255);
    };
    return {mix(fg.r, bg.r), mix(fg.g, bg.g), mix(fg.b, bg.b)};
}

// ---- Surfaces -------------------------------------------------------------------------------

Surface surface(QStringView name, bool dark)
{
    if (dark) {
        if (name == u"slate") // the logo's deep blue-grey
            return {hex(0x1c2130), hex(0x232a3b), hex(0x2b3245), hex(0x171b28),
                    hex(0x343c51), hex(0x2b3245), hex(0x48526b), hex(0x6c7793)};
        // "black", the default ("Nocny", for OLED screens): true black, the side panes (bgSunken)
        // and cards barely lifted
        return {hex(0x000000), hex(0x0d0f13), hex(0x1c2027), hex(0x08090b),
                hex(0x262a32), hex(0x1a1d23), hex(0x3a404d), hex(0x6c7793)};
    }
    if (name == u"white") // neutral white, grey wells
        return {hex(0xffffff), hex(0xffffff), hex(0xffffff), hex(0xf4f5f7),
                hex(0xe2e4e9), hex(0xeceef2), hex(0xc7cbd3), hex(0x828895)};
    // "paper", the default
    return {hex(0xfbf7ee), hex(0xfffdf7), hex(0xfffefb), hex(0xf3eee2),
            hex(0xe4dccb), hex(0xece6d8), hex(0xcdc2ab), hex(0x928775)};
}

QStringList surfaceNames(bool dark)
{
    return dark ? QStringList{u"black"_s, u"slate"_s} : QStringList{u"paper"_s, u"white"_s};
}

// ---- Accent ---------------------------------------------------------------------------------

QStringList accentNames()
{
    return {u"coral"_s, u"red"_s, u"blue"_s, u"green"_s, u"teal"_s, u"violet"_s, u"graphite"_s};
}

Rgb accentBase(QStringView name, bool dark)
{
    if (name == u"coral")
        return dark ? hex(0xff7a66) : hex(0xc4422f);
    if (name == u"blue")
        return hex(0x3b7dd8);
    if (name == u"green")
        return hex(0x2e9a5a);
    if (name == u"teal")
        return hex(0x13958b);
    if (name == u"violet")
        return hex(0x8a5cd6);
    if (name == u"graphite")
        return hex(0x5f6675);
    return hex(0xe2574c);
}

Accent deriveAccent(const Rgb &base, bool dark, const Surface &s)
{
    const QList<Rgb> surfaces = textSurfaces(s);
    const auto passes = [&](const Rgb &c, double ratio, const QList<Rgb> &on) {
        return std::all_of(on.cbegin(), on.cend(), [&](const Rgb &bg) { return contrast(c, bg) >= ratio; });
    };
    Accent a;
    // Marks: 3:1 on the surfaces (light surfaces: darker; dark ones: lighter).
    a.accent = adjustUntil(base, !dark, [&](const Rgb &c) { return passes(c, 3.0, surfaces); });
    // Text: 4.5:1 on the surfaces and on the accent's tint over each of them (badges, chips).
    QList<Rgb> tinted = surfaces;
    for (const Rgb &bg : surfaces)
        tinted.append(over(a.accent, accentBgAlpha(dark), bg));
    a.accentText = adjustUntil(a.accent, !dark, [&](const Rgb &c) { return passes(c, 4.5, tinted); });
    // Solid fills carry white text in both modes.
    a.accentSolid = adjustUntil(a.accent, true, [&](const Rgb &c) { return contrast(kWhite, c) >= 4.5; });
    a.accentHover = shifted(a.accent, dark ? 0.05 : -0.05);
    a.onAccent = kWhite;
    return a;
}

Accent accent(QStringView name, QStringView custom, bool dark, const Surface &s)
{
    if (name == u"custom") {
        if (const auto c = parseColor(custom))
            return deriveAccent(*c, dark, s);
    } else if (name == u"red") {
        // The margin red, hand-picked (passes every check on every surface: tst_themepalette).
        if (dark)
            return {hex(0xe2574c), hex(0xec6a5f), hex(0xef7a6f), hex(0xc4463a), kWhite};
        return {hex(0xe2574c), hex(0xd14b40), hex(0xad3e2c), hex(0xc4463a), kWhite};
    } else if (name != u"coral" && accentNames().contains(name)) {
        return deriveAccent(accentBase(name, dark), dark, s);
    }
    // Coral, the default ("Nocny"): #c4422f in light (its own solid fill under white text),
    // #ff7a66 in dark — bright enough that its solid fill is the accent itself, under dark ink.
    Accent a = deriveAccent(accentBase(u"coral", dark), dark, s);
    if (dark) {
        a.accentSolid = a.accent;
        a.onAccent = hex(0x2b0a05);
    }
    return a;
}

// ---- Text -----------------------------------------------------------------------------------

TextColors textColors(bool dark)
{
    if (dark)
        return {hex(0xd3d8e2), hex(0xa9b0bf), hex(0x8f96a7), hex(0x8fb8ea), hex(0xb9a3d8), hex(0xc89bf0), hex(0xf3877c),
                hex(0xf2b33d), hex(0x7fd1a8), hex(0xb9d4ee), hex(0xc7b5e2), hex(0xf2b33d), hex(0x8fd3a6)};
    return {hex(0x1f2430), hex(0x535a6a), hex(0x676c7b), hex(0x2a62a6), hex(0x7a5ea3), hex(0x7a4bb0), hex(0xa8382d),
            hex(0x835500), hex(0x2a6e44), hex(0x2b5f8f), hex(0x6a4f93), hex(0x8a5900), hex(0x2a6e44)};
}

Scrim modalScrim(bool dark, const Surface &s, const TextColors &t)
{
    if (dark)
        return {hex(0x080a10), 0.72};
    return {over(t.fg, 0.22, s.bg), 0.62};
}

QVariantMap paletteMap(bool dark, QStringView lightSurface, QStringView darkSurface, QStringView accentName,
                       QStringView customAccent)
{
    const Surface s = surface(dark ? darkSurface : lightSurface, dark);
    const Accent a = accent(accentName, customAccent, dark, s);
    const TextColors t = textColors(dark);
    QVariantMap m;
    const auto put = [&m](const char *name, const Rgb &c) { m.insert(QString::fromLatin1(name), toHex(c)); };
    put("bg", s.bg);
    put("bgElevated", s.bgElevated);
    put("bgElevated2", s.bgElevated2);
    put("bgSunken", s.bgSunken);
    put("border", s.border);
    put("borderSubtle", s.borderSubtle);
    put("borderStrong", s.borderStrong);
    put("borderControl", s.borderControl);
    put("accent", a.accent);
    put("accentHover", a.accentHover);
    put("accentText", a.accentText);
    put("accentSolid", a.accentSolid);
    put("accentOn", a.onAccent);
    put("fg", t.fg);
    put("fgMuted", t.fgMuted);
    put("fgFaint", t.fgFaint);
    put("link", t.link);
    put("linkMissing", t.linkMissing);
    put("tagFg", t.tagFg);
    put("dangerText", t.dangerText);
    put("warningText", t.warningText);
    put("successText", t.successText);
    put("chipDueFg", t.chipDueFg);
    put("chipScheduledFg", t.chipScheduledFg);
    put("chipReminderFg", t.chipReminderFg);
    put("chipRecurrenceFg", t.chipRecurrenceFg);
    const Scrim scrim = modalScrim(dark, s, t);
    m.insert(u"scrimModal"_s, toHexArgb(scrim.color, scrim.alpha));
    return m;
}

// ---- Fonts ----------------------------------------------------------------------------------

QStringList filterFontFamilies(const QStringList &families)
{
    // Families without text: emoji, symbols, mathematics, dingbats, icon fonts — and the app's
    // own fallback fonts (BrulionEmoji, BrulionMath: emoji and symbols only).
    static const QRegularExpression noText(
            u"\\b(emoji|symbols?|math|dingbats?|wingdings\\d*|webdings|opensymbol)\\b|^brulion"_s,
            QRegularExpression::CaseInsensitiveOption);
    QStringList out;
    QSet<QString> seen;
    for (const QString &raw : families) {
        const QString f = raw.trimmed();
        if (f.isEmpty() || f.startsWith(u'.') || noText.match(f).hasMatch())
            continue;
        const QString key = f.toCaseFolded();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        out.append(f);
    }
    QCollator collator{QLocale(QLocale::Polish, QLocale::Poland)};
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::stable_sort(out.begin(), out.end(),
                     [&collator](const QString &a, const QString &b) { return collator.compare(a, b) < 0; });
    return out;
}

// ---- QML ------------------------------------------------------------------------------------

ThemePalette::ThemePalette(QObject *parent) : QObject(parent) { }

QVariantMap ThemePalette::colors(bool dark, const QString &lightSurface, const QString &darkSurface,
                                 const QString &accentName, const QString &customAccent) const
{
    return paletteMap(dark, lightSurface, darkSurface, accentName, customAccent);
}

QStringList ThemePalette::accentNames() const
{
    return brulion::ui::accentNames();
}

QString ThemePalette::accentSwatch(const QString &name, const QString &customAccent, bool dark,
                                   const QString &surfaceName) const
{
    return toHex(accent(name, customAccent, dark, surface(surfaceName, dark)).accent);
}

QString ThemePalette::normalizeColor(const QString &text) const
{
    const auto c = parseColor(text);
    return c ? toHex(*c) : QString();
}

double ThemePalette::contrastRatio(const QString &a, const QString &b) const
{
    const auto x = parseColor(a);
    const auto y = parseColor(b);
    return x && y ? contrast(*x, *y) : 0.0;
}

QString ThemePalette::colorText(double r, double g, double b, double a) const
{
    // As QColor keeps them (16 bits a channel) and gives them back in 8 bits (qt_div_257).
    const auto channel = [](double v) {
        const int wide = int(std::lround(std::clamp(v, 0.0, 1.0) * 65535.0));
        return (wide - (wide >> 8) + 0x80) >> 8;
    };
    const auto hex2 = [](int v) { return QString::number(v, 16).rightJustified(2, u'0'); };
    const int alpha = channel(a);
    QString out = u"#"_s;
    if (alpha != 255)
        out += hex2(alpha);
    return out + hex2(channel(r)) + hex2(channel(g)) + hex2(channel(b));
}

// ---- ThemeColors ----------------------------------------------------------------------------

ThemeColors::ThemeColors(QObject *parent) : QObject(parent)
{
    update();
}

void ThemeColors::setDark(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    Q_EMIT inputsChanged();
    update();
}

void ThemeColors::setLightSurface(const QString &name)
{
    if (m_lightSurface == name)
        return;
    m_lightSurface = name;
    Q_EMIT inputsChanged();
    update();
}

void ThemeColors::setDarkSurface(const QString &name)
{
    if (m_darkSurface == name)
        return;
    m_darkSurface = name;
    Q_EMIT inputsChanged();
    update();
}

void ThemeColors::setAccentName(const QString &name)
{
    if (m_accentName == name)
        return;
    m_accentName = name;
    Q_EMIT inputsChanged();
    update();
}

void ThemeColors::setCustomAccent(const QString &color)
{
    if (m_customAccent == color)
        return;
    m_customAccent = color;
    Q_EMIT inputsChanged();
    update();
}

QString ThemeColors::token(QStringView name) const
{
    return m_map.value(name.toString()).toString();
}

void ThemeColors::update()
{
    QVariantMap map = paletteMap(m_dark, m_lightSurface, m_darkSurface, m_accentName, m_customAccent);
    if (map == m_map)
        return;
    m_map = std::move(map);
    Q_EMIT colorsChanged();
}

} // namespace brulion::ui
