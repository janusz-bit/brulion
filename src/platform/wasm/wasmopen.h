// The browser's new tab for a file of the notebook (openexternal.h): a blob: URL of its bytes,
// typed explicitly, opened with window.open inside the user's gesture. Include as "wasm/wasmopen.h".
#pragma once

#include <QByteArray>
#include <QString>

namespace brulion::platform::wasm {

/// `bytes` (type `mimeType`, named `name`) in a new tab; false when the browser blocked it (the
/// blob URL is revoked then). The last three blob URLs are kept (the browser viewer's reload and
/// "Save" need theirs); older ones are revoked.
bool openBlobInNewTab(const QByteArray &bytes, const QString &mimeType, const QString &name);

} // namespace brulion::platform::wasm
