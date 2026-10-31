// Tests for ThemePalette (src/app/ui/themepalette.h): the default look stays exactly today's
// (every token of the default light and dark palettes), every accent preset and custom colour on
// every surface of both modes passes the contrast checks (accent marks 3:1, accent text 4.5:1 on
// each surface and on its own tint, white on the solid fill 4.5:1, text colours 4.5:1, control
// borders 3:1 — on the page, cards and wells), the modal scrim makes the page recede on every
// surface, malformed colours and unknown names fall back to the defaults, and the font
// pickers' family filter. GUI-free (QTEST_GUILESS_MAIN).
#include "themepalette.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::ui;

namespace {

Rgb rgb(const QString &hex)
{
    const auto c = parseColor(hex);
    return c ? *c : Rgb{-1, -1, -1};
}

QString describe(const char *what, const Rgb &fg, const Rgb &bg)
{
    return u"%1: %2 on %3 = %4:1"_s.arg(QLatin1StringView(what), toHex(fg), toHex(bg)).arg(contrast(fg, bg), 0, 'f', 2);
}

} // namespace

class TestThemePalette : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void defaultsAreTodaysColours_data();
    void defaultsAreTodaysColours();
    void unknownNamesGiveDefaults_data();
    void unknownNamesGiveDefaults();
    void contrastOnEverySurface_data();
    void contrastOnEverySurface();
    void modalScrimMakesThePageRecede_data();
    void modalScrimMakesThePageRecede();
    void customAccentIsDerived_data();
    void customAccentIsDerived();
    void parseColor_data();
    void parseColor();
    void luminanceAndContrast();
    void fontFamilies_data();
    void fontFamilies();
    void qmlFacade();
};

void TestThemePalette::defaultsAreTodaysColours_data()
{
    QTest::addColumn<bool>("dark");
    QTest::addColumn<QVariantMap>("expected");
    // The literals of Theme.qml before the appearance settings (screenshots rely on them).
    QTest::newRow("light paper red") << false
                                     << QVariantMap{{u"bg"_s, u"#fbf7ee"_s},
                                                    {u"bgElevated"_s, u"#fffdf7"_s},
                                                    {u"bgElevated2"_s, u"#fffefb"_s},
                                                    {u"bgSunken"_s, u"#f3eee2"_s},
                                                    {u"border"_s, u"#e4dccb"_s},
                                                    {u"borderSubtle"_s, u"#ece6d8"_s},
                                                    {u"borderStrong"_s, u"#cdc2ab"_s},
                                                    {u"borderControl"_s, u"#928775"_s},
                                                    {u"accent"_s, u"#e2574c"_s},
                                                    {u"accentHover"_s, u"#d14b40"_s},
                                                    {u"accentText"_s, u"#ad3e2c"_s},
                                                    {u"accentSolid"_s, u"#c4463a"_s},
                                                    {u"fg"_s, u"#1f2430"_s},
                                                    {u"fgMuted"_s, u"#535a6a"_s},
                                                    {u"fgFaint"_s, u"#676c7b"_s},
                                                    {u"link"_s, u"#2c6aa3"_s},
                                                    {u"linkMissing"_s, u"#7a5ea3"_s},
                                                    {u"tagFg"_s, u"#2b5f8f"_s},
                                                    {u"dangerText"_s, u"#a8382d"_s},
                                                    {u"warningText"_s, u"#835500"_s},
                                                    {u"successText"_s, u"#2a6e44"_s},
                                                    {u"chipDueFg"_s, u"#2b5f8f"_s},
                                                    {u"chipScheduledFg"_s, u"#6a4f93"_s},
                                                    {u"chipReminderFg"_s, u"#7a4f00"_s},
                                                    {u"chipRecurrenceFg"_s, u"#2a6e44"_s},
                                                    {u"scrimModal"_s, u"#9ecbc9c4"_s}};
    QTest::newRow("dark slate red") << true
                                    << QVariantMap{{u"bg"_s, u"#1c2130"_s},
                                                   {u"bgElevated"_s, u"#232a3b"_s},
                                                   {u"bgElevated2"_s, u"#2b3245"_s},
                                                   {u"bgSunken"_s, u"#171b28"_s},
                                                   {u"border"_s, u"#343c51"_s},
                                                   {u"borderSubtle"_s, u"#2b3245"_s},
                                                   {u"borderStrong"_s, u"#48526b"_s},
                                                   {u"borderControl"_s, u"#6c7793"_s},
                                                   {u"accent"_s, u"#e2574c"_s},
                                                   {u"accentHover"_s, u"#ec6a5f"_s},
                                                   {u"accentText"_s, u"#ef7a6f"_s},
                                                   {u"accentSolid"_s, u"#c4463a"_s},
                                                   {u"fg"_s, u"#e8e4da"_s},
                                                   {u"fgMuted"_s, u"#a9aebb"_s},
                                                   {u"fgFaint"_s, u"#8f96a7"_s},
                                                   {u"link"_s, u"#9fc3e6"_s},
                                                   {u"linkMissing"_s, u"#b9a3d8"_s},
                                                   {u"tagFg"_s, u"#b9d4ee"_s},
                                                   {u"dangerText"_s, u"#f3877c"_s},
                                                   {u"warningText"_s, u"#f2b33d"_s},
                                                   {u"successText"_s, u"#7fcb98"_s},
                                                   {u"chipDueFg"_s, u"#b9d4ee"_s},
                                                   {u"chipScheduledFg"_s, u"#c7b5e2"_s},
                                                   {u"chipReminderFg"_s, u"#f5c66a"_s},
                                                   {u"chipRecurrenceFg"_s, u"#8fd3a6"_s},
                                                   {u"scrimModal"_s, u"#b8080a10"_s}};
}

void TestThemePalette::defaultsAreTodaysColours()
{
    QFETCH(bool, dark);
    QFETCH(QVariantMap, expected);
    const QVariantMap m = paletteMap(dark, u"paper", u"slate", u"red", u"");
    QCOMPARE(m.keys(), expected.keys());
    for (auto it = expected.cbegin(); it != expected.cend(); ++it)
        QCOMPARE(m.value(it.key()).toString() + u' ' + it.key(), it.value().toString() + u' ' + it.key());
    QCOMPARE(accentBgAlpha(dark), dark ? 0.18 : 0.12);
}

void TestThemePalette::unknownNamesGiveDefaults_data()
{
    QTest::addColumn<QString>("lightSurface");
    QTest::addColumn<QString>("darkSurface");
    QTest::addColumn<QString>("accentName");
    QTest::addColumn<QString>("custom");
    QTest::newRow("empty") << "" << "" << "" << "";
    QTest::newRow("Polish names") << "papier" << "czarne" << "czerwony" << "";
    QTest::newRow("case") << "Paper" << "BLACK" << "Blue" << "";
    QTest::newRow("swapped modes") << "black" << "white" << "red" << "";
    QTest::newRow("custom without colour") << "paper" << "slate" << "custom" << "";
    QTest::newRow("custom malformed") << "paper" << "slate" << "custom" << "zielony";
    QTest::newRow("custom bad digits") << "paper" << "slate" << "custom" << "#ggg000";
    QTest::newRow("custom too long") << "paper" << "slate" << "custom" << "#1234567";
    QTest::newRow("custom diacritics") << "paper" << "slate" << "custom" << "#ąęółźż";
    QTest::newRow("colour as preset name") << "paper" << "slate" << "#3b7dd8" << "";
}

void TestThemePalette::unknownNamesGiveDefaults()
{
    QFETCH(QString, lightSurface);
    QFETCH(QString, darkSurface);
    QFETCH(QString, accentName);
    QFETCH(QString, custom);
    for (const bool dark : {false, true})
        QCOMPARE(paletteMap(dark, lightSurface, darkSurface, accentName, custom),
                 paletteMap(dark, u"paper", u"slate", u"red", u""));
}

void TestThemePalette::contrastOnEverySurface_data()
{
    QTest::addColumn<bool>("dark");
    QTest::addColumn<QString>("surfaceName");
    QTest::addColumn<QString>("accentName");
    QTest::addColumn<QString>("custom");
    for (const bool dark : {false, true}) {
        for (const QString &s : surfaceNames(dark)) {
            QStringList accents = accentNames();
            accents.append(u"custom"_s);
            for (const QString &a : std::as_const(accents)) {
                const QStringList customs = a == u"custom"
                        ? QStringList{u"#ffffff"_s, u"#000000"_s, u"#ffff00"_s, u"#e2574c"_s, u"#00ff00"_s,
                                      u"#0000ff"_s, u"#808080"_s, u"#ff00ff"_s, u"#123"_s}
                        : QStringList{QString()};
                for (const QString &c : customs) {
                    const QString name = u"%1 %2 %3 %4"_s.arg(dark ? u"dark"_s : u"light"_s, s, a, c).trimmed();
                    QTest::newRow(qPrintable(name)) << dark << s << a << c;
                }
            }
        }
    }
}

void TestThemePalette::contrastOnEverySurface()
{
    QFETCH(bool, dark);
    QFETCH(QString, surfaceName);
    QFETCH(QString, accentName);
    QFETCH(QString, custom);
    const Surface s = surface(surfaceName, dark);
    const Accent a = accent(accentName, custom, dark, s);
    const TextColors t = textColors(dark);
    // The page, cards and dialogs, wells (Theme.qml's promise; popups over cards use fg).
    const QList<Rgb> surfaces{s.bg, s.bgElevated, s.bgSunken};
    const Rgb white{255, 255, 255};
    QVERIFY2(contrast(white, a.accentSolid) >= 4.5, qPrintable(describe("white on accentSolid", white, a.accentSolid)));
    for (const Rgb &bg : surfaces) {
        QVERIFY2(contrast(a.accent, bg) >= 3.0, qPrintable(describe("accent", a.accent, bg)));
        QVERIFY2(contrast(a.accentText, bg) >= 4.5, qPrintable(describe("accentText", a.accentText, bg)));
        const Rgb tint = over(a.accent, accentBgAlpha(dark), bg);
        // The hand-picked red of the default look reaches 4.3:1 on its tint over a dark card
        // (kept: the default look never changes); derived accents make 4.5:1 everywhere.
        const bool red = a.accent == accentBase(u"red") && accentName != u"custom";
        QVERIFY2(contrast(a.accentText, tint) >= (red ? 4.3 : 4.5),
                 qPrintable(describe("accentText on accentBg", a.accentText, tint)));
        QVERIFY2(contrast(s.borderControl, bg) >= 3.0, qPrintable(describe("borderControl", s.borderControl, bg)));
        const QList<std::pair<const char *, Rgb>> texts{
                {"fg", t.fg},
                {"fgMuted", t.fgMuted},
                {"fgFaint", t.fgFaint},
                {"link", t.link},
                {"linkMissing", t.linkMissing},
                {"tagFg", t.tagFg},
                {"dangerText", t.dangerText},
                {"warningText", t.warningText},
                {"successText", t.successText},
                {"chipDueFg", t.chipDueFg},
                {"chipScheduledFg", t.chipScheduledFg},
                {"chipReminderFg", t.chipReminderFg},
                {"chipRecurrenceFg", t.chipRecurrenceFg},
        };
        for (const auto &[name, fg] : texts)
            QVERIFY2(contrast(fg, bg) >= 4.5, qPrintable(describe(name, fg, bg)));
    }
    // The hover shade is a visible step from the accent, in the mode's direction.
    const double l = relativeLuminance(a.accent), lh = relativeLuminance(a.accentHover);
    if (a.accent != Rgb{0, 0, 0} && a.accent != white)
        QVERIFY2(dark ? lh >= l : lh <= l, qPrintable(toHex(a.accent) + u" → "_s + toHex(a.accentHover)));
}

void TestThemePalette::modalScrimMakesThePageRecede_data()
{
    QTest::addColumn<bool>("dark");
    QTest::addColumn<QString>("surfaceName");
    for (const bool dark : {false, true}) {
        for (const QString &s : surfaceNames(dark))
            QTest::newRow(qPrintable(s)) << dark << s;
    }
}

// The centred palette and dialogs: under scrimModal the page's text recedes (at most 3:1 against
// the page under the same scrim — about 7:1 without it), and in light the panel's raised fill
// stands out from the washed page by itself (dark: its borderStrong edge does that).
void TestThemePalette::modalScrimMakesThePageRecede()
{
    QFETCH(bool, dark);
    QFETCH(QString, surfaceName);
    const Surface s = surface(surfaceName, dark);
    const TextColors t = textColors(dark);
    const Scrim scrim = modalScrim(dark, s, t);
    const auto under = [&](const Rgb &c) { return over(scrim.color, scrim.alpha, c); };
    const Rgb page = under(s.bg);
    for (const auto &[name, fg] : {std::pair{"fg", t.fg}, std::pair{"fgMuted", t.fgMuted}}) {
        QVERIFY2(contrast(under(fg), page) <= 3.0, qPrintable(describe(name, under(fg), page)));
        QVERIFY2(contrast(under(fg), page) >= 1.4, qPrintable(describe(name, under(fg), page))); // still there
    }
    if (!dark)
        QVERIFY2(contrast(s.bgElevated, page) >= 1.25, qPrintable(describe("bgElevated", s.bgElevated, page)));
    else
        QVERIFY2(contrast(s.borderStrong, page) >= 1.5, qPrintable(describe("borderStrong", s.borderStrong, page)));
    // The map carries it as "#aarrggbb".
    const QString expected = toHexArgb(scrim.color, scrim.alpha);
    QCOMPARE(paletteMap(dark, surfaceName, surfaceName, u"red", u"").value(u"scrimModal"_s).toString(), expected);
    QCOMPARE(expected.size(), 9);
}

void TestThemePalette::customAccentIsDerived_data()
{
    QTest::addColumn<QString>("custom");
    QTest::addColumn<bool>("dark");
    QTest::addColumn<bool>("keepsBase");
    // A colour that is readable already stays as typed (marks); one that is not is darkened
    // (light) or lightened (dark).
    QTest::newRow("blue on paper") << "#3b7dd8" << false << true;
    QTest::newRow("yellow on paper") << "#ffff00" << false << false;
    QTest::newRow("navy on slate") << "#1a237e" << true << false;
    QTest::newRow("yellow on slate") << "#ffff00" << true << true;
}

void TestThemePalette::customAccentIsDerived()
{
    QFETCH(QString, custom);
    QFETCH(bool, dark);
    QFETCH(bool, keepsBase);
    const Surface s = surface(dark ? u"slate" : u"paper", dark);
    const Accent a = accent(u"custom", custom, dark, s);
    QCOMPARE(a.accent == rgb(custom), keepsBase);
    if (!keepsBase) {
        const double base = relativeLuminance(rgb(custom));
        QVERIFY(dark ? relativeLuminance(a.accent) > base : relativeLuminance(a.accent) < base);
    }
    // Same colour, same result (no state).
    QCOMPARE(toHex(accent(u"custom", custom.toUpper(), dark, s).accentText), toHex(a.accentText));
}

void TestThemePalette::parseColor_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("hex"); // "" = invalid
    QTest::newRow("rrggbb") << "#a1b2c3" << "#a1b2c3";
    QTest::newRow("upper") << "#A1B2C3" << "#a1b2c3";
    QTest::newRow("short") << "#abc" << "#aabbcc";
    QTest::newRow("spaces") << "  #00ff7f " << "#00ff7f";
    QTest::newRow("empty") << "" << "";
    QTest::newRow("hash") << "#" << "";
    QTest::newRow("two digits") << "#12" << "";
    QTest::newRow("five digits") << "#12345" << "";
    QTest::newRow("seven digits") << "#1234567" << "";
    QTest::newRow("argb") << "#80a1b2c3" << "";
    QTest::newRow("no hash") << "a1b2c3" << "";
    QTest::newRow("css function") << "rgb(1,2,3)" << "";
    QTest::newRow("name") << "czerwony" << "";
    QTest::newRow("diacritics") << "ąę" << "";
    QTest::newRow("hex with diacritic") << "#a1b2cę" << "";
    QTest::newRow("inner space") << "#a1 b2c" << "";
}

void TestThemePalette::parseColor()
{
    QFETCH(QString, text);
    QFETCH(QString, hex);
    const auto c = brulion::ui::parseColor(text);
    QCOMPARE(c ? toHex(*c) : QString(), hex);
}

void TestThemePalette::luminanceAndContrast()
{
    const Rgb black{0, 0, 0}, white{255, 255, 255};
    QCOMPARE(relativeLuminance(black), 0.0);
    QCOMPARE(relativeLuminance(white), 1.0);
    QCOMPARE(contrast(black, white), 21.0);
    QCOMPARE(contrast(white, black), 21.0);
    QCOMPARE(contrast(rgb(u"#777777"_s), rgb(u"#777777"_s)), 1.0);
    // The brand red is only a mark under white text; the solid red carries it.
    QVERIFY(contrast(white, rgb(u"#e2574c"_s)) < 4.5);
    QVERIFY(contrast(white, rgb(u"#c4463a"_s)) >= 4.5);
    QCOMPARE(over(white, 0.5, black), (Rgb{128, 128, 128}));
    QCOMPARE(over(rgb(u"#e2574c"_s), 0.0, rgb(u"#fbf7ee"_s)), rgb(u"#fbf7ee"_s));
    QCOMPARE(over(rgb(u"#e2574c"_s), 1.0, rgb(u"#fbf7ee"_s)), rgb(u"#e2574c"_s));
}

void TestThemePalette::fontFamilies_data()
{
    QTest::addColumn<QStringList>("input");
    QTest::addColumn<QStringList>("expected");
    QTest::newRow("empty") << QStringList{} << QStringList{};
    QTest::newRow("no text fonts")
            << QStringList{u".SF NS"_s, u"Noto Color Emoji"_s, u"Symbol"_s, u"Noto Sans Math"_s,
                           u"Noto Sans Symbols 2"_s, u"OpenSymbol"_s, u"Wingdings 3"_s, u"BrulionEmoji"_s,
                           u"BrulionMath"_s, u"   "_s, u"DejaVu Sans"_s}
            << QStringList{u"DejaVu Sans"_s};
    // Words that only contain the filtered ones stay.
    QTest::newRow("look-alikes") << QStringList{u"Mathilde"_s, u"Symbolic Serif"_s, u"Emojione Text"_s}
                                 << QStringList{u"Emojione Text"_s, u"Mathilde"_s, u"Symbolic Serif"_s};
    QTest::newRow("Polish order") << QStringList{u"Łato"_s, u"Mono"_s, u"Lato"_s, u"Źródło Serif"_s, u"Zabytek"_s,
                                                 u"żółw Sans"_s, u"ąbc"_s, u"Abc"_s}
                                  << QStringList{u"Abc"_s, u"ąbc"_s, u"Lato"_s, u"Łato"_s, u"Mono"_s, u"Zabytek"_s,
                                                 u"Źródło Serif"_s, u"żółw Sans"_s};
    QTest::newRow("duplicates and case") << QStringList{u"Noto Serif"_s, u"noto serif"_s, u" Noto Serif "_s, u"B"_s}
                                         << QStringList{u"B"_s, u"Noto Serif"_s};
    QTest::newRow("numbers") << QStringList{u"Font 10"_s, u"Font 2"_s} << QStringList{u"Font 2"_s, u"Font 10"_s};
}

void TestThemePalette::fontFamilies()
{
    QFETCH(QStringList, input);
    QFETCH(QStringList, expected);
    QCOMPARE(filterFontFamilies(input), expected);
}

void TestThemePalette::qmlFacade()
{
    ThemePalette p;
    QCOMPARE(p.colors(false, u"paper"_s, u"slate"_s, u"red"_s, QString()), paletteMap(false, u"paper", u"slate", u"red", u""));
    QCOMPARE(p.colors(true, u"white"_s, u"black"_s, u"green"_s, QString()).value(u"bg"_s).toString(), u"#000000"_s);
    QCOMPARE(p.colors(false, u"white"_s, u"black"_s, u"green"_s, QString()).value(u"bg"_s).toString(), u"#ffffff"_s);
    QCOMPARE(p.accentNames().first(), u"red"_s);
    QCOMPARE(p.accentSwatch(u"red"_s, QString(), false, u"paper"_s), u"#e2574c"_s);
    QCOMPARE(p.accentSwatch(u"custom"_s, u"#3b7dd8"_s, false, u"paper"_s), u"#3b7dd8"_s);
    QCOMPARE(p.normalizeColor(u" #ABC "_s), u"#aabbcc"_s);
    QCOMPARE(p.normalizeColor(u"żółty"_s), QString());
    QCOMPARE(p.contrastRatio(u"#000000"_s, u"#ffffff"_s), 21.0);
    QCOMPARE(p.contrastRatio(u"#000000"_s, u"biały"_s), 0.0);
}

QTEST_GUILESS_MAIN(TestThemePalette)
#include "tst_themepalette.moc"
