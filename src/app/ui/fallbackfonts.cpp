#include "fallbackfonts.h"

#include <QFontDatabase>
#include <QFontInfo>
#include <QGlyphRun>
#include <QJsonArray>
#include <QTextLayout>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

const QString EmojiFamily = u"Brulion Emoji"_s;
const QString MathFamily = u"Brulion Math"_s;
// Resources of brulion_ui_fonts (src/app/ui/CMakeLists.txt), made by scripts/fallback-fonts.py.
const QString EmojiResource = u":/fonts/BrulionEmoji.ttf"_s;
const QString MathResource = u":/fonts/BrulionMath.otf"_s;

/// True when every glyph of `text` laid out in `font` (with its fallbacks) is a real glyph.
bool drawsAll(const QString &text, const QFont &font)
{
    QTextLayout layout(text, font);
    layout.setCacheEnabled(false);
    layout.beginLayout();
    layout.createLine();
    layout.endLayout();
    const QList<QGlyphRun> runs = layout.glyphRuns();
    if (runs.isEmpty())
        return false;
    for (const QGlyphRun &run : runs) {
        // Glyph 0 is .notdef in every TrueType/OpenType font: the box Qt draws when no font of
        // the fallback chain has the character.
        if (run.glyphIndexes().contains(0u))
            return false;
    }
    return true;
}

/// Pins the families Brulion's text asks for to the fonts Qt resolves them to now. A name that
/// is no installed family — "serif" in the browser (Theme.fontSerif), fontconfig's generic names,
/// Qt's own default "Sans Serif" under a bare fontconfig — gets its font from Qt's fallback pass,
/// which tries the substitutes and then the application fallback families first: without the
/// pin, "serif" text would get Brulion Math as its font (and its line metrics).
void pinRequestedFamilies(const QFont &font)
{
    QStringList names = {font.family(), QFontDatabase::systemFont(QFontDatabase::GeneralFont).family(),
                         QFontDatabase::systemFont(QFontDatabase::FixedFont).family(),
                         // Theme.fontSerif, the spans of rendered formulas (core/markdown/math.cpp).
                         u"serif"_s, u"sans-serif"_s, u"monospace"_s};
    names.removeDuplicates();
    for (const QString &name : std::as_const(names)) {
        if (name.isEmpty())
            continue;
        const QString resolved = QFontInfo(QFont(name)).family();
        if (!resolved.isEmpty() && resolved.compare(name, Qt::CaseInsensitive) != 0
            && !QFont::substitutes(name).contains(resolved))
            QFont::insertSubstitution(name, resolved);
    }
}

/// Loads a bundled font; false when this build has none (Android) or it is not the font.
bool load(const QString &resource, const QString &family)
{
    const int id = QFontDatabase::addApplicationFont(resource);
    if (id >= 0 && QFontDatabase::applicationFontFamilies(id).contains(family))
        return true;
    if (id >= 0)
        QFontDatabase::removeApplicationFont(id);
    return false;
}

} // namespace

QString brulionEmoji()
{
    // Reminders and alarms (⏰ ⏰! 🔔), the task fields of the Tasks-plugin syntax (docs/SYNTAX.md):
    // dates, recurrence, priorities, done/cancelled, ids and dependencies, created; attachments
    // and images in rendered notes. tst_fallback_fonts checks that src/ uses no other emoji.
    return u"⏰🔔📅⏳🛫✅❌🔁🔂🔺⏫🔼🔽⏬➕🆔🏁📆📎🖼🗓⛔⌛"_s;
}

QString brulionSymbols()
{
    return u"⧉⌢⌣⎰⎱⟮⟯⨄⨆⨿𝒜"_s;
}

QString missingCharacters(const QString &text, const QFont &font)
{
    // One layout of the whole text first: the usual answer, nothing, costs a single pass.
    if (text.trimmed().isEmpty() || drawsAll(text, font))
        return {};
    QString missing;
    for (qsizetype i = 0; i < text.size();) {
        const qsizetype length =
                text.at(i).isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate() ? 2 : 1;
        const QString character = text.mid(i, length);
        i += length;
        if (character.trimmed().isEmpty() || missing.contains(character))
            continue;
        if (!drawsAll(character, font))
            missing += character;
    }
    return missing;
}

QList<FallbackFontReport> installFallbackFonts(FallbackFontPolicy policy, const QFont &font)
{
    static bool done = false;
    static QList<FallbackFontReport> reports;
    if (done)
        return reports;
    done = true;

    FallbackFontReport emoji;
    emoji.family = EmojiFamily;
    emoji.probe = brulionEmoji();
    FallbackFontReport math;
    math.family = MathFamily;
    math.probe = brulionSymbols();

    // The emoji decide (Linux: a system emoji font must keep drawing them). The symbols are not
    // looked for first: that costs ~15 ms of start-up (fontconfig opens font after font), and a
    // system with an emoji font has them too.
    emoji.checked = true;
    emoji.missingBefore = missingCharacters(emoji.probe, font);
    emoji.missingAfter = emoji.missingBefore;
    if (policy == FallbackFontPolicy::WhenMissing && emoji.missingBefore.isEmpty()) {
        reports = {emoji, math};
        return reports;
    }

    const QString defaultFamily = QFontInfo(font).family();
    pinRequestedFamilies(font);
    emoji.loaded = load(EmojiResource, EmojiFamily);
    math.loaded = load(MathResource, MathFamily);
    // Common text (Qt files Latin under it, and gives a symbol the script of the text around it:
    // "Spotkanie ⏰ jutro" is one Latin run): [default family, Brulion Emoji, Brulion Math, the
    // system's fallbacks…]. The default family first: a family name Qt cannot find gets it, as
    // before, never one of these. In the browser the same for every other script: its font
    // database has no fallbacks beyond DejaVu Sans, so 🗓 or ⧉ in Cyrillic or Greek text
    // ("Встреча 🗓") stayed a box; fontconfig (Linux) already tries every font for those, ours
    // included, and keeps its own order there.
    QStringList chain;
    if (!defaultFamily.isEmpty() && defaultFamily != EmojiFamily && defaultFamily != MathFamily)
        chain.append(defaultFamily);
    for (FallbackFontReport *report : {&emoji, &math}) {
        if (report->loaded) {
            chain.append(report->family);
            report->installed = true;
        }
    }
    if (emoji.installed || math.installed) {
        const int lastScript = policy == FallbackFontPolicy::Always ? QChar::ScriptCount - 1 : QChar::Script_Common;
        for (int script = QChar::Script_Common; script <= lastScript; ++script) {
            if (script == QChar::Script_Latin)
                continue; // the same list as Common
            const auto s = QChar::Script(script);
            QFontDatabase::setApplicationFallbackFontFamilies(
                    s, chain + QFontDatabase::applicationFallbackFontFamilies(s));
        }
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    // Qt >= 6.9 lays out emoji sequences as runs of their own (script "emoji") whose fallbacks are
    // the emoji families, colour fonts first: a colour emoji font of the system would still win.
    if (emoji.installed) {
        QFontDatabase::addApplicationEmojiFontFamily(EmojiFamily);
        emoji.emojiFamily = true;
    }
#endif
    for (FallbackFontReport *report : {&emoji, &math}) {
        if (report->installed)
            report->missingAfter = missingCharacters(report->probe, font);
    }
    reports = {emoji, math};
    return reports;
}

QJsonObject fallbackFontsJson(const QList<FallbackFontReport> &reports)
{
    bool ok = !reports.isEmpty();
    QJsonArray fonts;
    for (const FallbackFontReport &report : reports) {
        ok = ok && report.missingAfter.isEmpty();
        fonts.append(QJsonObject{
                {u"family"_s, report.family},
                {u"probe"_s, report.probe},
                {u"missingBefore"_s, report.missingBefore},
                {u"missingAfter"_s, report.missingAfter},
                {u"checked"_s, report.checked},
                {u"installed"_s, report.installed},
                {u"emojiFamily"_s, report.emojiFamily},
                {u"loaded"_s, report.loaded},
        });
    }
    return QJsonObject{{u"ok"_s, ok}, {u"fonts"_s, fonts}};
}

} // namespace brulion::ui
