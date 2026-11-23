// brulion::publish — a page or chosen notes as a website at an address with one command
// (docs/UI.md "Publishing", docs/CLI.md `publish`): GitHub Pages, a WebDAV folder of one's own,
// Nextcloud with a public link (optionally with a password and an expiry date), a folder on this
// device, optionally with password-protected pages. GUI-free; HTTP through sync::HttpTransport
// only; single-threaded (the thread that owns the objects) except buildSite(), which is pure.
// Module "publish"; deps: util, text, markdown, index, io, github, sync, log, chat, crypto;
// QtCore + QtNetwork.
//
// API overview
//
//   selection.h  PublishItem{kind Page | Folder | Tag | Notebook, value}, PublishScope{items,
//                linkedPages, onlyMarked, extensionPages, aiConversations}; selectFiles(index,
//                scope) -> PublishSelection{pages, attachments, leftOut [{path, reason Hidden |
//                Private | NotMarked | Extension | Conversation | Template | Encrypted |
//                ConflictCopy}], missingItems}; publishFlag(note) (`publish:` / `publikuj:`),
//                stripEncrypted(text), isEncryptedFile, isHiddenPath, isExtensionPage
//   target.h     PublishTarget{id, name, kind GitHubPages | WebDav | Nextcloud | Folder,
//                repository, branch, folder, url, server, username, path, share, shareExpires,
//                sharePassword, siteUrl, title, content Site | Document, protect, protectSalt,
//                scope, last (PublishStatus)}; JSON; checkTarget(), identity(), describe(),
//                davUrl(), pagesPath(), defaultPagesUrl(), siteAddress(), newTargetId(),
//                ensureProtectSalt()
//   site.h       buildSite(SiteInput) -> SiteBuild{files [SiteFile{path, data, sha256, gitSha}],
//                pages, attachments, bytes, fragmentsRemoved, encryptedPages, protectedPages,
//                errorCode}: io::htmlSiteFiles (or the HTML document export as index.html),
//                `.nojekyll` for GitHub, password protection (protect.h);
//                prepareSite(index, target, read, name, now) (the command line), siteInputFor(),
//                filesToRead()
//   protect.h    protectSite(files, SiteProtection{password, salt, iterations}, lockTexts()) ->
//                ProtectedSite{files, errorCode}: every page and file encrypted (AES-256-GCM, a key
//                from PBKDF2-HMAC-SHA256), lock pages that open them with WebCrypto
//   manifest.h   PublishManifest{identity, publishedAt, url, commit, shareId, shareUrl,
//                sharePasswordHash, shareExpires, files {path: {sha256, size, remote}}}; JSON
//   plan.h       planPublish(site, manifest, remote, kind, force), planUnpublish() ->
//                PublishPlan{files [{path, action Upload | Update | Unchanged | Delete |
//                Conflict, size, deletion, remote}], remoteChecked} — the dry run
//   publisher.h  Publisher(transport): preview / publish(force) / unpublish(force) ->
//                PublishReport{plan, uploaded, updated, deleted, unchanged, conflicts, failures,
//                url, commit, errorCode, remoteError, warnings, manifest}; progress(phase, done,
//                total); cancel()
//   ocs.h        OcsShareClient: Nextcloud's public links (find, get, create, setPassword,
//                setExpires, remove)
//   store.h      PublishStore(stateDir): targets.json and a manifest per target in the notebook's
//                device state folder; secretFile(), githubTokenFile(), writeSecret(), readSecret()
//   messages.h   errorText(code), warningText(code), reasonText(), actionText(), kindText(),
//                publishedFooter() — translated
//
// Privacy: a published page shows only published pages — its links to others are plain text, its
// backlinks and tag pages list published pages only, its queries and charts count published pages
// only; hidden folders, conflict copies, encrypted pages, files and fragments are never published;
// `publish: false` pages never, extension pages, conversations with AI and templates only when
// chosen; macros of extensions stay as written (they are computed over the whole notebook). Tokens
// and passwords are masked in every log line.
#pragma once

#include "publish/manifest.h"
#include "publish/messages.h"
#include "publish/ocs.h"
#include "publish/plan.h"
#include "publish/protect.h"
#include "publish/publisher.h"
#include "publish/selection.h"
#include "publish/site.h"
#include "publish/store.h"
#include "publish/target.h"
