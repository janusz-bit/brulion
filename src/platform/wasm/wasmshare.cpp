#include "wasmshare.h"

#include <QList>
#include <QLoggingCategory>
#include <QPointer>
#include <QStringList>

#include <emscripten/bind.h>
#include <emscripten/em_js.h>

#include <string>
#include <utility>

Q_LOGGING_CATEGORY(lcWasmShare, "brulion.platform.wasm.share")

namespace brulion::platform {

/// Bridge for JavaScript -> C++ calls (registered with embind as Module.brulionShareEvent).
struct WasmShareJs
{
    /// Queries kept while no WasmShare exists (the notes still loading).
    static constexpr int MaxPending = 8;
    static inline QList<QPointer<WasmShare>> instances;
    static inline QStringList pending;

    static void event(const std::string &raw)
    {
        const QString query = QString::fromStdString(raw);
        instances.removeAll(nullptr);
        if (instances.isEmpty()) {
            if (pending.size() < MaxPending)
                pending.append(query);
            else
                qCWarning(lcWasmShare) << "Dropping a share handed over while the app is still starting";
            return;
        }
        for (const QPointer<WasmShare> &target : std::as_const(instances))
            deliver(target, query);
    }

    static void deliver(const QPointer<WasmShare> &target, const QString &query)
    {
        // Called from a browser callback: continue in Qt's event loop.
        QMetaObject::invokeMethod(
            target.data(),
            [target, query] {
                if (target)
                    Q_EMIT target->received(query);
            },
            Qt::QueuedConnection);
    }
};

} // namespace brulion::platform

EMSCRIPTEN_BINDINGS(brulion_wasm_share)
{
    emscripten::function("brulionShareEvent", &brulion::platform::WasmShareJs::event);
}

// JavaScript in EM_JS: semicolons everywhere, double quotes only (see wasmstorage.cpp).
// The channel's name and messages are wasm/index.html's (forwardShare): keep them in sync.
// Only the tab that holds the storage's lock answers (Module.brulionStorage, wasmstorage.cpp): a
// tab waiting for it ("open in another tab") would show the share a second time later.
EM_JS(void, brulion_share_listen, (), {
    if (Module["brulionShareListening"]) return;
    Module["brulionShareListening"] = true;
    if (typeof BroadcastChannel === "undefined" || typeof location === "undefined" || typeof URL === "undefined") return;
    var channel;
    try {
        channel = new BroadcastChannel("brulion-share:" + new URL(".", location.href).pathname);
    } catch (e) {
        console.warn("Brulion share: no channel for shares from other tabs", e);
        return;
    }
    Module["brulionShareChannel"] = channel;
    var holdsStorage = function() {
        var st = Module["brulionStorage"];
        if (!st || st.failed || st.waitingForLock || st.lockReleased) return false;
        var locks = typeof navigator !== "undefined" && navigator.locks && navigator.locks.request;
        return !locks || !!st.releaseLock;
    };
    channel.onmessage = function(event) {
        var d = event.data;
        if (!d || d.type !== "share" || typeof d.query !== "string" || d.query.length > 8388608) return;
        if (!holdsStorage()) return;
        try {
            Module["brulionShareEvent"](d.query);
        } catch (e) {
            console.error("Brulion share: callback failed", e);
            return;
        }
        try {
            channel.postMessage({ type: "taken", id: String(d.id || "") });
        } catch (e) {
            console.error("Brulion share: no answer to the other tab", e);
        }
    };
});

namespace brulion::platform {

void WasmShare::listen()
{
    brulion_share_listen();
}

WasmShare::WasmShare(QObject *parent)
    : QObject(parent)
{
    listen();
    WasmShareJs::instances.removeAll(nullptr);
    WasmShareJs::instances.append(this);
    const QStringList pending = std::exchange(WasmShareJs::pending, {});
    for (const QString &query : pending)
        WasmShareJs::deliver(this, query);
}

WasmShare::~WasmShare()
{
    WasmShareJs::instances.removeAll(this);
}

} // namespace brulion::platform
