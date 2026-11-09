#include "webselftest.h"

#include "appcontroller.h"
#include "drawingcanvas.h"
#include "exportcontroller.h"
#include "workspace.h"

#include "drawing/drawing.h"
#include "io/exporter.h"
#include "vault/vault.h"

#include <QJsonArray>
#include <QPolygonF>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::diag {

namespace {

constexpr int StepMs = 250;

/// `visit(item)` for `item` and, while it returns true, for its children (the visual tree, depth
/// first).
template<typename Visit>
void walk(QQuickItem *item, const Visit &visit)
{
    if (!item || !visit(item))
        return;
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children)
        walk(child, visit);
}

/// The visible, interactive drawing canvas with the largest area under `item` (the editor's
/// paper, not a preview).
drawingview::DrawingCanvas *largestCanvas(QQuickItem *item)
{
    drawingview::DrawingCanvas *best = nullptr;
    qreal bestArea = 0;
    walk(item, [&](QQuickItem *it) {
        if (!it->isVisible())
            return false;
        auto *canvas = qobject_cast<drawingview::DrawingCanvas *>(it);
        if (canvas && canvas->interactive() && canvas->width() * canvas->height() > bestArea) {
            best = canvas;
            bestArea = canvas->width() * canvas->height();
        }
        return true;
    });
    return best;
}

/// The first shown rich text under `item` (a Text or TextEdit with linkAt()) whose text holds `needle`.
QQuickItem *richTextHolding(QQuickItem *item, const QString &needle)
{
    QQuickItem *found = nullptr;
    walk(item, [&](QQuickItem *it) {
        if (found || !it->isVisible())
            return false;
        if (it->metaObject()->indexOfMethod("linkAt(double,double)") >= 0 && it->width() > 0 && it->height() > 0
            && it->property("text").toString().contains(needle))
            found = it;
        return !found;
    });
    return found;
}

bool under(const QQuickItem *item, const QString &objectName)
{
    for (const QQuickItem *p = item; p; p = p->parentItem()) {
        if (p->objectName() == objectName)
            return true;
    }
    return false;
}

QJsonObject rectJson(const QRectF &r)
{
    return {{u"x"_s, r.x()}, {u"y"_s, r.y()}, {u"width"_s, r.width()}, {u"height"_s, r.height()}};
}

QJsonObject sceneRect(const QQuickItem *item)
{
    const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
    return {{u"x"_s, r.x()}, {u"y"_s, r.y()}, {u"w"_s, r.width()}, {u"h"_s, r.height()}};
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
    const auto [lo, hi] = std::ranges::minmax(stroke.points, {}, &drawing::StrokePoint::pressure);
    facts.insert(u"pressureMin"_s, lo.pressure);
    facts.insert(u"pressureMax"_s, hi.pressure);
    facts.insert(u"pressureFirst"_s, stroke.points.first().pressure);
    facts.insert(u"pressureLast"_s, stroke.points.last().pressure);
    return facts;
}

QStringList shownTexts(QQuickItem *root, const QString &objectName)
{
    QStringList out;
    walk(root, [&](QQuickItem *item) {
        if (!item->isVisible() || qFuzzyIsNull(item->opacity()))
            return false;
        if (item->objectName() == objectName && item->inherits("QQuickText"))
            out.append(item->property("text").toString());
        return true;
    });
    return out;
}

// ---- WebPicturesSelfTest ---------------------------------------------------------------------

WebPicturesSelfTest::WebPicturesSelfTest(shell::Workspace *workspace, Options options, PublishFn publish,
                                         QQuickWindow *window, QObject *parent)
    : QObject(parent)
    , m_workspace(workspace)
    , m_options(std::move(options))
    , m_publish(std::move(publish))
    , m_window(window)
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

void WebPicturesSelfTest::start()
{
    m_clock.start();
    // Only where the device shows pictures from the internet without asking (tests/web seeds
    // that): never a fetch the user was to be asked for, nor an export of their notebook.
    if (m_workspace && !m_workspace->webPicturesShown()) {
        m_picturesReport = QJsonObject{
                {u"page"_s, m_options.page},
                {u"done"_s, true},
                {u"refused"_s, true},
                {u"error"_s, u"pictures from the internet are not shown without asking on this device"_s},
                {u"pictures"_s, QJsonArray()},
        };
        if (m_publish)
            m_publish("brulionWebPictureSelfTest", m_picturesReport);
        finish();
        return;
    }
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
    runTimer(&WebPicturesSelfTest::graceStep);
    graceStep();
}

void WebPicturesSelfTest::stopTimer()
{
    m_timer.stop();
    disconnect(&m_timer, nullptr, this, nullptr);
}

void WebPicturesSelfTest::finish()
{
    m_finished = true;
    Q_EMIT finished();
}

void WebPicturesSelfTest::graceStep()
{
    const bool allTold = std::ranges::all_of(m_urls, [this](const QString &url) { return m_signalled.contains(url); });
    if (!allTold && m_phase.elapsed() < m_options.uiGraceMs)
        return;
    // Asking from now on: what was told so far, the editor fetched by itself.
    m_polling = true;
    for (const QString &url : std::as_const(m_urls)) {
        if (m_signalled.contains(url))
            m_uiFetched.insert(url);
    }
    runTimer(&WebPicturesSelfTest::pollStep);
    pollStep();
}

void WebPicturesSelfTest::pollStep()
{
    if (!m_workspace) {
        publishPictures(true);
        return;
    }
    // Looking is what the editor does (Workspace::webPicture fetches what may be fetched without
    // asking); "ask" (a host of the local network) stays: this test never asks for a picture.
    // Every picture is looked at on every step (that starts its fetch): no early exit.
    bool settled = true;
    for (const QString &url : std::as_const(m_urls)) {
        const QString state = m_workspace->webPicture(url).value(u"state"_s).toString();
        if (state != u"ready" && state != u"failed" && state != u"ask")
            settled = false;
    }
    if (settled) {
        // The editor's rows follow a moment later: their messages are waited for (a picture
        // scrolled out of view never shows one: up to uiSettleMs).
        if (!m_settledAt.isValid())
            m_settledAt.start();
        if (m_window && m_settledAt.elapsed() < m_options.uiSettleMs) {
            const QStringList shown = shownTexts(m_window->contentItem(), u"webPictureText"_s);
            for (const QString &url : std::as_const(m_urls)) {
                const QVariantMap picture = m_workspace->webPicture(url);
                if (picture.value(u"state"_s).toString() == u"failed"
                    && !shown.contains(picture.value(u"error"_s).toString()))
                    return;
            }
        }
        publishPictures(false);
    } else if (m_phase.elapsed() >= m_options.settleMs) {
        publishPictures(true);
    }
}

void WebPicturesSelfTest::publishPictures(bool timedOut)
{
    stopTimer();
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
            {u"page"_s, m_options.page}, {u"pageScheme"_s, m_workspace ? m_workspace->webPageScheme() : QString()},
            {u"done"_s, true},           {u"refused"_s, false},
            {u"timedOut"_s, timedOut},   {u"elapsedMs"_s, double(m_clock.elapsed())},
            {u"pictures"_s, pictures},
    };
    if (m_window)
        m_picturesReport.insert(u"shownTexts"_s,
                                QJsonArray::fromStringList(shownTexts(m_window->contentItem(), u"webPictureText"_s)));
    if (m_publish)
        m_publish("brulionWebPictureSelfTest", m_picturesReport);
    if (!m_options.exportCopies || !m_workspace) {
        finish();
        return;
    }
    // The export reads the notebook as indexed: wait for the index.
    runTimer([this] {
        if (m_workspace && m_workspace->ready())
            startExport();
        else if (m_phase.elapsed() >= m_options.exportMs)
            publishExport(u"timeout"_s);
    });
}

void WebPicturesSelfTest::startExport()
{
    m_export = new ioviews::ExportController(this);
    m_export->setWorkspace(m_workspace);
    m_export->setFormat(m_options.exportFormat);
    m_export->setScope(m_options.exportScope);
    m_export->setWebPictures(u"include"_s);
    connect(m_export, &ioviews::ExportController::stateChanged, this, &WebPicturesSelfTest::exportStateChanged);
    runTimer([this] {
        if (m_phase.elapsed() >= m_options.exportMs)
            publishExport(u"timeout"_s);
    });
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
    stopTimer();
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
    for (auto signal : {&DrawingCanvas::stylusSeenChanged, &DrawingCanvas::loadedChanged, &DrawingCanvas::viewChanged,
                        &DrawingCanvas::modifiedChanged})
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
            QPolygonF onScreen;
            onScreen.reserve(s.points.size());
            for (const drawing::StrokePoint &p : s.points)
                onScreen.append(canvas->mapToScene(canvas->toItem(QPointF(p.x, p.y))));
            facts.insert(u"bounds"_s, rectJson(onScreen.boundingRect()));
        }
        strokes.append(facts);
    }
    const QPointF origin = canvas->mapToScene(QPointF(0, 0));
    return {
            {u"found"_s, true},
            {u"editable"_s, canvas->editable()},
            {u"modified"_s, canvas->modified()},
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

// ---- LinkPartsSelfTest ------------------------------------------------------------------------

LinkPartsSelfTest::LinkPartsSelfTest(QQuickWindow *window, shell::AppController *app, PublishFn publish,
                                     QObject *parent)
    : QObject(parent), m_window(window), m_app(app), m_publish(std::move(publish))
{
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &LinkPartsSelfTest::look);
}

void LinkPartsSelfTest::start()
{
    m_timer.start();
    look();
}

QJsonArray LinkPartsSelfTest::linkBoxes(QQuickItem *text)
{
    struct Box {
        QString href;
        int x1, y1, x2, y2;
    };
    QList<Box> boxes; // in order of first sight
    for (int y = 1; text && y < text->height(); y += 2) {
        for (int x = 1; x < text->width(); x += 2) {
            QString link;
            QMetaObject::invokeMethod(text, "linkAt", qReturnArg(link), qreal(x), qreal(y));
            if (link.isEmpty())
                continue;
            auto it = std::ranges::find(boxes, link, &Box::href);
            if (it == boxes.end()) {
                boxes.append(Box{link, x, y, x, y});
            } else {
                it->x1 = std::min(it->x1, x);
                it->y1 = std::min(it->y1, y);
                it->x2 = std::max(it->x2, x);
                it->y2 = std::max(it->y2, y);
            }
        }
    }
    QJsonArray out;
    for (const Box &b : std::as_const(boxes)) {
        const QPointF origin = text->mapToScene(QPointF(b.x1, b.y1));
        QJsonObject box = rectJson(QRectF(origin, QSizeF(b.x2 - b.x1, b.y2 - b.y1)));
        box.insert(u"href"_s, b.href);
        out.append(box);
    }
    return out;
}

QJsonObject LinkPartsSelfTest::report() const
{
    QQuickItem *text = m_window ? richTextHolding(m_window->contentItem(), u"href=\"part:"_s) : nullptr;
    const QVariantMap route = m_app ? m_app->currentRoute() : QVariantMap();
    return {{u"ready"_s, text != nullptr},
            {u"parts"_s, text ? linkBoxes(text) : QJsonArray()},
            {u"route"_s, QJsonObject{{u"view"_s, route.value(u"view"_s).toString()},
                                     {u"path"_s, route.value(u"path"_s).toString()}}}};
}

void LinkPartsSelfTest::look()
{
    const QJsonObject now = report();
    if (now == m_last)
        return;
    m_last = now;
    if (m_publish)
        m_publish("brulionLinkPartsSelfTest", now);
}

// ---- PdfSelfTest -------------------------------------------------------------------------------

PdfSelfTest::PdfSelfTest(QQuickWindow *window, shell::Workspace *workspace, PublishFn publish, QObject *parent)
    : QObject(parent), m_window(window), m_workspace(workspace), m_publish(std::move(publish))
{
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &PdfSelfTest::look);
}

void PdfSelfTest::start()
{
    m_clock.start();
    m_timer.start();
    look();
}

QJsonObject PdfSelfTest::report() const
{
    QJsonObject out;
    const QVariantMap route = m_route ? m_route() : QVariantMap();
    out.insert(u"route"_s, QJsonObject{{u"view"_s, route.value(u"view"_s).toString()},
                                       {u"path"_s, route.value(u"path"_s).toString()}});
    // The first one of each on screen (the visual tree, depth first).
    QQuickItem *chip = nullptr;
    QQuickItem *link = nullptr;
    QQuickItem *message = nullptr;
    QQuickItem *button = nullptr;
    const auto first = [](QQuickItem *&slot, QQuickItem *item) {
        if (!slot)
            slot = item;
    };
    if (m_window) {
        walk(m_window->contentItem(), [&](QQuickItem *item) {
            if (!item->isVisible() || item->opacity() <= 0.01)
                return false;
            const QString name = item->objectName();
            if (name == u"pdfEmbedChip")
                first(chip, item);
            else if (name == u"pdfMessageText")
                first(message, item);
            else if (name == u"pdfOpenElsewhere")
                first(button, item);
            else if (!link && (item->inherits("QQuickText") || item->inherits("QQuickTextEdit"))
                     && !under(item, u"pdfEmbedChip"_s)) {
                const QString text = item->property("text").toString();
                if (text.contains(u"<a "_s) && text.contains(u".pdf"_s, Qt::CaseInsensitive))
                    link = item;
            }
            return true;
        });
    }
    if (chip) {
        QJsonObject c = sceneRect(chip);
        c.insert(u"icon"_s, chip->property("iconName").toString());
        c.insert(u"text"_s, chip->property("text").toString());
        out.insert(u"chip"_s, c);
    } else {
        out.insert(u"chip"_s, QJsonValue::Null);
    }
    out.insert(u"link"_s, link ? QJsonValue(sceneRect(link)) : QJsonValue(QJsonValue::Null));
    out.insert(u"message"_s, message ? message->property("text").toString() : QString());
    out.insert(u"button"_s, button ? button->property("text").toString() : QString());
    return out;
}

void PdfSelfTest::look()
{
    if (m_clock.elapsed() >= m_durationMs)
        m_timer.stop();
    const QJsonObject now = report();
    if (now == m_last)
        return;
    m_last = now;
    if (m_publish)
        m_publish("brulionPdfSelfTest", now);
}

void PdfSelfTest::openNow(const QString &path)
{
    QStringList notices;
    QStringList errors;
    bool opened = false;
    if (m_workspace) {
        // openInOtherApp() reports synchronously: the connections live only through the call.
        const QMetaObject::Connection n = connect(m_workspace, &shell::Workspace::noticeOccurred, this,
                                                  [&notices](const QString &text) { notices.append(text); });
        const QMetaObject::Connection e = connect(m_workspace, &shell::Workspace::errorOccurred, this,
                                                  [&errors](const QString &text) { errors.append(text); });
        opened = m_workspace->openInOtherApp(path);
        disconnect(n);
        disconnect(e);
    }
    if (m_publish)
        m_publish("brulionPdfOpenSelfTest", {{u"path"_s, path},
                                             {u"opened"_s, opened},
                                             {u"notices"_s, QJsonArray::fromStringList(notices)},
                                             {u"errors"_s, QJsonArray::fromStringList(errors)}});
}

// ---- main.cpp --------------------------------------------------------------------------------

void startBrowserSelfTests(QQmlApplicationEngine *engine, shell::AppController *app, const QStringList &args,
                           PublishFn publish)
{
    const bool pictures = args.contains(u"--selftest-webpictures"_s);
    const bool pen = args.contains(u"--selftest-pen"_s);
    const bool pdfAuto = args.contains(u"--selftest-pdfauto"_s);
    const bool pdf = pdfAuto || args.contains(u"--selftest-pdf"_s);
    const bool linkParts = args.contains(u"--selftest-linkparts"_s);
    if (!app || (!pictures && !pen && !pdf && !linkParts))
        return;
    const auto run = [engine, app, pictures, pen, pdf, pdfAuto, linkParts, publish = std::move(publish)] {
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
        if (pdf) {
            auto *test = new PdfSelfTest(window, app->workspace(), publish, app);
            test->setRouteSource([app] { return app->currentRoute(); });
            test->start();
            if (pdfAuto)
                QTimer::singleShot(2000, test, [test] { test->openNow(u"assets/wykład.pdf"_s); });
        }
        if (linkParts) {
            auto *test = new LinkPartsSelfTest(window, app, publish, app);
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
