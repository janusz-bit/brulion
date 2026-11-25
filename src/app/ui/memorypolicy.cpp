#include "memorypolicy.h"

#include "memorykeeper.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMetaProperty>
#include <QPixmapCache>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWindow>

#include <algorithm>
#include <utility>

namespace brulion::ui {

namespace {

Q_LOGGING_CATEGORY(lcMemory, "brulion.memory", QtInfoMsg)

/// The deletions letGo() started (views and popups: deleteLater) run before the engine and the
/// windows are asked for what they held.
constexpr int kReleaseDelayMs = 250;

/// Makes every Shape under `item` build its nodes again. Qt Quick's Shape (6.12) keeps pointers to
/// the nodes it made and makes a path's nodes only when the path changes: once its window's scene
/// graph was released it draws nothing when the window is shown again (the notebook's round mark
/// lost its fill), and at its next change it would delete the released nodes once more. A Shape
/// starts over when its renderer is another one: asked for another renderer, polished, and asked
/// for its own again. (The software renderer has one node it fills every time: nothing to do, and
/// nothing changes there.)
void rebuildShapes(QQuickItem *item)
{
    if (!item)
        return;
    if (item->inherits("QQuickShape")) {
        const QMetaObject *meta = item->metaObject();
        const int index = meta->indexOfProperty("preferredRendererType");
        if (index >= 0) {
            const QMetaProperty property = meta->property(index);
            const QVariant own = property.read(item);
            // (QQuickShape::RendererType: GeometryRenderer 1, CurveRenderer 4.)
            const int other = own.toInt() == 4 ? 1 : 4;
            if (property.write(item, other)) {
                item->ensurePolished();
                property.write(item, own);
            }
        }
    }
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children)
        rebuildShapes(child);
}

} // namespace

MemoryPolicy *MemoryPolicy::instance()
{
    static QPointer<MemoryPolicy> policy;
    if (!policy && QCoreApplication::instance()) {
        policy = new MemoryPolicy(QCoreApplication::instance());
        policy->follow();
    }
    return policy;
}

MemoryPolicy::MemoryPolicy(QObject *parent) : QObject(parent)
{
    m_awayTimer.setSingleShot(true);
    connect(&m_awayTimer, &QTimer::timeout, this, &MemoryPolicy::away);
    m_releaseTimer.setSingleShot(true);
    m_releaseTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_releaseTimer, &QTimer::timeout, this, &MemoryPolicy::release);
    bool ok = false;
    const int ms = qEnvironmentVariableIntValue("BRULION_AWAY_MS", &ok);
    if (ok && ms >= 0) {
        m_unfocusedMs = ms;
        m_hiddenMs = ms;
        m_startMs = ms;
    }
}

MemoryPolicy::~MemoryPolicy() = default;

bool MemoryPolicy::switchedOff()
{
    static const bool off = qEnvironmentVariable("BRULION_MEMORY_POLICY") == QLatin1String("0");
    return off;
}

void MemoryPolicy::setKeptViews(int count)
{
    if (m_keptViews == count)
        return;
    m_keptViews = count;
    Q_EMIT limitsChanged();
}

void MemoryPolicy::setHiddenViewMs(int ms)
{
    if (m_hiddenViewMs == ms)
        return;
    m_hiddenViewMs = ms;
    Q_EMIT limitsChanged();
}

void MemoryPolicy::setEngine(QQmlEngine *engine)
{
    m_engine = engine;
}

void MemoryPolicy::follow()
{
    auto *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!gui || switchedOff())
        return;
    connect(gui, &QGuiApplication::applicationStateChanged, this, &MemoryPolicy::evaluate);
    // A window shown or hidden (minimised, sent to the tray, covered on a phone) tells nobody but
    // itself: its Expose events are the application's.
    gui->installEventFilter(this);
    evaluate();
}

void MemoryPolicy::setFollowsApplication(bool follows)
{
    if (m_follows == follows)
        return;
    m_follows = follows;
    if (follows)
        evaluate();
}

bool MemoryPolicy::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Expose && m_follows && watched->isWindowType())
        QMetaObject::invokeMethod(this, &MemoryPolicy::evaluate, Qt::QueuedConnection);
    return false;
}

void MemoryPolicy::evaluate()
{
    if (!m_follows)
        return;
    const Qt::ApplicationState state = QGuiApplication::applicationState();
    bool shown = false;
    // (A phone's app in the background keeps its window object: its state says it is not shown.)
    if (state != Qt::ApplicationSuspended && state != Qt::ApplicationHidden) {
        const QList<QWindow *> windows = QGuiApplication::topLevelWindows();
        for (const QWindow *window : windows) {
            if (window->isExposed()) {
                shown = true;
                break;
            }
        }
    }
    setState(state == Qt::ApplicationActive, shown);
}

void MemoryPolicy::setState(bool active, bool shown)
{
    m_active = active;
    m_shown = shown;
    if (switchedOff())
        return;
    if (active) {
        m_everActive = true;
        m_everShown = m_everShown || shown;
        m_awayTimer.stop();
        m_letGoHidden = false;
        if (!m_inUse) {
            m_inUse = true;
            qCDebug(lcMemory) << "In use again";
            Q_EMIT inUseChanged();
        }
        return;
    }
    if (shown)
        m_everShown = true;
    // Shown but never in front: nothing says the user left (a window without a window manager;
    // the moment between a start and the window's first activation).
    if (shown && !m_everActive) {
        m_awayTimer.stop();
        return;
    }
    const bool hidden = !shown;
    if (!m_inUse) {
        // Away already; a window hidden only now lets go of its graphics too.
        if (hidden && !m_letGoHidden && !(m_awayTimer.isActive() && m_awayHidden)) {
            m_awayHidden = true;
            startAway(m_hiddenMs);
        } else if (!hidden && m_awayTimer.isActive()) {
            m_awayTimer.stop();
        }
        return;
    }
    if (!m_awayTimer.isActive() || hidden != m_awayHidden) {
        m_awayHidden = hidden;
        // (Never shown yet: a start into the tray, or a start that has not got to its window — it
        // is given the time to finish first.)
        const int hiddenMs = m_everShown ? m_hiddenMs : std::max(m_hiddenMs, m_startMs);
        startAway(hidden ? hiddenMs : m_unfocusedMs);
    }
}

void MemoryPolicy::startAway(int ms)
{
    // (Whole seconds are precise enough for the real delays, and wake the processor least.)
    m_awayTimer.setTimerType(ms >= 5000 ? Qt::VeryCoarseTimer : Qt::CoarseTimer);
    m_awayTimer.start(ms);
}

void MemoryPolicy::away()
{
    if (m_active)
        return;
    const bool hidden = !m_shown;
    if (m_inUse) {
        m_inUse = false;
        Q_EMIT inUseChanged();
    }
    qCDebug(lcMemory) << "Away" << (hidden ? "(no window shown)" : "(another program in front)");
    letGoNow(hidden);
    // Unfocused first, hidden later: setState() starts the second round when the window goes.
}

void MemoryPolicy::letGoNow(bool hidden)
{
    if (switchedOff())
        return;
    ++m_runs;
    if (hidden)
        m_letGoHidden = true;
    Q_EMIT letGo(hidden);
    m_releaseHidden = m_releaseHidden || hidden;
    m_releaseTimer.start(kReleaseDelayMs);
}

void MemoryPolicy::release()
{
    const bool hidden = std::exchange(m_releaseHidden, false);
    if (m_engine) {
        // The wrappers of what was deleted, then the QML types nothing uses any more (the views
        // and dialogs just let go, and what only they needed).
        m_engine->collectGarbage();
        m_engine->trimComponentCache();
    }
    const QList<QWindow *> windows = QGuiApplication::topLevelWindows();
    for (QWindow *window : windows) {
        auto *quick = qobject_cast<QQuickWindow *>(window);
        if (!quick)
            continue;
        // A window nobody sees gives up its scene graph and its graphics context (textures, glyph
        // caches, the renderer's buffers): Qt Quick builds them again when it is shown. One that
        // is shown only drops what it cached.
        const bool unseen = hidden && !quick->isExposed();
        const bool persistentSceneGraph = quick->isPersistentSceneGraph();
        const bool persistentGraphics = quick->isPersistentGraphics();
        if (unseen) {
            quick->setPersistentSceneGraph(false);
            quick->setPersistentGraphics(false);
        }
        quick->releaseResources();
        if (unseen) {
            quick->setPersistentSceneGraph(persistentSceneGraph);
            quick->setPersistentGraphics(persistentGraphics);
            rebuildShapes(quick->contentItem());
        }
    }
    QPixmapCache::clear();
    if (MemoryKeeper *keeper = MemoryKeeper::instance())
        keeper->releaseNow();
    Q_EMIT released(hidden);
}

} // namespace brulion::ui
