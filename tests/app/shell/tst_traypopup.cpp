// The tray calendar's placement (src/app/shell/traypopup.h): next to the tray icon for a taskbar
// on every edge, at the cursor when the icon's rectangle is unknown (X11), in the work area's
// corner with neither, kept inside the work area (secondary monitors with negative coordinates, a
// scaled screen, a popup larger than the screen), on the anchor's screen; and the guard against
// reopening the popup that the click on the icon has just hidden.
#include "traypopup.h"

#include <QGuiApplication>
#include <QScreen>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::shell;

namespace {

constexpr int kMargin = 8;
const QSize kPopup(320, 460);

} // namespace

class TestTrayPopup : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void position_data()
    {
        QTest::addColumn<QRect>("icon");
        QTest::addColumn<bool>("hasCursor");
        QTest::addColumn<QPoint>("cursor");
        QTest::addColumn<QRect>("available");
        QTest::addColumn<QSize>("popup");
        QTest::addColumn<QPoint>("expected");

        // 1920×1080 with a 40 px taskbar at the bottom: above the taskbar, kept 8 px from the
        // right side (centred on the icon it would stick out).
        QTest::newRow("pasek na dole") << QRect(1800, 1048, 24, 24) << false << QPoint() << QRect(0, 0, 1920, 1040)
                                       << kPopup << QPoint(1592, 572);
        QTest::newRow("pasek u góry") << QRect(1800, 8, 24, 24) << false << QPoint() << QRect(0, 40, 1920, 1040)
                                      << kPopup << QPoint(1592, 48);
        QTest::newRow("pasek po lewej") << QRect(12, 1000, 24, 24) << false << QPoint() << QRect(48, 0, 1872, 1080)
                                        << kPopup << QPoint(56, 612);
        QTest::newRow("pasek po prawej") << QRect(1884, 1000, 24, 24) << false << QPoint()
                                         << QRect(0, 0, 1872, 1080) << kPopup << QPoint(1544, 612);
        // Centred on the icon where that fits.
        QTest::newRow("ikona na środku paska") << QRect(948, 1048, 24, 24) << false << QPoint()
                                               << QRect(0, 0, 1920, 1040) << kPopup << QPoint(800, 572);
        QTest::newRow("ikona w lewym rogu ekranu") << QRect(0, 1048, 24, 24) << false << QPoint()
                                                   << QRect(0, 0, 1920, 1040) << kPopup << QPoint(8, 572);
        QTest::newRow("ikona w prawym rogu ekranu") << QRect(1896, 1056, 24, 24) << false << QPoint()
                                                    << QRect(0, 0, 1920, 1040) << kPopup << QPoint(1592, 572);
        // X11: no icon rectangle — the cursor that clicked it.
        QTest::newRow("bez prostokąta ikony: kursor") << QRect() << true << QPoint(1500, 1060)
                                                       << QRect(0, 0, 1920, 1040) << kPopup << QPoint(1340, 572);
        QTest::newRow("pusta ikona o zerowej szerokości") << QRect(1500, 1048, 0, 24) << true << QPoint(1500, 1060)
                                                           << QRect(0, 0, 1920, 1040) << kPopup << QPoint(1340, 572);
        // A panel the desktop does not reserve: the cursor is inside the work area.
        QTest::newRow("kursor w obszarze roboczym") << QRect() << true << QPoint(960, 1070)
                                                     << QRect(0, 0, 1920, 1080) << kPopup << QPoint(800, 602);
        QTest::newRow("kursor przy górnym panelu") << QRect() << true << QPoint(1700, 5) << QRect(0, 0, 1920, 1080)
                                                    << kPopup << QPoint(1540, 14);
        // Neither (Wayland, a menu entry): the work area's bottom-right corner.
        QTest::newRow("ani ikony, ani kursora") << QRect() << false << QPoint() << QRect(0, 0, 1920, 1040) << kPopup
                                                << QPoint(1592, 572);
        // A monitor left of the primary one (negative x).
        QTest::newRow("drugi monitor z ujemnymi współrzędnymi")
                << QRect(-100, 992, 24, 24) << false << QPoint() << QRect(-1280, 0, 1280, 984) << kPopup
                << QPoint(-328, 516);
        QTest::newRow("monitor nad głównym") << QRect(1800, -32, 24, 24) << false << QPoint()
                                             << QRect(0, -1080, 1920, 1040) << kPopup << QPoint(1592, -508);
        // Larger than the work area: from its top-left corner.
        QTest::newRow("okienko większe niż ekran") << QRect(760, 570, 24, 24) << false << QPoint()
                                                    << QRect(0, 0, 800, 560) << QSize(900, 700) << QPoint(0, 0);
        // It fits only without the margins: centred in the work area.
        QTest::newRow("ledwo się mieści") << QRect(900, 474, 24, 24) << false << QPoint() << QRect(0, 0, 1024, 470)
                                          << kPopup << QPoint(696, 5);
        // 1920×1080 at 150 %: a 1280×720 logical screen, the 48 px taskbar is 32 logical px.
        QTest::newRow("ekran 150%") << QRect(1200, 694, 16, 16) << false << QPoint() << QRect(0, 0, 1280, 688)
                                    << kPopup << QPoint(952, 220);
    }

    void position()
    {
        QFETCH(QRect, icon);
        QFETCH(bool, hasCursor);
        QFETCH(QPoint, cursor);
        QFETCH(QRect, available);
        QFETCH(QSize, popup);
        QFETCH(QPoint, expected);
        const QPoint at = trayPopupPosition(icon, hasCursor ? std::optional(cursor) : std::nullopt, available, popup,
                                            kMargin);
        QCOMPARE(at, expected);
        // Inside the work area whenever it fits there.
        if (popup.width() <= available.width() && popup.height() <= available.height())
            QVERIFY(available.contains(QRect(at, popup)));
    }

    void edge_data()
    {
        QTest::addColumn<QPoint>("anchor");
        QTest::addColumn<int>("edge");
        QTest::newRow("pod obszarem") << QPoint(500, 820) << int(TrayEdge::Bottom);
        QTest::newRow("nad obszarem") << QPoint(500, -10) << int(TrayEdge::Top);
        QTest::newRow("na lewo") << QPoint(-10, 400) << int(TrayEdge::Left);
        QTest::newRow("na prawo") << QPoint(1010, 400) << int(TrayEdge::Right);
        QTest::newRow("środek: remis wygrywa dół") << QPoint(500, 400) << int(TrayEdge::Bottom);
        QTest::newRow("prawy dolny róg: remis wygrywa dół") << QPoint(1000, 800) << int(TrayEdge::Bottom);
        QTest::newRow("bliżej prawej krawędzi") << QPoint(990, 700) << int(TrayEdge::Right);
    }

    void edge()
    {
        QFETCH(QPoint, anchor);
        QFETCH(int, edge);
        QCOMPARE(int(trayEdge(anchor, QRect(0, 0, 1000, 800))), edge);
    }

    void onScreen()
    {
        // Offscreen: one screen. Its work area holds the popup wherever the anchor is.
        QScreen *screen = QGuiApplication::primaryScreen();
        QVERIFY(screen);
        const QRect area = screen->availableGeometry();
        const QSize popup(qMin(320, area.width()), qMin(460, area.height()));
        QCOMPARE(trayPopupPositionOnScreen(QRect(), std::nullopt, popup, kMargin),
                 trayPopupPosition(QRect(), std::nullopt, area, popup, kMargin));
        const QRect icon(area.right() - 30, area.bottom() - 20, 24, 24);
        QCOMPARE(trayPopupPositionOnScreen(icon, std::nullopt, popup, kMargin),
                 trayPopupPosition(icon, std::nullopt, area, popup, kMargin));
        // On no screen: the primary one.
        const QPoint far(-50000, -50000);
        QVERIFY(area.contains(QRect(trayPopupPositionOnScreen(QRect(), far, popup, kMargin), popup)));
    }

    void reopen_data()
    {
        QTest::addColumn<qint64>("ms");
        QTest::addColumn<bool>("reopens");
        QTest::newRow("nigdy nie schowane") << qint64(-1) << true;
        QTest::newRow("ta sama chwila") << qint64(0) << false;
        QTest::newRow("tuż przed granicą") << qint64(TrayPopupReopenGuardMs - 1) << false;
        QTest::newRow("na granicy") << qint64(TrayPopupReopenGuardMs) << true;
        QTest::newRow("po pięciu sekundach") << qint64(5000) << true;
    }

    void reopen()
    {
        QFETCH(qint64, ms);
        QFETCH(bool, reopens);
        QCOMPARE(trayPopupShouldReopen(ms), reopens);
    }
};

QTEST_MAIN(TestTrayPopup)
#include "tst_traypopup.moc"
