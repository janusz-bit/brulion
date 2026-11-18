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
//   BENCH_SCENARIO   startup | open-page | typing | source-typing | select-copy | search |
//                    switcher | views |
//                    graph | journal-scroll | page-scroll | page-resize | switch-pages | idle |
//                    idle-edit | workload (see buildScenario())
//   BENCH_PAGE       page path for open-page / typing / source-typing / select-copy / idle-edit /
//                    page-scroll /
//                    page-resize ("Bench/Konspekt 5000.md")
//   BENCH_RESIZE_WIDTH page-resize: the narrower window width (default 920)
//   BENCH_SCROLL_WAIT page-scroll: ms between the page's first block on screen and the first
//                    wheel step (default: -1 = once the page has settled, as a reader would)
//   BENCH_ROW        row to edit (typing, select-copy; default 10)
//   BENCH_PAGES      file with one page path per line (switch-pages, workload)
//   BENCH_IDLE_MS    idle duration (default 30000)
//   BENCH_TIMEOUT_MS whole-run watchdog (default 240000): exit code 3
//   BENCH_KEEP       1: do not quit at the end
//   BENCH_WAIT_KEPT  1: typing / source-typing wait until every kept view of the main pane
//                    (ShellRouteView's KeptLoaders: Tasks, Calendar, Journal, All pages, Search,
//                    built ahead 1.2 s after ready, one per 0.4 s) exists before the first burst
//   BENCH_SPANS      signal spans in the typing windows: 1 (default) the watched signals and
//                    every other signal of a brulion:: class taking >= BENCH_SPAN_MIN_US (200 us)
//                    inclusive; "watched" only the watched ones; 0 none
//
// What it records (raw, analysed by tests/bench/app/analyze.py):
//   * marks: preload (library constructor), qapp (QCoreApplication constructed), window
//     (first expose), first_frame, vault_open, ready (Workspace.ready), content (first
//     BlockDelegate on screen / the page's blocks), settled (no GUI stall for 1 s), …
//   * frames: for every frame the render thread's beforeSynchronizing (GUI state handed over),
//     afterSynchronizing (the sync done: items' updatePaintNode) and frameSwapped time;
//   * stalls: a 4 ms PreciseTimer on the GUI thread; every gap > 12 ms is a stall (the event
//     loop was blocked: long tasks, the user's input would wait that long);
//   * actions: synthetic input through QWindowSystemInterface (the same path as real
//     keyboard / wheel events, shortcuts included): t0 before the press, t1 after the press
//     was handled synchronously, t2 after the release; the frame that shows the change is the
//     first frame whose sync starts after t1;
//   * memory (VmRSS, VmHWM, RssAnon, malloc in-use) and CPU (getrusage) at marks;
//   * model signals of a watched QAbstractItemModel (search results);
//   * typing windows (each burst of typing up to 2.4 s after it: the autosave and its fan-out):
//     signal spans through Qt's signal spy callbacks (qt_register_signal_spy_callbacks, registered
//     only then, GUI thread only) — inclusive and self wall time, thread CPU time
//     (CLOCK_THREAD_CPUTIME_ID) and user-space instructions (perf_event_open, exclude_kernel:
//     allowed with perf_event_paranoid 2) of the watched signals (Vault::changed, Workspace's
//     revisionChanged / fileChanged / filesChanged / fileListChanged / indexChanged,
//     VaultIndex::changed, PageController saved / dirtyChanged, every model's reset / layout /
//     data / rows signals with the rows they name, QTimer::timeout labelled by the timer's parent
//     or, parentless, by its interval and the first span in it — "timer:save" is the parentless
//     single-shot timer with the controller's saveDelay in which a PageController emits saved,
//     dirtyChanged or saveStarted (a background save sent): its autosave —, QML Timers) and of other brulion:: signals;
//     the GUI thread's CPU and instructions at every lag tick (fanout_deferred_cpu_ms: work under 12 ms);
//   * fsync / fdatasync / rename / renameat / renameat2 / linkat on the GUI thread (interposed:
//     the probe defines them and calls the next definition, dlsym(RTLD_NEXT)), counts elsewhere;
//   * per stall the GUI thread's CPU time and instructions.
//
// Signal spans, what Qt's callbacks see (verified with Qt 6.12, see tests/bench/app/README.md):
// signal_begin / signal_end wrap every direct connection of an emission — string-based
// (SIGNAL/SLOT), pointer-to-member and lambda alike — so a span's inclusive time holds its
// direct slots whatever their kind; slot_begin / slot_end (not used) fire only for string-based
// connections; queued connections run later, outside the span. QML handlers and bindings
// (`onX:`, Connections, property bindings re-evaluated by a NOTIFY signal) run before
// signal_begin, through QAbstractDeclarativeData::signalEmitted: the probe wraps that hook too
// while spans are on and records their time as a span of the same name with the flag `qml`.
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
#include <QtCore/private/qmetaobject_p.h>
#include <QtCore/private/qobject_p.h>
#include <qpa/qwindowsysteminterface.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <malloc.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
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

// ---- GUI-thread counters: wall, thread CPU, user-space instructions ------------------------------

pid_t currentTid()
{
    thread_local const pid_t tid = pid_t(syscall(SYS_gettid));
    return tid;
}
std::atomic<pid_t> g_guiTid{0};
bool onGuiThread()
{
    const pid_t gui = g_guiTid.load(std::memory_order_relaxed);
    return gui != 0 && currentTid() == gui;
}

qint64 threadCpuNs()
{
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return qint64(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

/// User-space instructions retired by the thread that opened it (the GUI thread). A hybrid CPU
/// (Intel P- and E-cores: cpu_core, cpu_atom) gets one counter per core PMU, summed: a generic
/// hardware event counts on one of them only.
struct InsnCounter {
    std::vector<int> fds;
    QStringList pmus;
    QString error;
    static int openOne(quint64 config)
    {
        perf_event_attr attr{};
        attr.size = sizeof(attr);
        attr.type = PERF_TYPE_HARDWARE;
        attr.config = config;
        attr.exclude_kernel = 1;
        attr.exclude_hv = 1;
        attr.exclude_idle = 1;
        return int(syscall(SYS_perf_event_open, &attr, 0 /* this thread */, -1, -1, PERF_FLAG_FD_CLOEXEC));
    }
    void open()
    {
        for (const char *pmu : {"cpu_core", "cpu_atom"}) {
            QFile f(u"/sys/bus/event_source/devices/%1/type"_s.arg(QLatin1StringView(pmu)));
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const quint64 type = f.readAll().trimmed().toULongLong();
            // PERF_PMU_TYPE_SHIFT (32): a generic hardware event on one PMU of a hybrid CPU.
            const int fd = openOne(PERF_COUNT_HW_INSTRUCTIONS | (type << 32));
            if (fd >= 0) {
                fds.push_back(fd);
                pmus.append(QString::fromLatin1(pmu));
            } else {
                error = QString::fromLocal8Bit(strerror(errno));
            }
        }
        if (fds.empty()) {
            const int fd = openOne(PERF_COUNT_HW_INSTRUCTIONS);
            if (fd >= 0) {
                fds.push_back(fd);
                pmus.append(u"generic"_s);
            } else {
                error = QString::fromLocal8Bit(strerror(errno));
            }
        }
    }
    qint64 read() const
    {
        qint64 sum = 0;
        for (const int fd : fds) {
            quint64 v = 0;
            if (::read(fd, &v, sizeof(v)) == ssize_t(sizeof(v)))
                sum += qint64(v);
        }
        return sum;
    }
};
InsnCounter g_insns;

struct Counters {
    qint64 wall = 0, cpu = 0, insns = 0;
    Counters operator-(const Counters &o) const { return {wall - o.wall, cpu - o.cpu, insns - o.insns}; }
    Counters &operator+=(const Counters &o)
    {
        wall += o.wall;
        cpu += o.cpu;
        insns += o.insns;
        return *this;
    }
};
Counters readCounters()
{
    return {monoNs(), threadCpuNs(), g_insns.read()};
}

// ---- file system calls on the GUI thread (interposed below, outside this namespace) ------------

enum IoKind { IoFsync, IoFdatasync, IoRename, IoRenameat, IoRenameat2, IoLinkat, IoKinds };
const char *const kIoNames[IoKinds] = {"fsync", "fdatasync", "rename", "renameat", "renameat2", "linkat"};
struct IoRec {
    qint64 t0, t1;
    int kind;
};
std::vector<IoRec> *g_io = nullptr; ///< GUI thread only (created by the probe)
std::atomic<qint64> g_ioOtherN[IoKinds];
std::atomic<qint64> g_ioOtherNs[IoKinds];

template<typename F>
auto timedIo(int kind, F &&call)
{
    const qint64 t0 = monoNs();
    auto r = call();
    const int savedErrno = errno;
    const qint64 t1 = monoNs();
    if (onGuiThread()) {
        if (g_io && g_io->size() < 200000)
            g_io->push_back({t0, t1, kind});
    } else {
        g_ioOtherN[kind].fetch_add(1, std::memory_order_relaxed);
        g_ioOtherNs[kind].fetch_add(t1 - t0, std::memory_order_relaxed);
    }
    errno = savedErrno;
    return r;
}
template<typename Fn>
Fn nextDefinition(const char *name)
{
    return reinterpret_cast<Fn>(dlsym(RTLD_NEXT, name));
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

/// The short name of a class for span names: "brulion::Vault" -> "Vault", QML types without
/// their "_QMLTYPE_n" suffix.
QByteArray shortClass(const char *className)
{
    QByteArray n(className);
    for (const char *cut : {"_QMLTYPE_", "_QML_"}) {
        const qsizetype i = n.indexOf(cut);
        if (i > 0)
            n.truncate(i);
    }
    const qsizetype colon = n.lastIndexOf("::");
    return colon >= 0 ? n.mid(colon + 2) : n;
}

/// Signal spans on the GUI thread (see the header). Fed by Qt's signal spy callbacks and by the
/// wrapped QAbstractDeclarativeData::signalEmitted (the QML part of an emission).
class SpanTracker {
public:
    enum Kind { Unwatched = -1, Plain, Generic, Timer, QmlTimer, ModelCount, ModelRange, ModelRows };
    struct Info {
        int name = -1;
        int kind = Unwatched;
    };
    struct Open {
        QObject *sender;
        int signal;
        int name;
        int kind;
        bool qml;
        int rows;
        Counters start;
        Counters child;
        int firstChild = -1;        ///< the name of the first span nested in it
        bool parentless = false;    ///< a QTimer without a parent
        bool saveCandidate = false; ///< … single-shot, with the controller's saveDelay
        bool save = false;          ///< … and a PageController saved or changed its dirty state in it
    };
    struct Rec {
        qint64 t0;
        int name;
        int depth;
        Counters incl, self;
        int rows;
        bool qml;
    };

    bool enabled = false;
    bool catchAll = true;
    qint64 genericMinNs = 200000;
    int saveDelay = 400;
    const QObject *skip[3] = {nullptr, nullptr, nullptr}; ///< the probe and its timers
    qint64 emissions = 0;                                 ///< every GUI-thread emission seen
    std::vector<Open> stack;
    std::vector<Rec> recs;
    QStringList names;

    void begin(QObject *sender, int signal, void **argv, bool qml)
    {
        if (!enabled || !onGuiThread())
            return;
        if (!qml)
            ++emissions;
        if (sender == skip[0] || sender == skip[1] || sender == skip[2])
            return;
        const Info info = classify(sender, signal);
        if (info.kind == Unwatched)
            return;
        int name = info.name;
        int rows = -1;
        bool parentless = false, candidate = false;
        switch (info.kind) {
        case Timer: {
            auto *t = static_cast<QTimer *>(sender);
            parentless = !t->parent();
            candidate = parentless && t->isSingleShot() && t->interval() == saveDelay;
            name = parentless ? nameId(u"timer:%1ms"_s.arg(t->interval()))
                              : nameId(u"timer:"_s
                                       + QString::fromLatin1(shortClass(t->parent()->metaObject()->className())));
            break;
        }
        case QmlTimer: name = qmlTimerName(sender); break;
        case ModelCount:
            if (auto *m = qobject_cast<QAbstractItemModel *>(sender))
                rows = m->rowCount();
            break;
        case ModelRange: // dataChanged(topLeft, bottomRight, roles)
            if (argv && argv[1] && argv[2])
                rows = static_cast<const QModelIndex *>(argv[2])->row()
                       - static_cast<const QModelIndex *>(argv[1])->row() + 1;
            break;
        case ModelRows: // rowsInserted / rowsRemoved / rowsMoved(parent, first, last, …)
            if (argv && argv[2] && argv[3])
                rows = *static_cast<const int *>(argv[3]) - *static_cast<const int *>(argv[2]) + 1;
            break;
        default: break;
        }
        if (!stack.empty() && stack.back().firstChild < 0)
            stack.back().firstChild = name;
        if (info.kind == Plain && (name == m_savedId || name == m_dirtyId || name == m_saveStartedId)) {
            // PageController's autosave: the parentless single-shot timer with its saveDelay in
            // which the controller saves (other parentless timers may have the same interval —
            // PageListModel's refresh delay is 400 ms too).
            for (Open &o : stack)
                o.save = o.save || o.saveCandidate;
        }
        Open open{sender, signal, name, info.kind, qml, rows, readCounters(), {}};
        open.parentless = parentless;
        open.saveCandidate = candidate;
        stack.push_back(open);
    }
    void end(QObject *sender, int signal, bool qml)
    {
        // The sender may be gone (an emission that deleted it): only compared, never used.
        if (!enabled || stack.empty() || !onGuiThread())
            return;
        const Open &top = stack.back();
        if (top.sender != sender || top.signal != signal || top.qml != qml)
            return; // a signal not tracked (or one begun before the spans were switched on)
        const Counters incl = readCounters() - top.start;
        const Counters self = incl - top.child;
        Open done = top;
        stack.pop_back();
        if (!stack.empty())
            stack.back().child += incl;
        if (done.save) {
            done.name = nameId(u"timer:save"_s);
        } else if (done.parentless && done.firstChild >= 0) {
            // A parentless timer is named by what it does first: "timer:400ms(model:PageListModel…)".
            done.name = nameId(names.at(done.name) + u'(' + names.at(done.firstChild) + u')');
        }
        if (done.kind != Generic || incl.wall >= genericMinNs)
            recs.push_back({done.start.wall, done.name, int(stack.size()), incl, self, done.rows, done.qml});
    }
    int nameId(const QString &name)
    {
        auto it = m_ids.constFind(name);
        if (it != m_ids.constEnd())
            return it.value();
        names.append(name);
        m_ids.insert(name, int(names.size() - 1));
        return int(names.size() - 1);
    }

private:
    Info classify(QObject *sender, int signal)
    {
        const QMetaObject *mo = sender->metaObject();
        const auto key = std::make_pair(static_cast<const void *>(mo), signal);
        const auto it = m_cache.constFind(key);
        if (it != m_cache.constEnd())
            return it.value();
        Info info;
        const QMetaMethod m = QMetaObjectPrivate::signal(mo, signal);
        if (m.isValid()) {
            const QMetaObject *encl = m.enclosingMetaObject();
            const QByteArray cls = encl->className();
            const QByteArray sig = m.name();
            const QByteArray shortName = shortClass(cls.constData());
            static const QList<QByteArray> watched = {
                    "Vault.changed",
                    "Workspace.revisionChanged",
                    "Workspace.fileChanged",
                    "Workspace.filesChanged",
                    "Workspace.fileListChanged",
                    "EditorHost.indexChanged",
                    "EditorHost.revisionChanged",
                    "EditorHost.fileChanged",
                    "EditorHost.filesChanged",
                    "EditorHost.fileListChanged",
                    "VaultIndex.changed",
                    "PageController.saved",
                    "PageController.dirtyChanged",
                    "PageController.saveStarted",
            };
            const QByteArray full = shortName + '.' + sig;
            if (encl == &QTimer::staticMetaObject && sig == "timeout") {
                info.kind = Timer;
            } else if (cls == "QQmlTimer" && sig == "triggered") {
                info.kind = QmlTimer;
            } else if (encl == &QAbstractItemModel::staticMetaObject
                       && (sig == "modelReset" || sig == "layoutChanged" || sig == "dataChanged"
                           || sig == "rowsInserted" || sig == "rowsRemoved" || sig == "rowsMoved")) {
                info.kind = sig == "dataChanged" ? ModelRange : (sig.startsWith("rows") ? ModelRows : ModelCount);
                info.name = nameId(u"model:"_s + QString::fromLatin1(shortClass(mo->className())) + u'.'
                                   + QString::fromLatin1(sig));
            } else if (watched.contains(full)) {
                info.kind = Plain;
                info.name = nameId(QString::fromLatin1(full));
            } else if (catchAll && cls.startsWith("brulion::")) {
                info.kind = Generic;
                info.name = nameId(QString::fromLatin1(full));
            }
        }
        m_cache.insert(key, info);
        return info;
    }
    int qmlTimerName(QObject *t)
    {
        const QObject *p = t->parent();
        return nameId(u"QmlTimer:"_s + (p ? QString::fromLatin1(shortClass(p->metaObject()->className())) : u"-"_s)
                      + u':' + QString::number(t->property("interval").toInt()) + u"ms"_s);
    }

    QHash<std::pair<const void *, int>, Info> m_cache; ///< every emission looks here first
    QHash<QString, int> m_ids;
    const int m_savedId = nameId(u"PageController.saved"_s);
    const int m_dirtyId = nameId(u"PageController.dirtyChanged"_s);
    const int m_saveStartedId = nameId(u"PageController.saveStarted"_s);
};
SpanTracker g_spans;

void spySignalBegin(QObject *caller, int signalIndex, void **argv)
{
    g_spans.begin(caller, signalIndex, argv, false);
}
void spySignalEnd(QObject *caller, int signalIndex)
{
    g_spans.end(caller, signalIndex, false);
}
QSignalSpyCallbackSet g_spySet = {spySignalBegin, nullptr, spySignalEnd, nullptr};
void (*g_declarativeSignalEmitted)(QAbstractDeclarativeData *, QObject *, int, void **) = nullptr;
void declarativeSignalEmitted(QAbstractDeclarativeData *data, QObject *sender, int signalIndex, void **argv)
{
    g_spans.begin(sender, signalIndex, argv, true);
    if (g_declarativeSignalEmitted)
        g_declarativeSignalEmitted(data, sender, signalIndex, argv);
    g_spans.end(sender, signalIndex, true);
}

void spansOn()
{
    g_spans.stack.clear();
    g_spans.enabled = true;
    if (!g_declarativeSignalEmitted && QAbstractDeclarativeData::signalEmitted != declarativeSignalEmitted)
        g_declarativeSignalEmitted = QAbstractDeclarativeData::signalEmitted;
    if (g_declarativeSignalEmitted)
        QAbstractDeclarativeData::signalEmitted = declarativeSignalEmitted;
    qt_register_signal_spy_callbacks(&g_spySet);
}
void spansOff()
{
    qt_register_signal_spy_callbacks(nullptr);
    if (g_declarativeSignalEmitted)
        QAbstractDeclarativeData::signalEmitted = g_declarativeSignalEmitted;
    g_spans.enabled = false;
    g_spans.stack.clear();
}

struct Frame {
    qint64 sync = 0;
    qint64 synced = 0; ///< afterSynchronizing: the end of the sync (the GUI thread waits until then)
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
        m_scrollWait = envOr("BENCH_SCROLL_WAIT", "-1").toInt();
        m_resizeWidth = envOr("BENCH_RESIZE_WIDTH", "920").toInt();
        m_keep = qgetenv("BENCH_KEEP") == "1";
        m_waitKept = qgetenv("BENCH_WAIT_KEPT") == "1";
        const QByteArray spans = envOr("BENCH_SPANS", "1");
        m_spans = spans != "0";
        g_spans.catchAll = spans != "watched";
        g_spans.genericMinNs = envOr("BENCH_SPAN_MIN_US", "200").toLongLong() * 1000;
        g_spans.skip[0] = this;
        g_spans.skip[1] = &m_lag;
        g_spans.skip[2] = &m_poll;
        // The constructor runs on the GUI thread (Q_COREAPP_STARTUP_FUNCTION): its counters.
        g_guiTid.store(currentTid());
        g_insns.open();
        g_io = new std::vector<IoRec>();
        g_io->reserve(4096);
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
        emitJson({{u"ev"_s, u"note"_s},
                  {u"what"_s, u"counters"_s},
                  {u"insns"_s, QJsonArray::fromStringList(g_insns.pmus)},
                  {u"insnsError"_s, g_insns.error},
                  {u"guiTid"_s, qint64(g_guiTid.load())}});
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
            m_lastLagCounters = readCounters();
            m_lastLag = m_lastLagCounters.wall;
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
                w, &QQuickWindow::afterSynchronizing, this,
                [this] {
                    std::lock_guard<std::mutex> lock(m_frameMutex);
                    m_pendingSynced = monoNs();
                },
                Qt::DirectConnection);
        connect(
                w, &QQuickWindow::frameSwapped, this,
                [this] {
                    const qint64 t = monoNs();
                    std::lock_guard<std::mutex> lock(m_frameMutex);
                    const qint64 sync = m_pendingSync ? m_pendingSync : t;
                    m_frames.push_back({sync, m_pendingSynced >= sync ? m_pendingSynced : sync, t});
                    m_pendingSync = 0;
                    m_pendingSynced = 0;
                },
                Qt::DirectConnection);
        m_engine = qmlEngine(w);
        if (!m_engine && w->contentItem() && !w->contentItem()->childItems().isEmpty())
            m_engine = qmlEngine(w->contentItem()->childItems().first());
        qApp->removeEventFilter(this);
    }

    void lagTick()
    {
        const Counters c = readCounters();
        const qint64 t = c.wall;
        const qint64 gap = t - m_lastLag;
        if (gap > 12000000LL) { // > 12 ms between 4 ms ticks: the GUI thread was busy
            m_stalls.push_back({m_lastLag, t, c.cpu - m_lastLagCounters.cpu, c.insns - m_lastLagCounters.insns});
            // "Settled" ignores short ones (a frame's sync, preemption on a loaded machine).
            if (gap > 24000000LL)
                m_lastStallEnd = t;
        }
        if (m_series)
            m_cpuSeries.push_back(c);
        m_lastLag = t;
        m_lastLagCounters = c;
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
            // A journal day drawn as one item (BRULION_JOURNAL_DAY_ITEM=1): its rows.
            if (typeName(it) == "brulion::editor::JournalDayItem") {
                n += it->property("shownRows").toInt();
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
        spansOff();
        m_series = false;
        m_lag.stop();
        m_poll.stop();
        markMem(u"end"_s);
        QJsonArray sync, synced, swap;
        for (const Frame &f : framesCopy()) {
            sync.append(msOf(f.sync));
            synced.append(msOf(f.synced));
            swap.append(msOf(f.swap));
        }
        emitJson({{u"ev"_s, u"frames"_s}, {u"sync"_s, sync}, {u"synced"_s, synced}, {u"swap"_s, swap}});
        // [start ms, duration ms, GUI thread CPU ms, user-space instructions in millions]
        QJsonArray st;
        for (const Stall &s : m_stalls)
            st.append(QJsonArray{msOf(s.start), double(s.end - s.start) / 1e6, double(s.cpu) / 1e6,
                                 double(s.insns) / 1e6});
        emitJson({{u"ev"_s, u"stalls"_s}, {u"list"_s, st}});
        emitSpans();
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

    /// The signal spans, the GUI thread's counters of the typing windows and its file system calls.
    void emitSpans()
    {
        QJsonArray list;
        for (const SpanTracker::Rec &r : g_spans.recs)
            list.append(QJsonArray{msOf(r.t0), r.name, r.depth, double(r.incl.wall) / 1e6, double(r.self.wall) / 1e6,
                                   double(r.incl.cpu) / 1e6, double(r.self.cpu) / 1e6, double(r.incl.insns) / 1e6,
                                   double(r.self.insns) / 1e6, r.rows, r.qml ? 1 : 0});
        emitJson(
                {{u"ev"_s, u"spans"_s},
                 {u"fields"_s, QJsonArray{u"t"_s, u"name"_s, u"depth"_s, u"incl_ms"_s, u"self_ms"_s, u"incl_cpu_ms"_s,
                                          u"self_cpu_ms"_s, u"incl_insns_M"_s, u"self_insns_M"_s, u"rows"_s, u"qml"_s}},
                 {u"names"_s, QJsonArray::fromStringList(g_spans.names)},
                 {u"list"_s, list}});
        QJsonArray t, cpu, insns;
        for (const Counters &c : m_cpuSeries) {
            t.append(msOf(c.wall));
            cpu.append(double(c.cpu) / 1e6);
            insns.append(double(c.insns));
        }
        emitJson({{u"ev"_s, u"cpu"_s}, {u"t"_s, t}, {u"cpu"_s, cpu}, {u"insns"_s, insns}});
        QJsonArray io;
        if (g_io) {
            for (const IoRec &r : std::as_const(*g_io))
                io.append(QJsonArray{msOf(r.t0), double(r.t1 - r.t0) / 1e6, QString::fromLatin1(kIoNames[r.kind])});
        }
        QJsonObject other;
        for (int k = 0; k < IoKinds; ++k) {
            if (const qint64 n = g_ioOtherN[k].load())
                other.insert(QString::fromLatin1(kIoNames[k]),
                             QJsonObject{{u"n"_s, n}, {u"ms"_s, double(g_ioOtherNs[k].load()) / 1e6}});
        }
        emitJson({{u"ev"_s, u"io"_s}, {u"list"_s, io}, {u"other_threads"_s, other}});
    }

    // ---- kept views -----------------------------------------------------------------------------
    struct KeptViews {
        int built = 0, total = 0, loading = 0;
    };
    /// The kept views of the panes that build them ahead (ShellRouteView.preloadViews: the main
    /// pane): its KeptLoaders (Loaders with `kept` and `everShown`), built = Loader.Ready with an
    /// item. Trees without them: total 0.
    KeptViews keptViews()
    {
        KeptViews k;
        walk(m_window ? m_window->contentItem() : nullptr, [&](QQuickItem *it) {
            if (typeName(it) != "ShellRouteView")
                return true;
            if (!it->property("preloadViews").toBool())
                return false;
            const QList<QQuickItem *> kids = it->childItems();
            for (QQuickItem *c : kids) {
                const QMetaObject *mo = c->metaObject();
                if (mo->indexOfProperty("kept") < 0 || mo->indexOfProperty("everShown") < 0)
                    continue;
                ++k.total;
                const int status = c->property("status").toInt(); // Loader: Null 0, Ready 1, Loading 2
                if (status == 1 && c->property("item").value<QObject *>())
                    ++k.built;
                else if (status == 2)
                    ++k.loading;
            }
            return false;
        });
        return k;
    }
    /// BENCH_WAIT_KEPT=1: until every kept view exists (at most 30 s), then settled again.
    void waitKeptSteps()
    {
        if (!m_waitKept)
            return;
        waitUntil(
                u"kept_wait"_s,
                [this] {
                    const KeptViews k = keptViews();
                    const bool timedOut = monoNs() - m_stepStart > 30000000000LL;
                    if (k.built < k.total && !timedOut)
                        return false;
                    mark(u"kept_ready"_s, 0, {{u"n"_s, k.built}, {u"of"_s, k.total}, {u"timedOut"_s, timedOut}});
                    return true;
                },
                40000, false);
        waitSettled(u"kept_settled"_s, 1000, 60000);
    }

    /// Three bursts of typing (120 ms between keys: a fast typist), each followed by 2.4 s in which
    /// the 400 ms autosave and its fan-out run (the save's end + 1.7 s fits unless the save takes
    /// over 300 ms) — the typing windows: signal spans on (BENCH_SPANS)
    /// and the GUI thread's counters at every lag tick, from 1 s of idle editing before the first
    /// burst (the idle rate fanout_deferred_cpu_ms subtracts) to the end of the last window.
    void burstSteps()
    {
        action(u"idle_ref"_s, [this] {
            m_series = true;
            mark(u"idle_ref_start"_s);
        });
        wait(1000);
        action(u"idle_ref_end"_s, [this] { mark(u"idle_ref_end"_s); });
        for (int burst = 0; burst < 3; ++burst) {
            action(u"burst"_s, [this, burst] {
                const KeptViews k = keptViews();
                mark(u"kept_views:"_s + QString::number(burst), 0,
                     {{u"n"_s, k.built}, {u"of"_s, k.total}, {u"loading"_s, k.loading}});
                m_emissionsAtOn = g_spans.emissions;
                m_recsAtOn = qint64(g_spans.recs.size());
                if (m_spans)
                    spansOn();
                mark(u"burst_start:"_s + QString::number(burst));
            });
            typeString(u" ala ma kota zażółć"_s, 120);
            action(u"burst_end"_s, [this, burst] { mark(u"burst_end:"_s + QString::number(burst)); });
            wait(2400);
            action(u"window_end"_s, [this, burst] {
                spansOff();
                mark(u"window_end:"_s + QString::number(burst), 0,
                     {{u"emissions"_s, g_spans.emissions - m_emissionsAtOn},
                      {u"spans"_s, qint64(g_spans.recs.size()) - m_recsAtOn}});
            });
        }
        action(u"series_off"_s, [this] { m_series = false; });
    }
    /// The autosave timer's interval (PageController.saveDelay; 400 ms in trees without it): the
    /// spans call the parentless single-shot QTimer with that interval "timer:save".
    void takeSaveDelay(QObject *ctl)
    {
        const QVariant delay = ctl ? ctl->property("saveDelay") : QVariant();
        if (delay.isValid())
            g_spans.saveDelay = delay.toInt();
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
            // Typed QML functions (compiled QML: startEditing(row: int, cursor: int)) take ints;
            // trees from before that took QVariants.
            if (!QMetaObject::invokeMethod(ed, "startEditing", Q_ARG(int, row), Q_ARG(int, -1)))
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
        waitKeptSteps();
        startEditing(m_page, m_row);
        action(u"watch_saved"_s, [this] {
            QObject *ctl = nullptr;
            pageEditor(m_page, &ctl);
            if (ctl)
                connect(ctl, SIGNAL(saved()), this, SLOT(onSaved()));
            takeSaveDelay(ctl);
        });
        burstSteps();
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

    /// The visible TextEdit with the longest text (source mode's: the whole file).
    QQuickItem *longestTextEdit()
    {
        QQuickItem *best = nullptr;
        int bestLength = 0;
        walk(m_window ? m_window->contentItem() : nullptr, [&](QQuickItem *it) {
            if (!it->isVisible())
                return false;
            for (const QMetaObject *mo = it->metaObject(); mo; mo = mo->superClass()) {
                if (QByteArray(mo->className()).startsWith("QQuickTextEdit")) {
                    const int length = it->property("length").toInt();
                    if (length > bestLength) {
                        best = it;
                        bestLength = length;
                    }
                    return false;
                }
            }
            return true;
        });
        return best;
    }

    /// source-typing: the page in source mode (Ctrl+E: the whole file in one TextEdit), the caret
    /// at the end of the line in the middle of the file, then the bursts and Enters of `typing`
    /// and a few Backspaces.
    void sourceTypingSteps()
    {
        waitKeptSteps();
        action(u"source_mode"_s, [this] {
            QObject *ctl = nullptr;
            pageEditor(m_page, &ctl);
            if (!ctl) {
                emitJson({{u"ev"_s, u"error"_s}, {u"what"_s, u"no controller for "_s + m_page}});
                return;
            }
            connect(ctl, SIGNAL(saved()), this, SLOT(onSaved()));
            takeSaveDelay(ctl);
            mark(u"source_start"_s);
            ctl->setProperty("sourceMode", true);
        });
        waitUntil(
                u"source_shown"_s,
                [this] {
                    QQuickItem *e = longestTextEdit();
                    return e && e->property("length").toInt() > 1000;
                },
                30000);
        action(u"source_caret"_s, [this] {
            QQuickItem *e = longestTextEdit();
            if (!e)
                return;
            const QString text = e->property("text").toString();
            const qsizetype lineEnd = text.indexOf(u'\n', text.size() / 2);
            e->forceActiveFocus();
            e->setProperty("cursorPosition", int(lineEnd < 0 ? text.size() : lineEnd));
            emitJson({{u"ev"_s, u"note"_s}, {u"what"_s, u"source"_s}, {u"chars"_s, int(text.size())}});
        });
        waitUntil(u"editing"_s, [this] { return focusIsText(); }, 10000);
        wait(400);
        waitSettled(u"edit_settled"_s, 600, 30000);
        burstSteps();
        for (int i = 0; i < 5; ++i) {
            keyStep(Qt::Key_Return, Qt::NoModifier, u"enter"_s, 250);
            action(u"type"_s, [this] { typeChar(u'x'); });
            wait(250);
        }
        for (int i = 0; i < 5; ++i)
            keyStep(Qt::Key_Backspace, Qt::NoModifier, u"backspace"_s, 250);
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
        waitUntil(
                u"app_active"_s,
                [this] {
                    return QGuiApplication::applicationState() == Qt::ApplicationActive
                           || monoNs() - m_stepStart > 3000000000LL;
                },
                5000, false);
        action(u"app_state"_s, [this] {
            mark(u"app_state"_s, 0,
                 {{u"before"_s, m_stateBefore}, {u"after"_s, int(QGuiApplication::applicationState())}});
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
                QMetaObject::invokeMethod(item, "nodeAt", Q_RETURN_ARG(QString, id), Q_ARG(qreal, p.x()),
                                          Q_ARG(qreal, p.y()));
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
        waitUntil(
                u"graph_layout_done"_s,
                [this] {
                    QObject *m = graphModel();
                    return m && !m->property("running").toBool();
                },
                180000);
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

    /// A page scrolled down and up again with the wheel (80 steps each way, 40 ms apart), opened
    /// fresh: the rows it creates on the way are what the frames show.
    void pageScrollSteps()
    {
        action(u"nav:1"_s, [this] {
            mark(u"nav_start:1"_s);
            navigate({{u"view"_s, u"page"_s}, {u"path"_s, m_page}});
            mark(u"nav_returned:1"_s);
        });
        waitUntil(u"page_content:1"_s, [this] { return pageShown(m_page); }, 120000);
        if (m_scrollWait >= 0)
            wait(m_scrollWait);
        else
            waitSettled(u"page_settled:1"_s, 1000, 120000);
        action(u"mem"_s, [this] { markMem(u"page:1"_s); });
        action(u"scroll_start"_s, [this] { mark(u"scroll_start"_s); });
        for (int i = 0; i < 80; ++i) {
            action(u"wheel"_s, [this] { wheel(-120, u"down"_s); });
            wait(40);
        }
        for (int i = 0; i < 80; ++i) {
            action(u"wheel"_s, [this] { wheel(120, u"up"_s); });
            wait(40);
        }
        action(u"scroll_end"_s, [this] { mark(u"scroll_end"_s); });
        waitSettled(u"scroll_settled"_s, 1000, 60000);
        action(u"mem"_s, [this] { markMem(u"scrolled"_s); });
    }

    /// The window made narrower and wider again six times (BENCH_RESIZE_WIDTH, default 920 — still
    /// wide, with the sidebar, but the text column narrower than the 680 px editor width — ↔ its
    /// width, 600 ms apart) on a settled page: every row that exists lays its text out again. An
    /// action per resize: t0 the request, t2 when the window has the new width (the frame after it
    /// shows the new layout).
    void pageResizeSteps()
    {
        openPageSteps(m_page, u"1"_s);
        action(u"mem"_s, [this] { markMem(u"page:1"_s); });
        action(u"resize_base"_s, [this] { m_resizeBase = m_window ? m_window->width() : 1280; });
        for (int i = 0; i < 12; ++i) {
            const bool narrow = i % 2 == 0;
            action(u"resize"_s, [this, narrow] {
                if (!m_window)
                    return;
                const int w = narrow ? m_resizeWidth : m_resizeBase;
                Action a;
                a.kind = u"resize"_s;
                a.label = QString::number(w);
                a.t0 = monoNs();
                m_window->setWidth(w);
                QWindowSystemInterface::flushWindowSystemEvents(QEventLoop::AllEvents);
                a.t1 = monoNs();
                m_actions.push_back(a);
                m_resizeTarget = w;
                m_resizeAction = qsizetype(m_actions.size()) - 1;
            });
            waitUntil(
                    u"resized"_s,
                    [this] {
                        if (!m_window || m_window->width() != m_resizeTarget)
                            return false;
                        if (m_resizeAction >= 0 && m_resizeAction < qsizetype(m_actions.size()))
                            m_actions[size_t(m_resizeAction)].t2 = monoNs();
                        return true;
                    },
                    5000, false);
            wait(600);
        }
        waitSettled(u"resize_settled"_s, 1000, 60000);
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
                        m_lastLagCounters = readCounters();
                        m_lastLag = m_lastLagCounters.wall;
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
        } else if (s == u"source-typing") {
            openPageSteps(m_page, u"1"_s);
            sourceTypingSteps();
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
        } else if (s == u"page-scroll") {
            pageScrollSteps();
        } else if (s == u"page-resize") {
            pageResizeSteps();
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
    int m_scrollWait = -1;
    bool m_keep = false;
    QStringList m_pages;
    int m_resizeBase = 1280;
    int m_resizeWidth = 920;
    int m_resizeTarget = 0;
    qsizetype m_resizeAction = -1;

    QPointer<QQuickWindow> m_window;
    QQmlEngine *m_engine = nullptr;
    QHash<QByteArray, QPointer<QObject>> m_singletons;

    std::mutex m_frameMutex;
    std::vector<Frame> m_frames;
    qint64 m_pendingSync = 0;
    qint64 m_pendingSynced = 0;

    QTimer m_lag, m_poll;
    qint64 m_lastLag = 0;
    qint64 m_lastStallEnd = 0;
    qint64 m_lastFalsePoll = 0;
    struct Stall {
        qint64 start, end, cpu, insns;
    };
    std::vector<Stall> m_stalls;
    Counters m_lastLagCounters;
    bool m_series = false; ///< the GUI thread's counters at every lag tick (the typing windows)
    std::vector<Counters> m_cpuSeries;
    bool m_spans = true;
    bool m_waitKept = false;
    qint64 m_emissionsAtOn = 0, m_recsAtOn = 0;
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

// ---- interposed file system calls (LD_PRELOAD: these definitions come first; each calls the next
// one, libc's). Timed on the GUI thread, counted on the others (timedIo()). ----------------------
extern "C" {
int fsync(int fd)
{
    static const auto next = nextDefinition<int (*)(int)>("fsync");
    return timedIo(IoFsync, [&] { return next(fd); });
}
int fdatasync(int fd)
{
    static const auto next = nextDefinition<int (*)(int)>("fdatasync");
    return timedIo(IoFdatasync, [&] { return next(fd); });
}
int rename(const char *from, const char *to) __THROW
{
    static const auto next = nextDefinition<int (*)(const char *, const char *)>("rename");
    return timedIo(IoRename, [&] { return next(from, to); });
}
int renameat(int fromDir, const char *from, int toDir, const char *to) __THROW
{
    static const auto next = nextDefinition<int (*)(int, const char *, int, const char *)>("renameat");
    return timedIo(IoRenameat, [&] { return next(fromDir, from, toDir, to); });
}
int renameat2(int fromDir, const char *from, int toDir, const char *to, unsigned int flags) __THROW
{
    static const auto next = nextDefinition<int (*)(int, const char *, int, const char *, unsigned int)>("renameat2");
    if (!next) {
        errno = ENOSYS;
        return -1;
    }
    return timedIo(IoRenameat2, [&] { return next(fromDir, from, toDir, to, flags); });
}
int linkat(int fromDir, const char *from, int toDir, const char *to, int flags) __THROW
{
    static const auto next = nextDefinition<int (*)(int, const char *, int, const char *, int)>("linkat");
    return timedIo(IoLinkat, [&] { return next(fromDir, from, toDir, to, flags); });
}
}

__attribute__((constructor)) static void benchProbeLoaded()
{
    g_preload = monoNs();
    const QByteArray t0 = qgetenv("BENCH_T0");
    g_t0 = t0.isEmpty() ? g_preload : t0.toLongLong();
}

Q_COREAPP_STARTUP_FUNCTION(startProbe)

#include "benchprobe.moc"
