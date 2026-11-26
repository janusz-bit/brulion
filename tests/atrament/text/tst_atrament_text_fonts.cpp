// Atrament's font collection: faces read from Brulion's bundled fonts, CSS matching, fallback,
// metrics and malformed font data.
#include "text/fonts.h"

#include "textfixtures.h"

#include <QtTest>

#include <hb.h>

#include <random>

using namespace brulion::atrament;
using namespace Qt::StringLiterals;
using textfixtures::BundledFonts;
using textfixtures::qs;

Q_DECLARE_METATYPE(brulion::atrament::FontStyle)

class TestAtramentTextFonts : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void faceInfo_data();
    void faceInfo();
    void matching_data();
    void matching();
    void unknownFamily();
    void fallback();
    void fallbackWithoutFamilies();
    void metrics();
    void invalidFaces();
    void malformedData_data();
    void malformedData();
    void missingFile();
};

void TestAtramentTextFonts::faceInfo_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("family");
    QTest::addColumn<QString>("postscript");
    QTest::addColumn<int>("weight");
    QTest::addColumn<FontStyle>("style");
    QTest::addColumn<bool>("monospace");
    QTest::newRow("Inter Regular") << u"ui/BrulionInter-Regular.ttf"_s << u"Brulion Inter"_s
                                   << u"BrulionInter-Regular"_s << 400 << FontStyle::Normal << false;
    QTest::newRow("Inter Medium") << u"ui/BrulionInter-Medium.ttf"_s << u"Brulion Inter"_s << u"BrulionInter-Medium"_s
                                  << 500 << FontStyle::Normal << false;
    QTest::newRow("Inter SemiBold") << u"ui/BrulionInter-SemiBold.ttf"_s << u"Brulion Inter"_s
                                    << u"BrulionInter-SemiBold"_s << 600 << FontStyle::Normal << false;
    QTest::newRow("Inter Bold") << u"ui/BrulionInter-Bold.ttf"_s << u"Brulion Inter"_s << u"BrulionInter-Bold"_s << 700
                                << FontStyle::Normal << false;
    QTest::newRow("Inter Italic") << u"ui/BrulionInter-Italic.ttf"_s << u"Brulion Inter"_s << u"BrulionInter-Italic"_s
                                  << 400 << FontStyle::Italic << false;
    QTest::newRow("Inter Bold Italic") << u"ui/BrulionInter-BoldItalic.ttf"_s << u"Brulion Inter"_s
                                       << u"BrulionInter-BoldItalic"_s << 700 << FontStyle::Italic << false;
    QTest::newRow("Literata Italic") << u"ui/BrulionLiterata-Italic.ttf"_s << u"Brulion Literata"_s
                                     << u"BrulionLiterata-Italic"_s << 400 << FontStyle::Italic << false;
    QTest::newRow("Literata Bold") << u"ui/BrulionLiterata-Bold.ttf"_s << u"Brulion Literata"_s
                                   << u"BrulionLiterata-Bold"_s << 700 << FontStyle::Normal << false;
    QTest::newRow("JetBrains Mono") << u"ui/BrulionJetBrainsMono-Regular.ttf"_s << u"Brulion JetBrains Mono"_s
                                    << u"BrulionJetBrainsMono-Regular"_s << 400 << FontStyle::Normal << true;
    QTest::newRow("JetBrains Mono Bold") << u"ui/BrulionJetBrainsMono-Bold.ttf"_s << u"Brulion JetBrains Mono"_s
                                         << u"BrulionJetBrainsMono-Bold"_s << 700 << FontStyle::Normal << true;
    // Not post.isFixedPitch, though its glyphs are all as wide.
    QTest::newRow("Emoji") << u"BrulionEmoji.ttf"_s << u"Brulion Emoji"_s << u"BrulionEmoji-Regular"_s << 400
                           << FontStyle::Normal << false;
    QTest::newRow("Math (CFF)") << u"BrulionMath.otf"_s << u"Brulion Math"_s << u"BrulionMath-Regular"_s << 400
                                << FontStyle::Normal << false;
}

void TestAtramentTextFonts::faceInfo()
{
    QFETCH(QString, file);
    QFETCH(QString, family);
    QFETCH(QString, postscript);
    QFETCH(int, weight);
    QFETCH(FontStyle, style);
    QFETCH(bool, monospace);
    FontCollection fonts;
    const std::vector<FaceId> faces = fonts.addFontFile(textfixtures::fontPath(file.toStdString()));
    QCOMPARE(faces.size(), std::size_t(1));
    QCOMPARE(fonts.faceCount(), std::size_t(1));
    const FaceInfo &info = fonts.faceInfo(faces[0]);
    QCOMPARE(qs(info.family), family);
    if (!qs(info.postscriptName).isEmpty() || !postscript.isEmpty())
        QCOMPARE(qs(info.postscriptName), postscript);
    QCOMPARE(int(info.weight.value), weight);
    QCOMPARE(info.style, style);
    QCOMPARE(info.monospace, monospace);
    QVERIFY(fonts.hbFont(faces[0]));
    QVERIFY(fonts.hbFace(faces[0]));
    QVERIFY(fonts.unitsPerEm(faces[0]) >= 16);
}

void TestAtramentTextFonts::matching_data()
{
    QTest::addColumn<QString>("family");
    QTest::addColumn<int>("weight");
    QTest::addColumn<FontStyle>("style");
    QTest::addColumn<QString>("postscript");
    const QString inter = u"Brulion Inter"_s;
    QTest::newRow("regular") << inter << 400 << FontStyle::Normal << u"BrulionInter-Regular"_s;
    QTest::newRow("case-insensitive") << u"bRULION inter"_s << 400 << FontStyle::Normal << u"BrulionInter-Regular"_s;
    QTest::newRow("bold") << inter << 700 << FontStyle::Normal << u"BrulionInter-Bold"_s;
    QTest::newRow("650: heavier first above 500") << inter << 650 << FontStyle::Normal << u"BrulionInter-Bold"_s;
    QTest::newRow("550") << inter << 550 << FontStyle::Normal << u"BrulionInter-SemiBold"_s;
    QTest::newRow("450: up to 500 first") << inter << 450 << FontStyle::Normal << u"BrulionInter-Medium"_s;
    QTest::newRow("300: lighter first, then heavier") << inter << 300 << FontStyle::Normal << u"BrulionInter-Regular"_s;
    QTest::newRow("900: nothing heavier") << inter << 900 << FontStyle::Normal << u"BrulionInter-Bold"_s;
    QTest::newRow("100") << inter << 100 << FontStyle::Normal << u"BrulionInter-Regular"_s;
    QTest::newRow("italic") << inter << 400 << FontStyle::Italic << u"BrulionInter-Italic"_s;
    QTest::newRow("bold italic") << inter << 700 << FontStyle::Italic << u"BrulionInter-BoldItalic"_s;
    QTest::newRow("italic before weight (500)") << inter << 500 << FontStyle::Italic << u"BrulionInter-Italic"_s;
    QTest::newRow("italic before weight (600)") << inter << 600 << FontStyle::Italic << u"BrulionInter-BoldItalic"_s;
    QTest::newRow("Literata 500: lighter when no heavier ≤ 500")
            << u"Brulion Literata"_s << 500 << FontStyle::Normal << u"BrulionLiterata-Regular"_s;
    QTest::newRow("Literata 600 italic") << u"Brulion Literata"_s << 600 << FontStyle::Italic
                                         << u"BrulionLiterata-BoldItalic"_s;
    QTest::newRow("mono without italic") << u"Brulion JetBrains Mono"_s << 700 << FontStyle::Italic
                                         << u"BrulionJetBrainsMono-Bold"_s;
}

void TestAtramentTextFonts::matching()
{
    QFETCH(QString, family);
    QFETCH(int, weight);
    QFETCH(FontStyle, style);
    QFETCH(QString, postscript);
    BundledFonts b;
    QVERIFY(b.allLoaded());
    const FaceId face = b.fonts->match(textfixtures::u16(family), FontWeight{std::uint16_t(weight)}, style);
    QVERIFY(face != kNoFace);
    QCOMPARE(qs(b.fonts->faceInfo(face).postscriptName), postscript);
}

void TestAtramentTextFonts::unknownFamily()
{
    BundledFonts b;
    QCOMPARE(b.fonts->match(u"Comic Sans"), kNoFace);
    QCOMPARE(b.fonts->match(u""), kNoFace);
    QCOMPARE(b.fonts->match(u"Brulion"), kNoFace);
    FontCollection empty;
    QCOMPARE(empty.match(u"Brulion Inter"), kNoFace);
}

void TestAtramentTextFonts::fallback()
{
    BundledFonts b;
    FontCollection &fonts = *b.fonts;
    QCOMPARE(fonts.faceFor(U'a', b.interRegular), b.interRegular);
    QCOMPARE(fonts.faceFor(U'ż', b.interRegular), b.interRegular);
    QCOMPARE(fonts.faceFor(U'Ź', b.literataItalic), b.literataItalic);
    QVERIFY(!fonts.hasGlyph(b.interRegular, 0x1F44D));
    QCOMPARE(fonts.faceFor(0x1F44D, b.interRegular), b.emoji); // 👍
    QCOMPARE(fonts.faceFor(0x1F44D, b.interRegular), b.emoji); // cached
    QCOMPARE(fonts.faceFor(0x1F3FD, b.interBold), b.emoji);    // skin tone
    QVERIFY(!fonts.hasGlyph(b.interRegular, 0x2A06));
    QCOMPARE(fonts.faceFor(0x2A06, b.interRegular), b.math);     // ⨆
    QCOMPARE(fonts.faceFor(0x1D49C, b.literataRegular), b.math); // 𝒜
    QCOMPARE(fonts.faceFor(0x1F44D, kNoFace), b.emoji);
    QCOMPARE(fonts.faceFor(U'a', kNoFace), b.interRegular);     // any face that has it: the first
    QCOMPARE(fonts.faceFor(0x10FFFD, b.interRegular), kNoFace); // nobody has it
    QCOMPARE(fonts.faceFor(0xD800, b.interRegular), kNoFace);
    QVERIFY(fonts.hasGlyph(b.emoji, 0x1F44D));
    QVERIFY(!fonts.hasGlyph(kNoFace, U'a'));
    // A fallback family loaded later: the cache is dropped.
    FontCollection late;
    const FaceId inter = textfixtures::loadFace(late, "ui/BrulionInter-Regular.ttf");
    late.setFallbackFamilies({u"Brulion Emoji"});
    QCOMPARE(late.faceFor(0x1F44D, inter), kNoFace);
    const FaceId emoji = textfixtures::loadFace(late, "BrulionEmoji.ttf");
    QCOMPARE(late.faceFor(0x1F44D, inter), emoji);
}

void TestAtramentTextFonts::fallbackWithoutFamilies()
{
    FontCollection fonts;
    const FaceId math = textfixtures::loadFace(fonts, "BrulionMath.otf");
    const FaceId inter = textfixtures::loadFace(fonts, "ui/BrulionInter-Regular.ttf");
    const FaceId emoji = textfixtures::loadFace(fonts, "BrulionEmoji.ttf");
    // No fallback families: any face with the glyph, in the order they were added.
    QCOMPARE(fonts.faceFor(0x1F600, inter), emoji);
    QCOMPARE(fonts.faceFor(0x2A06, inter), math);
    // The fallback families come before the others.
    fonts.setFallbackFamilies({u"Brulion Inter"});
    QCOMPARE(fonts.faceFor(0x2A06, math), math);
    QCOMPARE(fonts.faceFor(U'ł', math), inter);
}

void TestAtramentTextFonts::metrics()
{
    BundledFonts b;
    const FontCollection &fonts = *b.fonts;
    QCOMPARE(fonts.unitsPerEm(b.interRegular), 2048);
    const FontMetrics inter = fonts.metrics(b.interRegular);
    QCOMPARE(inter.ascent, 1984.0f / 2048);
    QCOMPARE(inter.descent, 494.0f / 2048);
    QCOMPARE(inter.lineGap, 0.0f);
    // post.underlinePosition −348, underlineThickness 140; OS/2 yStrikeoutPosition 671, size 140.
    QCOMPARE(inter.underlinePosition, 348.0f / 2048);
    QCOMPARE(inter.underlineThickness, 140.0f / 2048);
    QCOMPARE(inter.strikeoutPosition, 671.0f / 2048);
    QCOMPARE(inter.strikeoutThickness, 140.0f / 2048);
    QCOMPARE(inter.xHeight, 1118.0f / 2048);
    QCOMPARE(inter.capHeight, 1490.0f / 2048);
    // Brulion Emoji's post table puts the underline 1244 units above the baseline: replaced.
    const FontMetrics emoji = fonts.metrics(b.emoji);
    QCOMPARE(emoji.underlinePosition, emoji.descent / 2);
    QCOMPARE(fonts.metrics(b.literataRegular).underlinePosition, 100.0f / float(fonts.unitsPerEm(b.literataRegular)));
    for (FaceId face = 0; face < fonts.faceCount(); ++face) {
        const FontMetrics m = fonts.metrics(face);
        const QByteArray name = qs(fonts.faceInfo(face).postscriptName).toUtf8();
        QVERIFY2(m.ascent > 0.5f && m.ascent < 2, name.constData());
        QVERIFY2(m.descent > 0 && m.descent < 1, name.constData());
        QVERIFY2(m.lineGap >= 0, name.constData());
        QVERIFY2(m.underlinePosition > 0, name.constData());
        QVERIFY2(m.underlineThickness > 0, name.constData());
        QVERIFY2(m.strikeoutPosition > 0, name.constData());
        QVERIFY2(m.capHeight > 0 && m.capHeight <= m.ascent, name.constData());
    }
}

void TestAtramentTextFonts::invalidFaces()
{
    BundledFonts b;
    const FontCollection &fonts = *b.fonts;
    QVERIFY(fonts.faceInfo(kNoFace).family.empty());
    QCOMPARE(int(fonts.faceInfo(999).weight.value), 400);
    QCOMPARE(fonts.metrics(kNoFace).ascent, 0.0f);
    QCOMPARE(fonts.unitsPerEm(kNoFace), 0);
    QVERIFY(!fonts.hbFont(kNoFace));
    QVERIFY(!fonts.hbFace(12345));
    QCOMPARE(fonts.faceFor(U'a', 12345), b.interRegular);
}

void TestAtramentTextFonts::malformedData_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QFile inter(QString::fromStdString(textfixtures::fontPath("ui/BrulionInter-Regular.ttf")));
    QVERIFY(inter.open(QIODevice::ReadOnly));
    const QByteArray font = inter.readAll();
    QByteArray garbage(4096, '\0');
    std::mt19937 random(42);
    for (char &c : garbage)
        c = char(random() & 0xFF);
    QTest::newRow("random bytes") << garbage;
    QTest::newRow("one byte") << QByteArray(1, 'x');
    QTest::newRow("text") << QByteArray("Zażółć gęślą jaźń");
    QTest::newRow("truncated font") << font.left(100);
    QTest::newRow("header only") << font.left(12);
    QTest::newRow("TrueType tag and garbage") << (QByteArray("\x00\x01\x00\x00", 4) + garbage);
    QTest::newRow("OTTO and garbage") << (QByteArray("OTTO") + garbage);
    QTest::newRow("collection tag and garbage") << (QByteArray("ttcf\x00\x02\x00\x00\x00\x00\x00\x09", 12) + garbage);
    QByteArray scrambled = font;
    for (qsizetype i = 200; i < scrambled.size(); i += 7)
        scrambled[i] = char(random() & 0xFF);
    QTest::newRow("scrambled tables") << scrambled;
}

void TestAtramentTextFonts::malformedData()
{
    QFETCH(QByteArray, bytes);
    FontCollection fonts;
    auto data = std::make_shared<std::vector<std::uint8_t>>(bytes.begin(), bytes.end());
    const std::vector<FaceId> faces = fonts.addFontData(data);
    // Scrambled tables may still give a face HarfBuzz can read (it sanitizes every table): then it
    // must be usable without crashing; everything else gives nothing.
    QCOMPARE(fonts.faceCount(), faces.size());
    for (const FaceId face : faces) {
        (void)fonts.faceInfo(face);
        (void)fonts.metrics(face);
        (void)fonts.faceFor(U'a', face);
        hb_buffer_t *buffer = hb_buffer_create();
        hb_buffer_add_utf8(buffer, "Zażółć", -1, 0, -1);
        hb_buffer_guess_segment_properties(buffer);
        hb_shape(fonts.hbFont(face), buffer, nullptr, 0);
        hb_buffer_destroy(buffer);
    }
    if (QTest::currentDataTag() != QByteArrayLiteral("scrambled tables"))
        QVERIFY(faces.empty());
    QVERIFY(fonts.addFontData(nullptr).empty());
    QVERIFY(fonts.addFontData(std::make_shared<std::vector<std::uint8_t>>()).empty());
}

void TestAtramentTextFonts::missingFile()
{
    FontCollection fonts;
    QVERIFY(fonts.addFontFile("/nonexistent/font.ttf").empty());
    QVERIFY(fonts.addFontFile("").empty());
    QVERIFY(fonts.addFontFile(BRULION_SOURCE_DIR).empty()); // a directory
    QCOMPARE(fonts.faceCount(), std::size_t(0));
}

QTEST_GUILESS_MAIN(TestAtramentTextFonts)
#include "tst_atrament_text_fonts.moc"
