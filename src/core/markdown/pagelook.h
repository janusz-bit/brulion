// A page's look (docs/SYNTAX.md §4 "Page icon and cover"): its icon — an emoji or one of the app's
// icons — and its cover picture, read from page properties that Obsidian's Iconize and Banners
// plugins and Logseq read too (`icon`, `banner_icon`; `banner`, `banner_y`), and the values Brulion
// writes for them. Written through setPageProperty() (markdown/pageprops.h), so an edit changes
// only that property's line. Part of the markdown module — see markdown/markdown.h.
#pragma once

#include "types.h"

#include <QString>
#include <QStringView>
#include <QVariant>

namespace brulion::markdown {

/// The page property keys (lower case; YAML keys are matched case-insensitively).
inline constexpr QStringView PageIconKey = u"icon";
inline constexpr QStringView PageIconFallbackKey = u"banner_icon"; ///< the Banners plugin's
inline constexpr QStringView PageCoverKey = u"banner";
inline constexpr QStringView PageCoverPositionKey = u"banner_y";

/// A page icon.
struct PageIcon {
    enum class Kind { None, Emoji, App };
    Kind kind = Kind::None;
    /// Emoji: the emoji as written (a rocket, a ZWJ family, a keycap "1"); App: the icon's name in the app's set,
    /// Lucide's kebab case ("book-open").
    QString name;
    bool isNone() const { return kind == Kind::None; }
    friend bool operator==(const PageIcon &, const PageIcon &) = default;
};

/// The icon a property value names (trimmed; quotes of a Logseq value dropped):
/// * an emoji — at most two grapheme clusters, at most 32 UTF-16 units, no letters, no blanks or
///   control characters, and at least one character from U+2000 on (so "1", "-", "ab" are none);
/// * an app icon written as Iconize writes Lucide icons — "Li" and the name in Pascal case
///   (`LiBookOpen` → "book-open", `LiAlarmClock` → "alarm-clock"; a run of digits is a word of
///   its own: `LiHeading1` → "heading-1");
/// anything else (other icon packs `FasHome`, `RiHome`, words, links) → None: shown nowhere, kept
/// in the file as it is. Whether an app icon exists in this build is the app's to check.
PageIcon parsePageIcon(QStringView value);
/// What Brulion writes for `icon`: the emoji itself, or "Li" + the name in Pascal case; "" for None.
QString pageIconValue(const PageIcon &icon);

/// A page cover.
struct PageCover {
    enum class Kind { None, File, Web };
    Kind kind = Kind::None;
    /// File: the link target as written (`[[assets/okładka.jpg]]` → "assets/okładka.jpg"; resolved
    /// like any link, VaultIndex::resolveLink); Web: the http(s) URL.
    QString target;
    /// The vertical position of the picture in the cover's band: 0 top … 1 bottom (default ½).
    double position = 0.5;
    friend bool operator==(const PageCover &, const PageCover &) = default;
};

/// The cover a property value names: `![[file]]`, `[[file]]`, `[[file|alias]]` (an alias and a
/// fragment dropped), `![alt](file)`, `![alt](<file with spaces>)`, a bare path (`assets/a.png`)
/// or an http(s) URL with a host (also inside `![](…)`). A YAML list (an unquoted `[[file]]`) is
/// read by its first item. Anything else (empty, other schemes such as `javascript:` or `data:`,
/// a line break) → None.
PageCover parsePageCover(const QVariant &value);
/// A `banner_y` value as a position: a number 0 … 1 (also as text, a decimal comma allowed);
/// anything else (out of range, not a number) → the default ½.
double parseCoverPosition(const QVariant &value);

/// What Brulion writes for a cover: `![[target]]` for a file (Banners' own form), the URL for a
/// picture from the internet; "" for None.
QString pageCoverValue(const PageCover &cover);
/// What Brulion writes for a position: the number rounded to two decimals ("0.25", "0", "1").
QString coverPositionValue(double position);

/// The icon and cover of a page.
struct PageLook {
    PageIcon icon;
    PageCover cover;
};
/// The look of a page from its properties as parseNote() keeps them (ParsedNote::properties: keys
/// lower case): `icon`, else `banner_icon`; `banner` with `banner_y`.
PageLook pageLook(const Properties &properties);

} // namespace brulion::markdown
