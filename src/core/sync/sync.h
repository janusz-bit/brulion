// Sync — Nextcloud/WebDAV sync (client, engine with three-way merge, scheduler) and Syncthing
// helpers (REST client, conflict copies, .stignore). Module "sync"; deps: util, text, vault;
// QtCore + QtNetwork. Everything is single-threaded on the owner's thread and asynchronous
// through callbacks (std::function), never exceptions; tests drive it without a network through
// an in-memory WebDAV server (tests/core/sync/fakewebdavserver.h).
//
// API overview (namespace brulion::sync)
//
//   HTTP                                                                         sync/http.h
//     HttpRequest{method, url, headers, body, timeoutMs}, HttpResponse{status, headers (lower-
//     case), body, networkError, timedOut}; HttpTransport::send(request, done) — the seam for
//     tests; NetworkHttpTransport (QNetworkAccessManager: any verb, time limit, same-host
//     redirects only).
//
//   WebDAV                                              sync/webdavclient.h, sync/errors.h
//     WebDavClient(baseUrl, user, password, transport): test(cb), list(ListOptions, cb)
//     (recursive Depth-1 PROPFINDs; the server's NFD spelling of listed names is kept for
//     requests, also for new files inside such folders), stat(path, cb), get(path, GetOptions, cb),
//     put(path, data, PutOptions{ifMatch, ifNoneMatch, mtime}, cb) (creates parent folders),
//     remove(path, RemoveOptions, cb), mkcol(path, cb), ensureBase(cb), identity().
//     WebDavError{code, status, message, url}: Auth, Forbidden, NotFound, Precondition (412),
//     Network, Timeout, Parse, …; errorCodeName(code) -> "auth", "not-found", …
//
//   URLs and XML                                                    sync/url.h, sync/davxml.h
//     nextcloudDavUrl(server, user, folder) -> ".../remote.php/dav/files/<user>/<folder>",
//     encodePath, joinUrl, urlPathname, hrefPathname, relativeTo, normalizeEtag, quoteEtag,
//     basicAuth; propfindBody(), parseMultistatus(xml), parseStatusLine, parseHttpDate.
//
//   Merging                                                         sync/merge.h, sync/diff.h
//     merge3(base, local, remote) -> MergeResult{text, clean, hunks}: diff3 with a union of
//     concurrent insertions (journal appends on two devices merge cleanly), local EOL style and
//     a three-way merged final newline; mergeJson3() key-wise for .brulion/config.json;
//     diffLines/diffComm/diff3Regions (Myers with a work budget).
//
//   Engine                                              sync/syncengine.h, sync/synctypes.h
//     SyncEngine(vault, client, store, device): sync(options, done) / cancel() / isSyncing(),
//     signals started(), progress(SyncProgress), finished(SyncReport); resetState(),
//     stateOf(path), mergeBaseOf(path); setClock/setConcurrency/setMaxFileSize/setMaxBaseSize.
//     SyncReport{uploaded, downloaded, deletedLocal, deletedRemote, merged, conflicts,
//     conflictCopies, errors, skipped, changes, started, finished, cancelled, remoteError,
//     syncError, needsConfirmation, pendingDeletions}. Mass-deletion guard: > 25 % and >= 10
//     files of a side (or all of them) -> no deletion applied, needsConfirmation; confirm with
//     SyncRunOptions::confirmDeletions. A vault whose folder is missing / on another storage is
//     refused (SyncErrorCode::VaultUnavailable). Pure helpers: planSync(SyncFacts),
//     isSyncablePath, isSyncTextPath, decodeSyncText, groupByCase, remoteVersion.
//
//   Scheduling                                                             sync/scheduler.h
//     SyncScheduler(SyncRunner*): start()/stop()/syncNow(cb)/syncNowConfirmingDeletions(cb)/
//     notifyLocalChange()/
//     notifyChanges(vault changes)/networkOnline()/networkOffline()/setAppActive(bool);
//     status() -> SyncStatus{state Disabled|Idle|Syncing|Error|Offline, lastSync, lastError,
//     lastErrorCode, lastReport, progress, nextSync}, signal statusChanged(); injectable
//     SchedulerTimers and online check, or useSystemNetworkInformation(), else the state last
//     reported through networkOnline()/networkOffline().
//
//   Conflicts and Syncthing                                   sync/conflicts.h, sync/syncthing.h
//     findConflicts(files) (Syncthing ".sync-conflict-" + Brulion ".conflict-" copies),
//     conflictCopyPath, parseBrulionConflict, sanitizeDeviceName; SyncthingClient(apiKey,
//     transport, url = http://127.0.0.1:8384): ping, folders, connections, status(folderId),
//     findFolderForPath(absPath); findFolderForPath(), completionPercent(), stignoreTemplate().
#pragma once

#include "sync/conflicts.h"
#include "sync/davxml.h"
#include "sync/diff.h"
#include "sync/errors.h"
#include "sync/http.h"
#include "sync/merge.h"
#include "sync/scheduler.h"
#include "sync/syncengine.h"
#include "sync/synctypes.h"
#include "sync/syncthing.h"
#include "sync/url.h"
#include "sync/webdavclient.h"
