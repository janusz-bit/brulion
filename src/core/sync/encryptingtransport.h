// EncryptingTransport — the end-to-end encrypted sync's layer between a WebDavClient and the
// server (docs/SYNC-ENCRYPTION.md "Where"): an HttpTransport around the network's that encrypts
// every segment of a request's path below the notebook's folder (and a MOVE's Destination), the
// body of a PUT — whole or, where the inner transport streams, as Qt reads it —, decrypts the body
// of a GET — whole, or chunk by chunk into the request's bodySink — and rewrites PROPFIND's answers:
// each href decrypted, each getcontentlength the plain size, every entry that does not decrypt
// (the key file, a README, another app's file, one changed on the server) left out. Above it the
// client and the engine see a plain WebDAV server.
//
// Refused before they reach the server: a path whose name is too long to encrypt (400), a request
// outside the notebook's folder (403). An answer that does not decrypt — a file changed or cut on
// the server, or of another key — becomes 422, never plaintext of it: the engine keeps the
// device's version and reports that file (HttpResponse::reason says why), the others go on.
#pragma once

#include "sync/encryption.h"
#include "sync/http.h"

#include <QString>

#include <memory>
#include <optional>

namespace brulion::sync {

class EncryptingTransport : public HttpTransport {
public:
    /// `inner` (not owned) reaches the server; `baseUrl` is the WebDavClient's (the notebook's
    /// folder); `keys` from the notebook key (encryption::Keys::fromNotebookKey).
    EncryptingTransport(HttpTransport *inner, const QString &baseUrl, encryption::Keys keys);

    void send(const HttpRequest &request, HttpCallback done) override;
    bool streams() const override { return m_inner && m_inner->streams(); }

    /// The request as it goes to the server (tests: what the server sees); nullopt with the
    /// answer it gets instead when it cannot go.
    std::optional<HttpRequest> encrypted(const HttpRequest &request, HttpResponse *refusal) const;

    /// The notebook's folder and keys, shared with the answers still on their way (they may
    /// come after the transport has gone).
    struct Folder {
        QString baseUrl;
        QString basePathname;
        encryption::Keys keys;
    };

private:
    /// A MOVE's Destination encrypted as its URL; false (with the refusal) when it cannot be.
    bool encryptDestination(HttpRequest &request, HttpResponse *refusal) const;
    /// A PUT's body sealed, or its device wrapped to encrypt as it is read.
    void encryptBody(HttpRequest &request) const;
    void sendGet(HttpRequest request, HttpCallback done);

    HttpTransport *m_inner;
    std::shared_ptr<const Folder> m_folder;
};

} // namespace brulion::sync
