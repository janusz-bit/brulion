// openexternal.h in the browser: a new tab with a blob: URL (wasmopen.h), a download when blocked.
#include "wasm/wasmopen.h"

#include "openexternal.h"
#include "wasm/wasmfiles.h"

#include <emscripten.h>

// `'_blank'` without "noopener" in the features: with it window.open always returns null and a
// blocked tab could not be told apart; the opener is cut right after. Blob URLs die with the
// document, so pagehide needs no handler.
EM_JS(int, brulion_open_blob, (const char *data, int size, const char *mime, const char *name), {
    const bytes = HEAPU8.slice(data, data + size);
    const url = URL.createObjectURL(new File([bytes], UTF8ToString(name), { type: UTF8ToString(mime) }));
    const tab = window.open(url, '_blank');
    if (!tab) {
        URL.revokeObjectURL(url);
        return 0;
    }
    try { tab.opener = null; } catch (e) {}
    const kept = (globalThis.__brulionBlobUrls = globalThis.__brulionBlobUrls || []);
    kept.push(url);
    while (kept.length > 3)
        URL.revokeObjectURL(kept.shift());
    globalThis.__brulionLastBlobUrl = url;
    return 1;
});

namespace brulion::platform {

namespace wasm {

bool openBlobInNewTab(const QByteArray &bytes, const QString &mimeType, const QString &name)
{
    return brulion_open_blob(bytes.constData(), int(bytes.size()), mimeType.toUtf8().constData(),
                             name.toUtf8().constData())
           != 0;
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
