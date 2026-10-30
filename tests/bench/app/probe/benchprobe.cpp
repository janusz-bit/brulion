// benchprobe — an LD_PRELOAD library that measures the real Brulion app from the outside, without
// any hook in the product code (tests/bench/app/README.md). It works with any build of the app
// (BASELINE v0.1.1 and the current tree alike): it only uses Qt's public/QPA API, the QML
// singletons AppController / Workspace by name and the objectNames "pageController" /
// "pageEditor" that PageView has had since the first version.
//
//   LD_PRELOAD=libbenchprobe.so BENCH_OUT=run.jsonl BENCH_SCENARIO=startup brulion --vault …
//
// Environment:
//   BENCH_OUT        JSON-lines output (default stderr)
//   BENCH_T0         CLOCK_MONOTONIC ns at launch (the runner sets it just before exec); times
//                    in the output are ms since then (default: the library's load)
//   BENCH_SCENARIO   startup | open-page | typing | select-copy | search | switcher | views |
//                    graph | journal-scroll | switch-pages | idle | idle-edit | workload (see
//                    buildScenario())
//   BENCH_PAGE       page path for open-page / typing / select-copy / idle-edit
//                    ("Bench/Konspekt 5000.md")
//   BENCH_ROW        row to edit (typing, select-copy; default 10)
//   BENCH_PAGES      file with one page path per line (switch-pages, workload)
//   BENCH_IDLE_MS    idle duration (default 30000)
//   BENCH_TIMEOUT_MS whole-run watchdog (default 240000): exit code 3
//   BENCH_KEEP       1: do not quit at the end
//
// What it records (raw, analysed by tests/bench/app/analyze.py):
//   * marks: preload (library constructor), qapp (QCoreApplication constructed), window
//     (first expose), first_frame, vault_open, ready (Workspace.ready), content (first
//     BlockDelegate on screen / the page's blocks), settled (no GUI stall for 1 s), …
//   * frames: for every frame the render thread's beforeSynchronizing (GUI state handed over)
//     and frameSwapped time;
//   * stalls: a 4 ms PreciseTimer on the GUI thread; every gap > 12 ms is a stall (the event
//     loop was blocked: long tasks, the user's input would wait that long);
//   * actions: synthetic input through QWindowSystemInterface (the same path as real
//     keyboard / wheel events, shortcuts included): t0 before the press, t1 after the press
//     was handled synchronously, t2 after the release; the frame that shows the change is the
//     first frame whose sync starts after t1;
//   * memory (VmRSS, VmHWM, RssAnon, malloc in-use) and CPU (getrusage) at marks;
//   * model signals of a watched QAbstractItemModel (search results).
#include <QAbstractItemModel>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QTimer>
#include <qpa/qwindowsysteminterface.h>

#include <malloc.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <functional>
#include <mutex>
#include <set>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

qint64 monoNs()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return qint64(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

qint64 g_t0 = 0;
qint64 g_preload = 0;

double msOf(qint64 ns)
{
    return double(ns - g_t0) / 1e6;
}

QByteArray envOr(const char *name, const QByteArray &fallback)
{
    const QByteArray v = qgetenv(name);
    return v.isEmpty() ? fallback : v;
}

QByteArray typeName(const QObject *o)
{
    QByteArray n = o->metaObject()->className();
    for (const char *cut : {"_QMLTYPE_", "_QML_"}) {
        const qsizetype i = n.indexOf(cut);
        if (i > 0)
            n.truncate(i);
    }
    return n;
}

QJsonObject procStatus()
{
    QJsonObject o;
    QFile f(u"/proc/self/status"_s);
    if (f.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = f.readAll().split('\n');
        for (const QByteArray &l : lines) {
            for (const char *key : {"VmRSS", "VmHWM", "RssAnon", "RssFile", "VmSize", "Threads"}) {
                if (l.startsWith(key) && l.size() > qsizetype(strlen(key)) && l.at(qsizetype(strlen(key))) == ':') {
                    QByteArray v = l.mid(qsizetype(strlen(key)) + 1).trimmed();
                    v = v.split(' ').first();
                    o.insert(QString::fromLatin1(key), v.toLongLong());
                }
            }
        }
    }
    struct mallinfo2 mi = mallinfo2();
    o.insert(u"mallocInUseKB"_s, qint64(mi.uordblks / 1024));
    o.insert(u"mallocMmapKB"_s, qint64(mi.hblkhd / 1024));
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    o.insert(u"cpuUserMs"_s, double(ru.ru_utime.tv_sec) * 1000 + ru.ru_utime.tv_usec / 1000.0);
    o.insert(u"cpuSysMs"_s, double(ru.ru_stime.tv_sec) * 1000 + ru.ru_stime.tv_usec / 1000.0);
    o.insert(u"majflt"_s, qint64(ru.ru_majflt));
    o.insert(u"minflt"_s, qint64(ru.ru_minflt));
    return o;
}

/// Per-thread context switches (wakeups) and CPU ticks: /proc/self/task/*/{comm,status,stat}.
QJsonObject threadStats()
{
    QJsonObject out;
    const QDir dir(u"/proc/self/task"_s);
    for (const QString &tid : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile comm(dir.filePath(tid + u"/comm"_s));
        QFile status(dir.filePath(tid + u"/status"_s));
        QFile stat(dir.filePath(tid + u"/stat"_s));
        if (!comm.open(QIODevice::ReadOnly) || !status.open(QIODevice::ReadOnly) || !stat.open(QIODevice::ReadOnly))
            continue;
        QJsonObject t;
        t.insert(u"name"_s, QString::fromUtf8(comm.readAll().trimmed()));
        qint64 vol = 0, invol = 0;
        for (const QByteArray &l : status.readAll().split('\n')) {
            if (l.startsWith("voluntary_ctxt_switches:"))
                vol = l.mid(24).trimmed().toLongLong();
            else if (l.startsWith("nonvoluntary_ctxt_switches:"))
                invol = l.mid(27).trimmed().toLongLong();
        }
        t.insert(u"vcsw"_s, vol);
        t.insert(u"ivcsw"_s, invol);
        const QByteArray s = stat.readAll();
        const qsizetype close = s.lastIndexOf(')');
        const QList<QByteArray> f = s.mid(close + 2).split(' ');
        if (f.size() > 13) { // fields after ")" start at field 3 (state); utime = 14, stime = 15
            t.insert(u"utime"_s, f.at(11).toLongLong());
            t.insert(u"stime"_s, f.at(12).toLongLong());
        }
        out.insert(tid, t);
    }
    return out;
}

struct Frame {
    qint64 sync = 0;
    qint64 swap = 0;
};

struct Action {
    QString kind;
    QString label;
    qint64 t0 = 0, t1 = 0, t2 = 0;
};

class Probe : public QObject {
    Q_OBJECT
public:
    explicit Probe(QObject *parent = nullptr) : QObject(parent)
    {
        const QByteArray outPath = qgetenv("BENCH_OUT");
        m_out = outPath.isEmpty() ? stderr : std::fopen(outPath.constData(), "w");
        if (!m_out)
            m_out = stderr;
        m_scenario = QString::fromUtf8(envOr("BENCH_SCENARIO", "startup"));
        m_page = QString::fromUtf8(envOr("BENCH_PAGE", "Bench/Konspekt 5000.md"));
        m_row = envOr("BENCH_ROW", "10").toInt();
        m_idleMs = envOr("BENCH_IDLE_MS", "30000").toInt();
        m_keep = qgetenv("BENCH_KEEP") == "1";
        const QByteArray pagesFile = qgetenv("BENCH_PAGES");
        if (!pagesFile.isEmpty()) {
            QFile f(QString::fromUtf8(pagesFile));
            if (f.open(QIODevice::ReadOnly)) {
                for (const QByteArray &l : f.readAll().split('\n'))
                    if (!l.trimmed().isEmpty())
                        m_pages.append(QString::fromUtf8(l.trimmed()));
            }
        }
        emitJson({{u"ev"_s, u"start"_s},
                  {u"scenario"_s, m_scenario},
                  {u"pid"_s, qint64(getpid())},
                  {u"t0ns"_s, QString::number(g_t0)},
                  {u"args"_s, QJsonArray::fromStringList(QCoreApplication::arguments())}});
        mark(u"preload"_s, g_preload);
        mark(u"qapp"_s);
        qApp->installEventFilter(this);

        m_lag.setTimerType(Qt::PreciseTimer);
        m_lag.setInterval(4);
        connect(&m_lag, &QTimer::timeout, this, &Probe::lagTick);
        m_poll.setTimerType(Qt::PreciseTimer);
        m_poll.setInterval(5);
        connect(&m_poll, &QTimer::timeout, this, &Probe::pollTick);
        // Timers run once exec() runs; the time before it (QML loading in main()) shows up as
        // the gap between qapp and the first lag tick ("exec" mark).
        QTimer::singleShot(0, this, [this] {
            mark(u"exec"_s, 0, {{u"qpa"_s, QGuiApplication::platformName()}});
            m_lastLag = monoNs();
            m_lag.start();
            m_poll.start();
            const int timeout = envOr("BENCH_TIMEOUT_MS", "240000").toInt();
            QTimer::singleShot(timeout, this, [this] {
                emitJson({{u"ev"_s, u"error"_s}, {u"what"_s, u"timeout in step "_s + currentStepName()}});
                finish(3);
            });
        });
        buildScenario();
    }

    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (m_countTimers) {
            if (ev->type() == QEvent::Timer || ev->type() == QEvent::MetaCall || ev->type() == QEvent::UpdateRequest
                || ev->type() == QEvent::DeferredDelete) {
                QString key = QString::fromLatin1(QMetaEnum::fromType<QEvent::Type>().valueToKey(ev->type())) + u' '
                              + QString::fromLatin1(obj->metaObject()->className());
                if (auto *t = qobject_cast<QTimer *>(obj)) {
                    key += u" interval="_s + QString::number(t->interval());
                    if (obj->parent())
                        key += u" parent="_s + QString::fromLatin1(obj->parent()->metaObject()->className())
                               + (obj->parent()->objectName().isEmpty() ? QString()
                                                                        : u"#"_s + obj->parent()->objectName());
                } else if (!obj->objectName().isEmpty()) {
                    key += u'#' + obj->objectName();
                }
                m_eventCounts[key] += 1;
            }
            return false;
        }
        if (!m_window && (ev->type() == QEvent::Expose || ev->type() == QEvent::Show)) {
            if (auto *w = qobject_cast<QQuickWindow *>(obj))
                attach(w);
        }
        return false;
    }

public Q_SLOTS:
    void onModelChanged() { m_modelEvents.append(monoNs()); }
    void onSaved() { m_savedEvents.append(monoNs()); }

private:
    // ---- output -----------------------------------------------------------------------------
    void emitJson(const QJsonObject &o)
    {
        const QByteArray line = QJsonDocument(o).toJson(QJsonDocument::Compact);
        std::fwrite(line.constData(), 1, size_t(line.size()), m_out);
        std::fputc('\n', m_out);
        std::fflush(m_out);
    }
    void mark(const QString &name, qint64 t = 0, QJsonObject extra = {})
    {
        if (!t)
            t = monoNs();
        extra.insert(u"ev"_s, u"mark"_s);
        extra.insert(u"name"_s, name);
        extra.insert(u"t"_s, msOf(t));
        emitJson(extra);
    }
    void markMem(const QString &name, bool gc = false)
    {
        if (gc && m_engine) {
            m_engine->collectGarbage();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            m_engine->collectGarbage();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        QJsonObject o = procStatus();
        o.insert(u"items"_s, countItems());
        mark(u"mem:"_s + name, 0, o);
    }

    // ---- window, frames, stalls ---------------------------------------------------------------
    void attach(QQuickWindow *w)
    {
        m_window = w;
        mark(u"window"_s, 0,
             {{u"graphicsApi"_s, int(QQuickWindow::graphicsApi())},
              {u"sceneGraphBackend"_s, QQuickWindow::sceneGraphBackend()}});
        connect(
                w, &QQuickWindow::beforeSynchronizing, this,
                [this] {
                    std::lock_guard<std::mutex> lock(m_frameMutex);
                    m_pendingSync = monoNs();
                },
                Qt::DirectConnection);
        connect(
                w, &QQuickWindow::frameSwapped, this,
                [this] {
                    const qint64 t = monoNs();
                    std::lock_guard<std::mutex> lock(m_frameMutex);
                    m_frames.push_back({m_pendingSync ? m_pendingSync : t, t});
                    m_pendingSync = 0;
                },
                Qt::DirectConnection);
        m_engine = qmlEngine(w);
        if (!m_engine && w->contentItem() && !w->contentItem()->childItems().isEmpty())
            m_engine = qmlEngine(w->contentItem()->childItems().first());
        qApp->removeEventFilter(this);
    }

    void lagTick()
    {
        const qint64 t = monoNs();
        const qint64 gap = t - m_lastLag;
        if (gap > 12000000LL) { // > 12 ms between 4 ms ticks: the GUI thread was busy
            m_stalls.push_back({m_lastLag, t});
            // "Settled" ignores short ones (a frame's sync, preemption on a loaded machine).
            if (gap > 24000000LL)
                m_lastStallEnd = t;
        }
        m_lastLag = t;
    }

    std::vector<Frame> framesCopy()
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        return m_frames;
    }
    qint64 frameCount()
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        return qint64(m_frames.size());
    }

    // ---- QML access -----------------------------------------------------------------------------
    QObject *singleton(const char *name)
    {
        if (!m_engine)
            return nullptr;
        auto it = m_singletons.find(QByteArray(name));
        if (it != m_singletons.end() && it.value())
            return it.value();
        QObject *o = m_engine->singletonInstance<QObject *>("Brulion", name);
        m_singletons.insert(QByteArray(name), o);
        return o;
    }
    QVariant wsProp(const char *name)
    {
        QObject *ws = singleton("Workspace");
        return ws ? ws->property(name) : QVariant();
    }
    void navigate(const QVariantMap &route)
    {
        if (QObject *app = singleton("AppController"))
            QMetaObject::invokeMethod(app, "navigate", Q_ARG(QVariantMap, route), Q_ARG(bool, false));
    }
    void uiAction(const QString &action)
    {
        if (QObject *app = singleton("AppController"))
            QMetaObject::invokeMethod(app, "requestUiAction", Q_ARG(QString, action));
    }
    QString currentView()
    {
        QObject *app = singleton("AppController");
        return app ? app->property("currentView").toString() : QString();
    }

    template<typename F>
    void walk(QQuickItem *item, F &&f)
    {
        if (!item)
            return;
        if (!f(item))
            return;
        const QList<QQuickItem *> kids = item->childItems();
        for (QQuickItem *k : kids)
            walk(k, f);
    }
    int countItems()
    {
        if (!m_window)
            return 0;
        int n = 0;
        walk(m_window->contentItem(), [&n](QQuickItem *) {
            ++n;
            return true;
        });
        return n;
    }
    /// Visible BlockDelegates under `root` (the whole window when null).
    int visibleBlocks(QQuickItem *root = nullptr)
    {
        if (!m_window)
            return 0;
        int n = 0;
        walk(root ? root : m_window->contentItem(), [&n](QQuickItem *it) {
            if (!it->isVisible())
                return false;
            if (typeName(it) == "BlockDelegate") {
                ++n;
                return false;
            }
            return true;
        });
        return n;
    }
    /// The visible PageView editor ("pageEditor") whose controller shows `path`.
    QQuickItem *pageEditor(const QString &path, QObject **controllerOut = nullptr)
    {
        QQuickItem *found = nullptr;
        walk(m_window ? m_window->contentItem() : nullptr, [&](QQuickItem *it) {
            if (found || !it->isVisible())
                return false;
            if (it->objectName() == u"pageEditor") {
                auto *ctl = it->property("controller").value<QObject *>();
                if (ctl && ctl->property("path").toString() == path) {
                    found = it;
                    if (controllerOut)
                        *controllerOut = ctl;
                }
                return false;
            }
            return true;
        });
        return found;
    }
    bool pageShown(const QString &path)
    {
        QObject *ctl = nullptr;
        QQuickItem *ed = pageEditor(path, &ctl);
        return ed && ctl && ctl->property("loaded").toBool() && visibleBlocks(ed) > 0;
    }
    QQuickItem *findType(const QByteArray &type, QQuickItem *root = nullptr)
    {
        QQuickItem *found = nullptr;
        walk(root ? root : (m_window ? m_window->contentItem() : nullptr), [&](QQuickItem *it) {
            if (found || !it->isVisible())
                return false;
            if (typeName(it) == type) {
                found = it;
                return false;
            }
            return true;
        });
        return found;
    }
    /// The largest visible list (Flickable with a model) under `root`.
    QQuickItem *largestList(QQuickItem *root, bool includeHidden = false)
    {
        QQuickItem *best = nullptr;
        walk(root, [&](QQuickItem *it) {
            if (!includeHidden && !it->isVisible())
                return false;
            const QByteArray cls = it->metaObject()->className();
            if (cls.startsWith("QQuickListView") || cls.startsWith("QQuickGridView")) {
                if (!best || it->height() * it->width() > best->height() * best->width())
                    best = it;
            }
            return true;
        });
        return best;
    }
    bool focusIsText()
    {
        QQuickItem *f = m_window ? m_window->activeFocusItem() : nullptr;
        if (!f)
            return false;
        for (const QMetaObject *mo = f->metaObject(); mo; mo = mo->superClass()) {
            const QByteArray cls = mo->className();
            if (cls.startsWith("QQuickTextEdit") || cls.startsWith("QQuickTextInput"))
                return true;
        }
        return false;
    }
    QString focusName()
    {
        QQuickItem *f = m_window ? m_window->activeFocusItem() : nullptr;
        return f ? QString::fromLatin1(f->metaObject()->className()) : u"<none>"_s;
    }

    // ---- synthetic input ------------------------------------------------------------------------
    void key(int code, Qt::KeyboardModifiers mods, const QString &text, const QString &label)
    {
        if (!m_window)
            return;
        Action a;
        a.kind = u"key"_s;
        a.label = label;
        a.t0 = monoNs();
        QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(m_window, QEvent::KeyPress,
                                                                                            code, mods, text);
        a.t1 = monoNs();
        QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(
                m_window, QEvent::KeyRelease, code, mods, text);
        a.t2 = monoNs();
        m_actions.push_back(a);
    }
    void typeChar(QChar c)
    {
        int code = c.toUpper().unicode();
        if (c == u' ')
            code = Qt::Key_Space;
        key(code, c.isUpper() ? Qt::ShiftModifier : Qt::NoModifier, QString(c), QString(c));
    }
    void wheel(int dy, const QString &label)
    {
        if (!m_window)
            return;
        Action a;
        a.kind = u"wheel"_s;
        a.label = label;
        const QPointF local(m_window->width() * 0.6, m_window->height() * 0.55);
        a.t0 = monoNs();
        QWindowSystemInterface::handleWheelEvent(m_window, local, m_window->mapToGlobal(local), QPoint(), QPoint(0, dy),
                                                 Qt::NoModifier);
        QWindowSystemInterface::flushWindowSystemEvents(QEventLoop::AllEvents);
        a.t1 = monoNs();
        a.t2 = a.t1;
        m_actions.push_back(a);
    }

    /// A mouse event at a window position (synchronous delivery; t1 = handled). `buttons`: the
    /// buttons held after it; the left button changes on a press and a release.
    void mouse(QEvent::Type type, QPointF local, Qt::MouseButtons buttons, const QString &label)
    {
        if (!m_window)
            return;
        Action a;
        a.kind = u"mouse"_s;
        a.label = label;
        a.t0 = monoNs();
        const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(
                m_window, local, m_window->mapToGlobal(local), buttons, button, type);
        a.t1 = monoNs();
        a.t2 = a.t1;
        m_actions.push_back(a);
    }

    // ---- scenario steps -------------------------------------------------------------------------
    struct Step {
        QString name;
        std::function<void()> enter;
        std::function<bool()> done; // polled every 5 ms (and once right after enter)
        qint64 timeoutMs = 60000;
    };
    QList<Step> m_steps;
    int m_step = -1;
    qint64 m_stepStart = 0;

    QString currentStepName() const
    {
        return m_step >= 0 && m_step < m_steps.size() ? m_steps.at(m_step).name : u"<none>"_s;
    }
    void add(const QString &name, std::function<void()> enter, std::function<bool()> done, qint64 timeoutMs = 60000)
    {
        m_steps.append({name, std::move(enter), std::move(done), timeoutMs});
    }
    void action(const QString &name, std::function<void()> fn)
    {
        add(name, std::move(fn), [] { return true; });
    }
    void wait(int ms)
    {
        add(u"wait"_s, nullptr, [this, ms] { return monoNs() - m_stepStart >= qint64(ms) * 1000000; });
    }
    void waitUntil(const QString &name, std::function<bool()> cond, qint64 timeoutMs = 60000, bool markIt = true)
    {
        add(
                name, nullptr,
                [this, name, cond = std::move(cond), markIt] {
                    if (!cond()) {
                        m_lastFalsePoll = monoNs();
                        return false;
                    }
                    if (markIt)
                        mark(name, 0, {{u"prevPoll"_s, msOf(std::max(m_lastFalsePoll, m_stepStart))}});
                    return true;
                },
                timeoutMs);
    }
    /// Done when the GUI thread had no stall for `quietMs`; marks the end of the last stall.
    void waitSettled(const QString &name, int quietMs = 1000, qint64 maxMs = 60000)
    {
        add(
                name, nullptr,
                [this, name, quietMs] {
                    const qint64 t = monoNs();
                    const qint64 since = std::max(m_lastStallEnd, m_stepStart);
                    if (t - since < qint64(quietMs) * 1000000)
                        return false;
                    mark(name, since, {{u"stepStart"_s, msOf(m_stepStart)}});
                    return true;
                },
                maxMs);
    }

    void pollTick()
    {
        if (m_finished || !m_window || !m_engine)
            return;
        // A stall that ends now must be on record before any step looks at m_lastStallEnd (the
        // poll timer may fire before the lag timer after a long block).
        if (m_lag.isActive())
            lagTick();
        // Startup marks (cheap property reads).
        if (!m_vaultOpenMarked && wsProp("isOpen").toBool()) {
            m_vaultOpenMarked = true;
            mark(u"vault_open"_s, 0, {{u"pageCount"_s, wsProp("pageCount").toInt()}});
        }
        if (!m_firstFrameMarked && frameCount() > 0) {
            m_firstFrameMarked = true;
            mark(u"first_frame"_s, framesCopy().front().swap);
        }
        if (QObject *app = singleton("AppController")) {
            const QString status = app->property("status").toString();
            if (status != m_status) {
                m_status = status;
                mark(u"status:"_s + status);
            }
        }
        if (!m_readyMarked && wsProp("ready").toBool()) {
            m_readyMarked = true;
            mark(u"ready"_s);
            markMem(u"ready"_s);
        }
        runSteps();
    }
    void runSteps()
    {
        while (!m_finished) {
            if (m_step < 0 || m_step >= m_steps.size()) {
                if (m_step < 0) {
                    m_step = 0;
                    m_stepStart = monoNs();
                    m_lastFalsePoll = 0;
                    if (!m_steps.isEmpty() && m_steps.at(0).enter)
                        m_steps.at(0).enter();
                    continue;
                }
                finish(0);
                return;
            }
            const Step &s = m_steps.at(m_step);
            if (!s.done()) {
                if (monoNs() - m_stepStart > s.timeoutMs * 1000000) {
                    emitJson({{u"ev"_s, u"error"_s},
                              {u"what"_s, u"step timeout: "_s + s.name},
                              {u"focus"_s, focusName()},
                              {u"view"_s, currentView()}});
                    finish(4);
                }
                return;
            }
            ++m_step;
            m_stepStart = monoNs();
            m_lastFalsePoll = 0;
            if (m_step < m_steps.size() && m_steps.at(m_step).enter)
                m_steps.at(m_step).enter();
        }
    }

    void finish(int code)
    {
        if (m_finished)
            return;
        m_finished = true;
        m_lag.stop();
        m_poll.stop();
        markMem(u"end"_s);
        QJsonArray sync, swap;
        for (const Frame &f : framesCopy()) {
            sync.append(msOf(f.sync));
            swap.append(msOf(f.swap));
        }
        emitJson({{u"ev"_s, u"frames"_s}, {u"sync"_s, sync}, {u"swap"_s, swap}});
        QJsonArray st;
        for (const auto &s : m_stalls)
            st.append(QJsonArray{msOf(s.first), double(s.second - s.first) / 1e6});
        emitJson({{u"ev"_s, u"stalls"_s}, {u"list"_s, st}});
        QJsonArray acts;
        for (const Action &a : m_actions)
            acts.append(QJsonObject{{u"kind"_s, a.kind},
                                    {u"label"_s, a.label},
                                    {u"t0"_s, msOf(a.t0)},
                                    {u"t1"_s, msOf(a.t1)},
                                    {u"t2"_s, msOf(a.t2)}});
        emitJson({{u"ev"_s, u"actions"_s}, {u"list"_s, acts}});
        QJsonArray me, sv;
        for (qint64 t : m_modelEvents)
            me.append(msOf(t));
        for (qint64 t : m_savedEvents)
            sv.append(msOf(t));
        emitJson({{u"ev"_s, u"signals"_s}, {u"model"_s, me}, {u"saved"_s, sv}});
        emitJson({{u"ev"_s, u"end"_s}, {u"code"_s, code}, {u"step"_s, currentStepName()}});
        if (m_out != stderr)
            std::fclose(m_out);
        m_out = stderr;
        if (!m_keep || code != 0)
            QCoreApplication::exit(code);
    }

    // ---- scenarios ------------------------------------------------------------------------------
    void startupSteps()
    {
        waitUntil(
                u"content"_s,
                [this] {
                    const QString view = currentView();
                    if (view == u"page")
                        return pageShown(m_startPage);
                    return visibleBlocks() > 0 || (view != u"journals" && view != u"" && frameCount() > 2);
                },
                120000);
        waitUntil(u"ready_wait"_s, [this] { return m_readyMarked; }, 600000, false);
        waitSettled(u"settled"_s, 1000, 120000);
        action(u"mem"_s, [this] { markMem(u"settled"_s); });
    }

    void openPageSteps(const QString &page, const QString &tag)
    {
        action(u"nav:"_s + tag, [this, page, tag] {
            mark(u"nav_start:"_s + tag);
            navigate({{u"view"_s, u"page"_s}, {u"path"_s, page}});
            mark(u"nav_returned:"_s + tag);
        });
        waitUntil(u"page_content:"_s + tag, [this, page] { return pageShown(page); }, 120000);
        waitSettled(u"page_settled:"_s + tag, 1000, 120000);
    }

    void startEditing(const QString &page, int row)
    {
        action(u"start_editing"_s, [this, page, row] {
            QQuickItem *ed = pageEditor(page);
            if (!ed) {
                emitJson({{u"ev"_s, u"error"_s}, {u"what"_s, u"no editor for "_s + page}});
                return;
            }
            QMetaObject::invokeMethod(ed, "startEditing", Q_ARG(QVariant, row), Q_ARG(QVariant, -1));
        });
        waitUntil(u"editing"_s, [this] { return focusIsText(); }, 10000);
        wait(400);
        waitSettled(u"edit_settled"_s, 600, 20000);
    }

    void typeString(const QString &text, int spacingMs)
    {
        for (const QChar c : text) {
            action(u"type"_s, [this, c] { typeChar(c); });
            wait(spacingMs);
        }
    }
    void keyStep(int code, Qt::KeyboardModifiers mods, const QString &label, int spacingMs)
    {
        action(label, [this, code, mods, label] { key(code, mods, QString(), label); });
        wait(spacingMs);
    }

    void typingSteps()
    {
        startEditing(m_page, m_row);
        action(u"watch_saved"_s, [this] {
            QObject *ctl = nullptr;
            pageEditor(m_page, &ctl);
            if (ctl)
                connect(ctl, SIGNAL(saved()), this, SLOT(onSaved()));
        });
        // Three bursts of typing (120 ms between keys: a fast typist), each followed by a pause
        // in which the 400 ms autosave runs.
        for (int burst = 0; burst < 3; ++burst) {
            action(u"burst"_s, [this, burst] { mark(u"burst_start:"_s + QString::number(burst)); });
            typeString(u" ala ma kota zażółć"_s, 120);
            action(u"burst_end"_s, [this, burst] { mark(u"burst_end:"_s + QString::number(burst)); });
            wait(1800);
        }
        // Structure edits: Enter (a new block), Tab / Shift+Tab (indent, outdent), undo, redo.
        for (int i = 0; i < 5; ++i) {
            keyStep(Qt::Key_Return, Qt::NoModifier, u"enter"_s, 250);
            action(u"type"_s, [this] { typeChar(u'x'); });
            wait(250);
        }
        wait(1500);
        for (int i = 0; i < 3; ++i)
            keyStep(Qt::Key_Tab, Qt::NoModifier, u"tab"_s, 250);
        for (int i = 0; i < 3; ++i)
            keyStep(Qt::Key_Backtab, Qt::ShiftModifier, u"shift-tab"_s, 250);
        wait(1500);
        for (int i = 0; i < 6; ++i)
            keyStep(Qt::Key_Z, Qt::ControlModifier, u"undo"_s, 250);
        for (int i = 0; i < 3; ++i)
            keyStep(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier, u"redo"_s, 250);
        wait(1500);
        waitSettled(u"typing_settled"_s, 1000, 30000);
        action(u"mem"_s, [this] { markMem(u"typing_end"_s); });
    }

    /// Select every block of the page and copy it (docs/UI.md "Selecting and copying"), three
    /// times: Ctrl+A (the block's text), Ctrl+A (every block: PageController::selectAll()),
    /// Ctrl+C (copySelection(): the Markdown, its HTML, the clipboard), Escape, editing again.
    void selectCopySteps()
    {
        for (int round = 0; round < 3; ++round) {
            startEditing(m_page, m_row);
            keyStep(Qt::Key_A, Qt::ControlModifier, u"select-text"_s, 400);
            keyStep(Qt::Key_A, Qt::ControlModifier, u"select-blocks"_s, 800);
            action(u"selected"_s, [this] {
                QObject *ctl = nullptr;
                pageEditor(m_page, &ctl);
                emitJson({{u"ev"_s, u"note"_s},
                          {u"what"_s, u"selection"_s},
                          {u"count"_s, ctl ? ctl->property("selectionCount").toInt() : -1}});
            });
            keyStep(Qt::Key_C, Qt::ControlModifier, u"copy"_s, 1200);
            action(u"copied"_s, [this] {
                const QClipboard *clipboard = QGuiApplication::clipboard();
                emitJson({{u"ev"_s, u"note"_s},
                          {u"what"_s, u"clipboard"_s},
                          {u"chars"_s, clipboard ? int(clipboard->text().size()) : -1}});
            });
            keyStep(Qt::Key_Escape, Qt::NoModifier, u"escape"_s, 600);
        }
        waitSettled(u"select_settled"_s, 1000, 30000);
        action(u"mem"_s, [this] { markMem(u"select_end"_s); });
    }

    void watchListModel(QQuickItem *root)
    {
        QQuickItem *list = largestList(root, true);
        if (!list) {
            emitJson({{u"ev"_s, u"note"_s}, {u"what"_s, u"no list to watch"_s}});
            return;
        }
        emitJson({{u"ev"_s, u"note"_s},
                  {u"what"_s, u"watching list"_s},
                  {u"list"_s, QString::fromLatin1(list->metaObject()->className())},
                  {u"model"_s,
                   QString::fromLatin1(list->property("model").value<QObject *>()
                                               ? list->property("model").value<QObject *>()->metaObject()->className()
                                               : "<none>")}});
        auto *model = qobject_cast<QAbstractItemModel *>(list->property("model").value<QObject *>());
        if (!model) {
            emitJson({{u"ev"_s, u"note"_s}, {u"what"_s, u"list model is not a QAbstractItemModel"_s}});
            connect(list, SIGNAL(countChanged()), this, SLOT(onModelChanged()));
            return;
        }
        connect(model, &QAbstractItemModel::modelReset, this, &Probe::onModelChanged);
        connect(model, &QAbstractItemModel::layoutChanged, this, &Probe::onModelChanged);
        connect(model, &QAbstractItemModel::rowsInserted, this, &Probe::onModelChanged);
        connect(model, &QAbstractItemModel::rowsRemoved, this, &Probe::onModelChanged);
    }

    void clearField()
    {
        keyStep(Qt::Key_A, Qt::ControlModifier, u"select-all"_s, 100);
        keyStep(Qt::Key_Backspace, Qt::NoModifier, u"clear"_s, 1200);
    }

    void searchSteps()
    {
        action(u"nav:search"_s, [this] {
            mark(u"nav_start:search"_s);
            navigate({{u"view"_s, u"search"_s}});
        });
        waitUntil(u"search_view"_s, [this] { return findType("SearchView") != nullptr; }, 20000);
        waitSettled(u"search_settled"_s, 800, 20000);
        action(u"focus_field"_s, [this] {
            if (!focusIsText()) {
                if (QQuickItem *field = findType("BSearchField", findType("SearchView")))
                    QMetaObject::invokeMethod(field, "forceActiveFocus");
            }
            watchListModel(findType("SearchView"));
        });
        waitUntil(u"field_focus"_s, [this] { return focusIsText(); }, 5000);
        // Slow typing: every key runs a search (180 ms debounce).
        action(u"phase"_s, [this] { mark(u"phase:slow"_s); });
        typeString(u"spotkanie"_s, 700);
        clearField();
        // Fast typing: the debounce collapses the keys, one search at the end.
        action(u"phase"_s, [this] { mark(u"phase:fast"_s); });
        typeString(u"zażółć gęślą"_s, 60);
        wait(1500);
        clearField();
        // A short query with very many hits.
        action(u"phase"_s, [this] { mark(u"phase:many"_s); });
        typeString(u"do"_s, 700);
        wait(800);
        waitSettled(u"search_done"_s, 800, 30000);
    }

    void switcherSteps()
    {
        for (int round = 0; round < 2; ++round) {
            action(u"open_switcher"_s, [this, round] {
                mark(u"switcher_open:"_s + QString::number(round));
                uiAction(u"switcher"_s);
            });
            waitUntil(u"switcher_focus:"_s + QString::number(round), [this] { return focusIsText(); }, 5000);
            waitSettled(u"switcher_settled"_s, 600, 10000);
            action(u"phase"_s, [this, round] { mark(u"phase:"_s + (round == 0 ? u"slow"_s : u"fast"_s)); });
            typeString(u"przepis żurek"_s, round == 0 ? 300 : 60);
            wait(800);
            for (int i = 0; i < 6; ++i)
                keyStep(Qt::Key_Backspace, Qt::NoModifier, u"backspace"_s, 200);
            keyStep(Qt::Key_Escape, Qt::NoModifier, u"escape"_s, 800);
        }
        waitSettled(u"switcher_done"_s, 800, 20000);
    }

    /// Activates the window first: an X server without a window manager (Xvfb) never focuses it,
    /// and an inactive app pauses what runs only while it is in front (the graph's layout). The
    /// state before and after is in the "app_state" mark (Qt::ApplicationState: 4 active, 2
    /// inactive).
    void activateSteps()
    {
        action(u"activate"_s, [this] {
            m_stateBefore = int(QGuiApplication::applicationState());
            if (m_window)
                m_window->requestActivate();
        });
        waitUntil(u"app_active"_s, [this] {
            return QGuiApplication::applicationState() == Qt::ApplicationActive || monoNs() - m_stepStart > 3000000000LL;
        }, 5000, false);
        action(u"app_state"_s, [this] {
            mark(u"app_state"_s, 0, {{u"before"_s, m_stateBefore}, {u"after"_s, int(QGuiApplication::applicationState())}});
        });
    }

    void viewSteps()
    {
        activateSteps();
        const QList<QPair<QString, QVariantMap>> views = {
                {u"tasks"_s, {{u"view"_s, u"tasks"_s}}},
                {u"calendar"_s, {{u"view"_s, u"calendar"_s}}},
                {u"agenda"_s, {{u"view"_s, u"calendar"_s}, {u"mode"_s, u"agenda"_s}}},
                {u"graph"_s, {{u"view"_s, u"graph"_s}}},
                {u"allPages"_s, {{u"view"_s, u"allPages"_s}}},
                {u"tags"_s, {{u"view"_s, u"tags"_s}}},
        };
        for (int round = 0; round < 2; ++round) {
            for (const auto &[name, route] : views) {
                const QString tag = name + u':' + QString::number(round);
                action(u"nav:"_s + tag, [this, tag, route] {
                    mark(u"nav_start:"_s + tag);
                    navigate(route);
                    mark(u"nav_returned:"_s + tag);
                });
                waitSettled(u"view_settled:"_s + tag, 1000, 60000);
                action(u"mem"_s, [this, tag] { markMem(u"view:"_s + tag); });
                action(u"nav:journals"_s, [this, tag] {
                    mark(u"nav_start:journals-after-"_s + tag);
                    navigate({{u"view"_s, u"journals"_s}});
                });
                waitSettled(u"view_settled:journals-after-"_s + tag, 1000, 60000);
            }
        }
    }

    /// The visible GraphItem (GraphView's "graphItem") and its GraphModel.
    QQuickItem *graphItem()
    {
        QQuickItem *found = nullptr;
        walk(m_window ? m_window->contentItem() : nullptr, [&](QQuickItem *it) {
            if (found || !it->isVisible())
                return false;
            if (it->objectName() == u"graphItem") {
                found = it;
                return false;
            }
            return true;
        });
        return found;
    }
    QObject *graphModel()
    {
        QQuickItem *item = graphItem();
        return item ? item->property("model").value<QObject *>() : nullptr;
    }
    /// A window point over the graph's background (no node under it), else the item's centre.
    QPointF graphBackgroundPoint()
    {
        QQuickItem *item = graphItem();
        if (!item)
            return QPointF(m_window->width() / 2.0, m_window->height() / 2.0);
        for (const double fy : {0.62, 0.5, 0.38, 0.72, 0.28}) {
            for (const double fx : {0.3, 0.5, 0.7, 0.2, 0.8}) {
                const QPointF p(item->width() * fx, item->height() * fy);
                QString id;
                QMetaObject::invokeMethod(item, "nodeAt", Q_RETURN_ARG(QString, id), Q_ARG(qreal, p.x()), Q_ARG(qreal, p.y()));
                if (id.isEmpty())
                    return item->mapToScene(p);
            }
        }
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    }
    /// The graph's camera (centerX, centerY, zoom) for a mark: whether a drag panned it.
    QJsonObject graphCamera()
    {
        QQuickItem *item = graphItem();
        if (!item)
            return {};
        return {{u"cx"_s, item->property("centerX").toDouble()},
                {u"cy"_s, item->property("centerY").toDouble()},
                {u"zoom"_s, item->property("zoom").toDouble()}};
    }
    /// A mouse drag of `ms` on the graph's background, a move every 16 ms.
    void graphPan(const QString &name, int ms)
    {
        action(name + u":start"_s, [this, name] {
            m_panPos = graphBackgroundPoint();
            QJsonObject o = graphCamera();
            o.insert(u"x"_s, m_panPos.x());
            o.insert(u"y"_s, m_panPos.y());
            mark(name + u":start"_s, 0, o);
            mouse(QEvent::MouseButtonPress, m_panPos, Qt::LeftButton, name + u":press"_s);
        });
        for (int i = 0; i < ms / 16; ++i) {
            wait(16);
            action(name, [this, name, i] {
                m_panPos += QPointF(i % 2 ? 3 : 4, (i / 12) % 2 ? 2 : -2);
                mouse(QEvent::MouseMove, m_panPos, Qt::LeftButton, name);
            });
        }
        action(name + u":end"_s, [this, name] {
            mouse(QEvent::MouseButtonRelease, m_panPos, Qt::NoButton, name + u":release"_s);
            mark(name + u":end"_s, 0, graphCamera());
        });
    }

    void graphSteps()
    {
        // Open the graph, pan and zoom while the layout still runs, then once it has settled.
        activateSteps();
        action(u"nav:graph"_s, [this] {
            mark(u"nav_start:graph"_s);
            navigate({{u"view"_s, u"graph"_s}});
            mark(u"nav_returned:graph"_s);
        });
        waitUntil(u"graph_item"_s, [this] { return graphModel() != nullptr; }, 30000);
        wait(300);
        graphPan(u"graph_run_pan"_s, 1000);
        for (int i = 0; i < 3; ++i) {
            action(u"zoom"_s, [this] { wheel(120, u"graph_zoom"_s); });
            wait(120);
        }
        waitUntil(u"graph_layout_done"_s, [this] {
            QObject *m = graphModel();
            return m && !m->property("running").toBool();
        }, 180000);
        waitSettled(u"graph_settled"_s, 1000, 60000);
        graphPan(u"graph_settled_pan"_s, 1000);
        waitSettled(u"graph_done"_s, 1000, 30000);
        action(u"mem"_s, [this] { markMem(u"graph"_s); });
    }

    void journalScrollSteps()
    {
        action(u"scroll_start"_s, [this] { mark(u"scroll_start"_s); });
        for (int i = 0; i < 80; ++i) {
            action(u"wheel"_s, [this] { wheel(-120, u"down"_s); });
            wait(40);
        }
        action(u"scroll_end"_s, [this] { mark(u"scroll_end"_s); });
        waitSettled(u"scroll_settled"_s, 1000, 60000);
        action(u"mem"_s, [this] { markMem(u"scrolled"_s); });
    }

    void switchPagesSteps()
    {
        if (m_pages.isEmpty())
            m_pages = {m_page};
        action(u"mem"_s, [this] { markMem(u"switch:0"_s, true); });
        for (int i = 0; i < 200; ++i) {
            const QString page = m_pages.at(i % m_pages.size());
            const QString tag = QString::number(i);
            action(u"nav"_s, [this, page, tag] {
                mark(u"nav_start:"_s + tag);
                navigate({{u"view"_s, u"page"_s}, {u"path"_s, page}});
            });
            waitUntil(u"page_content:"_s + tag, [this, page] { return pageShown(page); }, 30000);
            wait(60);
            if ((i + 1) % 20 == 0)
                action(u"mem"_s, [this, i] { markMem(u"switch:"_s + QString::number(i + 1), true); });
        }
        action(u"home"_s, [this] { navigate({{u"view"_s, u"journals"_s}}); });
        waitSettled(u"switch_settled"_s, 1500, 60000);
        action(u"mem"_s, [this] { markMem(u"switch:end"_s, true); });
    }

    void idleSteps(const QString &tag)
    {
        add(
                u"idle:"_s + tag,
                [this, tag] {
                    m_lag.stop(); // nothing of ours may wake the GUI thread while idle
                    m_poll.stop();
                    m_idleFrames = frameCount();
                    QJsonObject o = procStatus();
                    o.insert(u"threads"_s, threadStats());
                    mark(u"idle_start:"_s + tag, 0, o);
                    m_eventCounts.clear();
                    m_countTimers = true;
                    qApp->installEventFilter(this);
                    QTimer::singleShot(m_idleMs, this, [this, tag] {
                        QJsonObject e = procStatus();
                        e.insert(u"threads"_s, threadStats());
                        e.insert(u"frames"_s, frameCount() - m_idleFrames);
                        m_countTimers = false;
                        qApp->removeEventFilter(this);
                        QJsonObject counts;
                        for (auto it = m_eventCounts.cbegin(); it != m_eventCounts.cend(); ++it)
                            counts.insert(it.key(), it.value());
                        e.insert(u"events"_s, counts);
                        mark(u"idle_end:"_s + tag, 0, e);
                        m_lastLag = monoNs();
                        m_lag.start();
                        m_poll.start();
                        m_idleDone = true;
                    });
                },
                [this] {
                    if (!m_idleDone)
                        return false;
                    m_idleDone = false;
                    return true;
                },
                qint64(m_idleMs) + 30000);
    }

    void workloadSteps()
    {
        if (m_pages.isEmpty())
            m_pages = {m_page};
        action(u"mem"_s, [this] { markMem(u"workload:0"_s, true); });
        for (int cycle = 0; cycle < 12; ++cycle) {
            const QString page = m_pages.at((cycle * 7) % m_pages.size());
            const QString tag = QString::number(cycle);
            openPageSteps(page, u"w"_s + tag);
            startEditing(page, 1);
            typeString(u" notatka z pracy"_s, 90);
            wait(900);
            action(u"search"_s, [this] { navigate({{u"view"_s, u"search"_s}, {u"query"_s, u"kawa"_s}}); });
            waitSettled(u"w_search:"_s + tag, 600, 30000);
            action(u"tasks"_s, [this] { navigate({{u"view"_s, u"tasks"_s}}); });
            waitSettled(u"w_tasks:"_s + tag, 600, 30000);
            action(u"calendar"_s, [this] { navigate({{u"view"_s, u"calendar"_s}}); });
            waitSettled(u"w_calendar:"_s + tag, 600, 30000);
            action(u"journals"_s, [this] { navigate({{u"view"_s, u"journals"_s}}); });
            waitSettled(u"w_journals:"_s + tag, 600, 30000);
            if ((cycle + 1) % 4 == 0)
                action(u"mem"_s, [this, cycle] { markMem(u"workload:"_s + QString::number(cycle + 1), true); });
        }
    }

    void buildScenario()
    {
        // The start route (--route) decides what "content" means for startup.
        const QStringList args = QCoreApplication::arguments();
        const qsizetype r = args.indexOf(u"--route"_s);
        if (r >= 0 && r + 1 < args.size()) {
            const QJsonObject route = QJsonDocument::fromJson(args.at(r + 1).toUtf8()).object();
            if (route.value(u"view"_s).toString() == u"page")
                m_startPage = route.value(u"path"_s).toString();
        }
        startupSteps();
        const QString s = m_scenario;
        if (s == u"startup") {
            // nothing more
        } else if (s == u"open-page") {
            openPageSteps(m_page, u"1"_s);
            action(u"mem"_s, [this] { markMem(u"page:1"_s); });
            action(u"home"_s, [this] { navigate({{u"view"_s, u"journals"_s}}); });
            waitSettled(u"home_settled"_s, 1000, 60000);
            openPageSteps(m_page, u"2"_s);
            action(u"mem"_s, [this] { markMem(u"page:2"_s); });
        } else if (s == u"typing") {
            openPageSteps(m_page, u"1"_s);
            typingSteps();
        } else if (s == u"select-copy") {
            openPageSteps(m_page, u"1"_s);
            selectCopySteps();
        } else if (s == u"search") {
            searchSteps();
        } else if (s == u"switcher") {
            switcherSteps();
        } else if (s == u"views") {
            viewSteps();
        } else if (s == u"graph") {
            graphSteps();
        } else if (s == u"journal-scroll") {
            journalScrollSteps();
        } else if (s == u"switch-pages") {
            switchPagesSteps();
        } else if (s == u"idle") {
            wait(2000);
            idleSteps(u"journals"_s);
        } else if (s == u"idle-edit") {
            openPageSteps(m_page, u"1"_s);
            startEditing(m_page, 1);
            idleSteps(u"editing"_s);
        } else if (s == u"workload") {
            workloadSteps();
        } else {
            emitJson({{u"ev"_s, u"error"_s}, {u"what"_s, u"unknown scenario "_s + s}});
        }
    }

    FILE *m_out = nullptr;
    QString m_scenario, m_page, m_startPage, m_status;
    QPointF m_panPos;
    int m_stateBefore = -1;
    int m_row = 10;
    int m_idleMs = 30000;
    bool m_keep = false;
    QStringList m_pages;

    QPointer<QQuickWindow> m_window;
    QQmlEngine *m_engine = nullptr;
    QHash<QByteArray, QPointer<QObject>> m_singletons;

    std::mutex m_frameMutex;
    std::vector<Frame> m_frames;
    qint64 m_pendingSync = 0;

    QTimer m_lag, m_poll;
    qint64 m_lastLag = 0;
    qint64 m_lastStallEnd = 0;
    qint64 m_lastFalsePoll = 0;
    std::vector<std::pair<qint64, qint64>> m_stalls;
    std::vector<Action> m_actions;
    QList<qint64> m_modelEvents, m_savedEvents;
    qint64 m_idleFrames = 0;
    bool m_idleDone = false;
    bool m_countTimers = false;
    QHash<QString, int> m_eventCounts;

    bool m_vaultOpenMarked = false, m_firstFrameMarked = false, m_readyMarked = false;
    bool m_finished = false;
};

void startProbe()
{
    if (qgetenv("BENCH_DISABLE") == "1")
        return;
    // Only in the app itself, not in a Qt tool that runs it (qmlprofiler inherits LD_PRELOAD).
    const QString exe = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    if (!exe.startsWith(QString::fromUtf8(envOr("BENCH_TARGET", "brulion"))) && !exe.startsWith(u".brulion"_s))
        return;
    new Probe(QCoreApplication::instance());
}

} // namespace

__attribute__((constructor)) static void benchProbeLoaded()
{
    g_preload = monoNs();
    const QByteArray t0 = qgetenv("BENCH_T0");
    g_t0 = t0.isEmpty() ? g_preload : t0.toLongLong();
}

Q_COREAPP_STARTUP_FUNCTION(startProbe)

#include "benchprobe.moc"
