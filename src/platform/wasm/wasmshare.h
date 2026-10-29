#pragma once

// WebAssembly only (compiled when EMSCRIPTEN is set, see src/platform/CMakeLists.txt).

#include <QObject>
#include <QString>

namespace brulion::platform {

struct WasmShareJs;

/// Shares another tab of this browser hands over (docs/WEB.md "Udostępnianie do Brulionu").
///
/// The browser version is in the system's share sheet once installed as an app
/// (wasm/manifest.webmanifest "share_target", GET `./?share-title=…&share-text=…&share-url=…`). A
/// share launch that finds Brulion running in another tab — wasm/index.html: the storage's Web Lock
/// "brulion-storage:…" is held (wasmstorage.cpp) — does not start a second app: it posts
/// `{type: "share", id, query}` on the BroadcastChannel "brulion-share:<the site's path>" (the
/// same name in index.html: keep the two in sync), and the tab holding the lock answers
/// `{type: "taken", id}` once the query reached C++ — then `received(query)`
/// (AppController::receiveShareQuery: notebook::sharedContentFromQuery). A tab still waiting for
/// the lock ignores it.
///
/// listen() installs the channel's listener; main() calls it before the storage asks for its lock,
/// so whenever the lock is held someone answers. Queries arriving before a WasmShare exists (the
/// notes still loading) are kept — at most 8 — and emitted by the first one created.
class WasmShare : public QObject {
    Q_OBJECT

public:
    /// Listen on the channel (once per page; nothing where the browser has no BroadcastChannel).
    static void listen();

    explicit WasmShare(QObject *parent = nullptr);
    ~WasmShare() override;

Q_SIGNALS:
    /// The share target's query another tab handed over ("share-title=…&share-text=…").
    void received(const QString &query);

private:
    friend struct WasmShareJs;
};

} // namespace brulion::platform
