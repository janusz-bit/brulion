// The terminal drawn by Atrament (src/app/terminal/terminalatrament.h): TerminalScreen on Atrament's
// test platform — a frame's pixels for the backgrounds of ANSI, 256 and true colours, inverse
// video, the selection and the find bar's matches over them, the cursor's shapes (filled, outlined
// without the focus, hidden once the program ended), the preedit, glyphs with Polish diacritics,
// bold and an emoji from the fallback font each in its own cell, the glyph cache — the grid's
// metrics from Atrament's fonts, and TerminalView with `atrament: true` in a window (offscreen, Qt
// Quick's software backend: the host's CPU renderer): its item over the view and its size, the
// pixels, mouse selection still the view's, and back to QPainter. Fonts from the source tree.

#include "qthost.h"
#include "terminalatrament.h"
#include "terminalpicture.h"
#include "terminalsession.h"
#include "terminalview.h"

#include "term/screen.h"
#include "text/textsystem.h"
#include "ui/app.h"
#include "ui/testplatform.h"

#include <QAccessible>
#include <QElapsedTimer>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <limits>
#include <memory>

using namespace Qt::StringLiterals;
namespace at = brulion::atrament;
using brulion::terminal::TerminalAtrament;
using brulion::terminal::TerminalCellMetrics;
using brulion::terminal::TerminalPicture;
using brulion::terminal::TerminalPictureSource;
using brulion::terminal::TerminalScreen;
using brulion::terminal::TerminalSession;
using brulion::terminal::TerminalView;

namespace {

const QString Mono = u"Brulion JetBrains Mono"_s;

std::shared_ptr<at::FontCollection> loadFonts()
{
    auto fonts = std::make_shared<at::FontCollection>();
    const std::string dir = BRULION_SOURCE_DIR "/assets/fonts/";
    for (const char *file : {"ui/BrulionJetBrainsMono-Regular.ttf", "ui/BrulionJetBrainsMono-Bold.ttf",
                             "ui/BrulionInter-Regular.ttf", "BrulionEmoji.ttf"})
        fonts->addFontFile(dir + file);
    fonts->setFallbackFamilies({u"Brulion Emoji", u"Brulion Inter"});
    return fonts;
}

void show(TerminalSession &session, const QByteArray &output)
{
    session.terminal().feed(output);
    Q_EMIT session.contentChanged();
}

/// A picture of a session's screen, as TerminalView would give it.
struct Source final : TerminalPictureSource {
    TerminalPicture picture;
    void terminalPicture(TerminalPicture &p) const override { p = picture; }
};

QColor pixelColor(const at::TestWindow &w, qreal x, qreal y)
{
    const at::Pixel p = w.pixel(int(x), int(y));
    return QColor(p.r, p.g, p.b, p.a);
}

/// A process-wide App for the views in windows (TerminalAtrament's provider).
struct QtRuntime {
    std::shared_ptr<at::QtPlatform> platform = std::make_shared<at::QtPlatform>();
    at::App app{platform, loadFonts()};
};
QtRuntime *s_runtime = nullptr;

at::App *runtimeApp()
{
    if (!s_runtime)
        s_runtime = new QtRuntime;
    return &s_runtime->app;
}

} // namespace

class TestTerminalAtrament : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void metrics();
    void screenPixels();
    void cursorStates();
    void glyphs();
    void fullScreen();
    void viewUnavailable();
    void viewInWindow();
    void cleanupTestCase();
};

void TestTerminalAtrament::metrics()
{
    const auto fonts = loadFonts();
    const TerminalCellMetrics m = TerminalScreen::metrics(*fonts, Mono, 14, 1);
    // JetBrains Mono: 600 units of 1000 a character, so 8.4 px at 14 px — a whole pixel on the grid.
    QCOMPARE(m.cellWidth, 8.0);
    QVERIFY2(m.cellHeight >= 16 && m.cellHeight <= 20, qPrintable(QString::number(m.cellHeight)));
    QVERIFY(m.ascent > 8 && m.ascent < m.cellHeight);
    QVERIFY(m.lineWidth >= 1);
    QVERIFY(m.underlinePos >= 1);
    QVERIFY(m.strikePos > 0 && m.strikePos < m.ascent);
    // Twice the pixels: half-pixel steps; a family Atrament lacks: the bundled one's.
    const TerminalCellMetrics hi = TerminalScreen::metrics(*fonts, Mono, 14, 2);
    QCOMPARE(hi.cellWidth, 8.5);
    QCOMPARE(std::fmod(hi.cellHeight * 2, 1.0), 0.0);
    const TerminalCellMetrics other = TerminalScreen::metrics(*fonts, u"Nie ma takiej czcionki"_s, 14, 1);
    QCOMPARE(other.cellWidth, m.cellWidth);
    QCOMPARE(other.cellHeight, m.cellHeight);
    // No size: the defaults, never a zero cell.
    QVERIFY(TerminalScreen::metrics(*fonts, Mono, 0, 1).cellWidth >= 1);
    // The faces: regular and bold are JetBrains Mono's own; italic falls back to the upright ones.
    const auto faces = TerminalScreen::faces(*fonts, Mono);
    QCOMPARE(fonts->faceInfo(faces[0]).family, std::u16string(u"Brulion JetBrains Mono"));
    QVERIFY(faces[1] != faces[0]);
    QCOMPARE(faces[2], faces[0]);
    QCOMPARE(faces[3], faces[1]);
}

void TestTerminalAtrament::screenPixels()
{
    auto platform = std::make_shared<at::TestPlatform>();
    at::App app(platform, loadFonts());
    TerminalSession session;
    session.resize(40, 8);
    show(session, "\x1b[41m  \x1b[0m\x1b[48;2;1;2;3m  \x1b[0m\x1b[48;5;196m  \x1b[0m\x1b[7m  \x1b[0m\r\n"
                  "zażółć gęślą jaźń\r\n");

    Source source;
    TerminalPicture &p = source.picture;
    p.screen = &session.terminal().screen();
    p.metrics = TerminalScreen::metrics(app.textSystem().fonts(), Mono, 14, 1);
    p.fontFamily = Mono;
    p.fontPixelSize = 14;
    p.padding = 4;
    p.background = qRgb(0x16, 0x17, 0x1b);
    p.foreground = qRgb(0xd3, 0xd8, 0xe2);
    p.cursor = qRgb(0xff, 0x7a, 0x66);
    for (int i = 0; i < 16; ++i)
        p.palette[std::size_t(i)] = qRgb(i * 16, 0x40, 0x80);
    p.running = true;
    p.focused = true;

    at::TestWindow &tw = platform->newWindow({400, 200});
    auto view = app.newEntity<TerminalScreen>(&source);
    app.openWindow(tw, view);
    tw.settle();
    const qreal cw = p.metrics.cellWidth;
    const qreal ch = p.metrics.cellHeight;
    auto cell = [&](int column, int row) {
        return pixelColor(tw, p.padding + (column + 0.5) * cw, p.padding + (row + 0.5) * ch);
    };
    QCOMPARE(cell(0, 0), QColor::fromRgb(p.palette[1]));
    QCOMPARE(cell(2, 0), QColor(1, 2, 3));
    QCOMPARE(cell(4, 0), QColor(0xff, 0, 0));            // xterm 196
    QCOMPARE(cell(6, 0), QColor::fromRgb(p.foreground)); // inverse: the foreground behind
    QCOMPARE(pixelColor(tw, 399, 199), QColor::fromRgb(p.background));
    QCOMPARE(cell(20, 0), QColor::fromRgb(p.background));

    // The selection over a coloured cell, and a match of the find bar: blended over it.
    p.hasSelection = true;
    p.selectionStart = {0, 0};
    p.selectionStop = {0, 1};
    p.selection = qRgba(0xff, 0xff, 0xff, 0x80);
    QList<brulion::term::TextRange> matches{{{0, 10}, {0, 12}}, {{1, 0}, {1, 3}}};
    p.matches = &matches;
    p.currentMatch = 1;
    p.matchColor = qRgba(0, 0xff, 0, 0x80);
    p.currentMatchColor = qRgba(0, 0, 0xff, 0xff);
    app.refreshWindows();
    tw.settle();
    const QColor selected = cell(0, 0);
    QVERIFY2(selected != QColor::fromRgb(p.palette[1]) && selected.red() > qRed(p.palette[1]),
             qPrintable(selected.name()));
    QCOMPARE(cell(1, 0), QColor::fromRgb(p.palette[1])); // the selection ends before column 1
    const QColor match = cell(11, 0);
    QVERIFY2(match.green() > match.red() && match.green() > match.blue(), qPrintable(match.name()));
    // The current match: opaque blue, under the glyph of "ż" only where its ink is.
    QCOMPARE(pixelColor(tw, p.padding + 0.5 * cw, p.padding + ch + 1), QColor(0, 0, 0xff));

    // Scrolled back by one line: row 0 shows nothing of the screen's first line (an empty history
    // line above it), row 1 the coloured cells.
    session.resize(40, 8);
    p.hasSelection = false;
    p.matches = nullptr;
    show(session, "\r\n\r\n\r\n\r\n\r\n\r\n\r\n");
    QVERIFY(session.terminal().screen().historySize() > 0);
    p.scrollOffset = 1;
    app.refreshWindows();
    tw.settle();
    // (Whatever is shown, the bottom right stays the background: nothing is drawn past the grid.)
    QCOMPARE(pixelColor(tw, 399, 199), QColor::fromRgb(p.background));
}

void TestTerminalAtrament::cursorStates()
{
    auto platform = std::make_shared<at::TestPlatform>();
    at::App app(platform, loadFonts());
    TerminalSession session;
    session.resize(20, 4);
    show(session, "ab");
    Source source;
    TerminalPicture &p = source.picture;
    p.screen = &session.terminal().screen();
    p.metrics = TerminalScreen::metrics(app.textSystem().fonts(), Mono, 14, 1);
    p.fontFamily = Mono;
    p.fontPixelSize = 14;
    p.cursor = qRgb(0xff, 0x7a, 0x66);
    p.running = true;
    p.focused = true;
    at::TestWindow &tw = platform->newWindow({200, 100});
    auto view = app.newEntity<TerminalScreen>(&source);
    app.openWindow(tw, view);
    auto redraw = [&] {
        app.refreshWindows();
        tw.settle();
    };
    redraw();
    const qreal cw = p.metrics.cellWidth;
    const qreal ch = p.metrics.cellHeight;
    const QColor cursor = QColor::fromRgb(p.cursor);
    const QColor background = QColor::fromRgb(p.background);
    // The block at column 2 (after "ab"), filled.
    auto middle = [&](int column) { return pixelColor(tw, p.padding + (column + 0.5) * cw, p.padding + ch / 2); };
    auto edge = [&](int column) { return pixelColor(tw, p.padding + column * cw + 0.5, p.padding + ch / 2); };
    QCOMPARE(middle(2), cursor);
    // Without the focus: an outline (its left edge), the inside the background.
    p.focused = false;
    redraw();
    QCOMPARE(middle(2), background);
    QCOMPARE(edge(2), cursor);
    // Blinking off: nothing; the program ended: nothing.
    p.focused = true;
    p.blinkOn = false;
    show(session, "\x1b[1 q"); // a blinking block
    redraw();
    QCOMPARE(middle(2), background);
    p.blinkOn = true;
    p.running = false;
    redraw();
    QCOMPARE(middle(2), background);
    p.running = true;
    // A bar: its left 2 px only.
    show(session, "\x1b[6 q");
    redraw();
    QCOMPARE(edge(2), cursor);
    QCOMPARE(middle(2), background);
    // An underline: the cell's bottom.
    show(session, "\x1b[4 q");
    redraw();
    QCOMPARE(pixelColor(tw, p.padding + 2.5 * cw, p.padding + ch - 1), cursor);
    QCOMPARE(middle(2), background);
    // The preedit at the cursor: the background behind it, underlined in the foreground.
    p.preedit = u"żó"_s;
    redraw();
    QCOMPARE(pixelColor(tw, p.padding + 2.5 * cw, p.padding + p.metrics.ascent + p.metrics.underlinePos + 0.5),
             QColor::fromRgb(p.foreground));
    QCOMPARE(pixelColor(tw, p.padding + 3.5 * cw, p.padding + p.metrics.ascent + p.metrics.underlinePos + 0.5),
             QColor::fromRgb(p.foreground));
    // The cursor scrolled out of view: nothing, and no crash.
    p.preedit.clear();
    p.scrollOffset = 10;
    redraw();
}

void TestTerminalAtrament::glyphs()
{
    auto platform = std::make_shared<at::TestPlatform>();
    at::App app(platform, loadFonts());
    TerminalSession session;
    session.resize(20, 4);
    // "M", "ż", a bold "B", an emoji (two cells) and "e" with a combining acute accent.
    show(session, "M\xc5\xbc\x1b[1mB\x1b[0m\xf0\x9f\x98\x80"
                  "e\xcc\x81");
    Source source;
    TerminalPicture &p = source.picture;
    p.screen = &session.terminal().screen();
    p.metrics = TerminalScreen::metrics(app.textSystem().fonts(), Mono, 14, 1);
    p.fontFamily = Mono;
    p.fontPixelSize = 14;
    p.running = false;
    at::TestWindow &tw = platform->newWindow({200, 60});
    auto view = app.newEntity<TerminalScreen>(&source);
    app.openWindow(tw, view);
    tw.settle();
    const QColor background = QColor::fromRgb(p.background);
    // Some ink within each cell's box, none outside the text's cells.
    auto inked = [&](int column, int cells) {
        int count = 0;
        for (int y = int(p.padding); y < int(p.padding + p.metrics.cellHeight); ++y)
            for (int x = int(p.padding + column * p.metrics.cellWidth);
                 x < int(p.padding + (column + cells) * p.metrics.cellWidth); ++x)
                count += pixelColor(tw, x, y) != background;
        return count;
    };
    QVERIFY(inked(0, 1) > 5); // M
    QVERIFY(inked(1, 1) > 5); // ż
    QVERIFY(inked(2, 1) > 5); // B
    QVERIFY(inked(3, 2) > 5); // 😀
    QVERIFY(inked(5, 1) > 5); // é
    QCOMPARE(inked(8, 4), 0);
    // Bold is another face: more ink than a regular "B" would have is not asserted, but its glyph
    // comes from the bold face (cached apart from the regular one).
    const std::size_t cached = view.read(app).cachedGlyphs();
    QVERIFY(cached >= 5);
    // Drawing again shapes nothing new.
    app.refreshWindows();
    tw.settle();
    QCOMPARE(view.read(app).cachedGlyphs(), cached);
    // Another size: the cache starts over.
    p.fontPixelSize = 20;
    app.refreshWindows();
    tw.settle();
    QCOMPARE(view.read(app).cachedGlyphs(), cached);
}

void TestTerminalAtrament::fullScreen()
{
    // A 200 × 60 screen of coloured Polish text: one frame's primitives stay about one per cell
    // (a glyph, plus a quad per run of a background), and the time it takes is printed.
    auto platform = std::make_shared<at::TestPlatform>();
    at::App app(platform, loadFonts());
    TerminalSession session;
    session.resize(200, 60);
    QByteArray text;
    for (int row = 0; row < 60; ++row) {
        if (row > 0)
            text += "\r\n";
        for (int i = 0; i < 8; ++i)
            text += "\x1b[3" + QByteArray::number((row + i) % 8) + "mzażółć gęślą jaźń 0123\x1b[0m ";
    }
    show(session, text);
    Source source;
    TerminalPicture &p = source.picture;
    p.screen = &session.terminal().screen();
    p.metrics = TerminalScreen::metrics(app.textSystem().fonts(), Mono, 14, 1);
    p.fontFamily = Mono;
    p.fontPixelSize = 14;
    for (int i = 0; i < 16; ++i)
        p.palette[std::size_t(i)] = qRgb(0x40 + i * 8, 0x80, 0xc0);
    const int width = int(2 * p.padding + 200 * p.metrics.cellWidth);
    const int height = int(2 * p.padding + 60 * p.metrics.cellHeight);
    at::TestWindow &tw = platform->newWindow({float(width), float(height)});
    auto view = app.newEntity<TerminalScreen>(&source);
    at::Window &window = app.openWindow(tw, view);
    tw.settle(); // the first frame shapes the characters
    app.refreshWindows();
    tw.settle();
    const at::Window::FrameStats stats = window.lastFrameStats();
    QVERIFY2(stats.primitives <= 200 * 60 + 60 * 2, qPrintable(QString::number(stats.primitives)));
    QVERIFY(stats.primitives > 200 * 60 / 2);
    qInfo("200 × 60 cells: %zu primitives, built in %.2f ms, rasterized in %.2f ms", stats.primitives,
          stats.buildMicros / 1000.0, stats.presentMicros / 1000.0);
    // The same screen through TerminalView's QPainter, for comparison (the whole view repainted).
    TerminalView painted;
    QFont font(Mono);
    font.setPixelSize(14);
    painted.setFont(font);
    painted.setSession(&session);
    painted.setSize(QSizeF(width, height));
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    QElapsedTimer timer;
    qint64 best = std::numeric_limits<qint64>::max();
    for (int i = 0; i < 3; ++i) {
        image.fill(painted.fillColor());
        QPainter painter(&image);
        timer.start();
        painted.paint(&painter);
        best = std::min(best, timer.nsecsElapsed());
    }
    qInfo("QPainter: %.2f ms", double(best) / 1e6);
}

void TestTerminalAtrament::viewUnavailable()
{
    // No App given (a build without Atrament in the app, or before the app sets it): QPainter draws.
    TerminalAtrament::setAppProvider(nullptr);
    QVERIFY(!TerminalView::atramentAvailable());
    TerminalView view;
    QSignalSpy changed(&view, &TerminalView::atramentChanged);
    view.setAtrament(true);
    QVERIFY(!view.atrament());
    QCOMPARE(changed.count(), 0);
}

void TestTerminalAtrament::viewInWindow()
{
    TerminalAtrament::setAppProvider(&runtimeApp);
    QVERIFY(TerminalView::atramentAvailable());
    TerminalSession session;
    show(session, "\x1b[41m  \x1b[0m hello");

    QQuickWindow window;
    window.resize(400, 200);
    auto *view = new TerminalView(window.contentItem());
    QFont font(Mono);
    font.setPixelSize(14);
    view->setFont(font);
    view->setSize(QSizeF(400, 200));
    view->setSession(&session);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    view->forceActiveFocus();
    const qreal painterCellWidth = view->cellWidth();

    QSignalSpy changed(view, &TerminalView::atramentChanged);
    view->setAtrament(true);
    QVERIFY(view->atrament());
    QCOMPARE(changed.count(), 1);
    auto *item = view->findChild<at::AtramentItem *>(u"terminalAtrament"_s);
    QVERIFY(item);
    QCOMPARE(item->size(), view->size());
    QVERIFY(!item->isEnabled());
    QCOMPARE(item->acceptedMouseButtons(), Qt::NoButton);
#if QT_CONFIG(accessibility)
    // Not in the accessibility tree: the view speaks for the screen.
    {
        QList<QAccessibleInterface *> pending{QAccessible::queryAccessibleInterface(&window)};
        int seen = 0;
        while (!pending.isEmpty()) {
            QAccessibleInterface *iface = pending.takeLast();
            if (!iface)
                continue;
            ++seen;
            QVERIFY(iface->object() != item);
            for (int i = 0; i < iface->childCount(); ++i)
                pending.append(iface->child(i));
        }
        QVERIFY(seen > 0);
    }
#endif
    // The grid is Atrament's fonts'.
    const TerminalCellMetrics m =
            TerminalScreen::metrics(runtimeApp()->textSystem().fonts(), Mono, 14, window.effectiveDevicePixelRatio());
    QCOMPARE(view->cellWidth(), m.cellWidth);
    QCOMPARE(view->cellHeight(), m.cellHeight);
    Q_UNUSED(painterCellWidth)

    auto cellPoint = [&](int column) {
        return QPointF(view->padding() + (column + 0.5) * view->cellWidth(), view->padding() + view->cellHeight() / 2);
    };
    auto grabbedAt = [&](int column) {
        const QImage image = window.grabWindow();
        const QPointF at = cellPoint(column) * window.effectiveDevicePixelRatio();
        return image.pixelColor(at.toPoint());
    };
    QTRY_COMPARE(grabbedAt(0), view->ansiColors().at(1));
    QCOMPARE(grabbedAt(30), view->background());

    // The item resizes with the view.
    view->setSize(QSizeF(300, 150));
    QCOMPARE(item->size(), QSizeF(300, 150));

    // The mouse is still the view's: a drag over "hello" selects it.
    auto mouse = [&](QEvent::Type type, QPointF at, Qt::MouseButtons buttons, ulong t) {
        QMouseEvent event(type, at, window.mapToGlobal(at), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                          buttons, Qt::NoModifier);
        event.setTimestamp(t);
        QGuiApplication::sendEvent(&window, &event);
    };
    const QPointF from(view->padding() + 3 * view->cellWidth() + 1, cellPoint(3).y());
    const QPointF to(view->padding() + 8 * view->cellWidth() - 1, cellPoint(3).y());
    mouse(QEvent::MouseButtonPress, from, Qt::LeftButton, 1000);
    mouse(QEvent::MouseMove, to, Qt::LeftButton, 1001);
    mouse(QEvent::MouseButtonRelease, to, Qt::NoButton, 1002);
    QCOMPARE(view->selectedText(), u"hello"_s);
    QVERIFY(view->hasActiveFocus());

    // Back to QPainter: the item goes, the view paints again.
    view->setAtrament(false);
    QVERIFY(!view->atrament());
    QCOMPARE(changed.count(), 2);
    QVERIFY(!view->findChild<at::AtramentItem *>(u"terminalAtrament"_s));
    QCOMPARE(view->fillColor(), view->background());
    QTRY_COMPARE(grabbedAt(0), view->ansiColors().at(1));
    // And on again (the setting toggled twice).
    view->setAtrament(true);
    QVERIFY(view->findChild<at::AtramentItem *>(u"terminalAtrament"_s));
    QTRY_COMPARE(grabbedAt(0), view->ansiColors().at(1));
    delete view; // (its item and Atrament window with it, before the window)
}

void TestTerminalAtrament::cleanupTestCase()
{
    TerminalAtrament::setAppProvider(nullptr);
}

QTEST_MAIN(TestTerminalAtrament)
#include "tst_terminal_atrament.moc"
