// Password-protected sites (docs/UI.md "Publishing" → "Password-protected pages"): a built site
// (publish/site.h) turned into one that holds only ciphertext, opened with the password in the
// browser through WebCrypto. Part of the publish module — see publish/publish.h.
//
// The scheme (version 1):
//   K       = PBKDF2-HMAC-SHA256(password as UTF-8, the site's salt, iterations, 32 bytes) — once a
//             session in the browser (remembered in sessionStorage as K, never the password)
//   a page  index.html stays index.html, every other page goes to p/<name>.html, every other file
//           (pictures, attachments) to f/<name>, with <name> = the first 10 bytes of
//           HMAC-SHA256(K, "name" 0x00 old path) in hex — the names tell nothing without the
//           password; style.css and .nojekyll stay as they are (the site's own, no note content)
//   sealing salt = HMAC-SHA256(K, "salt" 0x00 new path)[0..16), key = HMAC-SHA256(K, salt),
//           iv = HMAC-SHA256(key, plaintext)[0..12) (deterministic: an unchanged page is the same
//           bytes, so the incremental publish sends it once), AES-256-GCM, no AAD, 16-byte tag
//   a page  the lock page: a generic title, the password form and an inline script (no external
//           resources, no eval, no inline handlers), the sealed page in
//           <script type="application/json" id="brulion-sealed">{"v":1,"kdf":"PBKDF2","hash":
//           "SHA-256","iterations":N,"salt":B64,"page":B64 (sealing salt),"iv":B64,"data":B64
//           (ciphertext + tag)}</script>; the plaintext is the page's HTML with its links
//           rewritten to the new names (a picture's src / a file's href as data-sealed-src /
//           data-sealed-href, which the script fills with the decrypted file's blob: URL)
//   a file  "BRL1", sealing salt (16), iv (12), ciphertext + tag of: {"name":…,"type":…} (JSON, one
//           line), "\n", the file's bytes
#pragma once

#include "crypto/random.h"
#include "io/files.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace brulion::publish {

/// PBKDF2-HMAC-SHA256 rounds of a protected site: OWASP's figure for SHA-256, the strength of the
/// 220 000 SHA-512 rounds of the encrypted notes (encryption/meld.h).
inline constexpr quint32 ProtectIterations = 600000;
/// Bytes of a site's salt.
inline constexpr qsizetype ProtectSaltSize = 16;
/// What a sealed file starts with.
inline constexpr char ProtectFileMagic[] = "BRL1";

struct SiteProtection {
    /// "" — no password: protectSite() refuses ("no-page-password").
    QString password;
    /// The site's salt (PublishTarget::protectSalt decoded); shorter than ProtectSaltSize: a
    /// random one from `random` (the pages then change with every build).
    QByteArray salt;
    quint32 iterations = ProtectIterations;
    crypto::RandomSource random;
};

/// The lock page's words (translated: lockTexts()).
struct LockTexts {
    QString title;    ///< "Protected page" — the page's <title> and heading
    QString prompt;   ///< "Type the password to read this page."
    QString password; ///< the field's label
    QString remember; ///< "Remember it until the browser is closed"
    QString open;     ///< the button
    QString opening;  ///< "Opening…"
    QString wrong;    ///< "Wrong password."
    QString insecure; ///< no WebCrypto (plain http)
    QString noScript; ///< JavaScript off
};
LockTexts lockTexts();

struct ProtectedSite {
    QList<io::FileEntry> files; ///< the site's new files (paths as above)
    QString errorCode;          ///< "" | "no-page-password"
};

/// Protect the built site `files` (site-relative paths, as io::htmlSiteFiles() or the one-document
/// export make them, without the search's files): pure, any thread (one PBKDF2: a fraction of a
/// second).
ProtectedSite protectSite(const QList<io::FileEntry> &files, const SiteProtection &protection, const LockTexts &texts);

} // namespace brulion::publish
