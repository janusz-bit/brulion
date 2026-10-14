#include "wasmshell.h"

#include <emscripten/em_js.h>

// The shell defines globalThis.brulionShell = { stage(name), done() } (wasm/index.html).
// JavaScript in EM_JS: semicolons everywhere, double quotes only (see wasmstorage.cpp).
EM_JS(void, brulion_shell_stage, (int stage), {
    var shell = globalThis["brulionShell"];
    if (shell && shell.stage) shell.stage(stage === 0 ? "storage" : "ui");
});

EM_JS(void, brulion_shell_done, (), {
    var shell = globalThis["brulionShell"];
    if (shell && shell.done) shell.done();
});

namespace brulion::platform {

void setShellStage(ShellStage stage)
{
    brulion_shell_stage(stage == ShellStage::LoadingNotes ? 0 : 1);
}

void hideShellLoadingScreen()
{
    brulion_shell_done();
}

} // namespace brulion::platform
