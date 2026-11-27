// PictureCache (src/app/editor/atrament/picturecache.h; docs/ATRAMENT.md M2 step 5, "Pictures"): a
// note's pictures decoded off the GUI thread for Atrament — at the side their place shows them
// (PictureDecode's steps, never enlarged), premultiplied RGBA, from local files and data: URLs,
// held pictures never evicted, the least recently asked for going first over the byte budget, a
// header claiming too many pixels refused, a failure remembered until the file changes.
#include "picturecache.h"

#include <QBuffer>
#include <QImage>
#include <QImageWriter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <array>

using namespace brulion::editor::atr;
using namespace Qt::StringLiterals;

namespace {

QByteArray png(int w, int h, QColor colour)
{
    QImage image(w, h, QImage::Format_ARGB32);
    image.fill(colour);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "png");
    writer.write(image);
    return bytes;
}

QUrl writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes)
{
    QFile f(dir.filePath(name));
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(bytes);
    return QUrl::fromLocalFile(f.fileName());
}

quint32 crc32(const QByteArray &data)
{
    quint32 c = 0xffffffffu;
    for (const char ch : data) {
        c ^= quint8(ch);
        for (int k = 0; k < 8; ++k)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return c ^ 0xffffffffu;
}

QByteArray be32(quint32 v)
{
    return QByteArray(std::array<char, 4>{char(v >> 24), char(v >> 16), char(v >> 8), char(v)}.data(), 4);
}

// A PNG whose header says `w` × `h` and nothing else (a picture of 10 000 × 10 000 for nothing).
QByteArray pngHeaderOnly(quint32 w, quint32 h)
{
    QByteArray ihdr = "IHDR" + be32(w) + be32(h);
    ihdr.append(char(8)).append(char(6)).append(char(0)).append(char(0)).append(char(0));
    return QByteArray("\x89PNG\r\n\x1a\n", 8) + be32(13) + ihdr + be32(crc32(ihdr));
}

} // namespace

class TestAtramentPictureCache : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void sidesInSteps();
    void decodesForTheSide();
    void dataUrlsAndNames();
    void budgetEvictsTheLeastRecent();
    void refusesHugeHeaders();
    void failureRemembered();
};

void TestAtramentPictureCache::sidesInSteps()
{
    QCOMPARE(PictureCache::sideFor(10, 10, 1), PictureCache::MinSide);
    QCOMPARE(PictureCache::sideFor(400, 300, 1), 448);  // 64 px steps up to 512
    QCOMPARE(PictureCache::sideFor(400, 300, 2), 896);  // 128 px steps above
    QCOMPARE(PictureCache::sideFor(1500, 800, 2), PictureCache::MaxSide);
    QCOMPARE(PictureCache::sideFor(0, 0, 0), PictureCache::MinSide);
    QVERIFY(PictureCache::readable(QUrl::fromLocalFile(u"/tmp/łąka.png"_s)));
    QVERIFY(PictureCache::readable(QUrl(u"data:image/png;base64,AAAA"_s)));
    QVERIFY(!PictureCache::readable(QUrl(u"image://drawing/szkic.svg"_s)));
    QVERIFY(!PictureCache::readable(QUrl(u"https://example.com/kot.jpg"_s)));
}

void TestAtramentPictureCache::decodesForTheSide()
{
    QTemporaryDir dir;
    const QUrl url = writeFile(dir, u"żółw.png"_s, png(1000, 500, QColor(200, 30, 30)));
    PictureCache cache;
    int changes = 0;
    const int id = cache.subscribe([&changes] { ++changes; });
    PictureCache::Result r = cache.picture(u"assets/żółw.png"_s, url, 256, QSize(1000, 500));
    QVERIFY(!r.image);
    QVERIFY(r.loading);
    QTRY_VERIFY(cache.contains(u"assets/żółw.png"_s));
    QCOMPARE(changes, 1);
    r = cache.picture(u"assets/żółw.png"_s, url, 256, QSize(1000, 500));
    QVERIFY(r.image);
    QVERIFY(!r.loading);
    // Fitted into 256 × 256, its shape kept; the file's own size known.
    QCOMPARE(r.image->width, 256);
    QCOMPARE(r.image->height, 128);
    QCOMPARE(r.natural, QSize(1000, 500));
    // Premultiplied RGBA of the file's colour.
    const brulion::atrament::Pixel p = r.image->pixels[std::size_t(64 * 256 + 128)];
    QCOMPARE(int(p.r), 200);
    QCOMPARE(int(p.g), 30);
    QCOMPARE(int(p.a), 255);
    QCOMPARE(cache.bytes(), qint64(256 * 128 * 4));
    // A larger side: decoded again, the smaller one shown meanwhile.
    r = cache.picture(u"assets/żółw.png"_s, url, 640, QSize(1000, 500));
    QVERIFY(r.image);
    QCOMPARE(r.image->width, 256);
    QVERIFY(r.loading);
    QTRY_COMPARE(cache.picture(u"assets/żółw.png"_s, url, 640, QSize(1000, 500)).image->width, 640);
    QCOMPARE(cache.count(), 1);
    // A side twice as large as asked for is good enough (no decode for a smaller frame).
    r = cache.picture(u"assets/żółw.png"_s, url, 384, QSize(1000, 500));
    QVERIFY(!r.loading);
    QCOMPARE(r.image->width, 640);
    // Never enlarged: a small picture at its own size satisfies any larger side.
    const QUrl small = writeFile(dir, u"mały.png"_s, png(40, 30, Qt::blue));
    cache.picture(u"mały"_s, small, 1024, QSize(40, 30));
    QTRY_VERIFY(cache.contains(u"mały"_s));
    r = cache.picture(u"mały"_s, small, 2048, QSize(40, 30));
    QCOMPARE(r.image->width, 40);
    QVERIFY(!r.loading);
    cache.unsubscribe(id);
}

void TestAtramentPictureCache::dataUrlsAndNames()
{
    // The browser's attachments: data: URLs.
    const QByteArray bytes = png(64, 32, QColor(10, 200, 10));
    const QUrl url(u"data:image/png;base64,"_s + QString::fromLatin1(bytes.toBase64()));
    PictureCache cache;
    cache.picture(u"assets/trawa.png"_s, url, 64);
    QTRY_VERIFY(cache.contains(u"assets/trawa.png"_s));
    const PictureCache::Result r = cache.peek(u"assets/trawa.png"_s);
    QCOMPARE(r.image->width, 64);
    QCOMPARE(r.image->height, 32);
    QCOMPARE(int(r.image->pixels[0].g), 200);
    QVERIFY(PictureCache::decodableName(u"assets/łąka.png"_s));
    QVERIFY(PictureCache::decodableName(u"notatka"_s));
}

void TestAtramentPictureCache::budgetEvictsTheLeastRecent()
{
    QTemporaryDir dir;
    PictureCache cache;
    const qint64 one = 100 * 100 * 4;
    cache.setBudget(one * 3 / 2);
    const QStringList names{u"jeden"_s, u"dwa"_s, u"trzy"_s};
    QList<QUrl> urls;
    for (const QString &n : names)
        urls.append(writeFile(dir, n + u".png"_s, png(100, 100, Qt::red)));
    cache.picture(names[0], urls[0], 128, QSize(100, 100));
    QTRY_VERIFY(cache.contains(names[0]));
    // Nobody holds the first: the second puts it out.
    cache.picture(names[1], urls[1], 128, QSize(100, 100));
    QTRY_VERIFY(cache.contains(names[1]));
    QVERIFY(!cache.contains(names[0]));
    QCOMPARE(cache.bytes(), one);
    // Its size stays known (its frame keeps its place), only the pixels went.
    QCOMPARE(cache.peek(names[0]).natural, QSize(100, 100));
    // A held picture (on screen) stays, over the budget: nothing is decoded twice per frame.
    auto held = cache.picture(names[1], urls[1], 128, QSize(100, 100)).image;
    QVERIFY(held);
    cache.picture(names[2], urls[2], 128, QSize(100, 100));
    QTRY_VERIFY(cache.contains(names[2]));
    QVERIFY(cache.contains(names[1]));
    QCOMPARE(cache.bytes(), 2 * one);
    // Let go of, the least recently asked for goes when the budget is next looked at.
    held.reset();
    cache.setBudget(one * 3 / 2);
    QVERIFY(!cache.contains(names[1]));
    QVERIFY(cache.contains(names[2]));
    QCOMPARE(cache.bytes(), one);
}

void TestAtramentPictureCache::refusesHugeHeaders()
{
    QTemporaryDir dir;
    const QUrl url = writeFile(dir, u"olbrzym.png"_s, pngHeaderOnly(10000, 10000));
    PictureCache cache;
    QSignalSpy decoded(&cache, &PictureCache::decoded);
    cache.picture(u"olbrzym"_s, url, 512, QSize(10000, 10000));
    QTRY_COMPARE(decoded.count(), 1);
    const PictureCache::Result r = cache.picture(u"olbrzym"_s, url, 512, QSize(10000, 10000));
    QVERIFY(r.failed);
    QVERIFY(!r.image);
    QCOMPARE(cache.bytes(), 0);
}

void TestAtramentPictureCache::failureRemembered()
{
    QTemporaryDir dir;
    const QUrl url = writeFile(dir, u"zepsuty.png"_s, QByteArray("to nie jest obrazek, tylko tekst"));
    PictureCache cache;
    QSignalSpy decoded(&cache, &PictureCache::decoded);
    cache.picture(u"zepsuty"_s, url, 256, QSize(20, 20));
    QTRY_COMPARE(decoded.count(), 1);
    QVERIFY(cache.picture(u"zepsuty"_s, url, 256, QSize(20, 20)).failed);
    QCOMPARE(cache.pending(), 0); // (no new try every frame)
    // The file replaced by one of another size: tried again.
    writeFile(dir, u"zepsuty.png"_s, png(30, 20, Qt::green));
    const PictureCache::Result r = cache.picture(u"zepsuty"_s, url, 256, QSize(30, 20));
    QVERIFY(!r.failed);
    QTRY_VERIFY(cache.contains(u"zepsuty"_s));
}

QTEST_MAIN(TestAtramentPictureCache)
#include "tst_atrament_picturecache.moc"
