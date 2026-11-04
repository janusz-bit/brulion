// The colours of a diagram from a few theme colours, as Obsidian draws Mermaid: nodes tinted with
// the accent on the page's background, lines in the muted text colour, notes amber, pie slices
// from a fixed set (light pastels with dark text; deep tones with light text in the dark).
#include "diagram/common_p.h"
#include "diagram/diagram.h"

using namespace Qt::StringLiterals;

namespace brulion::diagram {

namespace {

QString valid(const QString &c, const QString &fallback)
{
    const auto rgba = detail::parseHex(c.trimmed());
    return rgba ? detail::toHex(*rgba) : fallback;
}

} // namespace

Palette themedPalette(const ThemeColors &theme)
{
    using detail::mix;
    const bool dark = theme.dark;
    const ThemeColors defaults =
            dark ? ThemeColors{u"#1b1d22"_s, u"#e6e3dc"_s, u"#a8a395"_s, u"#3a3d45"_s, u"#8ab8e6"_s, true}
                 : ThemeColors{};
    const QString bg = valid(theme.background, defaults.background);
    const QString fg = valid(theme.text, defaults.text);
    const QString muted = valid(theme.mutedText, defaults.mutedText);
    const QString border = valid(theme.border, defaults.border);
    const QString accent = valid(theme.accent, defaults.accent);
    const QString amber = u"#f2b33d"_s;
    Palette p;
    p[Role::Background] = bg;
    p[Role::Title] = fg;
    p[Role::NodeFill] = mix(bg, accent, dark ? 0.22 : 0.13);
    p[Role::NodeStroke] = mix(accent, fg, dark ? 0.15 : 0.3);
    p[Role::NodeText] = fg;
    p[Role::Line] = mix(muted, fg, 0.25);
    p[Role::LineText] = fg;
    p[Role::LabelBackground] = bg;
    p[Role::ClusterFill] = mix(bg, fg, dark ? 0.05 : 0.035);
    p[Role::ClusterStroke] = mix(border, fg, 0.15);
    p[Role::ClusterText] = muted;
    p[Role::NoteFill] = mix(bg, amber, dark ? 0.2 : 0.3);
    p[Role::NoteStroke] = dark ? mix(amber, bg, 0.35) : mix(amber, fg, 0.3);
    p[Role::NoteText] = fg;
    p[Role::ActorFill] = p[Role::NodeFill];
    p[Role::ActorStroke] = p[Role::NodeStroke];
    p[Role::ActorText] = fg;
    p[Role::Lifeline] = mix(border, muted, 0.5);
    p[Role::ActivationFill] = mix(bg, accent, dark ? 0.35 : 0.24);
    p[Role::ActivationStroke] = p[Role::NodeStroke];
    p[Role::FrameStroke] = mix(muted, bg, 0.2);
    p[Role::FrameText] = fg;
    p[Role::FrameLabelFill] = mix(bg, fg, dark ? 0.1 : 0.07);
    p[Role::NumberFill] = mix(accent, fg, 0.15);
    p[Role::NumberText] = detail::textOn(p[Role::NumberFill], u"#1f2430"_s, u"#ffffff"_s);
    p[Role::SliceStroke] = bg;
    p[Role::SliceText] = dark ? u"#f3f1ec"_s : u"#1f2430"_s;
    p[Role::LegendText] = fg;
    static const QString light[SliceCount] = {u"#9fc3e6"_s, u"#f5c66a"_s, u"#eda59e"_s, u"#95d3a9"_s,
                                              u"#c9b6e4"_s, u"#8fd0d0"_s, u"#f2a96b"_s, u"#c3cd94"_s,
                                              u"#e6a4c8"_s, u"#b3bccd"_s, u"#dcc795"_s, u"#a8c9aa"_s};
    static const QString deep[SliceCount] = {u"#3b6e9e"_s, u"#8f6a1c"_s, u"#a1483e"_s, u"#2f7a52"_s,
                                             u"#6a5296"_s, u"#2a7676"_s, u"#9a5a26"_s, u"#66733a"_s,
                                             u"#8f4a6e"_s, u"#4c566b"_s, u"#7d6a3f"_s, u"#4a6e4d"_s};
    // Chart series: lines and bar outlines, readable on the background (light: deeper than the
    // pastel fills; dark: lighter than the deep ones).
    static const QString lightLine[SliceCount] = {u"#2f6ea8"_s, u"#a8740f"_s, u"#b2493d"_s, u"#2c8350"_s,
                                                  u"#7253a6"_s, u"#1f7f80"_s, u"#b0601f"_s, u"#6a7a2a"_s,
                                                  u"#a8467b"_s, u"#55617a"_s, u"#8a7232"_s, u"#4a7d4e"_s};
    static const QString darkLine[SliceCount] = {u"#8fbce8"_s, u"#f0c060"_s, u"#f0968b"_s, u"#86d3a2"_s,
                                                 u"#c2aaeb"_s, u"#7fd0d0"_s, u"#f2a76b"_s, u"#c6d48c"_s,
                                                 u"#eda0c9"_s, u"#b3bdd2"_s, u"#ddc78f"_s, u"#a6cfa8"_s};
    for (int i = 0; i < SliceCount; ++i) {
        p[sliceRole(i)] = dark ? deep[i] : light[i];
        p[seriesRole(i)] = dark ? darkLine[i] : lightLine[i];
    }
    p[Role::Grid] = mix(border, bg, 0.2);
    return p;
}

Palette lightPalette()
{
    // The HTML export's light colours (io/export_html.cpp).
    return themedPalette({u"#fbf9f4"_s, u"#1f2430"_s, u"#535a6a"_s, u"#e4dfd3"_s, u"#2c6aa3"_s, false});
}

Palette darkPalette()
{
    return themedPalette({u"#1b1d22"_s, u"#e6e3dc"_s, u"#a8a395"_s, u"#3a3d45"_s, u"#8ab8e6"_s, true});
}

QString colorOf(const Paint &paint, const Palette &palette)
{
    if (!paint.color.isEmpty())
        return paint.color;
    return paint.role == Role::None ? QString() : palette[paint.role];
}

} // namespace brulion::diagram
