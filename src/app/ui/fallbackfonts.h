// Fallback fonts (docs/UI.md "Emoji and symbol fonts"): "Brulion Emoji" and "Brulion Math"
// (assets/fonts, subsets of Noto Emoji and Noto Sans Math, SIL OFL 1.1, made by
// scripts/fallback-fonts.py). They draw the emoji and the few symbols Brulion writes or shows
// where the system has no font for them: in the browser build — Qt for WebAssembly brings only
// DejaVu Sans and DejaVu Sans Mono, so every ⏰ 🔔 📅 ✅ was an empty box — and on Linux systems
// without an emoji font (the symbols also with one). Android always has an emoji font and never
// loads them (the APK leaves them out).
//
// Installed once at start-up, before any text is laid out: the application emoji family (Qt >=
// 6.9 lays out emoji runs with it) and fallback families of Latin/Common text (in the browser of
// every script), behind the app's default family. Text keeps its own font; only the characters
// it lacks come from these. Qt also picks the font of a family name that is no installed family
// ("serif" in the browser, fontconfig's generic names) from those fallbacks, so the families
// Brulion asks for are first pinned to the font Qt resolves them to (QFont::insertSubstitution),
// and the default family heads the list: no text ever gets Brulion Emoji as its font. A colour
// emoji font of the system stays in charge — FallbackFontPolicy::WhenMissing then only adds
// Brulion Math as an application font, which fontconfig's fallbacks take for ⧉ and the math
// symbols where no system font has them.
#pragma once

#include <QFont>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace brulion::ui {

enum class FallbackFontPolicy {
    Always, ///< install both fonts, for every script (the browser, where the system has none)
    /// Linux: install both only when the system draws some of brulionEmoji() as boxes; otherwise
    /// only load Brulion Math as an application font (fontconfig's fallbacks use it where no
    /// system font has a symbol), without fallback families or pins.
    WhenMissing,
};

/// What happened to one bundled font.
struct FallbackFontReport {
    QString family;           ///< "Brulion Emoji", "Brulion Math"
    QString probe;            ///< the characters it is for: brulionEmoji(), brulionSymbols()
    QString missingBefore;    ///< probe characters drawn as boxes before (when `checked`)
    QString missingAfter;     ///< … and afterwards: empty when the fallback works
    bool checked = false;     ///< the probe was laid out before installing (the emoji font's)
    bool installed = false;   ///< an application fallback family (Common/Latin; the browser: all)
    bool emojiFamily = false; ///< also the application emoji family (Qt >= 6.9)
    bool loaded = false;      ///< an application font (false: not needed, or not in this build)
};

/// The emoji Brulion writes into notes or shows (reminders, alarms, the task fields):
/// ⏰ 🔔 📅 ⏳ 🛫 ✅ ❌ 🔁 🔂 🔺 ⏫ 🔼 🔽 ⏬ ➕ 🆔 🏁 📆 📎 🖼 🗓 ⛔ ⌛ — "Brulion Emoji" has them all.
QString brulionEmoji();

/// The other characters of src/ that DejaVu Sans lacks — ⧉ of block embeds, rare LaTeX symbols
/// of formulas (⌢ ⌣ ⎰ ⎱ ⟮ ⟯ ⨄ ⨆ ⨿) and the 𝒜 of a comment — in "Brulion Math".
QString brulionSymbols();

/// The characters of `text` (whole code points, each once, in order) that `font` and its
/// fallbacks draw as the missing-glyph box — what Qt Quick's Text would show. Whitespace is
/// skipped. Needs a QGuiApplication.
QString missingCharacters(const QString &text, const QFont &font);

/// Installs the bundled fallback fonts by `policy`; `font` is the app's default font (the
/// checks, the family the fallbacks start with). Runs once per process: later calls return the
/// first call's reports.
QList<FallbackFontReport> installFallbackFonts(FallbackFontPolicy policy, const QFont &font);

/// The reports as JSON for the browser self-test (?selftest=glyphs, scripts/wasm-smoke.mjs):
/// `{ok, fonts: [{family, probe, missingBefore, missingAfter, checked, installed, emojiFamily,
/// loaded}]}`; `ok`: no probe character is left drawn as a box.
QJsonObject fallbackFontsJson(const QList<FallbackFontReport> &reports);

} // namespace brulion::ui
