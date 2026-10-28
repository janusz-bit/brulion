#include "wasmshell.h"

#include <QCoreApplication>

#include <emscripten/bind.h>
#include <emscripten/em_js.h>

#include <limits>
#include <string>
#include <utility>

// The shell defines globalThis.brulionShell = { stage(name), done(), indexed(), keyboardMode, … }
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

// The keyboard: raw readings of the viewports go to Module.brulionViewportEvent (below), which
// hands them to C++ (KeyboardInsets computes the keyboard, src/app/ui/keyboardinsets.h). Every
// event starts a loop over animation frames that reports each change and, once the values stayed
// put for 6 frames (at most 60 frames after the last event), a last reading marked settled: the
// keyboard slides for a few hundred milliseconds and the browser resizes in steps. iOS scrolls the
// page to the input element Qt keeps for the keyboard: the page is put back at the top (the shell
// also pins that element, wasm/index.html).
EM_JS(int, brulion_shell_watch_viewport, (), {
    var vv = globalThis["visualViewport"];
    if (!vv || !vv.addEventListener) return 0;
    var doc = globalThis["document"];
    var container = doc.getElementById("screen") || doc.documentElement;
    var mm = globalThis["matchMedia"];
    var touch = function() {
        if (!mm) return false;
        var nav = globalThis["navigator"] || {};
        return !!(mm("(pointer: coarse)").matches || ((nav.maxTouchPoints || 0) > 0 && mm("(hover: none)").matches));
    };
    var editing = function() {
        var host = doc.getElementById("qt-shadow-container");
        var active = host && host.shadowRoot ? host.shadowRoot.activeElement : null;
        return !!(active && active.classList && active.classList.contains("qt-window-input-element"));
    };
    var read = function(settled) {
        var shell = globalThis["brulionShell"];
        return { innerWidth: globalThis["innerWidth"], innerHeight: globalThis["innerHeight"],
                 height: vv.height, offsetTop: vv.offsetTop, scale: vv.scale,
                 containerHeight: container.getBoundingClientRect().height,
                 touch: touch(), editing: editing(), settled: settled,
                 mode: String((shell && shell.keyboardMode) || globalThis["brulionKeyboardMode"] || "") };
    };
    var keyOf = function(s) {
        return [s.innerWidth, s.innerHeight, s.height, s.offsetTop, s.scale, s.containerHeight]
            .map(function(v) { return Math.round(Number(v) * 2); }).join(",") + "," + s.touch + "," + s.editing;
    };
    var post = function(s) {
        try {
            Module["brulionViewportEvent"](s);
        } catch (e) {
            console.error("Brulion keyboard: callback failed", e);
        }
    };
    var last = "";
    var stable = 0;
    var frames = 0;
    var looping = false;
    var check = function() {
        var s = read(false);
        var k = keyOf(s);
        if (k !== last) {
            last = k;
            stable = 0;
            post(s);
        } else {
            ++stable;
        }
        return s;
    };
    var frame = function() {
        var s = check();
        if (stable >= 6 || ++frames >= 60) {
            looping = false;
            s.settled = true;
            post(s);
            return;
        }
        globalThis["requestAnimationFrame"](frame);
    };
    var trigger = function() {
        frames = 0;
        check();
        stable = 0;
        if (!looping) {
            looping = true;
            globalThis["requestAnimationFrame"](frame);
        }
    };
    vv.addEventListener("resize", trigger);
    vv.addEventListener("scroll", function() {
        if ((globalThis["scrollY"] || 0) > 0 && globalThis["scrollTo"]) globalThis["scrollTo"](0, 0);
        trigger();
    });
    globalThis.addEventListener("resize", trigger);
    doc.addEventListener("focusin", trigger, true);
    doc.addEventListener("focusout", trigger, true);
    if (globalThis["ResizeObserver"]) new ResizeObserver(trigger).observe(container);
    var first = read(true);
    last = keyOf(first);
    post(first);
    return 1;
});

namespace brulion::platform {

namespace {

std::function<void(const BrowserViewport &)> &viewportCallback()
{
    static std::function<void(const BrowserViewport &)> callback;
    return callback;
}

} // namespace

/// JavaScript -> C++ (registered with embind as Module.brulionViewportEvent).
struct WasmShellJs
{
    static void viewportEvent(emscripten::val reading)
    {
        if (reading.isNull() || reading.isUndefined())
            return;
        const auto number = [&reading](const char *key) {
            const emscripten::val v = reading[key];
            return v.isNumber() ? v.as<double>() : std::numeric_limits<double>::quiet_NaN();
        };
        BrowserViewport viewport;
        viewport.innerWidth = number("innerWidth");
        viewport.innerHeight = number("innerHeight");
        viewport.height = number("height");
        viewport.offsetTop = number("offsetTop");
        viewport.scale = number("scale");
        viewport.containerHeight = number("containerHeight");
        viewport.touch = reading["touch"].isTrue();
        viewport.editing = reading["editing"].isTrue();
        viewport.settled = reading["settled"].isTrue();
        const emscripten::val mode = reading["mode"];
        if (mode.isString())
            viewport.mode = QString::fromStdString(mode.as<std::string>()).left(16);
        // Called from browser events: continue in Qt's event loop.
        QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [viewport] {
                    if (viewportCallback())
                        viewportCallback()(viewport);
                },
                Qt::QueuedConnection);
    }
};

void watchVisualViewport(std::function<void(const BrowserViewport &viewport)> callback)
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
