// The QR codes area (src/app/qr): QrShare — the dialog's text as written or as plain text, with
// and without the title, cut into codes, the code shown kept in range and back to the first for a
// new text, an empty note and one too long for QR codes, `((id))` references through the host —
// and QrCodeView — the code painted black on white in whole device pixels a module, centred, read
// back from the picture (tests/core/qr/qrdecode.h) at several sizes and pixel ratios.
#include "qrcodeview.h"
#include "qrshare.h"

#include "../../core/qr/qrdecode.h"

#include "qr/qrcode.h"
#include "qr/qrtext.h"

#include <QHash>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;
using brulion::qrview::QrCodeView;
using brulion::qrview::QrShare;

namespace {

/// A host that knows the text of a few blocks, nothing else.
class BlocksHost : public brulion::EditorHost {
public:
    QHash<QString, QString> blocks;

    QString read(const QString &, bool *ok) override
    {
        if (ok)
            *ok = false;
        return {};
    }
    bool write(const QString &, const QString &) override { return false; }
    QVariantList suggestPages(const QString &, int) const override { return {}; }
    QVariantList suggestBlocks(const QString &, int) const override { return {}; }
    QVariantList suggestTags(const QString &, int) const override { return {}; }
    QString resolveLink(const QString &, const QString &) const override { return {}; }
    QString blockText(const QString &id) const override { return blocks.value(id); }
    QString ensureBlockId(const QString &, int) override { return {}; }
    QVariantList templates() const override { return {}; }
    QUrl assetUrl(const QString &) const override { return {}; }
    QString saveAttachment(const QString &, const QByteArray &) override { return {}; }
    QVariantMap vaultConfig() const override { return {}; }
    void navigate(const QVariantMap &, bool) override { }
    void openUrl(const QUrl &) override { }
};

/// The modules of a code drawn in `image` (black on white, `pixels` image pixels a module, the
/// quiet zone included in the light square that starts at `origin`), read at each module's centre.
QList<bool> sampled(const QImage &image, QPoint origin, int modules, qreal pixels)
{
    QList<bool> out;
    out.reserve(qsizetype(modules) * modules);
    for (int y = 0; y < modules; ++y) {
        for (int x = 0; x < modules; ++x) {
            const int px = origin.x() + int(std::floor((brulion::qr::QuietZone + x + 0.5) * pixels));
            const int py = origin.y() + int(std::floor((brulion::qr::QuietZone + y + 0.5) * pixels));
            out.append(qGray(image.pixel(px, py)) < 128);
        }
    }
    return out;
}

/// The top left corner of the light square a code is drawn on in `image` (the first white pixel
/// of the first row that has one); (-1, -1) for none.
QPoint lightSquare(const QImage &image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) == 255 && qGray(image.pixel(x, y)) > 250)
                return {x, y};
        }
    }
    return {-1, -1};
}

} // namespace

class TestQrView : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // --- QrShare -------------------------------------------------------------------------------------

    void shareFormats()
    {
        QrShare share;
        QSignalSpy text(&share, &QrShare::textChanged);
        share.setTitle(u"Zakupy"_s);
        share.setMarkdown(u"- [ ] mleko\n- [x] chleb\n"_s);
        QCOMPARE(share.state(), u"ok"_s);
        QCOMPARE(share.count(), 1);
        QCOMPARE(share.text(), u"# Zakupy\n\n- [ ] mleko\n- [x] chleb"_s);
        QCOMPARE(share.partText(), share.text());
        QCOMPARE(share.bytes(), int(share.text().toUtf8().size()));
        QVERIFY(share.titleOptional());
        QVERIFY(text.size() >= 1);

        share.setPlainText(true);
        QCOMPARE(share.text(), u"Zakupy\n\n☐ mleko\n☑ chleb"_s);
        share.setWithTitle(false);
        QCOMPARE(share.text(), u"☐ mleko\n☑ chleb"_s);
        share.setPlainText(false);
        QCOMPARE(share.text(), u"- [ ] mleko\n- [x] chleb"_s);

        // A note with a title of its own: the switch makes no difference.
        share.setWithTitle(true);
        share.setMarkdown(u"# Lista na sobotę\n- mleko\n"_s);
        QVERIFY(!share.titleOptional());
        QCOMPARE(share.text(), u"# Lista na sobotę\n- mleko"_s);
        // No title at all: neither.
        share.setTitle(QString());
        share.setMarkdown(u"- mleko\n"_s);
        QVERIFY(!share.titleOptional());
    }

    void shareEmptyAndTooLong()
    {
        QrShare share;
        QCOMPARE(share.state(), u"empty"_s); // nothing set yet
        share.setTitle(u"Pusta"_s);
        share.setMarkdown(u"---\ntags: dom\n---\n"_s);
        QCOMPARE(share.state(), u"empty"_s);
        QCOMPARE(share.count(), 0);
        QCOMPARE(share.partText(), QString());

        // 21 codes' worth: refused, never cut; the codes it would take are told.
        const int tooMany = brulion::qr::MaxCodes + 1;
        share.setMarkdown(QString(int(brulion::qr::MaxBytes) * brulion::qr::MaxCodes + 10, u'x'));
        QCOMPARE(share.state(), u"tooLong"_s);
        QCOMPARE(share.count(), 0);
        QCOMPARE(share.codesNeeded(), tooMany);
        QCOMPARE(share.partText(), QString());
        QCOMPARE(QrShare::maxCodes(), brulion::qr::MaxCodes);
    }

    void shareWalksTheCodes()
    {
        QrShare share;
        QString note;
        for (int i = 1; i <= 200; ++i)
            note += u"- [ ] Zadanie %1: zadzwonić do pani Łucji w sprawie żółtej ściany\n"_s.arg(i);
        share.setMarkdown(note);
        QCOMPARE(share.state(), u"ok"_s);
        QVERIFY(share.count() >= 4);
        QCOMPARE(share.codesNeeded(), share.count());
        QSignalSpy part(&share, &QrShare::partChanged);
        QString joined = share.partText();
        for (int i = 1; i < share.count(); ++i) {
            share.next();
            QCOMPARE(share.index(), i);
            joined += share.partText();
        }
        QCOMPARE(joined, share.text());
        QCOMPARE(part.size(), share.count() - 1);
        share.next(); // the last one stays
        QCOMPARE(share.index(), share.count() - 1);
        QCOMPARE(part.size(), share.count() - 1);
        share.setIndex(-5);
        QCOMPARE(share.index(), 0);
        share.setIndex(1000);
        QCOMPARE(share.index(), share.count() - 1);
        share.previous();
        QCOMPARE(share.index(), share.count() - 2);
        // Another text: from the first code again.
        share.setWithTitle(false);
        share.setTitle(u"Remont"_s);
        QCOMPARE(share.index(), share.count() - 2); // no title in the text: the same text
        share.setWithTitle(true);
        QCOMPARE(share.index(), 0);
        QVERIFY(share.partText().startsWith(u"# Remont\n\n"_s));
    }

    void shareBlockReferences()
    {
        BlocksHost host;
        host.blocks.insert(u"abc123"_s, u"zadzwonić do Łucji"_s);
        QrShare share;
        share.setMarkdown(u"- Pamiętaj: ((abc123))\n"_s);
        share.setPlainText(true);
        QCOMPARE(share.text(), u"• Pamiętaj: ((abc123))"_s); // no host: as written
        share.setHost(&host);
        QCOMPARE(share.text(), u"• Pamiętaj: zadzwonić do Łucji"_s);
    }

    // --- QrCodeView --------------------------------------------------------------------------------

    void viewCode()
    {
        QrCodeView view;
        QSignalSpy code(&view, &QrCodeView::codeChanged);
        QCOMPARE(view.modules(), 0);
        view.setText(u"Zażółć gęślą jaźń"_s);
        QCOMPARE(code.size(), 1);
        QVERIFY(view.version() >= 1);
        QCOMPARE(view.modules(), view.version() * 4 + 17);
        view.setText(u"Zażółć gęślą jaźń"_s); // the same: nothing new
        QCOMPARE(code.size(), 1);
        view.setText(QString(brulion::qr::MaxBytes + 1, u'x')); // one code cannot hold it
        QCOMPARE(view.modules(), 0);
        QCOMPARE(view.modulePixels(), 0);
    }

    void pixelsPerModule_data()
    {
        QTest::addColumn<int>("modules");
        QTest::addColumn<qreal>("side");
        QTest::addColumn<qreal>("ratio");
        QTest::addColumn<int>("pixels");
        QTest::newRow("version 1, 290 px") << 21 << 290.0 << 1.0 << 10; // 29 modules with the quiet zone
        QTest::newRow("version 1, 289 px") << 21 << 289.0 << 1.0 << 9;
        QTest::newRow("version 40, phone") << 177 << 328.0 << 3.0 << 5; // 984 / 185
        QTest::newRow("version 40, 1.25") << 177 << 712.0 << 1.25 << 4; // 890 / 185
        QTest::newRow("version 40, small") << 177 << 100.0 << 1.0 << 1; // never 0
        QTest::newRow("exact at 1.2") << 21 << 290.0 / 1.2 << 1.2 << 10;
        QTest::newRow("no code") << 0 << 300.0 << 1.0 << 0;
    }
    void pixelsPerModule()
    {
        QFETCH(int, modules);
        QFETCH(qreal, side);
        QFETCH(qreal, ratio);
        QFETCH(int, pixels);
        QCOMPARE(QrCodeView::pixelsPerModule(modules, side, ratio), pixels);
    }

    void pictureReadsBack()
    {
        const QString text = u"# Zakupy\n\n- [ ] mleko 🥛\n- [ ] chleb"_s;
        const brulion::qr::Code code = brulion::qr::encode(text);
        const QImage picture = QrCodeView::picture(code, 3);
        QCOMPARE(picture.width(), (code.size + 2 * brulion::qr::QuietZone) * 3);
        QCOMPARE(picture.height(), picture.width());
        QCOMPARE(qGray(picture.pixel(0, 0)), 255); // the quiet zone
        QString why;
        const auto decoded = qrtest::decode(code.size, sampled(picture, {0, 0}, code.size, 3), &why);
        QVERIFY2(decoded.has_value(), qPrintable(why));
        QCOMPARE(QString::fromUtf8(decoded->bytes), text);
        QVERIFY(QrCodeView::picture(brulion::qr::Code(), 3).isNull());
    }

    // The item paints the code centred, in whole pixels a module, and it reads back.
    void paintReadsBack_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::newRow("short, square") << u"Ala ma kota"_s << 300 << 300;
        QTest::newRow("short, wide") << u"Ala ma kota"_s << 420 << 250;
        QTest::newRow("full code") << QString(int(brulion::qr::MaxBytes), u'z') << 740 << 740;
        QTest::newRow("polish, odd size") << u"Zażółć gęślą jaźń — notatka"_s << 333 << 347;
    }
    void paintReadsBack()
    {
        QFETCH(QString, text);
        QFETCH(int, width);
        QFETCH(int, height);
        QrCodeView view;
        view.setSize(QSizeF(width, height));
        view.setText(text);
        const int total = view.modules() + 2 * brulion::qr::QuietZone;
        QCOMPARE(view.modulePixels(), std::max(1, std::min(width, height) / total));

        QImage canvas(width, height, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::transparent);
        {
            QPainter painter(&canvas);
            view.paint(&painter);
        }
        const QPoint origin = lightSquare(canvas);
        const int side = total * view.modulePixels();
        QCOMPARE(origin, QPoint((width - side) / 2, (height - side) / 2));
        // Outside the square nothing is painted.
        QCOMPARE(qAlpha(canvas.pixel(0, 0)), origin == QPoint(0, 0) ? 255 : 0);
        QString why;
        const auto decoded =
                qrtest::decode(view.modules(), sampled(canvas, origin, view.modules(), view.modulePixels()), &why);
        QVERIFY2(decoded.has_value(), qPrintable(why));
        QCOMPARE(QString::fromUtf8(decoded->bytes), text);
        // Whole pixels: every pixel of the square is black or white, none grey.
        for (int y = origin.y(); y < origin.y() + side; y += 7) {
            for (int x = origin.x(); x < origin.x() + side; ++x) {
                const int gray = qGray(canvas.pixel(x, y));
                QVERIFY2(gray == 0 || gray == 255, qPrintable(u"grey %1 at %2, %3"_s.arg(gray).arg(x).arg(y)));
            }
        }
    }
};

QTEST_MAIN(TestQrView)
#include "tst_qrview.moc"
