// The UI benchmark's metrics (uimetrics.h; docs/UI.md "UI benchmark (Brulion vs Obsidian)") on
// synthetic shots drawn with QPainter: WCAG text contrast on the sampled background (alpha, large
// text, rich-text fragments with a background, links, emoji, Polish glyphs), the background sampler,
// target sizes (WCAG 2.5.8 on the desktop, 44 px on phones), the spacing grid, the type scale,
// alignment, line length, token contrast, the objective score's weights, the dump parser on
// malformed input, screens.json's checker, the lint allow list and the command line.
#include "uimetrics.h"

#include "themepalette.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;
using namespace brulion::uibench;

namespace {

double wcag(const QString &fg, const QString &bg)
{
    const auto a = brulion::ui::parseColor(fg);
    const auto b = brulion::ui::parseColor(bg);
    return brulion::ui::contrast(*a, *b);
}

QImage filled(int w, int h, const QColor &bg)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(bg);
    return img;
}

void drawText(QImage &img, const QRectF &rect, const QColor &color, double px, int weight, const QString &text)
{
    QPainter p(&img);
    QFont font;
    font.setPixelSize(int(px));
    font.setWeight(QFont::Weight(weight));
    p.setFont(font);
    p.setPen(color);
    p.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, text);
}

LayoutDump dumpOf(QSizeF viewport, double dpr = 1)
{
    LayoutDump d;
    d.app = u"test"_s;
    d.viewport = viewport;
    d.dpr = dpr;
    return d;
}

TextItem textItem(const QRectF &rect, const QColor &color, double px, const QString &text, int weight = 400, int lines = 1)
{
    TextItem t;
    t.rect = rect;
    t.color = color;
    t.px = px;
    t.weight = weight;
    t.text = text;
    t.chars = int(text.size());
    t.lines = lines;
    return t;
}

/// "x y w h [i][d][o]; …" → targets (i inline, d disabled, o occluded).
QList<Target> targetsFrom(const QString &spec)
{
    QList<Target> out;
    for (const QString &part : spec.split(u';', Qt::SkipEmptyParts)) {
        const QStringList f = part.simplified().split(u' ');
        Target t;
        t.rect = QRectF(f.value(0).toDouble(), f.value(1).toDouble(), f.value(2).toDouble(), f.value(3).toDouble());
        const QString flags = f.value(4);
        t.inlineTarget = flags.contains(u'i');
        t.enabled = !flags.contains(u'd');
        t.occluded = flags.contains(u'o');
        t.label = u"t%1"_s.arg(out.size());
        out.append(t);
    }
    return out;
}

QByteArray minimalDump(const QString &textsJson = u"[]"_s)
{
    return (uR"({"schema": 1, "app": "x", "viewport": {"w": 100, "h": 50, "dpr": 1}, "texts": )"_s + textsJson
            + uR"(, "targets": [], "boxes": [], "tokens": {}})"_s)
        .toUtf8();
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestUiBenchMetrics : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void contrast_data();
    void contrast();
    void background_data();
    void background();
    void targets_data();
    void targets();
    void spacing_data();
    void spacing();
    void typeScale_data();
    void typeScale();
    void alignment_data();
    void alignment();
    void lineLength_data();
    void lineLength();
    void tokenContrast_data();
    void tokenContrast();
    void objective_data();
    void objective();
    void parseDump_data();
    void parseDump();
    void parseDumpLarge();
    void unicodeRoundTrip();
    void manifest_data();
    void manifest();
    void lintAllow();
    void cli();
};

void TestUiBenchMetrics::contrast_data()
{
    QTest::addColumn<QString>("fg");
    QTest::addColumn<QString>("bg");
    QTest::addColumn<double>("px");
    QTest::addColumn<int>("weight");
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("fragments"); // "color|bg|text;…" (fragments < 3 characters are skipped)
    QTest::addColumn<double>("expected");   // -1: skipped (no result)
    QTest::addColumn<double>("required");
    QTest::addColumn<bool>("pass");

    const QString ink = u"#1f2430ff"_s, paper = u"#fbf7ee"_s;
    QTest::newRow("ink-on-paper") << ink << paper << 14.0 << 400 << u"Zwykły tekst notatki"_s << QString()
                                  << wcag(u"#1f2430"_s, paper) << 4.5 << true;
    QTest::newRow("faint-on-paper") << u"#676c7bff"_s << paper << 14.0 << 400 << u"Etykieta pomocnicza"_s << QString()
                                    << wcag(u"#676c7b"_s, paper) << 4.5 << true;
    QTest::newRow("obsidian-faint") << u"#abababff"_s << u"#ffffff"_s << 14.0 << 400 << u"Faint text"_s << QString()
                                    << wcag(u"#ababab"_s, u"#ffffff"_s) << 4.5 << false;
    // #00000080 over white is drawn as #7f7f7f.
    QTest::newRow("alpha-text") << u"#00000080"_s << u"#ffffff"_s << 14.0 << 400 << u"Półprzezroczysty"_s << QString()
                                << wcag(u"#7f7f7f"_s, u"#ffffff"_s) << 4.5 << false;
    QTest::newRow("alpha-text-large") << u"#00000080"_s << u"#ffffff"_s << 24.0 << 400 << u"Nagłówek"_s << QString()
                                      << wcag(u"#7f7f7f"_s, u"#ffffff"_s) << 3.0 << true;
    QTest::newRow("bold-large") << u"#7f7f7fff"_s << u"#ffffff"_s << 18.66 << 700 << u"Pogrubiony"_s << QString()
                                << wcag(u"#7f7f7f"_s, u"#ffffff"_s) << 3.0 << true;
    QTest::newRow("bold-small") << u"#7f7f7fff"_s << u"#ffffff"_s << 18.0 << 700 << u"Pogrubiony"_s << QString()
                                << wcag(u"#7f7f7f"_s, u"#ffffff"_s) << 4.5 << false;
    // A tag chip: #9fc3e6 at 0x52/255 over the paper under the ink.
    const auto chip = brulion::ui::over(*brulion::ui::parseColor(u"#9fc3e6"), 0x52 / 255.0, *brulion::ui::parseColor(paper));
    QTest::newRow("chip-background") << ink << paper << 14.0 << 400 << u"przed #bieszczady"_s
                                     << u"#1f2430ff||przed ;#1f2430ff|#9fc3e652|#bieszczady"_s
                                     << brulion::ui::contrast(*brulion::ui::parseColor(u"#1f2430"), chip) << 4.5 << true;
    QTest::newRow("link-fragment") << ink << paper << 14.0 << 400 << u"Jedziemy z ekipą od remontu"_s
                                   << u"#1f2430ff||Jedziemy z ;#2c6aa3ff||ekipą od remontu;#c0c0c0ff||ok"_s
                                   << wcag(u"#2c6aa3"_s, paper) << 4.5 << true;
    QTest::newRow("emoji-only") << ink << paper << 14.0 << 400 << u"🙂🙂"_s << QString() << -1.0 << 4.5 << true;
    QTest::newRow("polish") << u"#535a6aff"_s << paper << 14.0 << 400 << u"Zażółć gęślą jaźń"_s << QString()
                            << wcag(u"#535a6a"_s, paper) << 4.5 << true;
}

void TestUiBenchMetrics::contrast()
{
    QFETCH(QString, fg);
    QFETCH(QString, bg);
    QFETCH(double, px);
    QFETCH(int, weight);
    QFETCH(QString, text);
    QFETCH(QString, fragments);
    QFETCH(double, expected);
    QFETCH(double, required);
    QFETCH(bool, pass);

    QImage img = filled(240, 60, parseHex(bg));
    const QRectF rect(10, 10, 220, 40);
    // The chip's tint under its fragment only (the sampled background stays the paper).
    if (fragments.contains(u"#9fc3e652"))
        QPainter(&img).fillRect(QRectF(80, 18, 70, 24), parseHex(u"#9fc3e652"_s));
    drawText(img, rect, parseHex(fg), px, weight, text);
    LayoutDump d = dumpOf(QSizeF(240, 60));
    TextItem t = textItem(rect, parseHex(fg), px, text, weight);
    for (const QString &f : fragments.split(u';', Qt::SkipEmptyParts)) {
        const QStringList parts = f.split(u'|');
        Fragment fr;
        fr.color = parseHex(parts.value(0));
        if (!parts.value(1).isEmpty())
            fr.background = parseHex(parts.value(1));
        fr.text = parts.value(2);
        fr.chars = int(fr.text.size());
        t.fragments.append(fr);
    }
    d.texts.append(t);
    const QList<TextContrast> result = textContrast(d, img);
    if (expected < 0) {
        QVERIFY(result.isEmpty());
        return;
    }
    QCOMPARE(result.size(), 1);
    QVERIFY2(std::abs(result.first().ratio - expected) < 0.1,
             qPrintable(u"ratio %1, expected %2"_s.arg(result.first().ratio).arg(expected)));
    QCOMPARE(result.first().required, required);
    QCOMPARE(result.first().pass, pass);
    if (QLatin1StringView(QTest::currentDataTag()) == "polish"_L1)
        QCOMPARE(toHex(result.first().bg), u"#fbf7eeff"_s);
}

void TestUiBenchMetrics::background_data()
{
    QTest::addColumn<QString>("kind");
    QTest::addColumn<QRectF>("rect");
    QTest::addColumn<double>("dpr");
    QTest::addColumn<QString>("expected");

    QTest::newRow("plain fill") << u"plain"_s << QRectF(20, 10, 60, 20) << 1.0 << u"#fbf7eeff"_s;
    QTest::newRow("text-covered box") << u"box"_s << QRectF(110, 20, 80, 24) << 1.0 << u"#1c2130ff"_s;
    QTest::newRow("box at the edge") << u"plain"_s << QRectF(-10, -5, 40, 20) << 1.0 << u"#fbf7eeff"_s;
    QTest::newRow("larger than 20000 px") << u"plain"_s << QRectF(0, 0, 300, 120) << 1.0 << u"#fbf7eeff"_s;
    QTest::newRow("dpr 2") << u"halves"_s << QRectF(160, 10, 60, 30) << 2.0 << u"#2c6aa3ff"_s;
}

void TestUiBenchMetrics::background()
{
    QFETCH(QString, kind);
    QFETCH(QRectF, rect);
    QFETCH(double, dpr);
    QFETCH(QString, expected);

    QImage img = filled(int(300 * dpr), int(120 * dpr), parseHex(u"#fbf7ee"_s));
    if (kind == u"box") {
        QPainter p(&img);
        p.fillRect(QRectF(100, 10, 120, 50), parseHex(u"#1c2130"_s));
        p.end();
        drawText(img, QRectF(110, 20, 100, 24), Qt::white, 16, 700, u"Zażółć gęślą"_s);
    } else if (kind == u"halves") {
        QPainter(&img).fillRect(QRectF(img.width() / 2.0, 0, img.width() / 2.0, img.height()), parseHex(u"#2c6aa3"_s));
    }
    QCOMPARE(toHex(sampleBackground(img, rect, dpr)), expected);
}

void TestUiBenchMetrics::targets_data()
{
    QTest::addColumn<bool>("phone");
    QTest::addColumn<QString>("spec");
    QTest::addColumn<QString>("expected"); // pass per checked target, "" when none is checked

    QTest::newRow("24x24") << false << u"10 10 24 24"_s << u"1"_s;
    QTest::newRow("20x20 isolated") << false << u"10 10 20 20; 100 10 20 20"_s << u"1,1"_s;
    // WCAG 2.5.8: 20-px targets 10 px apart have their centres 30 px apart — enough; 2 px apart
    // their 24-px circles meet.
    QTest::newRow("20x20 neighbours 10 px apart") << false << u"10 10 20 20; 40 10 20 20"_s << u"1,1"_s;
    QTest::newRow("20x20 neighbours 2 px apart") << false << u"10 10 20 20; 32 10 20 20"_s << u"0,0"_s;
    QTest::newRow("20x20 next to a large target") << false << u"10 10 20 20; 30 0 100 40"_s << u"0,1"_s;
    QTest::newRow("inside a row") << false << u"0 0 300 32; 270 6 20 20"_s << u"1,1"_s;
    QTest::newRow("phone 40x44") << true << u"10 10 40 44"_s << u"0"_s;
    QTest::newRow("phone 44x44") << true << u"10 10 44 44"_s << u"1"_s;
    QTest::newRow("inline link exempt") << false << u"10 10 30 14 i"_s << QString();
    QTest::newRow("disabled exempt") << true << u"10 10 20 20 d"_s << QString();
    QTest::newRow("occluded ignored") << true << u"10 10 20 20 o"_s << QString();
}

void TestUiBenchMetrics::targets()
{
    QFETCH(bool, phone);
    QFETCH(QString, spec);
    QFETCH(QString, expected);

    LayoutDump d = dumpOf(QSizeF(400, 200));
    d.targets = targetsFrom(spec);
    const QList<TargetCheck> checks = targetSizes(d, phone ? Device::Phone : Device::Desktop);
    QStringList got;
    for (const TargetCheck &c : checks)
        got.append(c.pass ? u"1"_s : u"0"_s);
    QCOMPARE(got.join(u','), expected);
}

void TestUiBenchMetrics::spacing_data()
{
    QTest::addColumn<double>("inset");     // -1: no box
    QTest::addColumn<double>("gap");       // -1: one text
    QTest::addColumn<double>("share");
    QTest::addColumn<int>("distances");

    QTest::newRow("inset 8") << 8.0 << -1.0 << 1.0 << 2;
    QTest::newRow("inset 9 (±1)") << 9.0 << -1.0 << 1.0 << 2;
    QTest::newRow("inset 10") << 10.0 << -1.0 << 0.0 << 2;
    QTest::newRow("inset 0") << 0.0 << -1.0 << 1.0 << 2;
    QTest::newRow("gap 12") << -1.0 << 12.0 << 1.0 << 1;
    QTest::newRow("gap 10") << -1.0 << 10.0 << 0.0 << 1;
    QTest::newRow("gap 64 ignored") << -1.0 << 64.0 << 1.0 << 0;
}

void TestUiBenchMetrics::spacing()
{
    QFETCH(double, inset);
    QFETCH(double, gap);
    QFETCH(double, share);
    QFETCH(int, distances);

    LayoutDump d = dumpOf(QSizeF(400, 400));
    if (inset >= 0) {
        Box b;
        b.rect = QRectF(20, 20, 200, 100);
        b.color = Qt::white;
        d.boxes.append(b);
        d.texts.append(textItem(QRectF(20 + inset, 20 + inset, 80, 16), Qt::black, 14, u"Tekst"_s));
    } else {
        d.texts.append(textItem(QRectF(10, 10, 80, 16), Qt::black, 14, u"Pierwszy"_s));
        d.texts.append(textItem(QRectF(10, 26 + gap, 80, 16), Qt::black, 14, u"Drugi"_s));
    }
    QList<double> list;
    QCOMPARE(spacingGridShare(d, 4, 1.0, &list), share);
    QCOMPARE(list.size(), distances);
}

void TestUiBenchMetrics::typeScale_data()
{
    QTest::addColumn<QList<double>>("sizes");
    QTest::addColumn<QList<int>>("weights");
    QTest::addColumn<double>("score");
    QTest::addColumn<int>("nearDuplicates");

    QTest::newRow("12 14 16 20 24 30") << QList<double>{ 12, 14, 16, 20, 24, 30 } << QList<int>{ 400 } << 10.0 << 0;
    QTest::newRow("13 … 18, 20") << QList<double>{ 13, 14, 15, 16, 17, 18, 20 } << QList<int>{ 400 } << 1.5 << 5;
    QTest::newRow("one size") << QList<double>{ 16 } << QList<int>{ 400 } << 8.0 << 0;
    QTest::newRow("four weights") << QList<double>{ 12, 16, 24 } << QList<int>{ 400, 500, 600, 700 } << 9.0 << 0;
}

void TestUiBenchMetrics::typeScale()
{
    QFETCH(QList<double>, sizes);
    QFETCH(QList<int>, weights);
    QFETCH(double, score);
    QFETCH(int, nearDuplicates);

    LayoutDump d = dumpOf(QSizeF(800, 800));
    int y = 0;
    for (const double px : sizes) {
        for (const int w : weights) {
            d.texts.append(textItem(QRectF(10, y, 300, px * 1.4), Qt::black, px, QString(50, u'a'), w));
            y += 40;
        }
    }
    // A size with < 2 % of the characters does not count.
    d.texts.append(textItem(QRectF(10, y, 30, 12), Qt::black, 9, u"x"_s));
    const TypeScale s = brulion::uibench::typeScale(d);
    QCOMPARE(s.score, score);
    QCOMPARE(s.nearDuplicates, nearDuplicates);
    QCOMPARE(s.sizes.size(), sizes.size());
}

void TestUiBenchMetrics::alignment_data()
{
    QTest::addColumn<QString>("layout");
    QTest::addColumn<double>("score");

    QTest::newRow("shared left edge") << u"shared"_s << 10.0;
    QTest::newRow("scattered") << u"scattered"_s << 4.0;
    QTest::newRow("row centred") << u"row-centred"_s << 10.0;
    QTest::newRow("row off by 5 px") << u"row-off"_s << 6.0;
}

void TestUiBenchMetrics::alignment()
{
    QFETCH(QString, layout);
    QFETCH(double, score);

    LayoutDump d = dumpOf(QSizeF(800, 800));
    if (layout == u"shared") {
        for (int i = 0; i < 3; ++i)
            d.texts.append(textItem(QRectF(10, 10 + i * 40, 100, 20), Qt::black, 14, u"Wiersz"_s));
    } else if (layout == u"scattered") {
        for (int i = 0; i < 3; ++i)
            d.texts.append(textItem(QRectF(10 + i * 20, 10 + i * 40, 100, 20), Qt::black, 14, u"Wiersz"_s));
    } else {
        // Three rows: label + button side by side, the left edges shared (labels at 10, buttons at 200).
        const double off = layout == u"row-off" ? 5 : 0.5;
        for (int i = 0; i < 3; ++i) {
            d.texts.append(textItem(QRectF(10, 10 + i * 50, 100, 20), Qt::black, 14, u"Etykieta"_s));
            Target t;
            t.rect = QRectF(200, 10 + i * 50 - 10 + off, 40, 40);
            d.targets.append(t);
        }
    }
    QCOMPARE(alignmentScore(d), score);
}

void TestUiBenchMetrics::lineLength_data()
{
    QTest::addColumn<bool>("phone");
    QTest::addColumn<int>("chars");
    QTest::addColumn<int>("lines");
    QTest::addColumn<double>("share"); // -1: n/a

    QTest::newRow("75 per line") << false << 600 << 8 << 1.0;
    QTest::newRow("760-px serif paragraph ≈ 95") << false << 950 << 10 << 0.0;
    QTest::newRow("phone") << true << 600 << 8 << -1.0;
    QTest::newRow("no body text") << false << 100 << 2 << -1.0;
}

void TestUiBenchMetrics::lineLength()
{
    QFETCH(bool, phone);
    QFETCH(int, chars);
    QFETCH(int, lines);
    QFETCH(double, share);

    LayoutDump d = dumpOf(QSizeF(1280, 800));
    TextItem t = textItem(QRectF(100, 100, 700, lines * 24.0), Qt::black, 16, QString(80, u'a'), 400, lines);
    t.chars = chars;
    d.texts.append(t);
    d.texts.append(textItem(QRectF(100, 40, 300, 30), Qt::black, 24, u"Nagłówek"_s));
    const auto got = lineLengthShare(d, phone ? Device::Phone : Device::Desktop);
    if (share < 0) {
        QVERIFY(!got.has_value());
        return;
    }
    QVERIFY(got.has_value());
    QCOMPARE(*got, share);
}

void TestUiBenchMetrics::tokenContrast_data()
{
    QTest::addColumn<QStringList>("tokens"); // fg fgMuted fgFaint bg bgSunken
    QTest::addColumn<double>("expected");    // -1: n/a

    QTest::newRow("black on white") << QStringList{ u"#000000"_s, u"#000000"_s, u"#000000"_s, u"#ffffff"_s, u"#ffffff"_s } << 10.0;
    const double faint = std::min(1.0, wcag(u"#ababab"_s, u"#ffffff"_s) / 4.5);
    const double faintSunken = std::min(1.0, wcag(u"#ababab"_s, u"#f6f6f6"_s) / 4.5);
    const double muted = std::min(1.0, wcag(u"#5c5c5c"_s, u"#f6f6f6"_s) / 4.5);
    QTest::newRow("faint grey") << QStringList{ u"#222222"_s, u"#5c5c5c"_s, u"#ababab"_s, u"#ffffff"_s, u"#f6f6f6"_s }
                                << 10.0 * (1 + 1 + faint + 1 + muted + faintSunken) / 6;
    QTest::newRow("missing") << QStringList{ u"#222222"_s, u"#5c5c5c"_s, u"#ababab"_s, u"#ffffff"_s, QString() } << -1.0;
}

void TestUiBenchMetrics::tokenContrast()
{
    QFETCH(QStringList, tokens);
    QFETCH(double, expected);

    LayoutDump d = dumpOf(QSizeF(10, 10));
    const QStringList names{ u"fg"_s, u"fgMuted"_s, u"fgFaint"_s, u"bg"_s, u"bgSunken"_s };
    for (int i = 0; i < names.size(); ++i) {
        if (!tokens.value(i).isEmpty())
            d.tokens.insert(names.at(i), parseHex(tokens.value(i)));
    }
    const auto k = brulion::uibench::tokenContrast(d);
    if (expected < 0) {
        QVERIFY(!k.has_value());
        return;
    }
    QVERIFY(k.has_value());
    QVERIFY2(std::abs(*k - expected) < 1e-9, qPrintable(u"%1 vs %2"_s.arg(*k).arg(expected)));
}

void TestUiBenchMetrics::objective_data()
{
    QTest::addColumn<double>("L");
    QTest::addColumn<double>("K");
    QTest::addColumn<double>("expected");

    // C 10, T 0, S 10, Y 10, A 10.
    QTest::newRow("without L and K") << -1.0 << -1.0 << 6.5 / 0.85;
    QTest::newRow("with K") << -1.0 << 10.0 << 7.5 / 0.95;
    QTest::newRow("with L and K") << 10.0 << 10.0 << 8.0;
    QTest::newRow("L 0") << 0.0 << 10.0 << 7.5;
}

void TestUiBenchMetrics::objective()
{
    QFETCH(double, L);
    QFETCH(double, K);
    QFETCH(double, expected);
    Objective o;
    o.C = 10;
    o.T = 0;
    o.S = 10;
    o.Y = 10;
    o.A = 10;
    if (L >= 0)
        o.L = L;
    if (K >= 0)
        o.K = K;
    QVERIFY(std::abs(objectiveScore(o) - expected) < 1e-9);

    // A whole shot: an empty screen scores 10 in everything it can.
    const LayoutDump d = dumpOf(QSizeF(100, 50));
    const Objective whole = brulion::uibench::objective(d, filled(100, 50, Qt::white), Device::Desktop, u"palette"_s);
    QCOMPARE(whole.score, 10.0);
    QVERIFY(!whole.L.has_value());
}

void TestUiBenchMetrics::parseDump_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<bool>("ok");

    QTest::newRow("valid") << minimalDump(u"[{\"rect\": [1, 2, 30, 14], \"color\": \"#1f2430ff\", \"px\": 14}]"_s) << true;
    QTest::newRow("valid #rrggbb") << minimalDump(u"[{\"rect\": [1, 2, 30, 14], \"color\": \"#1f2430\", \"px\": 14}]"_s) << true;
    QTest::newRow("not JSON") << QByteArray("{schema: 1") << false;
    QTest::newRow("schema 2") << minimalDump().replace("\"schema\": 1", "\"schema\": 2") << false;
    QTest::newRow("no viewport") << QByteArray(R"({"schema": 1, "texts": [], "targets": [], "boxes": []})") << false;
    QTest::newRow("colour red") << minimalDump(u"[{\"rect\": [1, 2, 30, 14], \"color\": \"red\", \"px\": 14}]"_s) << false;
    QTest::newRow("colour #12345") << minimalDump(u"[{\"rect\": [1, 2, 30, 14], \"color\": \"#12345\", \"px\": 14}]"_s) << false;
    QTest::newRow("oklch") << minimalDump(u"[{\"rect\": [1, 2, 30, 14], \"color\": \"oklch(0.5 0.1 200)\", \"px\": 14}]"_s) << false;
    QTest::newRow("NaN coordinates") << minimalDump(u"[{\"rect\": [NaN, 2, 30, 14], \"color\": \"#000000\", \"px\": 14}]"_s) << false;
    QTest::newRow("null coordinate") << minimalDump(u"[{\"rect\": [null, 2, 30, 14], \"color\": \"#000000\", \"px\": 14}]"_s) << false;
    QTest::newRow("negative size") << minimalDump(u"[{\"rect\": [1, 2, -30, 14], \"color\": \"#000000\", \"px\": 14}]"_s) << false;
    QTest::newRow("missing px") << minimalDump(u"[{\"rect\": [1, 2, 30, 14], \"color\": \"#000000\"}]"_s) << false;
    QTest::newRow("texts not an array") << minimalDump(u"{}"_s) << false;
    QTest::newRow("zero viewport") << minimalDump().replace("\"w\": 100", "\"w\": 0") << false;
}

void TestUiBenchMetrics::parseDump()
{
    QFETCH(QByteArray, json);
    QFETCH(bool, ok);
    QString error;
    const auto dump = brulion::uibench::parseDump(json, &error);
    QCOMPARE(dump.has_value(), ok);
    QCOMPARE(error.isEmpty(), ok);
}

void TestUiBenchMetrics::parseDumpLarge()
{
    QJsonArray texts;
    for (int i = 0; i < 10000; ++i) {
        texts.append(QJsonObject{ { u"rect"_s, QJsonArray{ 10, i * 2, 200, 16 } }, { u"color"_s, u"#1f2430ff"_s },
                                  { u"px"_s, 14 }, { u"text"_s, u"Wiersz %1 zażółć"_s.arg(i) } });
    }
    const QByteArray json = minimalDump(QString::fromUtf8(QJsonDocument(texts).toJson(QJsonDocument::Compact)));
    QElapsedTimer timer;
    timer.start();
    QString error;
    const auto dump = brulion::uibench::parseDump(json, &error);
    QVERIFY2(dump.has_value(), qPrintable(error));
    QCOMPARE(dump->texts.size(), 10000);
    QVERIFY2(timer.elapsed() < 1000 * BRULION_TEST_SLOWDOWN, qPrintable(u"%1 ms"_s.arg(timer.elapsed())));
}

void TestUiBenchMetrics::unicodeRoundTrip()
{
    const QString text = u"Zażółć gęślą jaźń 🙂"_s;
    const QByteArray json = minimalDump(u"[{\"rect\": [5, 5, 200, 20], \"color\": \"#cfcfcfff\", \"px\": 14, \"text\": \"%1\"}]"_s.arg(text));
    QString error;
    const auto dump = brulion::uibench::parseDump(json, &error);
    QVERIFY2(dump.has_value(), qPrintable(error));
    QCOMPARE(dump->texts.first().text, text);
    QImage img = filled(100, 50, Qt::white);
    const QJsonObject m = measureJson(*dump, img, Device::Desktop, u"doc"_s, { { u"theme"_s, u"light"_s } });
    const QJsonObject back = QJsonDocument::fromJson(QJsonDocument(m).toJson()).object();
    const QJsonArray failing = back[u"details"_s][u"contrast"_s][u"failing"_s].toArray();
    QCOMPARE(failing.size(), 1);
    QCOMPARE(failing.first()[u"text"_s].toString(), text);
    QCOMPARE(back[u"theme"_s].toString(), u"light"_s);
    QCOMPARE(back[u"app"_s].toString(), u"x"_s);
}

void TestUiBenchMetrics::manifest_data()
{
    QTest::addColumn<QString>("change");
    QTest::addColumn<QString>("error"); // "" = valid

    QTest::newRow("the real screens.json") << QString() << QString();
    QTest::newRow("unknown verb") << u"verb"_s << u"unknown verb"_s;
    QTest::newRow("wrong arity") << u"arity"_s << u"takes 1 argument"_s;
    QTest::newRow("duplicate id") << u"duplicate"_s << u"duplicate id"_s;
    QTest::newRow("unknown size") << u"size"_s << u"unknown size"_s;
    QTest::newRow("non-string arg") << u"arg"_s << u"non-string"_s;
}

void TestUiBenchMetrics::manifest()
{
    QFETCH(QString, change);
    QFETCH(QString, error);
    const QByteArray real = readFile(QStringLiteral(BRULION_UIBENCH_DIR "/screens.json"));
    QVERIFY(!real.isEmpty());
    QJsonObject root = QJsonDocument::fromJson(real).object();
    QJsonArray screens = root[u"screens"_s].toArray();
    QJsonObject first = screens.first().toObject();
    QJsonObject obsidian = first[u"obsidian"_s].toObject();
    QJsonObject brulion = first[u"brulion"_s].toObject();
    if (change == u"verb")
        obsidian[u"steps"_s] = QJsonArray{ QJsonArray{ u"teleport"_s } };
    else if (change == u"arity")
        obsidian[u"steps"_s] = QJsonArray{ QJsonArray{ u"open"_s } };
    else if (change == u"size")
        first[u"sizes"_s] = QJsonArray{ u"tablet"_s };
    else if (change == u"arg")
        brulion[u"args"_s] = QJsonArray{ u"--route"_s, 7 };
    first[u"obsidian"_s] = obsidian;
    first[u"brulion"_s] = brulion;
    screens[0] = first;
    if (change == u"duplicate")
        screens.append(first);
    root[u"screens"_s] = screens;
    const QStringList errors = checkManifest(QJsonDocument(root).toJson());
    if (error.isEmpty()) {
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(u"; ")));
        QCOMPARE(screens.size(), 14);
    } else {
        QVERIFY2(errors.join(u"; ").contains(error), qPrintable(errors.join(u"; ")));
    }
}

void TestUiBenchMetrics::lintAllow()
{
    LayoutDump d = dumpOf(QSizeF(200, 60));
    TextItem t = textItem(QRectF(10, 10, 150, 20), parseHex(u"#c8c8c8"_s), 14, u"Słaby podpis"_s);
    t.type = u"BText"_s;
    d.texts.append(t);
    const QImage img = filled(200, 60, Qt::white);
    QString error;
    QCOMPARE(lint(d, img, Device::Desktop, u"doc"_s, "[]", &error).size(), 1);
    QVERIFY(error.isEmpty());
    const QByteArray allow = R"([{"screen": "doc", "type": "BText", "text": "Słaby", "reason": "dekoracja"}])";
    QCOMPARE(lint(d, img, Device::Desktop, u"doc"_s, allow, &error).size(), 0);
    QCOMPARE(lint(d, img, Device::Desktop, u"settings"_s, allow, &error).size(), 1);
    lint(d, img, Device::Desktop, u"doc"_s, R"([{"screen": "doc", "type": "BText"}])", &error);
    QVERIFY(error.contains(u"reason"));
    error.clear();
    lint(d, img, Device::Desktop, u"doc"_s, R"([{"screen": "doc", "reason": "x"}])", &error);
    QVERIFY(error.contains(u"type"));
    error.clear();

    // `count`: a baseline of known failures — one more is caught; `kind` keeps targets apart.
    TextItem second = t;
    second.rect = QRectF(10, 32, 150, 20);
    second.text = u"Słaby drugi"_s;
    d.texts.append(second);
    Target small;
    small.rect = QRectF(150, 5, 10, 10);
    small.type = u"BText"_s;
    small.label = u"Słaby"_s;
    d.targets.append(small);
    const QByteArray one = R"([{"screen": "doc", "kind": "contrast", "type": "BText", "text": "Słaby", "count": 1, "reason": "x"}])";
    const QList<LintFailure> left = lint(d, img, Device::Phone, u"doc"_s, one, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(left.size(), 2);
    QCOMPARE(left.at(0).kind, u"contrast"_s);
    QCOMPARE(left.at(0).text, u"Słaby drugi"_s);
    QCOMPARE(left.at(1).kind, u"target"_s);
    const QByteArray two = R"([{"screen": "*", "kind": "contrast", "type": "BText", "count": 2, "reason": "x"},
                              {"kind": "target", "text": "Sła", "reason": "x"}])";
    QCOMPARE(lint(d, img, Device::Phone, u"doc"_s, two, &error).size(), 0);
}

void TestUiBenchMetrics::cli()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = dir.filePath(u"x.png"_s), dump = dir.filePath(u"x.layout.json"_s);
    QVERIFY(filled(120, 60, Qt::white).save(png));
    {
        QFile f(dump);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(minimalDump(u"[{\"rect\": [1, 2, 30, 14], \"color\": \"#1f2430ff\", \"px\": 14, \"text\": \"abc\"}]"_s));
    }
    QProcess p;
    // 120×60 px, but the dump says 100×50 at dpr 1.
    p.start(QStringLiteral(BRULION_UIMETRICS_BIN), { u"measure"_s, u"--png"_s, png, u"--dump"_s, dump, u"--device"_s,
                                                    u"desktop"_s, u"--out"_s, dir.filePath(u"m.json"_s) });
    QVERIFY(p.waitForFinished(30000));
    QCOMPARE(p.exitCode(), 2);
    QVERIFY(p.readAllStandardError().contains("x.png"));

    QVERIFY(filled(100, 50, Qt::white).save(png));
    p.start(QStringLiteral(BRULION_UIMETRICS_BIN), { u"measure"_s, u"--png"_s, png, u"--dump"_s, dump, u"--device"_s,
                                                    u"desktop"_s, u"--screen"_s, u"doc"_s, u"--out"_s, dir.filePath(u"m.json"_s) });
    QVERIFY(p.waitForFinished(30000));
    QCOMPARE(p.exitCode(), 0);
    const QJsonObject m = QJsonDocument::fromJson(readFile(dir.filePath(u"m.json"_s))).object();
    QVERIFY(m[u"scores"_s][u"O"_s].isDouble());

    p.start(QStringLiteral(BRULION_UIMETRICS_BIN), { u"check-manifest"_s, QStringLiteral(BRULION_UIBENCH_DIR "/screens.json") });
    QVERIFY(p.waitForFinished(30000));
    QCOMPARE(p.exitCode(), 0);
}

QTEST_MAIN(TestUiBenchMetrics)
#include "tst_uibench_metrics.moc"
