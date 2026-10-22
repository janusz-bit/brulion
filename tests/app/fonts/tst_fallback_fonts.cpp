// The fallback fonts (src/app/ui/fallbackfonts.h): "Brulion Emoji" and "Brulion Math" cover
// every emoji and symbol Brulion writes or shows — also any that comes into src/ later (the
// sources are scanned) —, missingCharacters() finds what the fonts draw as boxes (the browser's
// "white empty squares" instead of ⏰ 🔔 📅), and installFallbackFonts() makes the boxes go away
// while ordinary text, Polish or Cyrillic, keeps its own font, and no family Brulion asks for
// ("serif", the default font, an unknown name) gets one of these as its font. ctest runs it with
// the system's fonts and, as tst_fallback_fonts_browser_fonts, with only the browser's (DejaVu
// Sans and DejaVu Sans Mono, BRULION_TEST_BROWSER_FONTS=1).
#include "fallbackfonts.h"

#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGlyphRun>
#include <QGuiApplication>
#include <QJsonArray>
#include <QRawFont>
#include <QTest>
#include <QTextLayout>

#include <utility>

using namespace Qt::StringLiterals;
using namespace brulion::ui;

namespace {

const QString EmojiResource = u":/fonts/BrulionEmoji.ttf"_s;
const QString MathResource = u":/fonts/BrulionMath.otf"_s;
const QStringList OurFamilies = {u"Brulion Emoji"_s, u"Brulion Math"_s};
const QString NoSuchFamily = u"Brulion Test No Such Family"_s;

/// Emoji to Qt's emoji segmentation and to Unicode: the BMP code points with
/// Emoji_Presentation=Yes (emoji-data.txt) and the pictographs of U+1F000–U+1FAFF.
bool isEmoji(char32_t cp)
{
    static const std::pair<char32_t, char32_t> bmp[] = {
            {0x231A, 0x231B}, {0x23E9, 0x23EC}, {0x23F0, 0x23F0}, {0x23F3, 0x23F3}, {0x25FD, 0x25FE}, {0x2614, 0x2615},
            {0x2648, 0x2653}, {0x267F, 0x267F}, {0x2693, 0x2693}, {0x26A1, 0x26A1}, {0x26AA, 0x26AB}, {0x26BD, 0x26BE},
            {0x26C4, 0x26C5}, {0x26CE, 0x26CE}, {0x26D4, 0x26D4}, {0x26EA, 0x26EA}, {0x26F2, 0x26F3}, {0x26F5, 0x26F5},
            {0x26FA, 0x26FA}, {0x26FD, 0x26FD}, {0x2705, 0x2705}, {0x270A, 0x270B}, {0x2728, 0x2728}, {0x274C, 0x274C},
            {0x274E, 0x274E}, {0x2753, 0x2755}, {0x2757, 0x2757}, {0x2795, 0x2797}, {0x27B0, 0x27B0}, {0x27BF, 0x27BF},
            {0x2B1B, 0x2B1C}, {0x2B50, 0x2B50}, {0x2B55, 0x2B55},
    };
    if (cp >= 0x1F000 && cp <= 0x1FAFF)
        return true;
    for (const auto &[first, last] : bmp) {
        if (cp >= first && cp <= last)
            return true;
    }
    return false;
}

QString text(char32_t cp)
{
    return QString::fromUcs4(&cp, 1);
}

QString hex(char32_t cp)
{
    return u"U+"_s + QString::number(uint(cp), 16).toUpper().rightJustified(4, u'0');
}

QRawFont bundledFont(const QString &resource, qreal pixelSize = 16)
{
    QFile file(resource);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QRawFont(file.readAll(), pixelSize);
}

/// Every emoji in Brulion's sources (src/: C++, QML, JavaScript, the user guide) with the first
/// place it appears ("src/core/markdown/tokens.cpp:95").
QList<std::pair<char32_t, QString>> emojiInSources()
{
    QList<std::pair<char32_t, QString>> found;
    QSet<char32_t> seen;
    const QString root = QStringLiteral(BRULION_SOURCE_DIR);
    QStringList files;
    QDirIterator it(root + u"/src"_s, {u"*.cpp"_s, u"*.h"_s, u"*.qml"_s, u"*.js"_s, u"*.md"_s}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        files.append(it.next());
    files.sort();
    for (const QString &path : std::as_const(files)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QStringList lines = QString::fromUtf8(file.readAll()).split(u'\n');
        for (qsizetype line = 0; line < lines.size(); ++line) {
            for (const char32_t cp : lines.at(line).toUcs4()) {
                if (isEmoji(cp) && !seen.contains(cp)) {
                    seen.insert(cp);
                    found.append({cp, u"%1:%2"_s.arg(path.mid(root.size() + 1)).arg(line + 1)});
                }
            }
        }
    }
    return found;
}

/// Per character of `text` laid out in `font`: the family of the font that draws it, and
/// whether that is a box (glyph 0).
struct Drawn {
    QString family;
    bool box = false;
};

QList<Drawn> drawnCharacters(const QString &text, const QFont &font)
{
    QList<Drawn> drawn(text.size());
    QTextLayout layout(text, font);
    layout.beginLayout();
    layout.createLine();
    layout.endLayout();
    const auto runs = layout.glyphRuns(-1, -1, QTextLayout::RetrieveGlyphIndexes | QTextLayout::RetrieveStringIndexes);
    for (const QGlyphRun &run : runs) {
        const QList<quint32> glyphs = run.glyphIndexes();
        const QList<qsizetype> indexes = run.stringIndexes();
        for (qsizetype i = 0; i < glyphs.size() && i < indexes.size(); ++i) {
            const qsizetype at = indexes.at(i);
            if (at < 0 || at >= drawn.size())
                continue;
            drawn[at].family = run.rawFont().familyName();
            drawn[at].box = drawn[at].box || glyphs.at(i) == 0;
        }
    }
    return drawn;
}

qreal lineHeight(const QString &text, const QFont &font)
{
    QTextLayout layout(text, font);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    layout.endLayout();
    return line.ascent() + line.descent();
}

/// The fonts Brulion's text asks for: the default (UI) font, Theme.fontSerif, the fixed font,
/// the generic names of rendered formulas, and a family that exists nowhere.
QList<std::pair<QString, QFont>> requestedFonts()
{
    QFont serif(u"serif"_s);
    serif.setStyleHint(QFont::Serif);
    return {
            {u"default"_s, QGuiApplication::font()},
            {u"serif"_s, serif},
            {u"fixed"_s, QFontDatabase::systemFont(QFontDatabase::FixedFont)},
            {u"monospace"_s, QFont(u"monospace"_s)},
            {u"sans-serif"_s, QFont(u"sans-serif"_s)},
            {u"unknown"_s, QFont(NoSuchFamily)},
    };
}

bool hasFonts()
{
    return !QFontDatabase::families().isEmpty();
}

bool browserFonts()
{
    return qEnvironmentVariableIntValue("BRULION_TEST_BROWSER_FONTS") == 1;
}

} // namespace

class TstFallbackFonts : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        if (browserFonts()) {
            // The browser's set: DejaVu only (fontconfig adds its generic names), so the emoji
            // are boxes there.
            const QStringList families = QFontDatabase::families();
            QVERIFY2(families.contains(u"DejaVu Sans"_s) && families.contains(u"DejaVu Sans Mono"_s),
                     qPrintable(families.join(u", "_s)));
            for (const QString &family : families) {
                QVERIFY2(family.startsWith(u"DejaVu Sans"_s) || family == u"Serif"_s || family == u"Sans Serif"_s
                                 || family == u"Monospace"_s,
                         qPrintable(families.join(u", "_s)));
            }
            QCOMPARE(brulion::ui::missingCharacters(u"⏰🔔📅"_s, QGuiApplication::font()), u"⏰🔔📅"_s);
        }
    }

    void bundledFonts_data()
    {
        QTest::addColumn<QString>("resource");
        QTest::addColumn<QString>("family");
        QTest::addColumn<QString>("probe");
        QTest::newRow("emoji") << EmojiResource << u"Brulion Emoji"_s << brulionEmoji();
        QTest::newRow("math") << MathResource << u"Brulion Math"_s << brulionSymbols();
    }

    /// Both subsets load, carry their own family names, have every character of their probe, and
    /// DejaVu Sans's vertical metrics (0.928 / 0.236 em), so a fallback glyph never makes a line
    /// taller (Noto Sans Math's are half as tall again).
    void bundledFonts()
    {
        QFETCH(QString, resource);
        QFETCH(QString, family);
        QFETCH(QString, probe);
        const QRawFont font = bundledFont(resource, 1000);
        QVERIFY2(font.isValid(), qPrintable(resource));
        QCOMPARE(font.familyName(), family);
        QVERIFY(!probe.isEmpty());
        for (const char32_t cp : probe.toUcs4())
            QVERIFY2(font.supportsCharacter(cp), qPrintable(u"%1 %2 not in %3"_s.arg(hex(cp), text(cp), family)));
        QVERIFY2(qAbs(font.ascent() - 928) <= 1, qPrintable(QString::number(font.ascent())));
        QVERIFY2(qAbs(font.descent() - 236) <= 1, qPrintable(QString::number(font.descent())));
    }

    /// The two probes are distinct characters without whitespace (the "drawn as boxes" checks
    /// compare against them) and do not overlap.
    void probesAreDistinct()
    {
        const QList<uint> emoji = brulionEmoji().toUcs4();
        const QList<uint> symbols = brulionSymbols().toUcs4();
        QCOMPARE(QSet<uint>(emoji.cbegin(), emoji.cend()).size(), emoji.size());
        QCOMPARE(QSet<uint>(symbols.cbegin(), symbols.cend()).size(), symbols.size());
        QVERIFY(!QSet<uint>(emoji.cbegin(), emoji.cend()).intersects(QSet<uint>(symbols.cbegin(), symbols.cend())));
        QCOMPARE(brulionEmoji().count(u' '), 0);
    }

    void sourcesUseOnlyProbedEmoji_data()
    {
        QTest::addColumn<uint>("codePoint");
        QTest::addColumn<QString>("where");
        const auto found = emojiInSources();
        QVERIFY2(found.size() >= 15, "the scan of src/ found too few emoji: BRULION_SOURCE_DIR wrong?");
        for (const auto &[cp, where] : found)
            QTest::addRow("%s %s", qPrintable(hex(cp)), qPrintable(text(cp))) << uint(cp) << where;
    }

    /// Every emoji of src/ is in brulionEmoji() (so the start-up check looks for it) and in
    /// "Brulion Emoji" — a new one needs scripts/fallback-fonts.py and the probe updated.
    void sourcesUseOnlyProbedEmoji()
    {
        QFETCH(uint, codePoint);
        QFETCH(QString, where);
        const QString ch = text(codePoint);
        QVERIFY2(
                brulionEmoji().contains(ch),
                qPrintable(u"%1 (%2) is missing from brulionEmoji() in src/app/ui/fallbackfonts.cpp"_s.arg(ch, where)));
        QVERIFY2(bundledFont(EmojiResource).supportsCharacter(codePoint),
                 qPrintable(u"%1 (%2) is missing from Brulion Emoji: run scripts/fallback-fonts.py"_s.arg(ch, where)));
    }

    void missingCharacters_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("missing");
        // U+10FFFD/U+10FFFC: private use characters of plane 16, which no font has.
        const QString none1 = text(0x10FFFD);
        const QString none2 = text(0x10FFFC);
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("whitespace") << u" \t "_s << QString();
        QTest::newRow("polish") << u"Zażółć gęślą jaźń ĄĆĘŁŃÓŚŹŻ"_s << QString();
        QTest::newRow("unknown") << none1 << none1;
        QTest::newRow("each once, in order") << (none2 + u" a "_s + none1 + none2 + u"ż"_s + none1) << (none2 + none1);
    }

    /// missingCharacters() reports exactly the characters drawn as boxes, whole code points, once.
    void missingCharacters()
    {
        QFETCH(QString, input);
        QFETCH(QString, missing);
        if (!hasFonts())
            QSKIP("no fonts on this system");
        QCOMPARE(brulion::ui::missingCharacters(input, QGuiApplication::font()), missing);
    }

    /// The Linux policy: both fonts exactly when the system's fonts draw some of Brulion's emoji
    /// as boxes (a colour emoji font keeps drawing them), and then nothing stays a box. Its own
    /// ctest entries (…_when_missing): fonts are installed once per process.
    void installWhenMissing()
    {
        if (!QCoreApplication::arguments().contains(u"installWhenMissing"_s))
            QSKIP("runs alone (ctest tst_fallback_fonts_when_missing): fonts are installed once per process");
        if (!hasFonts())
            QSKIP("no fonts on this system");
        QElapsedTimer timer;
        timer.start();
        const QList<FallbackFontReport> reports =
                installFallbackFonts(FallbackFontPolicy::WhenMissing, QGuiApplication::font());
        qInfo("check and install: %lld ms", static_cast<long long>(timer.elapsed()));
        QCOMPARE(reports.size(), 2);
        for (const FallbackFontReport &report : reports) {
            qInfo("%s: checked %d, boxes before \"%s\", installed %d, boxes after \"%s\"", qPrintable(report.family),
                  report.checked, qPrintable(report.missingBefore), report.installed, qPrintable(report.missingAfter));
            QCOMPARE(report.loaded, report.installed);
            QVERIFY2(report.missingAfter.isEmpty(), qPrintable(report.missingAfter));
            QCOMPARE(QFontDatabase::applicationFallbackFontFamilies(QChar::Script_Common).contains(report.family),
                     report.installed);
            // Other scripts keep fontconfig's fallbacks, which try every font (ours included).
            QVERIFY(!QFontDatabase::applicationFallbackFontFamilies(QChar::Script_Cyrillic).contains(report.family));
        }
        const FallbackFontReport &emoji = reports.at(0);
        const FallbackFontReport &math = reports.at(1);
        QCOMPARE(emoji.family, u"Brulion Emoji"_s);
        QCOMPARE(math.family, u"Brulion Math"_s);
        QVERIFY(emoji.checked);
        QVERIFY(!math.checked); // not looked for first: a system with emoji fonts has them
        QCOMPARE(emoji.installed, !emoji.missingBefore.isEmpty());
        QCOMPARE(math.installed, emoji.installed);
        if (browserFonts())
            QVERIFY(emoji.installed);
        QCOMPARE(QFontDatabase::applicationEmojiFontFamilies().contains(emoji.family), emoji.installed);
        QCOMPARE(brulion::ui::missingCharacters(brulionEmoji(), QGuiApplication::font()), QString());
        if (emoji.installed)
            QCOMPARE(brulion::ui::missingCharacters(brulionSymbols(), QGuiApplication::font()), QString());
        QVERIFY(fallbackFontsJson(reports).value(u"ok"_s).toBool());
    }

    /// The browser's policy: both fonts, as fallbacks of Common/Latin text behind the default
    /// family and (Qt >= 6.9) as the emoji family; Brulion's emoji and symbols draw without boxes
    /// in every font it asks for, and each of those fonts stays what it was — an unknown family
    /// gets the default one — never Brulion Emoji or Brulion Math.
    void installAlways()
    {
        QHash<QString, QString> before;
        for (const auto &[name, font] : requestedFonts())
            before.insert(name, QFontInfo(font).family());
        const QString defaultFamily = QFontInfo(QGuiApplication::font()).family();

        const QList<FallbackFontReport> reports =
                installFallbackFonts(FallbackFontPolicy::Always, QGuiApplication::font());
        QCOMPARE(reports.size(), 2);
        QCOMPARE(reports.at(0).family, u"Brulion Emoji"_s);
        QCOMPARE(reports.at(0).probe, brulionEmoji());
        QVERIFY(reports.at(0).checked);
        QCOMPARE(reports.at(1).family, u"Brulion Math"_s);
        QCOMPARE(reports.at(1).probe, brulionSymbols());
        for (const FallbackFontReport &report : reports) {
            QVERIFY(report.loaded);
            QVERIFY(report.installed);
            QVERIFY2(report.missingAfter.isEmpty(), qPrintable(report.family + u": "_s + report.missingAfter));
        }
        const QStringList common = QFontDatabase::applicationFallbackFontFamilies(QChar::Script_Common);
        QCOMPARE(common.mid(0, 3), (QStringList{defaultFamily, u"Brulion Emoji"_s, u"Brulion Math"_s}));
        QCOMPARE(QFontDatabase::applicationFallbackFontFamilies(QChar::Script_Latin), common);
        // Every other script too: the browser's font database has no fallbacks of its own, and a
        // symbol takes the script of the text around it ("Встреча 🗓" is one Cyrillic run).
        for (const QChar::Script script : {QChar::Script_Cyrillic, QChar::Script_Greek, QChar::Script_Hebrew,
                                           QChar::Script_Han, QChar::Script(QChar::ScriptCount - 1)}) {
            QCOMPARE(QFontDatabase::applicationFallbackFontFamilies(script).mid(0, 3),
                     (QStringList{defaultFamily, u"Brulion Emoji"_s, u"Brulion Math"_s}));
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        QVERIFY(reports.at(0).emojiFamily);
        QVERIFY(!reports.at(1).emojiFamily);
        QCOMPARE(QFontDatabase::applicationEmojiFontFamilies(), QStringList{u"Brulion Emoji"_s});
#endif
        const QJsonObject json = fallbackFontsJson(reports);
        QVERIFY(json.value(u"ok"_s).toBool());
        QCOMPARE(json.value(u"fonts"_s).toArray().size(), 2);

        for (const auto &[name, font] : requestedFonts()) {
            const QString family = QFontInfo(font).family();
            const QString expected = name == u"unknown"_s ? defaultFamily : before.value(name);
            QVERIFY2(!OurFamilies.contains(family), qPrintable(name + u" got "_s + family));
            QCOMPARE(family, expected);
            QCOMPARE(brulion::ui::missingCharacters(brulionEmoji() + brulionSymbols() + u"🙂👍❤⭐🎉"_s, font),
                     QString());
        }

        // Once per process: a second call changes nothing and reports the same.
        const QList<FallbackFontReport> again = installFallbackFonts(FallbackFontPolicy::WhenMissing, QFont());
        QCOMPARE(again.size(), 2);
        QCOMPARE(again.at(0).installed, true);
        QCOMPARE(QFontDatabase::applicationFallbackFontFamilies(QChar::Script_Common).count(u"Brulion Emoji"_s), 1);
    }

    void skinTones_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("thumbs up, light") << u"👍🏻"_s;
        QTest::newRow("folded hands, medium") << u"🙏🏽"_s;
        QTest::newRow("waving hand, dark") << u"👋🏿"_s;
        QTest::newRow("raised hand, medium-light") << u"✋🏼"_s;
    }

    /// Phone keyboards add skin tones (👍🏻). Brulion Emoji is monochrome: it draws a toned hand
    /// as the plain hand, a single glyph, with no box (or swatch) after it.
    void skinTones()
    {
        QFETCH(QString, text);
        if (!hasFonts())
            QSKIP("no fonts on this system");
        installFallbackFonts(FallbackFontPolicy::Always, QGuiApplication::font());
        QCOMPARE(brulion::ui::missingCharacters(text, QGuiApplication::font()), QString());
        QTextLayout layout(text, QGuiApplication::font());
        layout.beginLayout();
        layout.createLine();
        layout.endLayout();
        const QList<QGlyphRun> runs = layout.glyphRuns();
        QCOMPARE(runs.size(), 1);
        if (runs.constFirst().rawFont().familyName() == u"Brulion Emoji"_s)
            QCOMPARE(runs.constFirst().glyphIndexes().size(), 1);
    }

    void ordinaryTextKeepsItsFont_data()
    {
        QTest::addColumn<QString>("font");
        QTest::addColumn<QString>("line");
        const QList<std::pair<QString, QString>> lines = {
                {u"reminder"_s, u"Spotkanie z Anią ⏰ jutro 9:00"_s},
                {u"alarm"_s, u"Pobudka 🔔 jutro 07:00"_s},
                {u"task"_s, u"Zadzwonić do hydraulika 📅 2026-10-23 🔁 co tydzień ⏫"_s},
                {u"done"_s, u"Odebrać paczkę ✅ 2026-10-21 🗓 🖼"_s},
                {u"embed"_s, u"⧉ Zażółć gęślą jaźń"_s},
                {u"cyrillic"_s, u"Встреча ⏰ завтра"_s},
        };
        for (const QString &font : {u"default"_s, u"serif"_s, u"fixed"_s}) {
            for (const auto &[name, line] : lines)
                QTest::addRow("%s, %s", qPrintable(font), qPrintable(name)) << font << line;
        }
    }

    /// With the fonts installed, letters still come from the text's own font — only the emoji
    /// and symbols it lacks come from the fallbacks — and nothing is a box.
    void ordinaryTextKeepsItsFont()
    {
        QFETCH(QString, font);
        QFETCH(QString, line);
        if (!hasFonts())
            QSKIP("no fonts on this system");
        installFallbackFonts(FallbackFontPolicy::Always, QGuiApplication::font());
        QFont requested;
        for (const auto &[name, f] : requestedFonts()) {
            if (name == font)
                requested = f;
        }
        const QList<Drawn> drawn = drawnCharacters(line, requested);
        for (qsizetype i = 0; i < line.size(); ++i) {
            const QChar ch = line.at(i);
            if (ch.isLowSurrogate() || ch.isSpace())
                continue;
            const QString what = u"'%1' at %2 drawn by %3"_s.arg(ch.isHighSurrogate() ? line.mid(i, 2) : QString(ch))
                                         .arg(i)
                                         .arg(drawn.at(i).family);
            QVERIFY2(!drawn.at(i).box, qPrintable(what + u" is a box"_s));
            if (ch.isLetterOrNumber())
                QVERIFY2(!OurFamilies.contains(drawn.at(i).family), qPrintable(what));
        }
    }

    /// With the browser's fonts, a line with emoji and symbols from the fallbacks is exactly as
    /// tall as one without (the subsets have DejaVu's vertical metrics).
    void linesKeepTheirHeight()
    {
        if (!browserFonts())
            QSKIP("needs the browser's fonts (ctest tst_fallback_fonts_browser_fonts)");
        installFallbackFonts(FallbackFontPolicy::Always, QGuiApplication::font());
        for (const auto &[name, font] : requestedFonts()) {
            const qreal plain = lineHeight(u"Zażółć gęślą jaźń"_s, font);
            const qreal mixed = lineHeight(u"Zażółć ⏰ 🔔 📅 ✅ 🗓 ⧉ 𝒜 jaźń"_s, font);
            QVERIFY2(mixed <= plain + 0.5, qPrintable(u"%1: %2 > %3"_s.arg(name).arg(mixed).arg(plain)));
        }
    }

    void json_data()
    {
        QTest::addColumn<QList<FallbackFontReport>>("reports");
        QTest::addColumn<bool>("ok");
        FallbackFontReport fine;
        fine.family = u"Brulion Emoji"_s;
        fine.probe = u"⏰"_s;
        fine.missingBefore = u"⏰"_s;
        fine.checked = fine.installed = fine.loaded = true;
        FallbackFontReport notNeeded;
        notNeeded.family = u"Brulion Math"_s;
        notNeeded.probe = u"⧉"_s;
        FallbackFontReport broken = fine;
        broken.missingAfter = u"⏰"_s;
        QTest::newRow("installed") << QList<FallbackFontReport>{fine, notNeeded} << true;
        QTest::newRow("boxes left") << QList<FallbackFontReport>{fine, broken} << false;
        QTest::newRow("nothing checked") << QList<FallbackFontReport>{} << false;
    }

    /// The self-test report of the browser (?selftest=glyphs): ok only when no box is left.
    void json()
    {
        QFETCH(QList<FallbackFontReport>, reports);
        QFETCH(bool, ok);
        const QJsonObject json = fallbackFontsJson(reports);
        QCOMPARE(json.value(u"ok"_s).toBool(), ok);
        const QJsonArray fonts = json.value(u"fonts"_s).toArray();
        QCOMPARE(fonts.size(), reports.size());
        for (qsizetype i = 0; i < fonts.size(); ++i) {
            const QJsonObject font = fonts.at(i).toObject();
            QCOMPARE(font.value(u"family"_s).toString(), reports.at(i).family);
            QCOMPARE(font.value(u"missingBefore"_s).toString(), reports.at(i).missingBefore);
            QCOMPARE(font.value(u"missingAfter"_s).toString(), reports.at(i).missingAfter);
            QCOMPARE(font.value(u"checked"_s).toBool(), reports.at(i).checked);
            QCOMPARE(font.value(u"installed"_s).toBool(), reports.at(i).installed);
        }
    }
};

QTEST_MAIN(TstFallbackFonts)
#include "tst_fallback_fonts.moc"
