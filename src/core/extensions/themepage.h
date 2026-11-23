#pragma once
// Theme pages (docs/SYNTAX.md §13 "Theme pages", docs/UI.md "Look & feel"): a page tagged `motyw`
// (`theme`) whose ```theme blocks declare tokens of the app's theme — the same tokens as
// b.app.theme (appapi.h themeTokens()), as data: no code runs. Settings → Appearance lists the
// notebook's theme pages and applies the one picked (the app's ThemePages, src/app/shell). A theme
// travels as the page's file: copy it into another notebook and it is there. Pure functions over a
// page's text.
//
//   ```theme
//   // Sepia: paper surfaces in the light, a teal accent in both
//   accent: #1f8a80
//   radius: 9
//   fontUi: Inter
//   light:
//     bg: #f4ecd8
//     fg: #3b2f20
//   dark:
//     bg: #14110c
//   ```
//
//   const ThemePage page = parseThemePage(content, [](const QString &family) { return installed(family); });
//   page.layer        // ThemeLayer: what passed (both / light / dark)
//   page.issues       // [{line 0-based, kind, token, value, detail}]: what was ignored, and why
//
// The block's lines: `token: value` (blank around the colon allowed; the value may be quoted with "
// or '; ` // …` after it is a comment), `light:` / `dark:` (Polish `jasny:` / `ciemny:`) alone start
// that mode's tokens — the lines after it indented deeper than it —, blank lines and lines starting
// with `//` or `#` are skipped. Token names are read in any letter case ("BG" is bg). A colour is
// "#rgb", "#rrggbb" or "#aarrggbb" (alpha first), a size a number (an optional "px" after it) within
// the token's range, a font a family's name — one this device has, when the caller says which it has.
// Of a token set twice in one mode the last wins. A wrong line is an issue and ignored; the rest
// applies.

#include "extensions/appapi.h"
#include "types.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace brulion::extensions {

/// The tags that make a page a theme page (page-level: frontmatter `tags:` or a `tags::` line).
inline const QStringList &themeTags()
{
    static const QStringList tags{QStringLiteral("motyw"), QStringLiteral("theme")};
    return tags;
}

/// Is the page tagged as a theme at page level?
bool isThemePage(const ParsedNote &note);

/// The page's `opis::` / `description::` property ("" without one), one line.
QString themeDescription(const ParsedNote &note);

/// Why a line of a ```theme block was ignored.
enum class ThemeIssueKind {
    Syntax,       ///< not `token: value`, nor `light:` / `dark:`
    UnknownToken, ///< `token` is no token of the theme (`detail`: the nearest name, "" none)
    BadColor,     ///< `value` is no colour
    BadNumber,    ///< `value` is no number within the range (`detail`: "min … max")
    BadFont,      ///< `value` is no font family's name (two lines, too long)
    MissingFont,  ///< the family `value` is not installed on this device (the default font shows)
    ModeInMode,   ///< `light:` / `dark:` inside a mode
    ModeValue,    ///< `light:` / `dark:` with a value after it (`light: #fff`)
    TooMany,      ///< beyond MaxThemeTokens
};

struct ThemeIssue {
    int line = 0; ///< the page's line, 0-based
    ThemeIssueKind kind = ThemeIssueKind::Syntax;
    QString token; ///< as written
    QString value; ///< as written (at most 60 characters)
    QString detail;
    bool operator==(const ThemeIssue &other) const = default;
};

/// What a theme page declares.
struct ThemePage {
    ThemeLayer layer;
    QList<ThemeIssue> issues;
    int blocks = 0;     ///< ```theme blocks found
    int firstLine = -1; ///< the first block's fence line (0-based), -1: none
    /// Tokens that apply (both, light and dark together).
    int tokenCount() const { return int(layer.both.size() + layer.light.size() + layer.dark.size()); }
};

/// Whether a font family is installed (the app asks QFontDatabase); unset: every name passes.
using FontCheck = std::function<bool(const QString &family)>;

/// Every ```theme block of a page (also ```{theme}, ```.theme, inside a list item; not in the
/// frontmatter, not inside another fence), in order: one layer.
ThemePage parseThemePage(QStringView content, const FontCheck &fontInstalled = {});

/// The lines of one block (without its fences); `firstLine` is the page's line of lines[0].
ThemePage parseThemeBlock(const QStringList &lines, int firstLine = 0, const FontCheck &fontInstalled = {});

/// A layer as the lines of a ```theme block (without the fences): the tokens of both modes first,
/// then `light:` and `dark:` with theirs indented by two spaces, each part in themeTokens()' order.
/// parseThemeBlock(themeBlockLines(layer)).layer == layer.
QStringList themeBlockLines(const ThemeLayer &layer);

/// A layer as a whole ```theme block, its fences included, ending in "\n".
QString themeBlockText(const ThemeLayer &layer);

/// Two layers' tokens of both modes made one: what light and dark hold alike goes to `both`.
ThemeLayer foldedThemeLayer(const QVariantMap &light, const QVariantMap &dark);

} // namespace brulion::extensions
