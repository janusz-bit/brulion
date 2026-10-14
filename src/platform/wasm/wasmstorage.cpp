#include "wasmstorage.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QPointer>
#include <QStandardPaths>

#include <emscripten/bind.h>
#include <emscripten/em_js.h>

#include <algorithm>
#include <string>

Q_LOGGING_CATEGORY(lcWasmStorage, "brulion.platform.wasm.storage")

namespace brulion::platform {

namespace {

// Operation codes reported from JavaScript (keep in sync with the EM_JS code below).
enum JsOp { OpLoad = 1, OpFlush = 2, OpPersist = 3 };

} // namespace

/// Bridge for JavaScript -> C++ calls (registered with embind as Module.brulionStorageEvent).
struct WasmStorageJs
{
    static inline WasmStorage *instance = nullptr;

    static void event(int op, bool ok, const std::string &message)
    {
        QPointer<WasmStorage> target = instance;
        if (!target)
            return;
        const QString text = QString::fromStdString(message);
        // Called from browser callbacks (IndexedDB, promises): continue in Qt's event loop.
        QMetaObject::invokeMethod(
            target.data(),
            [target, op, ok, text] {
                if (target)
                    target->handleJsEvent(op, ok, text);
            },
            Qt::QueuedConnection);
    }
};

} // namespace brulion::platform

EMSCRIPTEN_BINDINGS(brulion_wasm_storage)
{
    emscripten::function("brulionStorageEvent", &brulion::platform::WasmStorageJs::event);
}

// JavaScript side. Notes for editing (the code is a macro argument, stringified into one line):
// every statement needs a semicolon; no comments, no backtick strings, double quotes only
// (an empty '' is an invalid C character literal).
//
// State machine (Module.brulionStorage, also globalThis.brulionStorage for the HTML shell):
//   ready  - initial FS.syncfs(true) succeeded; write-back is only ever allowed after that,
//            otherwise the empty in-memory tree would delete the stored files.
//   failed - mounting or loading failed: in-memory only, never write back.
//   busy   - a syncfs is running; `again` queues one more write-back behind it.
//   dirty/timer/firstChange - debounce: write back `delay` ms after the last change, but at
//            most `maxDelay` ms after the first unsaved change.
// Change tracking wraps the public Emscripten FS API (FS.close/write/rename/unlink/rmdir/
// mkdir/symlink/truncate/utime/chmod), which every libc file syscall goes through.
// One tab at a time: each tab has its own in-memory tree and a write-back deletes from
// IndexedDB what the tab does not have, so two tabs would destroy each other's new notes. A Web
// Lock held for the page's lifetime makes a second tab wait (loading screen "open in another
// tab") until the first one is closed. A page entering the back/forward cache (pagehide with
// persisted) keeps nothing: it releases the lock once its changes are written back and
// reloads when it is shown again.
EM_JS_DEPS(brulion_wasm_storage, "$FS,$IDBFS,$UTF8ToString");

EM_JS(void, brulion_storage_start, (const char *mountsJson, int delayMs, int maxDelayMs), {
    var report = function(op, ok, message) {
        try {
            Module["brulionStorageEvent"](op, !!ok, message ? String(message) : "");
        } catch (e) {
            console.error("Brulion storage: callback failed", e);
        }
    };
    var describe = function(e) {
        if (!e) return "";
        if (typeof e === "string") return e;
        if (e.message) return String(e.message);
        if (e.name) return String(e.name);
        if (e.errno !== undefined) return "errno " + e.errno;
        return String(e);
    };
    var st = {
        mounts: JSON.parse(UTF8ToString(mountsJson)),
        ready: false, failed: false, busy: false, again: false, waitingForLock: false,
        releaseLock: null, lockReleased: false, releaseAfterFlush: false, suspended: false,
        dirty: false, timer: 0, firstChange: 0, delay: delayMs, maxDelay: maxDelayMs
    };
    Module["brulionStorage"] = st;
    globalThis["brulionStorage"] = st;

    var normalize = function(p) {
        if (p.charAt(0) !== "/") p = FS.cwd() + "/" + p;
        var out = [];
        var parts = p.split("/");
        for (var i = 0; i < parts.length; ++i) {
            var s = parts[i];
            if (s === "" || s === ".") continue;
            if (s === "..") out.pop(); else out.push(s);
        }
        return "/" + out.join("/");
    };
    var covers = function(p) {
        if (typeof p !== "string" || p === "") return false;
        p = normalize(p);
        for (var i = 0; i < st.mounts.length; ++i) {
            var m = st.mounts[i];
            if (p === m || p.indexOf(m + "/") === 0) return true;
        }
        return false;
    };

    var releaseLock = function() {
        if (!st.releaseLock) return;
        st.releaseLock();
        st.releaseLock = null;
        st.lockReleased = true;
    };
    var flush = function() {
        if (st.timer) { clearTimeout(st.timer); st.timer = 0; }
        if (!st.ready || st.lockReleased) return;
        if (st.busy) { st.again = true; return; }
        st.busy = true;
        st.dirty = false;
        st.firstChange = 0;
        FS.syncfs(false, function(err) {
            st.busy = false;
            if (err) {
                st.dirty = true;
                console.error("Brulion: saving to IndexedDB failed", err);
            }
            report(2, !err, describe(err));
            if (st.again) { st.again = false; flush(); }
            if (st.releaseAfterFlush && !st.pending()) { st.releaseAfterFlush = false; releaseLock(); }
        });
    };
    var schedule = function() {
        st.dirty = true;
        if (!st.ready) return;
        var now = Date.now();
        if (!st.firstChange) st.firstChange = now;
        var wait = Math.max(0, Math.min(st.delay, st.firstChange + st.maxDelay - now));
        if (st.timer) clearTimeout(st.timer);
        st.timer = setTimeout(flush, wait);
    };
    var flushIfPending = function() {
        if (st.ready && (st.dirty || st.timer)) flush();
    };
    st.flush = flush;
    st.schedule = schedule;
    st.pending = function() { return st.dirty || st.busy || !!st.timer; };

    var wrap = function(name, touches) {
        var original = FS[name];
        if (typeof original !== "function") return;
        FS[name] = function() {
            var result = original.apply(this, arguments);
            try {
                if (st.ready && touches.apply(null, arguments)) schedule();
            } catch (e) {
                console.error("Brulion storage: change tracking failed", e);
            }
            return result;
        };
    };
    var writableStream = function(stream) {
        return !!stream && (stream.flags & 3) !== 0 && covers(stream.path);
    };
    var firstPath = function(path) { return covers(path); };
    wrap("close", writableStream);
    wrap("write", writableStream);
    wrap("rename", function(from, to) { return covers(from) || covers(to); });
    wrap("symlink", function(target, link) { return covers(link); });
    wrap("unlink", firstPath);
    wrap("rmdir", firstPath);
    wrap("mkdir", firstPath);
    wrap("truncate", firstPath);
    wrap("utime", firstPath);
    wrap("chmod", firstPath);

    try {
        for (var i = 0; i < st.mounts.length; ++i) {
            FS.mkdirTree(st.mounts[i]);
            FS.mount(IDBFS, {}, st.mounts[i]);
        }
    } catch (e) {
        st.failed = true;
        console.error("Brulion: cannot mount IndexedDB storage", e);
        report(1, false, describe(e));
        return;
    }

    if (typeof document !== "undefined") {
        document.addEventListener("visibilitychange", function() {
            if (document.visibilityState === "hidden") flushIfPending();
        });
    }
    if (typeof window !== "undefined") {
        window.addEventListener("pagehide", function(event) {
            flushIfPending();
            if (!event.persisted || !st.releaseLock) return;
            st.suspended = true;
            if (st.pending()) st.releaseAfterFlush = true; else releaseLock();
        });
        window.addEventListener("pageshow", function(event) {
            if (!event.persisted || !st.suspended) return;
            st.ready = false;
            location.reload();
        });
        window.addEventListener("beforeunload", function(event) {
            if (!st.ready || !st.pending()) return;
            flushIfPending();
            event.preventDefault();
            event.returnValue = "";
        });
    }

    var load = function() {
        st.busy = true;
        FS.syncfs(true, function(err) {
            st.busy = false;
            if (err) {
                st.failed = true;
                console.error("Brulion: loading from IndexedDB failed", err);
                report(1, false, describe(err));
                return;
            }
            st.ready = true;
            report(1, true, "");
            if (st.dirty) schedule();
            if (st.releaseAfterFlush && !st.pending()) { st.releaseAfterFlush = false; releaseLock(); }
        });
    };
    var shellStage = function(name) {
        var shell = globalThis["brulionShell"];
        if (shell && shell.stage) shell.stage(name);
    };
    if (typeof navigator === "undefined" || !navigator.locks || !navigator.locks.request) {
        load();
        return;
    }
    var lockName = "brulion-storage:" + st.mounts.join("|");
    var holdLock = function() {
        return new Promise(function(resolve) { st.releaseLock = resolve; });
    };
    navigator.locks.request(lockName, { ifAvailable: true }, function(lock) {
        if (lock) {
            load();
            return holdLock();
        }
        console.warn("Brulion: already open in another tab, waiting until it is closed");
        st.waitingForLock = true;
        shellStage("locked");
        navigator.locks.request(lockName, function() {
            st.waitingForLock = false;
            shellStage("storage");
            load();
            return holdLock();
        });
        return undefined;
    }).catch(function(e) {
        console.warn("Brulion: Web Locks unavailable, not guarding against a second tab", e);
        if (!st.busy && !st.ready && !st.failed && !st.waitingForLock) load();
    });
});

EM_JS(void, brulion_storage_schedule, (), {
    var st = Module["brulionStorage"];
    if (st && st.schedule) st.schedule();
});

EM_JS(void, brulion_storage_flush, (), {
    var st = Module["brulionStorage"];
    if (st && st.flush) st.flush();
});

EM_JS(int, brulion_storage_pending, (), {
    var st = Module["brulionStorage"];
    return st && st.pending && st.pending() ? 1 : 0;
});

EM_JS(void, brulion_storage_set_delays, (int delayMs, int maxDelayMs), {
    var st = Module["brulionStorage"];
    if (st) {
        st.delay = delayMs;
        st.maxDelay = maxDelayMs;
    }
});

EM_JS(void, brulion_storage_request_persist, (), {
    var done = function(granted) {
        try {
            Module["brulionStorageEvent"](3, !!granted, "");
        } catch (e) {
            console.error("Brulion storage: callback failed", e);
        }
    };
    if (typeof navigator === "undefined" || !navigator.storage || !navigator.storage.persist) {
        done(false);
        return;
    }
    navigator.storage.persist().then(done, function() { done(false); });
});

namespace brulion::platform {

WasmStorage::WasmStorage(QObject *parent)
    : QObject(parent)
{
    if (WasmStorageJs::instance)
        qCWarning(lcWasmStorage) << "more than one WasmStorage instance; only the newest gets events";
    WasmStorageJs::instance = this;
}

WasmStorage::~WasmStorage()
{
    if (WasmStorageJs::instance == this)
        WasmStorageJs::instance = nullptr;
}

QString WasmStorage::vaultRoot()
{
    return QDir::homePath() + QStringLiteral("/Brulion");
}

QStringList WasmStorage::defaultMountPoints()
{
    return normalizeMountPoints(
        {vaultRoot(), QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)});
}

QStringList WasmStorage::normalizeMountPoints(const QStringList &paths)
{
    QStringList cleaned;
    for (const QString &path : paths) {
        if (!path.startsWith(QLatin1Char('/')))
            continue;
        const QString clean = QDir::cleanPath(path);
        if (clean == QLatin1String("/") || cleaned.contains(clean))
            continue;
        cleaned.append(clean);
    }
    // Shorter paths first, so a nested path always meets its parent before itself.
    std::stable_sort(cleaned.begin(), cleaned.end(),
                     [](const QString &a, const QString &b) { return a.size() < b.size(); });
    QStringList result;
    for (const QString &path : std::as_const(cleaned)) {
        const bool nested = std::any_of(result.cbegin(), result.cend(), [&path](const QString &m) {
            return path.startsWith(m + QLatin1Char('/'));
        });
        if (!nested)
            result.append(path);
    }
    return result;
}

void WasmStorage::start(const QStringList &mountPoints)
{
    if (m_started) {
        qCWarning(lcWasmStorage) << "start() called twice; ignored";
        return;
    }
    m_started = true;
    m_mountPoints = normalizeMountPoints(mountPoints);
    qCDebug(lcWasmStorage) << "mounting IndexedDB storage on" << m_mountPoints;
    const QByteArray json =
        QJsonDocument(QJsonArray::fromStringList(m_mountPoints)).toJson(QJsonDocument::Compact);
    brulion_storage_start(json.constData(), m_flushDelay, m_maxFlushDelay);
}

bool WasmStorage::hasPendingChanges() const
{
    return m_started && brulion_storage_pending() != 0;
}

void WasmStorage::setFlushDelay(int ms)
{
    m_flushDelay = std::max(0, ms);
    pushDelays();
}

void WasmStorage::setMaxFlushDelay(int ms)
{
    m_maxFlushDelay = std::max(0, ms);
    pushDelays();
}

void WasmStorage::pushDelays()
{
    if (m_started)
        brulion_storage_set_delays(m_flushDelay, m_maxFlushDelay);
}

void WasmStorage::scheduleFlush()
{
    if (m_started)
        brulion_storage_schedule();
}

void WasmStorage::flushNow()
{
    if (m_started)
        brulion_storage_flush();
}

void WasmStorage::requestPersistentStorage()
{
    brulion_storage_request_persist();
}

void WasmStorage::handleJsEvent(int op, bool ok, const QString &message)
{
    switch (op) {
    case OpLoad:
        if (m_ready)
            return;
        m_ready = true;
        m_backed = ok;
        if (!ok) {
            m_lastError = message;
            qCWarning(lcWasmStorage) << "IndexedDB unavailable, files stay in memory:" << message;
            Q_EMIT error(message);
        }
        Q_EMIT ready();
        break;
    case OpFlush:
        if (!ok) {
            m_lastError = message;
            qCWarning(lcWasmStorage) << "saving to IndexedDB failed:" << message;
            Q_EMIT error(message);
        }
        Q_EMIT flushed(ok);
        break;
    case OpPersist:
        Q_EMIT persistentStorageResult(ok);
        break;
    default:
        qCWarning(lcWasmStorage) << "unknown storage event" << op;
    }
}

} // namespace brulion::platform
