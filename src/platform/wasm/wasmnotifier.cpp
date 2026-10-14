#include "wasmnotifier.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLoggingCategory>
#include <QPointer>

#include <emscripten/bind.h>
#include <emscripten/em_js.h>

#include <cstdlib>
#include <string>

Q_LOGGING_CATEGORY(lcWasmNotifier, "brulion.platform.wasm.notifier")

namespace brulion::platform {

namespace {

// Event codes reported from JavaScript (keep in sync with the EM_JS code below).
enum JsEvent { EventActivated = 1, EventPermission = 2 };

WasmNotifier::Permission permissionFromString(const QString &state)
{
    if (state == QLatin1String("granted"))
        return WasmNotifier::Permission::Granted;
    if (state == QLatin1String("denied"))
        return WasmNotifier::Permission::Denied;
    if (state == QLatin1String("default"))
        return WasmNotifier::Permission::Default;
    return WasmNotifier::Permission::Unsupported;
}

} // namespace

/// Bridge for JavaScript -> C++ calls (registered with embind as Module.brulionNotifierEvent).
struct WasmNotifierJs
{
    static inline QList<QPointer<WasmNotifier>> instances;

    static void event(int op, const std::string &a, const std::string &b, int line)
    {
        const QString first = QString::fromStdString(a);
        const QString second = QString::fromStdString(b);
        for (const QPointer<WasmNotifier> &target : std::as_const(instances)) {
            if (!target)
                continue;
            // Called from browser callbacks: continue in Qt's event loop.
            QMetaObject::invokeMethod(
                target.data(),
                [target, op, first, second, line] {
                    if (!target)
                        return;
                    if (op == EventActivated)
                        Q_EMIT target->activated(first, second, line);
                    else if (op == EventPermission)
                        Q_EMIT target->permissionChanged(permissionFromString(first));
                },
                Qt::QueuedConnection);
        }
    }
};

} // namespace brulion::platform

EMSCRIPTEN_BINDINGS(brulion_wasm_notifier)
{
    emscripten::function("brulionNotifierEvent", &brulion::platform::WasmNotifierJs::event);
}

// JavaScript in EM_JS: semicolons everywhere, double quotes only (see wasmstorage.cpp).
// Notification data { brulion, id, path, line } is shared with wasm/sw.js
// ("brulion:notificationclick" messages to the page).
EM_JS_DEPS(brulion_wasm_notifier, "$UTF8ToString,$stringToNewUTF8");

EM_JS(void, brulion_notifier_install, (), {
    if (Module["brulionNotifierInstalled"]) return;
    Module["brulionNotifierInstalled"] = true;
    Module["brulionNotifications"] = {};
    globalThis["brulionNotifications"] = Module["brulionNotifications"];
    var send = function(op, a, b, line) {
        try {
            Module["brulionNotifierEvent"](op, String(a || ""), String(b || ""), line);
        } catch (e) {
            console.error("Brulion notifier: callback failed", e);
        }
    };
    if (typeof navigator === "undefined") return;
    if (navigator.serviceWorker) {
        navigator.serviceWorker.addEventListener("message", function(event) {
            var d = event.data;
            if (!d || d.type !== "brulion:notificationclick") return;
            send(1, d.id, d.path, typeof d.line === "number" ? d.line : -1);
        });
        if (navigator.serviceWorker.startMessages) navigator.serviceWorker.startMessages();
    }
    if (navigator.permissions && navigator.permissions.query && typeof Notification !== "undefined") {
        navigator.permissions.query({ name: "notifications" }).then(function(status) {
            status.onchange = function() { send(2, Notification.permission, "", -1); };
        }).catch(function() {});
    }
});

EM_JS(int, brulion_notifier_permission, (), {
    if (typeof Notification === "undefined") return 0;
    var p = Notification.permission;
    return p === "granted" ? 2 : (p === "denied" ? 3 : 1);
});

EM_JS(void, brulion_notifier_request, (), {
    var answered = false;
    var answer = function(p) {
        if (answered) return;
        answered = true;
        try {
            Module["brulionNotifierEvent"](2, String(p || "default"), "", -1);
        } catch (e) {
            console.error("Brulion notifier: callback failed", e);
        }
    };
    if (typeof Notification === "undefined") {
        answer("unsupported");
        return;
    }
    try {
        var promise = Notification.requestPermission(answer);
        if (promise && promise.then) {
            promise.then(answer, function() { answer(Notification.permission); });
        }
    } catch (e) {
        answer(Notification.permission);
    }
});

EM_JS(int, brulion_notifier_show,
      (const char *idUtf8, const char *titleUtf8, const char *bodyUtf8, const char *pathUtf8,
       int line, int alarm),
      {
    if (typeof Notification === "undefined" || Notification.permission !== "granted") return 0;
    var id = UTF8ToString(idUtf8);
    var title = UTF8ToString(titleUtf8);
    var path = UTF8ToString(pathUtf8);
    var options = {
        body: UTF8ToString(bodyUtf8),
        tag: "brulion:" + id,
        renotify: true,
        requireInteraction: !!alarm,
        icon: "icons/icon-192.png",
        badge: "icons/badge-96.png",
        lang: (typeof document !== "undefined" && document.documentElement.lang) || "pl",
        data: { brulion: true, id: id, path: path, line: line }
    };
    if (alarm) options.vibrate = [400, 200, 400, 200, 800];
    var store = Module["brulionNotifications"] || (Module["brulionNotifications"] = {});
    try {
        var n = new Notification(title, options);
        n.onclick = function(event) {
            if (event && event.preventDefault) event.preventDefault();
            n.close();
            try { window.focus(); } catch (e) {}
            try {
                Module["brulionNotifierEvent"](1, id, path, line);
            } catch (e) {
                console.error("Brulion notifier: callback failed", e);
            }
        };
        n.onclose = function() { if (store[id] === n) delete store[id]; };
        store[id] = n;
        return 1;
    } catch (e) {
        if (typeof navigator === "undefined" || !navigator.serviceWorker) {
            console.error("Brulion: cannot show a notification", e);
            return 0;
        }
        navigator.serviceWorker.getRegistration().then(function(registration) {
            if (!registration) throw new Error("no service worker registered");
            return registration.showNotification(title, options);
        }).catch(function(err) {
            console.error("Brulion: cannot show a notification", err);
        });
        return 1;
    }
});

EM_JS(void, brulion_notifier_close, (const char *idUtf8), {
    var id = UTF8ToString(idUtf8);
    var store = Module["brulionNotifications"];
    if (store && store[id]) {
        store[id].close();
        delete store[id];
    }
    if (typeof navigator === "undefined" || !navigator.serviceWorker) return;
    navigator.serviceWorker.getRegistration().then(function(registration) {
        if (!registration) return;
        return registration.getNotifications({ tag: "brulion:" + id }).then(function(list) {
            list.forEach(function(n) { n.close(); });
        });
    }).catch(function() {});
});

EM_JS(char *, brulion_notifier_take_launch, (), {
    if (typeof location === "undefined" || typeof URL === "undefined") return 0;
    var url = new URL(location.href);
    var path = url.searchParams.get("brulion-note");
    if (path === null) return 0;
    var result = {
        id: url.searchParams.get("brulion-id") || "",
        path: path,
        line: parseInt(url.searchParams.get("brulion-line") || "-1", 10)
    };
    url.searchParams.delete("brulion-id");
    url.searchParams.delete("brulion-note");
    url.searchParams.delete("brulion-line");
    try { history.replaceState(history.state, "", url.href); } catch (e) {}
    return stringToNewUTF8(JSON.stringify(result));
});

namespace brulion::platform {

WasmNotifier::WasmNotifier(QObject *parent)
    : QObject(parent)
{
    WasmNotifierJs::instances.removeAll(nullptr);
    WasmNotifierJs::instances.append(this);
    brulion_notifier_install();
}

WasmNotifier::~WasmNotifier()
{
    WasmNotifierJs::instances.removeAll(this);
}

bool WasmNotifier::isSupported()
{
    return brulion_notifier_permission() != 0;
}

WasmNotifier::Permission WasmNotifier::permission() const
{
    switch (brulion_notifier_permission()) {
    case 1:
        return Permission::Default;
    case 2:
        return Permission::Granted;
    case 3:
        return Permission::Denied;
    default:
        return Permission::Unsupported;
    }
}

bool WasmNotifier::takeLaunchActivation(Activation *activation)
{
    char *json = brulion_notifier_take_launch();
    if (!json)
        return false;
    const QJsonObject object = QJsonDocument::fromJson(QByteArray(json)).object();
    std::free(json);
    if (activation) {
        activation->id = object.value(QLatin1String("id")).toString();
        activation->path = object.value(QLatin1String("path")).toString();
        const double line = object.value(QLatin1String("line")).toDouble(-1);
        activation->line = line >= 0 && line < 1e9 ? static_cast<int>(line) : -1;
    }
    return true;
}

void WasmNotifier::requestPermission()
{
    brulion_notifier_request();
}

bool WasmNotifier::showNow(const QString &id, const QString &title, const QString &body,
                           const QString &path, int line, bool alarm)
{
    const bool shown = brulion_notifier_show(id.toUtf8().constData(), title.toUtf8().constData(),
                                             body.toUtf8().constData(), path.toUtf8().constData(),
                                             line, alarm ? 1 : 0)
        != 0;
    if (!shown)
        qCWarning(lcWasmNotifier) << "notification not shown (no permission):" << id;
    return shown;
}

void WasmNotifier::close(const QString &id)
{
    brulion_notifier_close(id.toUtf8().constData());
}

} // namespace brulion::platform
