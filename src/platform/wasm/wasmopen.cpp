// openexternal.h in the browser: a new tab with a blob: URL (wasmopen.h), a download when blocked.
#include "wasm/wasmopen.h"

#include "openexternal.h"
#include "wasm/wasmfiles.h"

#include <emscripten.h>

// JavaScript side (the code is a macro argument, stringified into one line: every statement needs a
// semicolon, no comments, no backtick strings, double quotes only — see wasmstorage.cpp). Opened
// with "_blank" and no "noopener" feature: with it window.open always returns null and a blocked tab
// could not be told apart; the opener is cut right after. The blob is typed explicitly (a file whose
// bytes are HTML never becomes a page of Brulion's origin). The last three blob URLs are kept (the
// browser viewer's reload and "Save" need theirs), older ones revoked; they die with the document,
// so pagehide needs no handler.
EM_JS_DEPS(brulion_wasm_open, "$UTF8ToString");
EM_JS(int, brulion_open_blob, (const char *data, int size, const char *mime, const char *name), {
    var bytes = HEAPU8.slice(data, data + size);
    var url = URL.createObjectURL(new File([bytes], UTF8ToString(name), {type : UTF8ToString(mime)}));
    var tab = window.open(url, "_blank");
    if (!tab) {
        URL.revokeObjectURL(url);
        return 0;
    }
    try {
        tab.opener = null;
    } catch (e) {
    }
    var kept = globalThis.__brulionBlobUrls || [];
    globalThis.__brulionBlobUrls = kept;
    kept.push(url);
    while (kept.length > 3) {
        URL.revokeObjectURL(kept.shift());
    }
    return 1;
});

// "Print…" (ExportController::print): the HTML document export in a hidden frame and its print().
// The frame is sandboxed without scripts (the document has none; same origin only so that this
// page may call its print()); the previous one is removed first.
EM_JS(void, brulion_print_html, (const char *data, int size), {
    var html = UTF8ToString(data, size);
    var old = document.getElementById("brulion-print-frame");
    if (old) {
        old.remove();
    }
    var frame = document.createElement("iframe");
    frame.id = "brulion-print-frame";
    frame.setAttribute("sandbox", "allow-same-origin allow-modals");
    frame.setAttribute("aria-hidden", "true");
    frame.setAttribute("tabindex", "-1");
    frame.style.position = "fixed";
    frame.style.right = "0";
    frame.style.bottom = "0";
    frame.style.width = "0";
    frame.style.height = "0";
    frame.style.border = "0";
    frame.onload = function() {
        try {
            frame.contentWindow.focus();
            frame.contentWindow.print();
        } catch (e) {
            console.warn("Brulion: printing failed", e);
        }
    };
    frame.srcdoc = html;
    document.body.appendChild(frame);
});

namespace brulion::platform {

namespace wasm {

bool openBlobInNewTab(const QByteArray &bytes, const QString &mimeType, const QString &name)
{
    return brulion_open_blob(bytes.constData(), int(bytes.size()), mimeType.toUtf8().constData(),
                             name.toUtf8().constData())
           != 0;
}

void printHtml(const QByteArray &html)
{
    brulion_print_html(html.constData(), int(html.size()));
}

} // namespace wasm

namespace detail {

OpenResult openFileExternallyNow(const QString &absolutePath, const QString &mimeType, const QString &displayName,
                                 const QByteArray &bytes)
{
    Q_UNUSED(absolutePath);
    if (bytes.isEmpty())
        return OpenResult::Failed;
    if (wasm::openBlobInNewTab(bytes, mimeType, displayName))
        return OpenResult::Opened;
    wasm::saveFile(bytes, displayName);
    return OpenResult::Blocked;
}

} // namespace detail

} // namespace brulion::platform
