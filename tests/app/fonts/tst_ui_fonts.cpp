// The bundled text fonts (src/app/ui/uifonts.h, assets/fonts/ui): all three families load, cover
// Polish and the punctuation Brulion writes, and shape text only into glyphs that have a character
// of their own in the font's cmap — Qt's PDF writer maps glyphs back to text through the cmap, so
// a ligature or a contextual alternate (Inter's em dash between spaces) came out of a PDF as
// nothing (scripts/ui-fonts.py leaves those features out).
#include "uifonts.h"

#include <QFontDatabase>
#include <QGlyphRun>
#include <QRawFont>
#include <QSet>
#include <QTest>
#include <QTextLayout>

using namespace Qt::StringLiterals;

class TestUiFonts : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        const QStringList families = brulion::ui::installBundledFonts();
        QCOMPARE(families.size(), 3);
        QVERIFY(families.contains(QString::fromUtf16(brulion::ui::kBundledUiFamily)));
        QVERIFY(families.contains(QString::fromUtf16(brulion::ui::kBundledSerifFamily)));
        QVERIFY(families.contains(QString::fromUtf16(brulion::ui::kBundledMonoFamily)));
        // Once per process: the second call gives the same.
        QCOMPARE(brulion::ui::installBundledFonts(), families);
    }

    void shapedGlyphsHaveCharacters_data()
    {
        QTest::addColumn<QString>("family");
        QTest::addColumn<int>("weight");
        QTest::addColumn<bool>("italic");
        const QString ui = QString::fromUtf16(brulion::ui::kBundledUiFamily);
        const QString serif = QString::fromUtf16(brulion::ui::kBundledSerifFamily);
        const QString mono = QString::fromUtf16(brulion::ui::kBundledMonoFamily);
        QTest::newRow("Inter Regular") << ui << int(QFont::Normal) << false;
        QTest::newRow("Inter Medium") << ui << int(QFont::Medium) << false;
        QTest::newRow("Inter SemiBold") << ui << int(QFont::DemiBold) << false;
        QTest::newRow("Inter Bold") << ui << int(QFont::Bold) << false;
        QTest::newRow("Inter Italic") << ui << int(QFont::Normal) << true;
        QTest::newRow("Literata Regular") << serif << int(QFont::Normal) << false;
        QTest::newRow("Literata Italic") << serif << int(QFont::Normal) << true;
        QTest::newRow("Literata Bold") << serif << int(QFont::Bold) << false;
        QTest::newRow("Literata Bold Italic") << serif << int(QFont::Bold) << true;
        QTest::newRow("JetBrains Mono Regular") << mono << int(QFont::Normal) << false;
        QTest::newRow("JetBrains Mono Bold") << mono << int(QFont::Bold) << false;
    }
    void shapedGlyphsHaveCharacters()
    {
        QFETCH(QString, family);
        QFETCH(int, weight);
        QFETCH(bool, italic);
        QFont font(family, 24);
        font.setWeight(QFont::Weight(weight));
        font.setItalic(italic);
        const QRawFont raw = QRawFont::fromFont(font);
        QVERIFY(raw.isValid());
        QCOMPARE(raw.familyName(), family);
        // Text with what fonts like to substitute: dashes between words and digits, fi / fl / ff,
        // arrows typed as -> and =>, != and ===, 1:30, x×y, a fraction, Polish letters.
        const QString text = u"Wykład 3 — Zażółć gęślą jaźń – 10–12, fi fl ffi -> => != === 1:30 x×y ½ «ą» „ż” ÓŚĆŃ…"_s;
        for (const QChar c : text) {
            if (!c.isSpace())
                QVERIFY2(raw.supportsCharacter(c), qPrintable(u"%1 has no %2"_s.arg(family, c)));
        }
        QSet<quint32> mapped;
        for (const quint32 g : raw.glyphIndexesForString(text))
            mapped.insert(g);
        QTextLayout layout(text, font);
        layout.beginLayout();
        layout.createLine();
        layout.endLayout();
        int glyphs = 0;
        for (const QGlyphRun &run : layout.glyphRuns()) {
            QCOMPARE(run.rawFont().familyName(), family);
            for (const quint32 g : run.glyphIndexes()) {
                ++glyphs;
                QVERIFY2(mapped.contains(g),
                         qPrintable(u"%1: glyph %2 has no character of the text (a substitution)"_s.arg(family).arg(g)));
            }
        }
        QVERIFY(glyphs > 0);
    }
};

QTEST_MAIN(TestUiFonts)
#include "tst_ui_fonts.moc"
