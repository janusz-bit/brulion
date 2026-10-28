#include "drawing/palette.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace brulion::drawing {

namespace {

int hexDigit(QChar c)
{
    const char16_t u = c.unicode();
    if (u >= u'0' && u <= u'9')
        return u - u'0';
    if (u >= u'a' && u <= u'f')
        return 10 + (u - u'a');
    if (u >= u'A' && u <= u'F')
        return 10 + (u - u'A');
    return -1;
}

/// JavaScript's Math.round (halves up, also below zero).
double jsRound(double v)
{
    return std::floor(v + 0.5);
}

} // namespace

std::optional<Rgba> parseColor(QStringView hex)
{
    const QStringView c = hex.trimmed();
    if ((c.size() != 4 && c.size() != 7 && c.size() != 9) || !c.startsWith(u'#'))
        return std::nullopt;
    int digits[8] = {};
    for (qsizetype i = 1; i < c.size(); ++i) {
        digits[i - 1] = hexDigit(c.at(i));
        if (digits[i - 1] < 0)
            return std::nullopt;
    }
    if (c.size() == 4)
        return Rgba{digits[0] * 17, digits[1] * 17, digits[2] * 17, 255};
    const auto byte = [&digits](int i) { return digits[2 * i] * 16 + digits[2 * i + 1]; };
    return Rgba{byte(0), byte(1), byte(2), c.size() == 9 ? byte(3) : 255};
}

QString colorName(Rgba c)
{
    const auto two = [](int v) { return QString::number(std::clamp(v, 0, 255), 16).rightJustified(2, u'0'); };
    QString out = u'#' + two(c.r) + two(c.g) + two(c.b);
    if (c.a < 255)
        out += two(c.a);
    return out;
}

Rgba darkRgba(Rgba c)
{
    // cssInvert(93 %): every channel rounded and clamped (colors.ts).
    const auto invert = [](int v) {
        return std::clamp(jsRound(v * (1 - DarkInvert) + (255 - v) * DarkInvert), 0.0, 255.0);
    };
    const double r = invert(c.r) / 255;
    const double g = invert(c.g) / 255;
    const double b = invert(c.b) / 255;
    // cssHueRotate(180°): the CSS filter's matrix (Filter Effects 1, feColorMatrix hueRotate),
    // sin(π) as the reference computes it (≈ 1.2e-16, not 0: the results match to the integer).
    const double a = 180 * std::numbers::pi / 180;
    const double cs = std::cos(a);
    const double sn = std::sin(a);
    const double m[9] = {
            0.213 + cs * 0.787 - sn * 0.213, 0.715 - cs * 0.715 - sn * 0.715, 0.072 - cs * 0.072 + sn * 0.928,
            0.213 - cs * 0.213 + sn * 0.143, 0.715 + cs * 0.285 + sn * 0.14,  0.072 - cs * 0.072 - sn * 0.283,
            0.213 - cs * 0.213 - sn * 0.787, 0.715 - cs * 0.715 + sn * 0.715, 0.072 + cs * 0.928 + sn * 0.072,
    };
    const auto channel = [](double v) { return int(jsRound(std::clamp(v, 0.0, 1.0) * 255)); };
    return Rgba{channel(r * m[0] + g * m[1] + b * m[2]), channel(r * m[3] + g * m[4] + b * m[5]),
                channel(r * m[6] + g * m[7] + b * m[8]), c.a};
}

QString darkColor(QStringView hex, const QString &fallback)
{
    const std::optional<Rgba> c = parseColor(hex);
    return c ? colorName(darkRgba(*c)) : fallback;
}

bool rendersDark(PaperTheme theme, bool appDark)
{
    switch (theme) {
    case PaperTheme::Light: return false;
    case PaperTheme::Dark: return true;
    case PaperTheme::Auto: break;
    }
    return appDark;
}

int highlighterDarkOpacity(int opacity)
{
    return std::clamp(int(jsRound(std::clamp(opacity, 0, 100) * HighlighterDarkOpacityFactor)), 0, 100);
}

Ink displayInk(const Stroke &stroke, bool dark)
{
    const Rgba stored = parseColor(stroke.color).value_or(*parseColor(FallbackInk));
    const int opacity = std::clamp(stroke.opacity, 0, 100);
    if (!dark)
        return Ink{colorName(stored), opacity};
    if (stroke.tool == Tool::Highlighter)
        return Ink{colorName(stored), highlighterDarkOpacity(opacity)};
    return Ink{colorName(darkRgba(stored)), opacity};
}

QString displayPaper(QStringView paper, bool dark)
{
    const Rgba c = parseColor(paper).value_or(Rgba{255, 255, 255, 255});
    return colorName(dark ? darkRgba(c) : c);
}

QString displayRule(bool dark)
{
    return dark ? darkColor(RuleColor) : RuleColor.toString();
}

double luminance(Rgba c)
{
    const auto linear = [](int v) {
        const double s = std::clamp(v, 0, 255) / 255.0;
        return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b);
}

double contrast(Rgba a, Rgba b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

Rgba over(Rgba fg, int opacity, Rgba bg)
{
    const double k = std::clamp(opacity, 0, 100) / 100.0 * std::clamp(fg.a, 0, 255) / 255.0;
    const auto mix = [k](int f, int b) { return int(jsRound(f * k + b * (1 - k))); };
    return Rgba{mix(fg.r, bg.r), mix(fg.g, bg.g), mix(fg.b, bg.b), 255};
}

} // namespace brulion::drawing
