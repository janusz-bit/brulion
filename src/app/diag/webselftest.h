// Self-tests of the browser build that need the running app (tests/web, the Playwright harness;
// docs/WEB.md "Testy w przeglądarkach"): `?selftest=webpictures` and `?selftest=pen`
// (wasm/index.html → --selftest-webpictures / --selftest-pen; main.cpp starts them once the
// notebook is open).
//
//   WebPicturesSelfTest   the pictures from the internet of one page as the editor fetches them
//                         (published as "brulionWebPictureSelfTest"), then an export with copies
//                         of them ("brulionExportSelfTest")
//   PenSelfTest           the drawing editor's canvas under a pen ("brulionPenSelfTest": its
//                         freehand lines with their pressures, where they are on screen)
//   startBrowserSelfTests the flows named by the app's arguments, once the app is ready
//
// Platform-neutral: results go through `publish` (in the browser platform::publishSelfTestResult:
// a JavaScript global and a console line), so tests/app/diag runs them on the desktop. They write
// no notes; the export makes its file in memory only (and offers it as a download in the
// browser). Test output: nothing here is translated.
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <functional>

class QQmlApplicationEngine;
class QQuickItem;
class QQuickWindow;

namespace brulion::shell {
class AppController;
class Workspace;
} // namespace brulion::shell
namespace brulion::drawing {
struct Stroke;
}
namespace brulion::drawingview {
class DrawingCanvas;
}
namespace brulion::ioviews {
class ExportController;
}

namespace brulion::diag {

/// Where a self-test's result goes: `name` is the JavaScript global it becomes in the browser.
using PublishFn = std::function<void(const char *name, const QJsonObject &result)>;

/// What the pen test checks of a freehand line: {tool: "pen"|"highlighter", simulatePressure,
/// points, pressureMin, pressureMax, pressureFirst, pressureLast (null without points), width}.
/// The pressures as stored (clamping is the canvas's job).
QJsonObject strokeFacts(const drawing::Stroke &stroke);

/// The texts of the visible `Text` items called `objectName` under `root` (the visual tree, so
/// the delegates of views count), in tree order.
QStringList shownTexts(QQuickItem *root, const QString &objectName);

/// The pictures from the internet on one page, as the editor fetches them, then an export of the
/// notebook with copies of them.
///
///  1. the page's pictures as written (io::webPictureTargets: the editor's keys);
///  2. waits up to `uiGraceMs` for the editor to fetch them by itself (Workspace::
///     webPictureChanged before this test asked: `uiFetched`);
///  3. asks for the rest (Workspace::webPicture, "ask" → loadWebPicture) until every one is
///     ready or failed, at most `settleMs` (`timedOut`) → publishes "brulionWebPictureSelfTest"
///     {page, pageScheme, done, timedOut, elapsedMs, pictures: [{url, state, error, format,
///     width, height, uiFetched}], shownTexts (the editor's "webPictureText" items; with a
///     window)};
///  4. with `exportCopies`: ExportController (`exportFormat`, `exportScope`, webPictures
///     "include"), once the notebook is indexed; ready → saveInBrowser() when `save` (false off
///     the browser) → publishes "brulionExportSelfTest" {state, errorText, format, scope,
///     fileName, bytes, dataPictures (`src="data:image/` in the first file), warnings, saved};
///  5. finished().
class WebPicturesSelfTest : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString page;          ///< vault path whose pictures are watched (main.cpp: today's journal)
        int uiGraceMs = 10000; ///< wait this long for the editor to fetch them before asking
        int settleMs = 45000;  ///< give up on pictures still loading after this
        int exportMs = 90000;  ///< give up on the export after this (state "timeout")
        bool exportCopies = true;
        QString exportFormat = QStringLiteral("html-single");
        QString exportScope = QStringLiteral("vault");
        bool save = true; ///< ExportController::saveInBrowser() once the file is ready
    };

    WebPicturesSelfTest(shell::Workspace *workspace, Options options, PublishFn publish, QQuickWindow *window = nullptr,
                        QObject *parent = nullptr);
    ~WebPicturesSelfTest() override;

    void start();
    /// The last results published (tests).
    QJsonObject picturesReport() const { return m_picturesReport; }
    QJsonObject exportReport() const { return m_exportReport; }

Q_SIGNALS:
    void finished();

private:
    void graceStep();
    void pollStep();
    void publishPictures(bool timedOut);
    void startExport();
    void exportStateChanged();
    void publishExport(const QString &state);

    QPointer<shell::Workspace> m_workspace;
    Options m_options;
    PublishFn m_publish;
    QPointer<QQuickWindow> m_window;
    QStringList m_urls;
    QSet<QString> m_signalled; ///< told by the workspace (before this test asked: the editor's)
    QSet<QString> m_uiFetched;
    QSet<QString> m_asked; ///< loadWebPicture() called by this test
    bool m_polling = false;
    QElapsedTimer m_clock;
    QElapsedTimer m_phase;
    QTimer m_timer;
    ioviews::ExportController *m_export = nullptr;
    QJsonObject m_picturesReport;
    QJsonObject m_exportReport;
    bool m_finished = false;
};

/// The drawing editor's canvas under a pen: publishes "brulionPenSelfTest" once the canvas is
/// shown and after every change of it — {found, editable, stylusSeen, zoom, strokeCount (its
/// freehand lines), elements (all), changes, canvas: {x, y, width, height} (window coordinates),
/// strokes: [strokeFacts + bounds: {x, y, width, height} in window coordinates]}. Without a
/// canvas within the deadline: {found: false, error}.
class PenSelfTest : public QObject {
    Q_OBJECT
public:
    PenSelfTest(QQuickWindow *window, PublishFn publish, QObject *parent = nullptr);

    /// Looks for the canvas every 200 ms, up to the deadline (30 s).
    void start();
    QJsonObject report() const;
    /// The deadline of start() (tests).
    void setDeadlineMs(int ms) { m_deadlineMs = ms; }

private:
    void look();
    void publish();

    QPointer<QQuickWindow> m_window;
    PublishFn m_publish;
    QPointer<drawingview::DrawingCanvas> m_canvas;
    QTimer m_timer;
    QElapsedTimer m_clock;
    int m_deadlineMs = 30000;
    int m_changes = 0;
};

/// main.cpp, the browser: starts the flows `args` names (--selftest-webpictures: the pictures of
/// today's journal; --selftest-pen) once `app`'s status is "ready" (at once when it already is).
/// Nothing without those arguments.
void startBrowserSelfTests(QQmlApplicationEngine *engine, shell::AppController *app, const QStringList &args,
                           PublishFn publish);

} // namespace brulion::diag
