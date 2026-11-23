#pragma once
// The app as extensions change it (`b.app`, docs/EXTENSIONS.md "The app (`b.app`)"): what the
// engine checks before the app sees it — the theme's tokens an extension may override and their
// kinds, the places in the app's UI where an extension may put an item ("slots") and what such an
// item holds. GUI-free: the app (src/app/shell/appscripting.h) applies what passes, the remote
// control's live methods and `brulion-cli app theme` check their input with the same functions.
//
//   ThemeLayer layer;
//   QString error;
//   if (!themeLayerFrom(QVariantMap{{u"accent"_s, u"#3a7"_s}, {u"dark"_s, QVariantMap{{u"bg"_s, u"#0b0d10"_s}}}},
//                       &layer, &error))
//       …error: "unknown token “bgg” (did you mean “bg”?)"
//   Contribution item;
//   contributionFrom(u"toolbar"_s, QVariantMap{{u"id"_s, u"gym"_s}, {u"icon"_s, u"dumbbell"_s},
//                                             {u"label"_s, u"Gym"_s}, {u"view"_s, u"today"_s}}, &item, &error);

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace brulion::extensions {

/// What a token of the theme holds.
enum class ThemeTokenKind {
    Color,  ///< "#rgb", "#rrggbb" or "#aarrggbb" (alpha first, as QML reads it); kept as "#rrggbb" / "#aarrggbb"
    Number, ///< a size in pixels, within the token's range
    Font,   ///< a font family's name ("" = the app's default)
};

struct ThemeToken {
    QString name; ///< as Theme.qml names it ("bg", "accent", "radius")
    ThemeTokenKind kind = ThemeTokenKind::Color;
    double min = 0;
    double max = 0;
};

/// Every token an extension may override, in Theme.qml's order: the surfaces, text, lines,
/// accent and links, tags, states, chips, selection, diffs, the radii, the focus ring, the spacing
/// unit (`space`), fonts.
const QList<ThemeToken> &themeTokens();
/// The token `name` (exact, case-sensitive), nullptr when there is none.
const ThemeToken *themeToken(QStringView name);
/// The token nearest to a misspelt `name` (at most 3 edits, any case), "" when none is near.
QString nearestThemeToken(QStringView name);
/// `value` checked against `token`'s kind and range, normalized into `*out` (a colour lower case,
/// a number a double, a font trimmed); false and `*error` (English) when it does not fit.
bool themeTokenValue(const ThemeToken &token, const QVariant &value, QVariant *out, QString *error);

/// One extension's overrides of the theme (b.app.theme): tokens for both modes, and tokens for
/// the dark or the light mode only (those win over `both` in their mode). Values are normalized
/// (colours lower case "#rrggbb" / "#aarrggbb", numbers as doubles, fonts trimmed).
struct ThemeLayer {
    QVariantMap both;
    QVariantMap light;
    QVariantMap dark;
    bool isEmpty() const { return both.isEmpty() && light.isEmpty() && dark.isEmpty(); }
    /// The tokens in force in a mode: `both` with that mode's on top.
    QVariantMap forMode(bool darkMode) const;
    bool operator==(const ThemeLayer &other) const = default;
};

/// At most this many tokens in one layer (both, light and dark together).
inline constexpr int MaxThemeTokens = 200;

/// A table of b.app.theme as a layer: tokens by name, and `dark` / `light` tables of tokens. An
/// unknown token (with the nearest name), a value of the wrong kind or out of its range is an
/// error; nothing is kept then. An empty or missing table is an empty layer (the overrides gone).
bool themeLayerFrom(const QVariantMap &table, ThemeLayer *layer, QString *error);
/// "#rrggbb" / "#aarrggbb" (lower case) of a typed colour ("#ABC" → "#aabbcc"); "" when it is none.
QString normalizedThemeColor(const QString &text);
/// The layers merged in their order (a later one wins per token), for one mode.
QVariantMap mergedTheme(const QList<ThemeLayer> &layers, bool darkMode);
/// A layer as a table (the remote control's `theme`, the console): {…both, dark = {…}, light = {…}}.
QVariantMap themeLayerTable(const ThemeLayer &layer);

// ---- Slots: where an extension puts items into the app's UI (b.app.contribute) --------------------

/// The slots, as b.app.contribute names them:
///   toolbar    a button of the top bar (desktop; in the More menu on phones): icon, label (its
///              name and tooltip), and what it does
///   statusbar  a text in the status bar (desktop): label, optionally icon, tone and what a click does
///   sidebar    an entry of the sidebar's navigation (the drawer on phones)
///   pageMenu   an item of a page's menu (the top bar's More, a page's ⋯); a command gets ctx.page
///   blockMenu  an item of a block's menu (its bullet's menu, a right-click); a command gets ctx.block
///   slash      an entry of the editor's `/` menu: a command on the block (ctx.block) or Markdown
///              inserted (`insert`)
QStringList contributionSlots();
bool isContributionSlot(QStringView slot);

/// An item an extension put into a slot. Exactly one of command, view, page, run, insert says
/// what it does (none: a text in the status bar that does nothing).
struct Contribution {
    QString extension;
    QString slot;
    QString id;      ///< the extension's own name for it: contributing the same id again replaces it
    QString label;   ///< one line, at most MaxContributionLabel characters
    QString icon;    ///< one of the app's icons ("dumbbell"), "" = the slot's default
    QString tooltip; ///< "" = the label
    QString tone;    ///< "" | "accent" | "success" | "danger" | "warning" | "info"
    QString command; ///< one of the extension's commands (b.command): run as from the palette
    QString view;    ///< one of the extension's views (b.view), opened with `params`
    QVariantMap params;
    QString page;     ///< a page opened (made when missing, as a link)
    QString run;      ///< a command of the palette ("go.calendar", "theme.toggle", "ext.<id>.<name>")
    QString insert;   ///< slash only: Markdown that replaces the typed "/…"
    double order = 0; ///< smaller first; equal ones in the order contributed
    bool operator==(const Contribution &other) const = default;
};

inline constexpr int MaxContributionLabel = 100;
inline constexpr int MaxContributionTooltip = 300;
inline constexpr int MaxContributionInsert = 20000;
/// Items of one extension in one slot (more is an error), and of every extension in one slot.
inline constexpr int MaxContributionsPerSlot = 16;
inline constexpr int MaxContributionsInSlot = 64;

/// An item table of b.app.contribute in `slot` as a Contribution (its extension left empty): `id`
/// (letters, digits, - and _, at most 64), `label`, `icon`, `tooltip`, `tone`, `order` and one
/// of `command`, `view` (+ `params`), `page`, `run`, `insert` — the slot decides which it needs.
/// Unknown keys are errors (a misspelt `comand` must not pass silently).
bool contributionFrom(const QString &slot, const QVariantMap &item, Contribution *out, QString *error);
/// A Contribution as a table (b.app.contributions(), the remote control's `contributions`).
QVariantMap contributionTable(const Contribution &item);

// ---- Input rules: text replaced as it is typed (b.app.inputRule) ----------------------------------

/// A replacement the block editor makes as the user types (`->` → `→`): declared once, kept by the
/// app, applied without Luau (docs/EXTENSIONS.md "Input rules"). The editor's matcher:
/// src/app/editor/inputrules.h.
struct InputRule {
    QString extension;
    QString match;     ///< the text just typed before the caret: 1 … MaxInputRuleMatch characters, one line
    QString replace;   ///< what it becomes: at most MaxInputRuleReplace characters, one line ("" removes it)
    bool word = false; ///< a whole word only: replaced when a character that ends a word follows it
    bool operator==(const InputRule &other) const = default;
};

inline constexpr int MaxInputRuleMatch = 32;
inline constexpr int MaxInputRuleReplace = 200;
inline constexpr int MaxInputRulesPerExtension = 64;
inline constexpr int MaxInputRules = 256;

/// A table of b.app.inputRule ({ match, replace, word? }) as an InputRule (its extension left
/// empty): `match` a non-empty text of one line with no surrounding spaces when `word`, `replace`
/// one line, `word` a boolean. Unknown fields are errors.
bool inputRuleFrom(const QVariantMap &table, InputRule *out, QString *error);
/// An InputRule as a table (b.app.inputRules()): {extension, match, replace, word}.
QVariantMap inputRuleTable(const InputRule &rule);

// ---- Styles of blocks by tag (b.app.tagStyle) -------------------------------------------------------

/// Blocks carrying a tag drawn tinted (`#pilne` on a red band): the editor gives such a block a
/// background and a bar at its left edge in the tint, in the light and the dark theme.
struct TagStyle {
    QString extension;
    QString tag;      ///< as written after '#', NFC ("pilne", "Projekt/Dom", "Jan Kowalski")
    QString tint;     ///< one of tagTints() (the theme's colours) or "#rrggbb"
    bool fill = true; ///< the tinted background
    bool bar = true;  ///< the bar at the left edge
    bool operator==(const TagStyle &other) const = default;
};

inline constexpr int MaxTagStylesPerExtension = 64;
inline constexpr int MaxTagStyles = 256;

/// The tints that follow the theme: "accent", "success", "danger", "warning", "info", "neutral".
QStringList tagTints();
/// A tag as tag styles compare it: without '#' (and `[[…]]`), NFC, lower case; "" when no tag
/// can be written so (docs/SYNTAX.md §3).
QString tagStyleKey(const QString &tag);
/// A table of b.app.tagStyle ({ tag, tint, fill?, bar? }) as a TagStyle (its extension left empty):
/// `tag` a tag's name ("pilne", "#pilne", "#[[Jan Kowalski]]"), `tint` one of tagTints() or a
/// colour "#rgb" / "#rrggbb" (kept as "#rrggbb"; no alpha), `fill` / `bar` booleans, not both false.
bool tagStyleFrom(const QVariantMap &table, TagStyle *out, QString *error);
/// A TagStyle as a table (b.app.tagStyles()): {extension, tag, tint, fill, bar}.
QVariantMap tagStyleTable(const TagStyle &style);

// ---- The app's operations (Host::app) -------------------------------------------------------------

/// The operations b.app asks of the app through Host::app(), each with its arguments:
///   state                       → {view, route, page?, path?, date?, sidePanel, platform, touch,
///                                  narrow, dark, width, height, language, version}
///   commands                    → [{id, title, subtitle, shortcut, enabled}]
///   settings                    → [{key, kind, value, options?, min?, max?, label, group}]
///   get {key}                   → the value
///   set {key, value}            → nothing (applied at once)
///   navigate {route}            → nothing (shown after the call)
///   open {page, line?, side?}   → nothing
///   back                        → nothing
///   run {id}                    → nothing (run after the call)
///   toast {text, tone?, button?: {label, command}} → nothing
/// The ones that change the app need the permission "app" and are refused while the extension
/// loads and in macros (isAppChange()).
QStringList appOperations();
bool isAppChange(QStringView operation);

} // namespace brulion::extensions
