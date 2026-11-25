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
//                    idle-edit | workload | pictures | memory (see buildScenario())
//   BENCH_PAGE       page path for open-page / typing / source-typing / select-copy / idle-edit /
//                    page-scroll /
//                    page-resize ("Bench/Konspekt 5000.md")
//   BENCH_RESIZE_WIDTH page-resize: the narrower window width (default 920)
//   BENCH_SCROLL_WAIT page-scroll: ms between the page's first block on screen and the first
//                    wheel step (default: -1 = once the page has settled, as a reader would)
//   BENCH_ROW        row to edit (typing, select-copy; default 10)
//   BENCH_NAV_DELAY_MS open-page: ms to wait before the first opening (mark nav_delay; default 0):
//                    time for a profiler attached at "settled" / "kept_settled" to start
//   BENCH_PAGES      file with one page path per line (switch-pages, workload, pictures)
//   BENCH_PICTURES   pictures: file with one picture path per line (opened one after another)
//   BENCH_CYCLES     pictures: how many rounds of album / viewer / pages (default 2)
//   BENCH_REST_MS    pictures: the wait at home after each round (default 3000; 35000 lets Qt's
//                    pixmap cache expire: 30 s)
//   BENCH_SETTLE_MS  memory: the wait after the start has settled, for what the app builds ahead
//                    and for what only the start needed to go (the spell checker's dictionaries,
//                    8 s after the first page's words; default 15000)
//   BENCH_LONG_REST_MS memory: a second rest at the end (default 0: none; 30000 lets the
//                    program's own pages go — MemoryPolicy.idleMs, 12 s —, 100000 with
//                    BRULION_HIDDEN_VIEW_MS=20000 the hidden views too)
//   BENCH_AWAY       0: memory: without the steps of an app that is away (mem:away, away_hidden, back)
//   BENCH_AWAY_VIEWS 1: memory: those steps for every view of the tour too (back_picture:<view>)
//   BENCH_PICTURES_DIR memory: where the window's pictures before it was away and back again go
//   BENCH_QML_TYPES  1: memory: every mark lists the app's QML files the engine has loaded
//                    (their count is always there: qmlTypes)
//   BENCH_VIEWPORT_OBSERVERS  1: every memory mark lists the shown items that observe the
//                    viewport (QQuickItem::ItemObservesViewport), by class and name
//   BENCH_IDLE_MS    idle duration (default 30000)
//   BENCH_TIMEOUT_MS whole-run watchdog (default 240000): exit code 3
//   BENCH_KEEP       1: do not quit at the end
//   BENCH_WAIT_KEPT  1: typing / source-typing / open-page / journal-scroll wait until the main pane
//                    built ahead what it meant to (ShellRouteView: the kept view the user went to
//                    last, 1.2 s after ready; every kept view, one per 0.4 s, in builds before
//                    MemoryPolicy and with BRULION_MEMORY_POLICY=0) before the first burst (the
//                    open, the scroll)
//   BENCH_SPANS      signal spans in the typing windows: 1 (default) the watched signals and
//                    every other signal of a brulion:: class taking >= BENCH_SPAN_MIN_US (200 us)
//                    inclusive; "watched" only the watched ones; 0 none
//   BENCH_OBJECT_TYPES 1: at the startup's "content" and "settled" marks (memory: at its "start",
//                    "home" and "rested" marks too), the live QObjects by class
//                    and by the direct child of AppShell that holds them ("objects" records; slower);
//                    BENCH_OBJECT_DEPTH: how many named ancestors name a group (default 4)
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
//   * at every mark made on the GUI thread its CPU time and user-space instructions since the
//     probe started (guiCpuMs, guiInsnsM) and the QObjects created and alive since then
//     (objCreated, objAlive: QtCore's AddQObject / RemoveQObject hooks);
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
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QRect>
#include <QTimer>
#include <QtCore/private/qhooks_p.h>
#if __has_include(<QtQml/private/qqmltypeloader_p.h>)
#include <QtQml/private/qqmltypeloader_p.h> // which QML files of the app are loaded (memory)
#define BENCH_HAS_TYPE_LOADER 1
#endif
#if __has_include(<QtQml/private/qqmldata_p.h>)
#include <QtQml/private/qqmlcontextdata_p.h> // ids without reading properties; declaration sites
#include <QtQml/private/qqmldata_p.h>
#define BENCH_HAS_QML_DATA 1
#endif
#if __has_include(<QtQuick/private/qquickitem_p.h>)
#include <QtQuick/private/qquickitem_p.h> // an item's lazily made anchors, layer, palette
#define BENCH_HAS_ITEM_PRIVATE 1
#endif
#include <QtCore/private/qmetaobject_p.h>
#include <QtCore/private/qobject_p.h>
#include <qpa/qwindowsysteminterface.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <malloc.h>
#include <pthread.h>
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
/// The GUI thread's CPU clock (pthread_getcpuclockid), readable from any thread: the render
/// thread reads it at a frame's sync, while the GUI thread waits for that sync.
clockid_t g_guiCpuClock = CLOCK_THREAD_CPUTIME_ID;
qint64 guiCpuNs()
{
    timespec ts;
    clock_gettime(g_guiCpuClock, &ts);
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
/// The GUI thread's counters from any thread (the instruction counter's file descriptors belong to
/// the GUI thread's counters, whichever thread reads them).
Counters readGuiCounters()
{
    return {monoNs(), guiCpuNs(), g_insns.read()};
}

// ---- QObjects: created and alive (QtCore's hooks, qhooks_p.h) -----------------------------------
//
// Every QObject constructed or destroyed after the probe started (the QML engine, every object a
// component creates, the views built ahead…), any thread. BENCH_OBJECT_TYPES=1 also keeps the set
// of live objects and prints, at the "settled" mark, how many there are of each class and under
// which of AppShell's direct children (docs/PERFORMANCE.md, L10: what the shell creates at startup).

/// The QML id of `object` ("" for none). QQmlContext::nameForObject() only on the context whose ids
/// hold it: when it finds no id it reads every QObject property of the context's object — an item's
/// lazily made `anchors`, `layer` (QQuickItemLayer) and `palette` (QQuickPalette) —, thousands of
/// objects the probe itself added to the counts of the marks after its first object dump. (The
/// QQmlContext made for a context with ids stays: the names live in a hash Qt does not export.) Without Qt's private
/// QML headers, nameForObject() for every object.
QString qmlIdOf(QObject *object)
{
#ifdef BENCH_HAS_QML_DATA
    QQmlData *d = QQmlData::get(object);
    if (!d)
        return {};
    for (QQmlContextData *c : {d->context, d->outerContext}) {
        for (int i = 0; c && i < c->numIdValues(); ++i) {
            if (c->idValue(i) == object) // (nameForObject() of this context finds it among its ids)
                return c->asQQmlContext()->nameForObject(object);
        }
    }
    return {};
#else
    QQmlContext *c = qmlContext(object);
    return c ? c->nameForObject(object) : QString();
#endif
}

/// Where `object` is declared: "<file>.qml:<line>", "" when it was not made from QML.
QString qmlSiteOf(QObject *object)
{
#ifdef BENCH_HAS_QML_DATA
    QQmlData *d = QQmlData::get(object);
    if (!d || !d->outerContext || d->lineNumber == 0)
        return {};
    const QString url = d->outerContext->url().toString();
    return url.mid(url.lastIndexOf(u'/') + 1) + u':' + QString::number(d->lineNumber);
#else
    Q_UNUSED(object);
    return {};
#endif
}

std::atomic<qint64> g_objCreated{0};
std::atomic<qint64> g_objDestroyed{0};
quintptr g_prevAddHook = 0;
quintptr g_prevRemoveHook = 0;
bool g_objTypes = false;
std::mutex g_liveMutex;
std::set<QObject *> *g_live = nullptr;

void addObjectHook(QObject *o)
{
    g_objCreated.fetch_add(1, std::memory_order_relaxed);
    if (g_live) {
        std::lock_guard<std::mutex> lock(g_liveMutex);
        g_live->insert(o);
    }
    if (g_prevAddHook)
        reinterpret_cast<QHooks::AddQObjectCallback>(g_prevAddHook)(o);
}
void removeObjectHook(QObject *o)
{
    g_objDestroyed.fetch_add(1, std::memory_order_relaxed);
    if (g_live) {
        std::lock_guard<std::mutex> lock(g_liveMutex);
        g_live->erase(o);
    }
    if (g_prevRemoveHook)
        reinterpret_cast<QHooks::RemoveQObjectCallback>(g_prevRemoveHook)(o);
}
void installObjectHooks()
{
    g_objTypes = qgetenv("BENCH_OBJECT_TYPES") == "1";
    if (g_objTypes)
        g_live = new std::set<QObject *>();
    g_prevAddHook = qtHookData[QHooks::AddQObject];
    g_prevRemoveHook = qtHookData[QHooks::RemoveQObject];
    qtHookData[QHooks::AddQObject] = reinterpret_cast<quintptr>(&addObjectHook);
    qtHookData[QHooks::RemoveQObject] = reinterpret_cast<quintptr>(&removeObjectHook);
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

QStringList readLines(const QByteArray &file)
{
    QStringList lines;
    QFile f(QString::fromUtf8(file));
    if (!file.isEmpty() && f.open(QIODevice::ReadOnly)) {
        for (const QByteArray &l : f.readAll().split('\n'))
            if (!l.trimmed().isEmpty())
                lines.append(QString::fromUtf8(l.trimmed()));
    }
    return lines;
}

QJsonObject procStatus()
{
    QJsonObject o;
    QFile f(u"/proc/self/status"_s);
    if (f.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = f.readAll().split('\n');
        for (const QByteArray &l : lines) {
            for (const char *key : {"VmRSS", "VmHWM", "RssAnon", "RssFile", "RssShmem", "VmSize", "Threads"}) {
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

/// What the kernel counts for the whole process (/proc/self/smaps_rollup, kB): the proportional
/// set size and its anonymous / file parts, the private pages (dirty: what only this process holds
/// and nothing can drop; clean: file pages mapped by this process alone) and what is swapped out.
QJsonObject smapsRollup()
{
    QJsonObject o;
    QFile f(u"/proc/self/smaps_rollup"_s);
    if (!f.open(QIODevice::ReadOnly))
        return o;
    const QList<QByteArray> lines = f.readAll().split('\n');
    for (const QByteArray &l : lines) {
        for (const char *key : {"Pss", "Pss_Anon", "Pss_File", "Pss_Shmem", "Private_Dirty", "Private_Clean",
                                "Shared_Clean", "Anonymous", "Swap"}) {
            if (l.startsWith(key) && l.size() > qsizetype(strlen(key)) && l.at(qsizetype(strlen(key))) == ':') {
                QByteArray v = l.mid(qsizetype(strlen(key)) + 1).trimmed();
                o.insert(QString::fromLatin1(key), v.split(' ').first().toLongLong());
            }
        }
    }
    return o;
}

/// malloc's own count over every arena (mallinfo2() sees the main one only): what the arenas
/// took from the system, what of it is free, the blocks mapped on their own and the arenas
/// (glibc's malloc_info XML; kB).
QJsonObject mallocArenas()
{
    QJsonObject o;
    char *text = nullptr;
    size_t size = 0;
    FILE *mem = open_memstream(&text, &size);
    if (!mem)
        return o;
    const int rc = malloc_info(0, mem);
    std::fclose(mem);
    if (rc == 0 && text) {
        const QByteArray xml(text, qsizetype(size));
        // The totals follow the last </heap>.
        const QByteArray tail = xml.mid(std::max<qsizetype>(0, xml.lastIndexOf("</heap>")));
        const auto number = [&tail](const char *element, const char *type) -> qint64 {
            const QByteArray open = QByteArray("<") + element + " type=\"" + type + "\"";
            const qsizetype at = tail.indexOf(open);
            if (at < 0)
                return 0;
            const qsizetype sizeAt = tail.indexOf("size=\"", at);
            const qsizetype end = tail.indexOf('"', sizeAt + 6);
            return sizeAt < 0 || end < 0 ? 0 : tail.mid(sizeAt + 6, end - sizeAt - 6).toLongLong();
        };
        const qint64 system = number("system", "current");
        const qint64 free = number("total", "fast") + number("total", "rest");
        const qint64 mapped = number("total", "mmap");
        o.insert(u"arenasSystemKB"_s, system / 1024);
        o.insert(u"arenasFreeKB"_s, free / 1024);
        o.insert(u"arenasMmapKB"_s, mapped / 1024);
        o.insert(u"arenasInUseKB"_s, (system - free + mapped) / 1024);
        o.insert(u"arenas"_s, qint64(xml.count("<heap nr=")));
    }
    std::free(text);
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
    /// The GUI thread's CPU time and user-space instructions at the sync's start: its work up to
    /// this frame (navigation → the frame showing the page: open-page's `*_frame_insns_M`).
    qint64 guiCpu = 0;
    qint64 guiInsns = 0;
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
        m_navDelay = envOr("BENCH_NAV_DELAY_MS", "0").toInt();
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
        if (pthread_getcpuclockid(pthread_self(), &g_guiCpuClock) != 0)
            g_guiCpuClock = CLOCK_THREAD_CPUTIME_ID;
        g_insns.open();
        m_counters0 = readCounters();
        installObjectHooks();
        g_io = new std::vector<IoRec>();
        g_io->reserve(4096);
        m_pages = readLines(qgetenv("BENCH_PAGES"));
        m_pictures = readLines(qgetenv("BENCH_PICTURES"));
        m_cycles = std::max(1, envOr("BENCH_CYCLES", "2").toInt());
        m_restMs = std::max(0, envOr("BENCH_REST_MS", "3000").toInt());
        m_settleMs = std::max(0, envOr("BENCH_SETTLE_MS", "15000").toInt());
        m_longRestMs = std::max(0, envOr("BENCH_LONG_REST_MS", "0").toInt());
        m_memoryDetail = m_scenario == u"memory";
        m_viewportObservers = envOr("BENCH_VIEWPORT_OBSERVERS", "0") == "1";
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
    void mark(const QString &name, qint64 t = 0, QJsonObject extra = {}, const Counters *at = nullptr)
    {
        if (!t)
            t = monoNs();
        // The GUI thread's counters (CPU ms, instructions in millions) now, or at `at` (a mark
        // dated back: settled at the end of the last stall); also since the probe started
        // (QApplication constructed), and the QObjects created and alive: what the marks of a
        // start cost.
        if (at || onGuiThread()) {
            const Counters c = at ? *at : readCounters();
            extra.insert(u"gcpu"_s, double(c.cpu) / 1e6);
            extra.insert(u"gins"_s, double(c.insns) / 1e6);
            extra.insert(u"guiCpuMs"_s, double(c.cpu - m_counters0.cpu) / 1e6);
            extra.insert(u"guiInsnsM"_s, double(c.insns - m_counters0.insns) / 1e6);
            const qint64 created = g_objCreated.load(std::memory_order_relaxed);
            extra.insert(u"objCreated"_s, created);
            extra.insert(u"objAlive"_s, created - g_objDestroyed.load(std::memory_order_relaxed));
        }
        extra.insert(u"ev"_s, u"mark"_s);
        extra.insert(u"name"_s, name);
        extra.insert(u"t"_s, msOf(t));
        emitJson(extra);
    }
    /// A mark with the GUI thread's counters now: CPU ms and user-space instructions.
    void markCounters(const QString &name)
    {
        const Counters c = readCounters();
        mark(name, c.wall, {{u"cpu_ms"_s, double(c.cpu) / 1e6}, {u"insns"_s, double(c.insns)}});
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
        const PictureStats pictures = pictureStats();
        o.insert(u"pictures"_s, pictures.ready);
        o.insert(u"picturesLoading"_s, pictures.loading);
        o.insert(u"pictureKB"_s, pictures.bytes / 1024);
        if (m_viewportObservers)
            o.insert(u"viewportObservers"_s, viewportObservers());
        if (m_memoryDetail) {
            o.insert(u"smaps"_s, smapsRollup());
            o.insert(u"malloc"_s, mallocArenas());
            const QStringList types = loadedQmlTypes();
            o.insert(u"qmlTypes"_s, types.size());
            // (The names once per run and whenever BENCH_QML_TYPES=1: which files a step loaded.)
            if (qgetenv("BENCH_QML_TYPES") == "1")
                o.insert(u"qmlTypeNames"_s, QJsonArray::fromStringList(types));
        }
        mark(u"mem:"_s + name, 0, o);
    }

    /// The QML files of the app's module (":/qt/qml/Brulion/qmldir") the engine has loaded: every
    /// one costs its compiled unit, property caches and bindings' tables for as long as the engine
    /// keeps it (docs/PERFORMANCE.md "QML files loaded when first used"). Empty without Qt's
    /// private QML headers.
    QStringList loadedQmlTypes()
    {
        QStringList loaded;
#ifdef BENCH_HAS_TYPE_LOADER
        if (!m_engine)
            return loaded;
        if (m_moduleFiles.isEmpty()) {
            QFile qmldir(u":/qt/qml/Brulion/qmldir"_s);
            if (qmldir.open(QIODevice::ReadOnly)) {
                const QList<QByteArray> lines = qmldir.readAll().split('\n');
                for (const QByteArray &line : lines) {
                    const QList<QByteArray> words = line.simplified().split(' ');
                    if (!words.isEmpty() && words.last().endsWith(".qml"))
                        m_moduleFiles.append(QString::fromUtf8(words.last()));
                }
            }
        }
        QQmlTypeLoader *loader = QQmlTypeLoader::get(m_engine);
        for (const QString &file : std::as_const(m_moduleFiles)) {
            if (loader->isTypeLoaded(QUrl(u"qrc:/qt/qml/Brulion/"_s + file)))
                loaded.append(file);
        }
#endif
        return loaded;
    }

    /// BENCH_VIEWPORT_OBSERVERS=1: the items with QQuickItem::ItemObservesViewport, by class (and
    /// name; "(hidden)": not visible — they count too). While a scrolled view has one below it,
    /// every scroll step walks the item tree from the view down to it
    /// (QQuickItemPrivate::transformChanged; docs/PERFORMANCE.md "Pitfalls").
    QJsonObject viewportObservers()
    {
        QHash<QString, int> counts;
        walk(m_window ? m_window->contentItem() : nullptr, [&counts](QQuickItem *it) {
            if (it->flags().testFlag(QQuickItem::ItemObservesViewport)) {
                QString key = QString::fromLatin1(it->metaObject()->className());
                if (!it->objectName().isEmpty())
                    key += u'#' + it->objectName();
                if (!it->isVisible())
                    key += u" (hidden)"_s;
                ++counts[key];
            }
            return true;
        });
        QJsonObject out;
        for (auto it = counts.cbegin(); it != counts.cend(); ++it)
            out.insert(it.key(), it.value());
        return out;
    }

    /// The pictures the window's items hold: every Image / AnimatedImage (QQuickImageBase), shown
    /// or not, that has loaded its source — and what their pixels take decoded, 4 bytes each (the
    /// copy Qt keeps beside the texture; the texture itself is as much again, a third more with
    /// mipmaps). Icons count too (a few kilobytes each).
    struct PictureStats {
        int ready = 0, loading = 0;
        qint64 bytes = 0;
    };
    PictureStats pictureStats()
    {
        PictureStats stats;
        if (!m_window)
            return stats;
        walk(m_window->contentItem(), [&stats](QQuickItem *it) {
            if (!it->inherits("QQuickImageBase"))
                return true;
            const int status = it->property("status").toInt(); // QQuickImageBase::Ready 1, Loading 2
            if (status == 2) {
                ++stats.loading;
            } else if (status == 1) {
                // (implicit size: the pixmap's, over its device pixel ratio.)
                const qreal dpr = it->window() ? it->window()->effectiveDevicePixelRatio() : 1.0;
                const qreal w = it->implicitWidth() * dpr, h = it->implicitHeight() * dpr;
                if (w > 0 && h > 0) {
                    ++stats.ready;
                    stats.bytes += qint64(w * h * 4);
                }
            }
            return true;
        });
        return stats;
    }
    /// The picture view shows `path` loaded (ContentPictureView: `pictureLoaded`).
    bool pictureShown(const QString &path)
    {
        bool shown = false;
        walk(m_window ? m_window->contentItem() : nullptr, [&](QQuickItem *it) {
            if (shown || !it->isVisible())
                return false;
            if (it->objectName() == u"pictureView" && it->property("path").toString() == path) {
                shown = it->property("pictureLoaded").toBool();
                return false;
            }
            return true;
        });
        return shown;
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
                    const Counters gui = readGuiCounters();
                    std::lock_guard<std::mutex> lock(m_frameMutex);
                    m_pendingSync = gui.wall;
                    m_pendingGui = gui;
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
                    m_frames.push_back({sync, m_pendingSynced >= sync ? m_pendingSynced : sync, t,
                                        m_pendingSync ? m_pendingGui.cpu : 0, m_pendingSync ? m_pendingGui.insns : 0});
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
            if (gap > 24000000LL) {
                m_lastStallEnd = t;
                m_lastStallEndCounters = c;
            }
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
    /// The probe steers the app without keys or clicks: what a user would have pressed for it
    /// counts as something done (MemoryPolicy.used(): the wait for "not used for a while" starts
    /// over; trees without the policy have nothing to tell).
    void userDid()
    {
        if (QObject *policy = singleton("MemoryPolicy"); policy && policy->metaObject()->indexOfMethod("used()") >= 0)
            QMetaObject::invokeMethod(policy, "used");
    }
    void navigate(const QVariantMap &route)
    {
        userDid();
        if (QObject *app = singleton("AppController"))
            QMetaObject::invokeMethod(app, "navigate", Q_ARG(QVariantMap, route), Q_ARG(bool, false));
    }
    void uiAction(const QString &action)
    {
        userDid();
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
    /// Visible BlockDelegates under `root` (the whole window when null), and the rows a PageItem
    /// draws itself (BRULION_PAGE_ITEM=1: its `drawnRowCount`; its hosted rows are BlockDelegates).
    int visibleBlocks(QQuickItem *root = nullptr)
    {
        if (!m_window)
            return 0;
        int n = 0;
        walk(root ? root : m_window->contentItem(), [&n](QQuickItem *it) {
            if (!it->isVisible())
                return false;
            const QByteArray type = typeName(it);
            if (type == "BlockDelegate") {
                ++n;
                return false;
            }
            if (it->inherits("brulion::editor::PageItem"))
                n += it->property("drawnRowCount").toInt();
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
    Counters m_stepStartCounters;

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
        // (A step fails after its timeout: a long wait needs a longer one.)
        add(
                u"wait"_s, nullptr, [this, ms] { return monoNs() - m_stepStart >= qint64(ms) * 1000000; },
                std::max<qint64>(60000, qint64(ms) + 30000));
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
                    const Counters at = since == m_lastStallEnd ? m_lastStallEndCounters : m_stepStartCounters;
                    mark(name, since, {{u"stepStart"_s, msOf(m_stepStart)}}, &at);
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
                    m_stepStartCounters = readCounters();
                    m_stepStart = m_stepStartCounters.wall;
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
            m_stepStartCounters = readCounters();
            m_stepStart = m_stepStartCounters.wall;
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
        QJsonArray sync, synced, swap, gcpu, gins;
        for (const Frame &f : framesCopy()) {
            sync.append(msOf(f.sync));
            synced.append(msOf(f.synced));
            swap.append(msOf(f.swap));
            gcpu.append(double(f.guiCpu) / 1e6);
            gins.append(double(f.guiInsns) / 1e6);
        }
        emitJson({{u"ev"_s, u"frames"_s},
                  {u"sync"_s, sync},
                  {u"synced"_s, synced},
                  {u"swap"_s, swap},
                  {u"gcpu"_s, gcpu},
                  {u"gins"_s, gins}});
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
        bool done = false; ///< the pane built ahead all it meant to (ShellRouteView._preloaded)
    };
    /// The kept views of the panes that build them ahead (ShellRouteView.preloadViews: the main
    /// pane): its KeptLoaders (Loaders with `kept` and `everShown`), built = Loader.Ready with an
    /// item. Trees without them: total 0. `done`: the pane says it built ahead what it meant to
    /// (only the view the user went to last, since MemoryPolicy; builds before it have no such
    /// property and are done when every kept view exists).
    KeptViews keptViews()
    {
        KeptViews k;
        walk(m_window ? m_window->contentItem() : nullptr, [&](QQuickItem *it) {
            if (typeName(it) != "ShellRouteView")
                return true;
            if (!it->property("preloadViews").toBool())
                return false;
            k.done = it->property("_preloaded").toBool();
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
    /// BENCH_WAIT_KEPT=1: until every kept view exists, or the pane built ahead all it meant to
    /// (at most 30 s), then settled again.
    void waitKeptSteps()
    {
        if (!m_waitKept)
            return;
        waitUntil(
                u"kept_wait"_s,
                [this] {
                    const KeptViews k = keptViews();
                    const bool timedOut = monoNs() - m_stepStart > 30000000000LL;
                    const bool builtAhead = k.done && k.loading == 0;
                    if (k.built < k.total && !builtAhead && !timedOut)
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
        if (g_objTypes)
            action(u"objects_content"_s, [this] { dumpObjectTypes(u"content"_s); });
        waitUntil(u"ready_wait"_s, [this] { return m_readyMarked; }, 600000, false);
        waitSettled(u"settled"_s, 1000, 120000);
        action(u"mem"_s, [this] {
            markMem(u"settled"_s);
            if (g_objTypes)
                dumpObjectTypes(u"settled"_s);
        });
    }

    /// BENCH_OBJECT_TYPES=1: the live QObjects by class, and by the direct child of the shell
    /// (AppShell's QML type) whose subtree holds them — QObject parents, as QML sets them for the
    /// objects a file declares — named by its QML id where it has one.
    void dumpObjectTypes(const QString &at)
    {
        std::vector<QObject *> live;
        {
            std::lock_guard<std::mutex> lock(g_liveMutex);
            live.assign(g_live->begin(), g_live->end());
        }
        QHash<QString, qint64> byClass, byShellChild, byPath, bySite, byFile;
        const int depth = envOr("BENCH_OBJECT_DEPTH", "4").toInt(); // named ancestors in byPath
        const auto nameOf = [](QObject *object) {
            QString name = QString::fromLatin1(object->metaObject()->className());
            name.truncate(name.indexOf(u"_QML"_s) >= 0 ? name.indexOf(u"_QML"_s) : name.size());
            const QString id = qmlIdOf(object);
            if (!id.isEmpty())
                name += u" #"_s + id;
            if (!object->objectName().isEmpty())
                name += u" \""_s + object->objectName() + u'"';
            return name;
        };
        for (QObject *o : live) {
            if (o->thread() != thread())
                continue;
            byClass[QString::fromLatin1(o->metaObject()->className())] += 1;
            // Where it comes from: "<class> @ <file>:<line>" for an object a QML file declares,
            // "<class> ^<n> <site>" for one made by the object n levels up (an attached object,
            // a Repeater's model data…), "^ ?" when no ancestor was declared in QML (anchors,
            // contexts: no QObject parent).
            {
                QString cls = QString::fromLatin1(o->metaObject()->className());
                cls.truncate(cls.indexOf(u"_QML"_s) >= 0 ? cls.indexOf(u"_QML"_s) : cls.size());
                QString site = qmlSiteOf(o);
                QString key;
                if (!site.isEmpty()) {
                    key = cls + u" @ "_s + site;
                } else {
                    int up = 1;
                    for (QObject *p = o->parent(); p && up <= 6; p = p->parent(), ++up) {
                        site = qmlSiteOf(p);
                        if (!site.isEmpty())
                            break;
                    }
                    key = cls + u" ^"_s + (site.isEmpty() ? u"?"_s : QString::number(up) + u' ' + site);
                }
                bySite[key] += 1;
                byFile[site.isEmpty() ? u"?"_s : site.left(site.indexOf(u':'))] += 1;
#ifdef BENCH_HAS_ITEM_PRIVATE
                // The helpers an item makes on first use, which have no QObject parent.
                if (auto *item = qobject_cast<QQuickItem *>(o)) {
                    QQuickItemPrivate *d = QQuickItemPrivate::get(item);
                    const QString at = (site.isEmpty() ? u"?"_s : site) + u" ("_s + cls + u')';
                    if (d->_anchors)
                        bySite[u"+anchors @ "_s + at] += 1;
                    if (d->extra.isAllocated() && d->extra->layer)
                        bySite[u"+layer @ "_s + at] += 1;
                    if (d->providesPalette())
                        bySite[u"+palette @ "_s + at] += 1;
                }
#endif
            }
            // The ancestors below the shell that have a QML id or an objectName, outermost first.
            QObject *child = nullptr;
            QStringList named;
            for (QObject *p = o; p; p = p->parent()) {
                QObject *up = p->parent();
                if (p != o && (!p->objectName().isEmpty() || !qmlIdOf(p).isEmpty()))
                    named.prepend(nameOf(p));
                if (up && QByteArray(up->metaObject()->className()).startsWith("AppShell")) {
                    child = p;
                    break;
                }
            }
            if (!child)
                continue;
            byShellChild[nameOf(child)] += 1;
            byPath[named.mid(0, depth).join(u" / "_s)] += 1;
        }
        const auto toJson = [](const QHash<QString, qint64> &h) {
            QJsonObject o;
            for (auto it = h.begin(); it != h.end(); ++it)
                o.insert(it.key(), it.value());
            return o;
        };
        emitJson({{u"ev"_s, u"objects"_s},
                  {u"at"_s, at},
                  {u"t"_s, msOf(monoNs())},
                  {u"live"_s, qint64(live.size())},
                  {u"byClass"_s, toJson(byClass)},
                  {u"byShellChild"_s, toJson(byShellChild)},
                  {u"byPath"_s, toJson(byPath)},
                  {u"bySite"_s, toJson(bySite)},
                  {u"byFile"_s, toJson(byFile)}});
    }

    void openPageSteps(const QString &page, const QString &tag)
    {
        action(u"nav:"_s + tag, [this, page, tag] {
            markCounters(u"open_counters_start:"_s + tag); // open<tag>_insns_M
            mark(u"nav_start:"_s + tag);
            navigate({{u"view"_s, u"page"_s}, {u"path"_s, page}});
            mark(u"nav_returned:"_s + tag);
        });
        waitUntil(u"page_content:"_s + tag, [this, page] { return pageShown(page); }, 120000);
        waitSettled(u"page_settled:"_s + tag, 1000, 120000);
        // (To the settled page, not to its first blocks: a view may show its first blocks before
        // or after the rest of the opening's work.)
        action(u"open_counters_end:"_s + tag, [this, tag] { markCounters(u"open_counters_end:"_s + tag); });
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
        // (BENCH_WAIT_KEPT=1: the views built ahead are not built during the scroll.)
        waitKeptSteps();
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
            markCounters(u"open_counters_start:1"_s); // open1_insns_M
            mark(u"nav_start:1"_s);
            navigate({{u"view"_s, u"page"_s}, {u"path"_s, m_page}});
            mark(u"nav_returned:1"_s);
        });
        waitUntil(u"page_content:1"_s, [this] { return pageShown(m_page); }, 120000);
        if (m_scrollWait >= 0) {
            wait(m_scrollWait);
        } else {
            // The GUI thread's counters 500 ms apart while idle (scroll_insns_M subtracts them).
            waitSettled(u"page_settled:1"_s, 1000, 120000);
            action(u"open_counters_end:1"_s, [this] { markCounters(u"open_counters_end:1"_s); });
            action(u"scroll_idle_start"_s, [this] { markCounters(u"scroll_idle_start"_s); });
            wait(500);
            action(u"scroll_idle_end"_s, [this] { markCounters(u"scroll_idle_end"_s); });
        }
        action(u"mem"_s, [this] { markMem(u"page:1"_s); });
        action(u"scroll_start"_s, [this] {
            markCounters(u"scroll_counters_start"_s);
            mark(u"scroll_start"_s);
        });
        for (int i = 0; i < 80; ++i) {
            action(u"wheel"_s, [this] { wheel(-120, u"down"_s); });
            wait(40);
        }
        for (int i = 0; i < 80; ++i) {
            action(u"wheel"_s, [this] { wheel(120, u"up"_s); });
            wait(40);
        }
        action(u"scroll_end"_s, [this] { mark(u"scroll_end"_s); });
        // (The last notch's smooth scroll ends within 200 ms.)
        wait(500);
        action(u"scroll_counters_end"_s, [this] { markCounters(u"scroll_counters_end"_s); });
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
        // The GUI thread's counters read 500 ms apart: once idle (the idle cost), then from each
        // resize's request on (resize_insns_M: what the new width cost, whatever else runs).
        action(u"resize_base"_s, [this] {
            m_resizeBase = m_window ? m_window->width() : 1280;
            markCounters(u"resize_idle_start"_s);
        });
        wait(500);
        action(u"resize_idle_end"_s, [this] { markCounters(u"resize_idle_end"_s); });
        for (int i = 0; i < 12; ++i) {
            const bool narrow = i % 2 == 0;
            action(u"resize"_s, [this, narrow] {
                if (!m_window)
                    return;
                const int w = narrow ? m_resizeWidth : m_resizeBase;
                markCounters(u"resize_start"_s);
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
            wait(500);
            action(u"resize_end"_s, [this] { markCounters(u"resize_end"_s); });
            wait(100);
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

    /// Memory while pictures and notes are opened and after they are left: `BENCH_CYCLES` rounds
    /// of (1) the album page (BENCH_PAGE: a page of many photos) opened, scrolled to its end and
    /// back, (2) every picture of BENCH_PICTURES opened in the picture view, (3) every page of
    /// BENCH_PAGES opened, each followed by the journal ("home") and BENCH_REST_MS of rest. Memory
    /// marks (`mem:<step>:<round>`, after a garbage collection): what a step holds, what is given
    /// back at home, and whether the second round ends higher than the first (a leak).
    void picturesSteps()
    {
        if (m_pages.isEmpty())
            m_pages = {m_page};
        const auto home = [this](const QString &tag) {
            action(u"home"_s, [this] { navigate({{u"view"_s, u"journals"_s}}); });
            waitUntil(u"home_loaded:"_s + tag, [this] { return pictureStats().loading == 0; }, 60000);
            waitSettled(u"home_settled:"_s + tag, 1000, 60000);
            wait(m_restMs);
            action(u"mem"_s, [this, tag] { markMem(u"home:"_s + tag, true); });
        };
        home(u"start"_s);
        for (int cycle = 0; cycle < m_cycles; ++cycle) {
            const QString c = QString::number(cycle);
            // (1) The album: opened, read to its end and back.
            action(u"nav"_s, [this, c] {
                mark(u"nav_start:album:"_s + c);
                navigate({{u"view"_s, u"page"_s}, {u"path"_s, m_page}});
            });
            waitUntil(u"page_content:album:"_s + c, [this] { return pageShown(m_page); }, 120000);
            waitUntil(u"album_loaded:"_s + c, [this] { return pictureStats().loading == 0; }, 120000);
            waitSettled(u"album_settled:"_s + c, 1000, 120000);
            action(u"mem"_s, [this, c] { markMem(u"album:"_s + c, true); });
            for (int i = 0; i < 240; ++i) {
                action(u"wheel"_s, [this] { wheel(-240, u"down"_s); });
                wait(30);
            }
            waitUntil(u"album_end_loaded:"_s + c, [this] { return pictureStats().loading == 0; }, 120000);
            waitSettled(u"album_end_settled:"_s + c, 1000, 120000);
            action(u"mem"_s, [this, c] { markMem(u"album_end:"_s + c, true); });
            for (int i = 0; i < 240; ++i) {
                action(u"wheel"_s, [this] { wheel(240, u"up"_s); });
                wait(30);
            }
            waitUntil(u"album_back_loaded:"_s + c, [this] { return pictureStats().loading == 0; }, 120000);
            waitSettled(u"album_back_settled:"_s + c, 1000, 120000);
            action(u"mem"_s, [this, c] { markMem(u"album_back:"_s + c, true); });
            home(u"after_album:"_s + c);
            // (2) The picture view: one picture after another.
            for (qsizetype i = 0; i < m_pictures.size(); ++i) {
                const QString picture = m_pictures.at(i);
                action(u"nav"_s, [this, picture] { navigate({{u"view"_s, u"page"_s}, {u"path"_s, picture}}); });
                waitUntil(u"picture"_s, [this, picture] { return pictureShown(picture); }, 60000, false);
                wait(120);
                if ((i + 1) % 10 == 0 || i + 1 == m_pictures.size())
                    action(u"mem"_s, [this, c, i] { markMem(u"viewer:%1:%2"_s.arg(c).arg(i + 1), true); });
            }
            if (!m_pictures.isEmpty())
                home(u"after_viewer:"_s + c);
            // (3) Pages, with pictures and without.
            for (qsizetype i = 0; i < m_pages.size(); ++i) {
                const QString page = m_pages.at(i);
                action(u"nav"_s, [this, page] { navigate({{u"view"_s, u"page"_s}, {u"path"_s, page}}); });
                waitUntil(u"page"_s, [this, page] { return pageShown(page); }, 60000, false);
                waitUntil(u"page_loaded"_s, [this] { return pictureStats().loading == 0; }, 60000, false);
                wait(120);
                if ((i + 1) % 10 == 0 || i + 1 == m_pages.size())
                    action(u"mem"_s, [this, c, i] { markMem(u"pages:%1:%2"_s.arg(c).arg(i + 1), true); });
            }
            home(u"after_pages:"_s + c);
        }
        action(u"mem"_s, [this] { markMem(u"pictures:end"_s, true); });
    }

    /// The app away and back (memorySteps(); MemoryPolicy.setAway): away as with another program
    /// in front, the window hidden and away as with no window shown, the window shown and the app
    /// in use again. Memory marks `away`,
    /// `away_hidden`, `back` (with ":<tag>" for a tag) and `back_picture`: the pixels of the window
    /// that differ from before it was away.
    void awaySteps(const QString &tag)
    {
        const QString suffix = tag.isEmpty() ? QString() : u':' + tag;
        const auto mem = [this](const QString &name) { action(u"mem"_s, [this, name] { markMem(name, true); }); };
        action(u"away"_s, [this] {
            // What the window shows now: it must show the same when it is back.
            if (m_window)
                m_beforeAway = m_window->grabWindow();
            if (QObject *p = singleton("MemoryPolicy"))
                QMetaObject::invokeMethod(p, "setAway", Q_ARG(bool, true), Q_ARG(bool, false));
        });
        wait(1500);
        mem(u"away"_s + suffix);
        action(u"hide"_s, [this] {
            if (m_window)
                m_window->hide();
        });
        wait(500);
        action(u"away_hidden"_s, [this] {
            if (QObject *p = singleton("MemoryPolicy"))
                QMetaObject::invokeMethod(p, "setAway", Q_ARG(bool, true), Q_ARG(bool, true));
        });
        wait(1500);
        mem(u"away_hidden"_s + suffix);
        action(u"show"_s, [this] {
            if (m_window)
                m_window->show();
            if (QObject *p = singleton("MemoryPolicy"))
                QMetaObject::invokeMethod(p, "setAway", Q_ARG(bool, false), Q_ARG(bool, false));
        });
        waitSettled(u"back_settled"_s, 1000, 60000);
        wait(1000);
        mem(u"back"_s + suffix);
        action(u"back_picture"_s, [this, tag, suffix] {
            // The window drawn again from nothing (its scene graph and graphics were released).
            // `differing`: any difference (glyphs land elsewhere in their atlas: their edges
            // differ by a shade); `changed`: a channel off by more than a quarter of its range —
            // something that is not drawn as before; `changedBox`: where those are.
            const QImage now = m_window ? m_window->grabWindow() : QImage();
            qint64 differing = -1;
            qint64 changed = -1;
            QRect box;
            if (!now.isNull() && now.size() == m_beforeAway.size()) {
                const QImage a = m_beforeAway.convertToFormat(QImage::Format_RGB32);
                const QImage b = now.convertToFormat(QImage::Format_RGB32);
                differing = 0;
                changed = 0;
                for (int y = 0; y < a.height(); ++y) {
                    const QRgb *la = reinterpret_cast<const QRgb *>(a.constScanLine(y));
                    const QRgb *lb = reinterpret_cast<const QRgb *>(b.constScanLine(y));
                    for (int x = 0; x < a.width(); ++x) {
                        if (la[x] == lb[x])
                            continue;
                        ++differing;
                        const int off =
                                std::max({std::abs(qRed(la[x]) - qRed(lb[x])), std::abs(qGreen(la[x]) - qGreen(lb[x])),
                                          std::abs(qBlue(la[x]) - qBlue(lb[x]))});
                        if (off > 64) {
                            ++changed;
                            box |= QRect(x, y, 1, 1);
                        }
                    }
                }
            }
            if (const QByteArray dir = qgetenv("BENCH_PICTURES_DIR"); !dir.isEmpty()) {
                const QString base = QString::fromUtf8(dir) + u'/' + (tag.isEmpty() ? u"home"_s : tag);
                m_beforeAway.save(base + u"-before.png"_s);
                now.save(base + u"-back.png"_s);
            }
            mark(u"back_picture"_s + suffix, 0,
                 {{u"differingPixels"_s, differing},
                  {u"changedPixels"_s, changed},
                  {u"changedBox"_s, u"%1x%2+%3+%4"_s.arg(box.width()).arg(box.height()).arg(box.x()).arg(box.y())},
                  {u"pixels"_s, qint64(now.width()) * now.height()},
                  {u"sameSize"_s, now.size() == m_beforeAway.size()}});
            m_beforeAway = QImage();
        });
    }

    /// What the app holds: after its start (with everything it builds ahead), after a tour of its
    /// views (a page, Tasks, Calendar, All pages, Search, Graph, Tags, Settings — each `mem:view:<name>`),
    /// back at the journal after BENCH_REST_MS (`mem:home`), after BENCH_LONG_REST_MS more
    /// (`mem:rested`: what goes when it is not used), then away, hidden and back (awaySteps()).
    /// Every mark after a garbage collection, with the kernel's and malloc's own counts.
    void memorySteps()
    {
        const auto mem = [this](const QString &name, bool objects) {
            action(u"mem"_s, [this, name, objects] {
                markMem(name, true);
                if (objects && g_objTypes)
                    dumpObjectTypes(name);
            });
        };
        wait(m_settleMs);
        waitSettled(u"start_settled"_s, 1000, 120000);
        mem(u"start"_s, true);
        // BENCH_UNLOAD=1: what each hidden part holds — the hidden kept views and the popups built
        // ahead unloaded one by one, freed memory given back (malloc_trim), a mark after each.
        if (qgetenv("BENCH_UNLOAD") == "1") {
            action(u"trim"_s, [this] {
                malloc_trim(0);
                markMem(u"trimmed"_s, true);
            });
            for (const char *id : {"tasksLoader", "calendarLoader", "allPagesLoader", "searchLoader", "journalsLoader",
                                   "switcherLoader", "paletteLoader", "captureLoader", "moreMenuLoader"}) {
                const QString name = QString::fromLatin1(id);
                action(u"unload:"_s + name, [this, name] {
                    int done = 0;
                    walk(m_window ? m_window->contentItem() : nullptr, [&](QQuickItem *it) {
                        if (qmlIdOf(it) != name)
                            return true;
                        if (it->isVisible() && it->metaObject()->indexOfProperty("kept") >= 0)
                            return false; // the view shown stays
                        if (it->metaObject()->indexOfProperty("kept") >= 0)
                            it->setProperty("kept", false);
                        else
                            it->setProperty("active", false);
                        ++done;
                        return false;
                    });
                    mark(u"unloaded:"_s + name, 0, {{u"n"_s, done}});
                });
                wait(600);
                action(u"mem"_s, [this, name] {
                    if (m_engine) {
                        m_engine->collectGarbage();
                        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                    }
                    malloc_trim(0);
                    markMem(u"unload:"_s + name, true);
                });
            }
        }
        QList<QPair<QString, QVariantMap>> views;
        if (!qEnvironmentVariableIsEmpty("BENCH_PAGE"))
            views.append({u"page"_s, {{u"view"_s, u"page"_s}, {u"path"_s, m_page}}});
        views.append({u"tasks"_s, {{u"view"_s, u"tasks"_s}}});
        views.append({u"calendar"_s, {{u"view"_s, u"calendar"_s}}});
        views.append({u"allPages"_s, {{u"view"_s, u"allPages"_s}}});
        views.append({u"search"_s, {{u"view"_s, u"search"_s}}});
        views.append({u"graph"_s, {{u"view"_s, u"graph"_s}}});
        views.append({u"tags"_s, {{u"view"_s, u"tags"_s}}});
        views.append({u"settings"_s, {{u"view"_s, u"settings"_s}}});
        for (const auto &[name, route] : std::as_const(views)) {
            action(u"nav:"_s + name, [this, route] { navigate(route); });
            waitUntil(u"view_loaded:"_s + name, [this] { return pictureStats().loading == 0; }, 60000, false);
            waitSettled(u"view_settled:"_s + name, 1000, 120000);
            mem(u"view:"_s + name, false);
            // BENCH_AWAY_VIEWS=1: every view of the tour hidden, let go of and shown again — does
            // each draw as before (`back_picture:<view>`)?
            if (qgetenv("BENCH_AWAY_VIEWS") == "1")
                awaySteps(name);
        }
        action(u"home"_s, [this] { navigate({{u"view"_s, u"journals"_s}}); });
        waitSettled(u"home_settled"_s, 1000, 60000);
        wait(m_restMs);
        mem(u"home"_s, true);
        // BENCH_LONG_REST_MS: the same view for a while longer — what goes by itself when it is
        // not used (the hidden views after MemoryPolicy.hiddenViewMs: BRULION_HIDDEN_VIEW_MS
        // shortens that for a measurement).
        if (m_longRestMs > 0) {
            wait(m_longRestMs);
            mem(u"rested"_s, true);
        }
        // What goes while the app is away (MemoryPolicy, trees that have it): another program in
        // front (`mem:away`), then no window shown (`mem:away_hidden`: the window hidden, its
        // graphics released too), then shown again (`mem:back`: it must draw as before —
        // `back_picture`).
        if (qgetenv("BENCH_AWAY") != "0")
            awaySteps(QString());
        // BENCH_HIDE=sg|graphics: the window hidden and its scene graph (sg), or the scene graph and
        // the graphics context (graphics), released by hand — what a window nobody sees holds —,
        // then shown again (must draw as before).
        if (const QByteArray hide = qgetenv("BENCH_HIDE"); !hide.isEmpty()) {
            action(u"hide"_s, [this] {
                if (m_window)
                    m_window->hide();
            });
            wait(1500);
            action(u"mem"_s, [this] {
                malloc_trim(0);
                markMem(u"hidden"_s, true);
            });
            action(u"release"_s, [this, hide] {
                if (!m_window)
                    return;
                m_window->setPersistentSceneGraph(false);
                m_window->setPersistentGraphics(hide != "graphics");
                m_window->releaseResources();
                m_window->setPersistentSceneGraph(true);
                m_window->setPersistentGraphics(true);
            });
            wait(1000);
            action(u"mem"_s, [this] {
                malloc_trim(0);
                markMem(u"hidden_released"_s, true);
            });
            action(u"show"_s, [this] {
                if (m_window)
                    m_window->show();
            });
            waitSettled(u"shown_settled"_s, 1000, 60000);
            wait(1000);
            mem(u"shown_again"_s, false);
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
            // (BENCH_WAIT_KEPT=1: the views built ahead exist first — on a loaded machine they may
            // still be built while the page opens, and their work lands in its numbers.)
            waitKeptSteps();
            if (m_navDelay > 0) {
                action(u"nav_delay"_s, [this] { mark(u"nav_delay"_s); });
                wait(m_navDelay);
            }
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
        } else if (s == u"pictures") {
            picturesSteps();
        } else if (s == u"memory") {
            memorySteps();
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
    int m_navDelay = 0;
    bool m_keep = false;
    QStringList m_pages, m_pictures;
    int m_cycles = 2;
    int m_restMs = 3000;
    int m_settleMs = 15000;
    int m_longRestMs = 0;
    bool m_memoryDetail = false;
    QStringList m_moduleFiles; ///< the QML files of the module Brulion (loadedQmlTypes())
    QImage m_beforeAway;       ///< memory: the window before the app was "away" (back_picture)
    bool m_viewportObservers = false;
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
    Counters m_pendingGui; ///< the GUI thread's counters at m_pendingSync

    QTimer m_lag, m_poll;
    qint64 m_lastLag = 0;
    qint64 m_lastStallEnd = 0;
    Counters m_lastStallEndCounters; ///< the GUI thread's counters at m_lastStallEnd
    qint64 m_lastFalsePoll = 0;
    struct Stall {
        qint64 start, end, cpu, insns;
    };
    std::vector<Stall> m_stalls;
    Counters m_lastLagCounters;
    Counters m_counters0;  ///< the GUI thread's at the probe's start (the marks' guiCpuMs, guiInsnsM)
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
