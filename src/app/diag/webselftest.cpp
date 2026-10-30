#include "webselftest.h"

#include "appcontroller.h"
#include "drawingcanvas.h"
#include "exportcontroller.h"
#include "workspace.h"

#include "drawing/drawing.h"
#include "io/exporter.h"
#include "vault/vault.h"

#include <QJsonArray>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>

#include <algorithm>
#include <limits>
#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::diag {

namespace {

constexpr int StepMs = 250;

void collectTexts(QQuickItem *item, const QString &objectName, bool hidden, QStringList *out)
{
    if (!item)
        return;
    hidden = hidden || !item->isVisible() || qFuzzyIsNull(item->opacity());
    if (!hidden && item->objectName() == objectName && item->inherits("QQuickText"))
        out->append(item->property("text").toString());
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children)
        collectTexts(child, objectName, hidden, out);
}

/// The visible, interactive drawing canvas with the largest area under `item` (the editor's
/// paper, not a preview).
drawingview::DrawingCanvas *largestCanvas(QQuickItem *item)
{
    drawingview::DrawingCanvas *best = nullptr;
    qreal bestArea = 0;
    const std::function<void(QQuickItem *)> walk = [&](QQuickItem *it) {
        if (!it || !it->isVisible())
            return;
        if (auto *canvas = qobject_cast<drawingview::DrawingCanvas *>(it); canvas && canvas->interactive()) {
            const qreal area = canvas->width() * canvas->height();
            if (area > bestArea) {
                best = canvas;
                bestArea = area;
            }
        }
        const QList<QQuickItem *> children = it->childItems();
        for (QQuickItem *child : children)
            walk(child);
    };
    walk(item);
    return best;
}

QJsonObject rectJson(const QRectF &r)
{
    return {{u"x"_s, r.x()}, {u"y"_s, r.y()}, {u"width"_s, r.width()}, {u"height"_s, r.height()}};
}

} // namespace

QJsonObject strokeFacts(const drawing::Stroke &stroke)
{
    QJsonObject facts{
            {u"tool"_s, stroke.tool == drawing::Tool::Highlighter ? u"highlighter"_s : u"pen"_s},
            {u"simulatePressure"_s, stroke.simulatePressure},
            {u"points"_s, int(stroke.points.size())},
            {u"width"_s, stroke.width},
    };
    if (stroke.points.isEmpty()) {
        for (const QString &key : {u"pressureMin"_s, u"pressureMax"_s, u"pressureFirst"_s, u"pressureLast"_s})
            facts.insert(key, QJsonValue(QJsonValue::Null));
        return facts;
    }
    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    for (const drawing::StrokePoint &p : stroke.points) {
        lo = std::min(lo, p.pressure);
        hi = std::max(hi, p.pressure);
    }
    facts.insert(u"pressureMin"_s, lo);
    facts.insert(u"pressureMax"_s, hi);
    facts.insert(u"pressureFirst"_s, stroke.points.first().pressure);
    facts.insert(u"pressureLast"_s, stroke.points.last().pressure);
    return facts;
}

QStringList shownTexts(QQuickItem *root, const QString &objectName)
{
    QStringList out;
    collectTexts(root, objectName, false, &out);
    return out;
}

// ---- WebPicturesSelfTest ---------------------------------------------------------------------

WebPicturesSelfTest::WebPicturesSelfTest(shell::Workspace *workspace, Options options, PublishFn publish,
                                         QQuickWindow *window, QObject *parent)
    : QObject(parent), m_workspace(workspace), m_options(std::move(options)), m_publish(std::move(publish)),
      m_window(window)
{
    m_timer.setInterval(StepMs);
    // What the workspace tells before this test asks for anything was asked for by the editor.
    if (m_workspace) {
        connect(m_workspace, &shell::Workspace::webPictureChanged, this, [this](const QString &url) {
            if (!m_polling && !url.isEmpty())
                m_signalled.insert(url);
        });
    }
}

WebPicturesSelfTest::~WebPicturesSelfTest() = default;

void WebPicturesSelfTest::start()
{
    m_clock.start();
    m_phase.start();
    Vault *vault = m_workspace ? m_workspace->vault() : nullptr;
    if (vault && !m_options.page.isEmpty()) {
        bool ok = false;
        const QByteArray bytes = vault->readBinary(m_options.page, &ok);
        if (ok) {
            io::ExportInput input;
            input.files.append(io::FileEntry{m_options.page, bytes});
            input.config = vault->config();
            m_urls = io::webPictureTargets(input);
        }
    }
    disconnect(&m_timer, nullptr, this, nullptr);
    connect(&m_timer, &QTimer::timeout, this, &WebPicturesSelfTest::graceStep);
    m_timer.start();
    graceStep();
}

void WebPicturesSelfTest::graceStep()
{
    const bool allTold = std::all_of(m_urls.cbegin(), m_urls.cend(),
                                     [this](const QString &url) { return m_signalled.contains(url); });
    if (!allTold && m_phase.elapsed() < m_options.uiGraceMs)
        return;
    // Asking from now on: what was told so far, the editor fetched by itself.
    m_polling = true;
    for (const QString &url : std::as_const(m_urls)) {
        if (m_signalled.contains(url))
            m_uiFetched.insert(url);
    }
    m_phase.start();
    disconnect(&m_timer, nullptr, this, nullptr);
    connect(&m_timer, &QTimer::timeout, this, &WebPicturesSelfTest::pollStep);
    pollStep();
}

void WebPicturesSelfTest::pollStep()
{
    if (!m_workspace) {
        publishPictures(true);
        return;
    }
    bool settled = true;
    for (const QString &url : std::as_const(m_urls)) {
        QString state = m_workspace->webPicture(url).value(u"state"_s).toString();
        if (state == u"ask" && !m_asked.contains(url)) {
            m_asked.insert(url);
            m_workspace->loadWebPicture(url);
            state = u"loading"_s;
        }
        if (state != u"ready" && state != u"failed")
            settled = false;
    }
    if (settled)
        publishPictures(false);
    else if (m_phase.elapsed() >= m_options.settleMs)
        publishPictures(true);
}

void WebPicturesSelfTest::publishPictures(bool timedOut)
{
    m_timer.stop();
    disconnect(&m_timer, nullptr, this, nullptr);
    QJsonArray pictures;
    for (const QString &url : std::as_const(m_urls)) {
        const QVariantMap p = m_workspace ? m_workspace->webPicture(url) : QVariantMap();
        pictures.append(QJsonObject{
                {u"url"_s, url},
                {u"state"_s, p.value(u"state"_s).toString()},
                {u"error"_s, p.value(u"error"_s).toString()},
                {u"format"_s, p.value(u"format"_s).toString()},
                {u"width"_s, p.value(u"width"_s).toInt()},
                {u"height"_s, p.value(u"height"_s).toInt()},
                {u"uiFetched"_s, m_uiFetched.contains(url)},
        });
    }
    m_picturesReport = QJsonObject{
            {u"page"_s, m_options.page},
            {u"pageScheme"_s, m_workspace ? m_workspace->webPageScheme() : QString()},
            {u"done"_s, true},
            {u"timedOut"_s, timedOut},
            {u"elapsedMs"_s, double(m_clock.elapsed())},
            {u"pictures"_s, pictures},
    };
    if (m_window)
        m_picturesReport.insert(u"shownTexts"_s,
                                QJsonArray::fromStringList(shownTexts(m_window->contentItem(), u"webPictureText"_s)));
    if (m_publish)
        m_publish("brulionWebPictureSelfTest", m_picturesReport);
    if (!m_options.exportCopies || !m_workspace) {
        m_finished = true;
        Q_EMIT finished();
        return;
    }
    // The export reads the notebook as indexed: wait for the index.
    m_phase.start();
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_workspace && m_workspace->ready())
            startExport();
        else if (m_phase.elapsed() >= m_options.exportMs)
            publishExport(u"timeout"_s);
    });
    m_timer.start();
}

void WebPicturesSelfTest::startExport()
{
    m_timer.stop();
    disconnect(&m_timer, nullptr, this, nullptr);
    m_export = new ioviews::ExportController(this);
    m_export->setWorkspace(m_workspace);
    m_export->setFormat(m_options.exportFormat);
    m_export->setScope(m_options.exportScope);
    m_export->setWebPictures(u"include"_s);
    connect(m_export, &ioviews::ExportController::stateChanged, this, &WebPicturesSelfTest::exportStateChanged);
    m_phase.start();
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_phase.elapsed() >= m_options.exportMs)
            publishExport(u"timeout"_s);
    });
    m_timer.start();
    m_export->prepare();
}

void WebPicturesSelfTest::exportStateChanged()
{
    if (m_finished || !m_export)
        return;
    const QString state = m_export->state();
    if (state == u"ready") {
        // saveInBrowser() reports "saved" (handled re-entrantly) or does nothing off the browser.
        if (m_options.save && m_export->saveInBrowser())
            return;
        publishExport(state);
    } else if (state == u"saved" || state == u"error") {
        publishExport(state);
    }
}

void WebPicturesSelfTest::publishExport(const QString &state)
{
    if (m_finished)
        return;
    m_finished = true;
    m_timer.stop();
    disconnect(&m_timer, nullptr, this, nullptr);
    const QList<io::ExportedFile> files = m_export ? m_export->result() : QList<io::ExportedFile>();
    const QByteArray data = files.isEmpty() ? QByteArray() : files.first().data;
    m_exportReport = QJsonObject{
            {u"state"_s, state},
            {u"errorText"_s, m_export ? m_export->errorText() : QString()},
            {u"format"_s, m_options.exportFormat},
            {u"scope"_s, m_options.exportScope},
            {u"fileName"_s, files.isEmpty() ? QString() : files.first().name},
            {u"bytes"_s, double(data.size())},
            {u"dataPictures"_s, int(data.count("src=\"data:image/"))},
            {u"warnings"_s, QJsonArray::fromStringList(m_export ? m_export->warnings() : QStringList())},
            {u"saved"_s, state == u"saved"},
    };
    if (m_publish)
        m_publish("brulionExportSelfTest", m_exportReport);
    Q_EMIT finished();
}

// ---- PenSelfTest -----------------------------------------------------------------------------

PenSelfTest::PenSelfTest(QQuickWindow *window, PublishFn publish, QObject *parent)
    : QObject(parent), m_window(window), m_publish(std::move(publish))
{
    m_timer.setInterval(200);
    connect(&m_timer, &QTimer::timeout, this, &PenSelfTest::look);
}

void PenSelfTest::start()
{
    m_clock.start();
    m_timer.start();
    look();
}

void PenSelfTest::look()
{
    if (m_canvas)
        return;
    drawingview::DrawingCanvas *canvas = m_window ? largestCanvas(m_window->contentItem()) : nullptr;
    if (!canvas) {
        if (m_clock.elapsed() >= m_deadlineMs) {
            m_timer.stop();
            if (m_publish)
                m_publish("brulionPenSelfTest",
                          QJsonObject{{u"found"_s, false}, {u"error"_s, u"no drawing canvas on screen"_s}});
        }
        return;
    }
    m_timer.stop();
    m_canvas = canvas;
    using drawingview::DrawingCanvas;
    connect(canvas, &DrawingCanvas::contentChanged, this, [this] {
        ++m_changes;
        publish();
    });
    for (auto signal : {&DrawingCanvas::stylusSeenChanged, &DrawingCanvas::loadedChanged, &DrawingCanvas::viewChanged})
        connect(canvas, signal, this, &PenSelfTest::publish);
    // The editor closed (another route): look again.
    connect(canvas, &QObject::destroyed, this, [this] {
        m_clock.start();
        m_timer.start();
    });
    publish();
}

QJsonObject PenSelfTest::report() const
{
    if (!m_canvas)
        return {{u"found"_s, false}, {u"error"_s, u"no drawing canvas on screen"_s}};
    const drawingview::DrawingCanvas *canvas = m_canvas;
    const drawing::Drawing doc = canvas->currentDrawing();
    QJsonArray strokes;
    for (const drawing::Stroke &s : doc.strokes) {
        if (s.type != drawing::ElementType::Freedraw)
            continue;
        QJsonObject facts = strokeFacts(s);
        if (!s.points.isEmpty()) {
            QRectF bounds;
            bool first = true;
            for (const drawing::StrokePoint &p : s.points) {
                const QPointF at = canvas->mapToScene(canvas->toItem(QPointF(p.x, p.y)));
                if (first) {
                    bounds = QRectF(at, QSizeF(0, 0));
                    first = false;
                } else {
                    bounds.setLeft(std::min(bounds.left(), at.x()));
                    bounds.setRight(std::max(bounds.right(), at.x()));
                    bounds.setTop(std::min(bounds.top(), at.y()));
                    bounds.setBottom(std::max(bounds.bottom(), at.y()));
                }
            }
            facts.insert(u"bounds"_s, rectJson(bounds));
        }
        strokes.append(facts);
    }
    const QPointF origin = canvas->mapToScene(QPointF(0, 0));
    return {
            {u"found"_s, true},
            {u"editable"_s, canvas->editable()},
            {u"stylusSeen"_s, canvas->stylusSeen()},
            {u"zoom"_s, canvas->zoom()},
            {u"strokeCount"_s, int(strokes.size())},
            {u"elements"_s, int(doc.strokes.size())},
            {u"changes"_s, m_changes},
            {u"canvas"_s, rectJson(QRectF(origin, QSizeF(canvas->width(), canvas->height())))},
            {u"strokes"_s, strokes},
    };
}

void PenSelfTest::publish()
{
    if (m_publish)
        m_publish("brulionPenSelfTest", report());
}

// ---- main.cpp --------------------------------------------------------------------------------

void startBrowserSelfTests(QQmlApplicationEngine *engine, shell::AppController *app, const QStringList &args,
                           PublishFn publish)
{
    const bool pictures = args.contains(u"--selftest-webpictures"_s);
    const bool pen = args.contains(u"--selftest-pen"_s);
    if (!app || (!pictures && !pen))
        return;
    const auto run = [engine, app, pictures, pen, publish] {
        QQuickWindow *window = engine ? qobject_cast<QQuickWindow *>(engine->rootObjects().value(0)) : nullptr;
        if (pictures) {
            WebPicturesSelfTest::Options options;
            options.page = app->workspace()->journalFileFor(app->today());
            auto *test = new WebPicturesSelfTest(app->workspace(), options, publish, window, app);
            test->start();
        }
        if (pen) {
            auto *test = new PenSelfTest(window, publish, app);
            test->start();
        }
    };
    if (app->status() == u"ready") {
        run();
        return;
    }
    auto started = std::make_shared<bool>(false);
    QObject::connect(app, &shell::AppController::statusChanged, app, [app, run, started] {
        if (*started || app->status() != u"ready")
            return;
        *started = true;
        run();
    });
}

} // namespace brulion::diag
