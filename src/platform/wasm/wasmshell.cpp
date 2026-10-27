#include "wasmshell.h"

#include <QCoreApplication>

#include <emscripten/bind.h>
#include <emscripten/em_js.h>

#include <cmath>
#include <utility>

// The shell defines globalThis.brulionShell = { stage(name), done(), indexed() }
// (wasm/index.html). JavaScript in EM_JS: semicolons everywhere, double quotes only (see
// wasmstorage.cpp).
EM_JS(void, brulion_shell_stage, (int stage), {
    var shell = globalThis["brulionShell"];
    if (shell && shell.stage) shell.stage(stage === 0 ? "storage" : "ui");
});

EM_JS(void, brulion_shell_done, (), {
    var shell = globalThis["brulionShell"];
    if (shell && shell.done) shell.done();
});

EM_JS(void, brulion_shell_indexed, (), {
    var shell = globalThis["brulionShell"];
    if (shell && shell.indexed) shell.indexed();
});

EM_JS(int, brulion_shell_page_scheme, (), {
    var location = globalThis["location"];
    var protocol = location ? String(location.protocol) : "";
    return protocol === "https:" ? 1 : protocol === "http:" ? 2 : 0;
});

// The keyboard: window.visualViewport's resize and scroll events report
// innerHeight - height - offsetTop to Module.brulionViewportEvent (below), 0 for a pinch-zoomed
// page. iOS scrolls the page to the input element Qt keeps for the keyboard: the page is put back
// at the top (best effort).
EM_JS(int, brulion_shell_watch_viewport, (), {
    var vv = globalThis["visualViewport"];
    if (!vv || !vv.addEventListener) return 0;
    var last = -1;
    var send = function() {
        var overlap = Math.max(0, (globalThis["innerHeight"] || 0) - vv.height - vv.offsetTop);
        if (!isFinite(overlap)) overlap = 0;
        // A pinch-zoomed page (a desktop touchpad) shrinks the visual viewport too: no keyboard.
        if (vv.scale > 1.01) overlap = 0;
        if (Math.abs(overlap - last) < 0.5) return;
        last = overlap;
        try {
            Module["brulionViewportEvent"](overlap);
        } catch (e) {
            console.error("Brulion keyboard: callback failed", e);
        }
    };
    var onScroll = function() {
        if ((globalThis["scrollY"] || 0) > 0 && globalThis["scrollTo"]) globalThis["scrollTo"](0, 0);
        send();
    };
    vv.addEventListener("resize", send);
    vv.addEventListener("scroll", onScroll);
    globalThis.addEventListener("resize", send);
    send();
    return 1;
});

namespace brulion::platform {

namespace {

std::function<void(double)> &viewportCallback()
{
    static std::function<void(double)> callback;
    return callback;
}

} // namespace

/// JavaScript -> C++ (registered with embind as Module.brulionViewportEvent).
struct WasmShellJs
{
    static void viewportEvent(double overlap)
    {
        const double value = std::isfinite(overlap) && overlap > 0 ? overlap : 0;
        // Called from browser events: continue in Qt's event loop.
        QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [value] {
                    if (viewportCallback())
                        viewportCallback()(value);
                },
                Qt::QueuedConnection);
    }
};

void watchVisualViewport(std::function<void(double overlap)> callback)
{
    viewportCallback() = std::move(callback);
    brulion_shell_watch_viewport();
}

void setShellStage(ShellStage stage)
{
    brulion_shell_stage(stage == ShellStage::LoadingNotes ? 0 : 1);
}

void hideShellLoadingScreen()
{
    brulion_shell_done();
}

void reportIndexReady()
{
    brulion_shell_indexed();
}

QString pageScheme()
{
    switch (brulion_shell_page_scheme()) {
    case 1: return QStringLiteral("https");
    case 2: return QStringLiteral("http");
    default: return {};
    }
}

} // namespace brulion::platform

EMSCRIPTEN_BINDINGS(brulion_wasm_shell)
{
    emscripten::function("brulionViewportEvent", &brulion::platform::WasmShellJs::viewportEvent);
}
