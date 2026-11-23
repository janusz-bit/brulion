// Theme pages (extensions/themepage.h; docs/SYNTAX.md §13 "Theme pages"): which pages are theme
// pages, the ```theme block's lines (tokens, modes, comments, quotes, sizes with "px", fonts this
// device lacks), what is ignored and why, several blocks on a page, the export's block read back to
// the same layer, and two modes folded into one layer.
#include "extensions/themepage.h"
#include "markdown/parse.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::extensions;

Q_DECLARE_METATYPE(brulion::extensions::ThemeIssueKind)

namespace {

// The colours in plain literals of their own: moc reads this file, and a hash sign in a raw
// string literal would empty its output.
const QString kPage = u"---\n"
                      "tags: [motyw]\n"
                      "opis: Ciepły, papierowy — jak zeszyt z liceum.\n"
                      "---\n"
                      "Motyw „Sepia”: papier za dnia, morska zieleń zawsze.\n"
                      "\n"
                      "```theme\n"
                      "// akcent w obu trybach\n"
                      "accent: \x23" "1f8a80\n"
                      "radius: 9px\n"
                      "fontUi: Łato\n"
                      "jasny:\n"
                      "  bg: \x23" "f4ecd8\n"
                      "  fg: \x23" "3b2f20\n"
                      "dark:\n"
                      "  bg: \x23" "14110c\n"
                      "```\n"_s;

QString hash(const char *hex)
{
    return u'\x23' + QString::fromLatin1(hex);
}

} // namespace

class TestThemePages : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void themePagesAreTagged_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<bool>("theme");
        QTest::newRow("frontmatter, Polish") << u"---\ntags: [motyw]\n---\ntekst\n"_s << true;
        QTest::newRow("frontmatter, English") << u"---\ntags: theme\n---\n"_s << true;
        QTest::newRow("property line") << u"tags:: motyw, kolory\n- coś\n"_s << true;
        QTest::newRow("in a block only") << u"- ładny \x23motyw\n"_s << false;
        QTest::newRow("another tag") << u"---\ntags: [motywacja]\n---\n"_s << false;
        QTest::newRow("no tags") << u"```theme\nbg: \x23" "000\n```\n"_s << false;
    }
    void themePagesAreTagged()
    {
        QFETCH(QString, content);
        QFETCH(bool, theme);
        QCOMPARE(isThemePage(markdown::parseNote(u"Motyw.md"_s, content)), theme);
    }

    void aPageReadsWhole()
    {
        const ParsedNote note = markdown::parseNote(u"Motywy/Sepia.md"_s, kPage);
        QVERIFY(isThemePage(note));
        QCOMPARE(themeDescription(note), u"Ciepły, papierowy — jak zeszyt z liceum."_s);
        const ThemePage page = parseThemePage(kPage);
        QCOMPARE(page.blocks, 1);
        QCOMPARE(page.firstLine, 6);
        QVERIFY2(page.issues.isEmpty(), page.issues.isEmpty() ? "" : qPrintable(page.issues.first().token));
        QCOMPARE(page.layer.both.value(u"accent"_s).toString(), hash("1f8a80"));
        QCOMPARE(page.layer.both.value(u"radius"_s).toDouble(), 9.0);
        QCOMPARE(page.layer.both.value(u"fontUi"_s).toString(), u"Łato"_s);
        QCOMPARE(page.layer.light.value(u"bg"_s).toString(), hash("f4ecd8"));
        QCOMPARE(page.layer.light.value(u"fg"_s).toString(), hash("3b2f20"));
        QCOMPARE(page.layer.dark.value(u"bg"_s).toString(), hash("14110c"));
        QCOMPARE(page.tokenCount(), 6);
        QCOMPARE(page.layer.forMode(true).value(u"accent"_s).toString(), hash("1f8a80"));
    }

    void linesAreRead_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("token");
        QTest::addColumn<QVariant>("value");
        QTest::newRow("colour") << u"bg: \x23" "0B0D10"_s << u"bg"_s << QVariant(hash("0b0d10"));
        QTest::newRow("short colour") << u"fg: \x23" "abc"_s << u"fg"_s << QVariant(hash("aabbcc"));
        QTest::newRow("alpha first") << u"scrim: \x23" "80000000"_s << u"scrim"_s << QVariant(hash("80000000"));
        QTest::newRow("no blank") << u"accent:\x23" "123456"_s << u"accent"_s << QVariant(hash("123456"));
        QTest::newRow("blanks") << u"  accent  :   \x23" "123456  "_s << u"accent"_s << QVariant(hash("123456"));
        QTest::newRow("quoted") << u"accent: \"\x23" "123456\""_s << u"accent"_s << QVariant(hash("123456"));
        QTest::newRow("single quotes") << u"link: '\x23" "2a62a6'"_s << u"link"_s << QVariant(hash("2a62a6"));
        QTest::newRow("comment after") << u"accent: \x23" "123456 // morski"_s << u"accent"_s
                                       << QVariant(hash("123456"));
        QTest::newRow("any case") << u"BG: \x23" "000000"_s << u"bg"_s << QVariant(hash("000000"));
        QTest::newRow("camel any case") << u"chipduebg: \x23" "112233"_s << u"chipDueBg"_s << QVariant(hash("112233"));
        QTest::newRow("number") << u"radiusLg: 14"_s << u"radiusLg"_s << QVariant(14.0);
        QTest::newRow("number px") << u"radiusXl: 20 PX"_s << u"radiusXl"_s << QVariant(20.0);
        QTest::newRow("fraction") << u"radius: 9.5"_s << u"radius"_s << QVariant(9.5);
        QTest::newRow("spacing") << u"space: 5"_s << u"space"_s << QVariant(5.0);
        QTest::newRow("font") << u"fontSerif: Literata"_s << u"fontSerif"_s << QVariant(u"Literata"_s);
        QTest::newRow("font with blanks") << u"fontMono: JetBrains Mono"_s << u"fontMono"_s
                                          << QVariant(u"JetBrains Mono"_s);
        QTest::newRow("font quoted") << u"fontUi: \"Źdźbło // Sans\""_s << u"fontUi"_s
                                     << QVariant(u"Źdźbło // Sans"_s);
        QTest::newRow("font default") << u"fontUi:"_s << u"fontUi"_s << QVariant(QString());
        QTest::newRow("windows line end") << u"bg: \x23" "ffffff\r"_s << u"bg"_s << QVariant(hash("ffffff"));
    }
    void linesAreRead()
    {
        QFETCH(QString, line);
        QFETCH(QString, token);
        QFETCH(QVariant, value);
        const ThemePage page = parseThemeBlock({line}, 3);
        QVERIFY2(page.issues.isEmpty(), page.issues.isEmpty() ? "" : qPrintable(page.issues.first().value));
        QCOMPARE(page.layer.both.size(), 1);
        QCOMPARE(page.layer.both.value(token), value);
    }

    void wrongLinesAreIgnored_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<ThemeIssueKind>("kind");
        QTest::addColumn<QString>("detail");
        QTest::newRow("no colon") << u"bg \x23" "000"_s << ThemeIssueKind::Syntax << QString();
        QTest::newRow("only a colon") << u": \x23" "000"_s << ThemeIssueKind::Syntax << QString();
        QTest::newRow("prose") << u"Ładny motyw"_s << ThemeIssueKind::Syntax << QString();
        QTest::newRow("misspelt") << u"bgg: \x23" "000"_s << ThemeIssueKind::UnknownToken << u"bg"_s;
        QTest::newRow("unknown far") << u"tapeta: \x23" "000"_s << ThemeIssueKind::UnknownToken << QString();
        QTest::newRow("Polish token") << u"kolorTłaStrony: \x23" "000"_s << ThemeIssueKind::UnknownToken << QString();
        QTest::newRow("colour name") << u"fg: czerwony"_s << ThemeIssueKind::BadColor << QString();
        QTest::newRow("five digits") << u"fg: \x23" "12345"_s << ThemeIssueKind::BadColor << QString();
        QTest::newRow("rgb()") << u"fg: rgb(1, 2, 3)"_s << ThemeIssueKind::BadColor << QString();
        QTest::newRow("empty colour") << u"fg:"_s << ThemeIssueKind::BadColor << QString();
        QTest::newRow("url") << u"bg: url(http:\x2f/x.pl/a.png)"_s << ThemeIssueKind::BadColor << QString();
        QTest::newRow("radius too big") << u"radius: 1000"_s << ThemeIssueKind::BadNumber << u"0 … 32"_s;
        QTest::newRow("radius negative") << u"radius: -1"_s << ThemeIssueKind::BadNumber << u"0 … 32"_s;
        QTest::newRow("radius a word") << u"radius: duży"_s << ThemeIssueKind::BadNumber << u"0 … 32"_s;
        QTest::newRow("radius Polish comma") << u"radius: 9,5"_s << ThemeIssueKind::BadNumber << u"0 … 32"_s;
        QTest::newRow("spacing too small") << u"space: 1"_s << ThemeIssueKind::BadNumber << u"3 … 6"_s;
        QTest::newRow("infinity") << u"radius: inf"_s << ThemeIssueKind::BadNumber << u"0 … 32"_s;
        QTest::newRow("font too long") << u"fontUi: "_s + QString(120, u'ż') << ThemeIssueKind::BadFont << QString();
        QTest::newRow("mode with a value") << u"dark: \x23" "000"_s << ThemeIssueKind::ModeValue << QString();
    }
    void wrongLinesAreIgnored()
    {
        QFETCH(QString, line);
        QFETCH(ThemeIssueKind, kind);
        QFETCH(QString, detail);
        // The wrong line among good ones: they apply, it does not.
        const QStringList lines{u"accent: \x23" "1f8a80"_s, line, u"radius: 8"_s};
        const ThemePage page = parseThemeBlock(lines, 10);
        QCOMPARE(page.issues.size(), 1);
        QCOMPARE(page.issues.first().kind, kind);
        QCOMPARE(page.issues.first().line, 11);
        QCOMPARE(page.issues.first().detail, detail);
        QVERIFY(page.issues.first().value.size() <= 60);
        QCOMPARE(page.layer.both.value(u"accent"_s).toString(), hash("1f8a80"));
        QCOMPARE(page.layer.both.value(u"radius"_s).toDouble(), 8.0);
        QCOMPARE(page.tokenCount(), 2);
    }

    void modes()
    {
        const QStringList lines{
                u"- motyw w liście:"_s, // (a line of prose is an issue, nothing else)
                u"light:"_s,
                u"    bg: \x23" "ffffff"_s,
                u"\tfg: \x23" "111111"_s, // a tab is deeper than the header
                u""_s,
                u"    # a comment inside the mode"_s,
                u"    border: \x23" "dddddd"_s,
                u"accent: \x23" "222222"_s, // back at the header's depth: both modes
                u"  ciemny:"_s,
                u"    bg: \x23" "000000"_s,
                u"    light:"_s, // a mode in a mode
                u"  radius: 4"_s, // as deep as `ciemny:`: both modes again
                u"dark:"_s,
                u"dark:"_s, // an empty mode, then the same mode again
                u"  bg: \x23" "010101"_s,
                u"  bg: \x23" "020202"_s, // set twice: the last wins
        };
        const ThemePage page = parseThemeBlock(lines);
        QCOMPARE(page.issues.size(), 2);
        QCOMPARE(page.issues[0].kind, ThemeIssueKind::Syntax);
        QCOMPARE(page.issues[0].line, 0);
        QCOMPARE(page.issues[1].kind, ThemeIssueKind::ModeInMode);
        QCOMPARE(page.issues[1].line, 10);
        QCOMPARE(page.issues[1].detail, u"dark"_s);
        QCOMPARE(page.layer.light,
                 (QVariantMap{{u"bg"_s, hash("ffffff")}, {u"fg"_s, hash("111111")}, {u"border"_s, hash("dddddd")}}));
        QCOMPARE(page.layer.both, (QVariantMap{{u"accent"_s, hash("222222")}, {u"radius"_s, 4.0}}));
        QCOMPARE(page.layer.dark, (QVariantMap{{u"bg"_s, hash("020202")}}));
    }

    void missingFonts()
    {
        const auto installed = [](const QString &family) { return family == u"Inter"_s; };
        const ThemePage page = parseThemeBlock(
                {u"fontUi: Inter"_s, u"fontSerif: Zażółć Serif"_s, u"fontMono:"_s}, 0, installed);
        QCOMPARE(page.issues.size(), 1);
        QCOMPARE(page.issues.first().kind, ThemeIssueKind::MissingFont);
        QCOMPARE(page.issues.first().value, u"Zażółć Serif"_s);
        QCOMPARE(page.layer.both, (QVariantMap{{u"fontUi"_s, u"Inter"_s}, {u"fontMono"_s, QString()}}));
    }

    void tooManyTokens()
    {
        QStringList lines;
        for (const QString &mode : {QString(), u"light:"_s, u"dark:"_s}) {
            if (!mode.isEmpty())
                lines << mode;
            for (const ThemeToken &t : themeTokens())
                if (t.kind == ThemeTokenKind::Color)
                    lines << (mode.isEmpty() ? QString() : u"  "_s) + t.name + u": \x23" "123456"_s;
        }
        const ThemePage page = parseThemeBlock(lines);
        QCOMPARE(page.tokenCount(), MaxThemeTokens);
        QVERIFY(!page.issues.isEmpty());
        QVERIFY(std::ranges::all_of(page.issues, [](const ThemeIssue &i) { return i.kind == ThemeIssueKind::TooMany; }));
    }

    void blocksOfAPage()
    {
        const QString content = u"---\n"
                                "tags: [theme]\n"
                                "---\n"
                                "```theme\n"
                                "accent: \x23" "111111\n"
                                "bgg: \x23" "000000\n"
                                "```\n"
                                "```luau\n"
                                "accent: \x23" "999999\n"
                                "```\n"
                                "- Ciemniej:\n"
                                "  ```{theme}\n"
                                "  accent: \x23" "222222\n"
                                "  dark:\n"
                                "    bg: \x23" "000000\n"
                                "  ```\n"
                                "````markdown\n"
                                "```theme\n"
                                "accent: \x23" "333333\n"
                                "```\n"
                                "````\n"_s;
        const ThemePage page = parseThemePage(content);
        QCOMPARE(page.blocks, 2);
        QCOMPARE(page.firstLine, 3);
        QCOMPARE(page.issues.size(), 1);
        QCOMPARE(page.issues.first().line, 5);
        QCOMPARE(page.issues.first().detail, u"bg"_s);
        QCOMPARE(page.layer.both.value(u"accent"_s).toString(), hash("222222")); // the later block wins
        QCOMPARE(page.layer.dark.value(u"bg"_s).toString(), hash("000000"));
        QCOMPARE(page.tokenCount(), 2);
    }

    void aPageWithoutABlock()
    {
        const ThemePage page = parseThemePage(u"---\ntags: [motyw]\n---\nJeszcze nic.\n"_s);
        QCOMPARE(page.blocks, 0);
        QCOMPARE(page.firstLine, -1);
        QVERIFY(page.layer.isEmpty());
        // An unclosed block runs to the page's end; the frontmatter is never one.
        const ThemePage open = parseThemePage(u"---\ntheme: x\n---\n```theme\nfg: \x23" "fff\n"_s);
        QCOMPARE(open.blocks, 1);
        QCOMPARE(open.layer.both.value(u"fg"_s).toString(), hash("ffffff"));
    }

    void theExportReadsBack()
    {
        ThemeLayer layer;
        layer.both = {{u"accent"_s, hash("1f8a80")}, {u"radius"_s, 9.0}, {u"radiusLg"_s, 14.5},
                      {u"fontUi"_s, u"Łato Sans"_s}, {u"fontMono"_s, u"Mono // 2"_s}, {u"space"_s, 5.0}};
        layer.light = {{u"bg"_s, hash("f4ecd8")}, {u"scrimModal"_s, hash("9effffff")}};
        layer.dark = {{u"bg"_s, hash("14110c")}, {u"fg"_s, hash("eeeeee")}};
        const QStringList lines = themeBlockLines(layer);
        QCOMPARE(lines.first(), u"accent: \x23" "1f8a80"_s); // themeTokens()' order
        QVERIFY(lines.contains(u"radius: 9"_s));
        QVERIFY(lines.contains(u"radiusLg: 14.5"_s));
        QVERIFY(lines.contains(u"light:"_s));
        QVERIFY(lines.contains(u"  bg: \x23" "14110c"_s));
        QCOMPARE(parseThemeBlock(lines).layer, layer);
        const QString text = themeBlockText(layer);
        QVERIFY(text.startsWith(u"```theme\n"_s));
        QVERIFY(text.endsWith(u"\n```\n"_s));
        const QString pageText = u"---\ntags: [motyw]\n---\n"_s + text;
        const ThemePage page = parseThemePage(pageText);
        QVERIFY(page.issues.isEmpty());
        QCOMPARE(page.layer, layer);
        QCOMPARE(themeBlockLines({}), QStringList());
    }

    void modesFold()
    {
        const QVariantMap light{{u"bg"_s, hash("ffffff")}, {u"accent"_s, hash("1f8a80")}, {u"radius"_s, 9.0}};
        const QVariantMap dark{{u"bg"_s, hash("000000")}, {u"accent"_s, hash("1f8a80")}, {u"fg"_s, hash("eeeeee")}};
        const ThemeLayer layer = foldedThemeLayer(light, dark);
        QCOMPARE(layer.both, (QVariantMap{{u"accent"_s, hash("1f8a80")}}));
        QCOMPARE(layer.light, (QVariantMap{{u"bg"_s, hash("ffffff")}, {u"radius"_s, 9.0}}));
        QCOMPARE(layer.dark, (QVariantMap{{u"bg"_s, hash("000000")}, {u"fg"_s, hash("eeeeee")}}));
        QCOMPARE(layer.forMode(false), light);
        QCOMPARE(layer.forMode(true), dark);
    }
};

QTEST_GUILESS_MAIN(TestThemePages)
#include "tst_extensions_themes.moc"
