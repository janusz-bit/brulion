// Sync — Nextcloud/WebDAV sync (client, engine with three-way merge, scheduler) and Syncthing
// helpers (REST client, conflict copies, .stignore). Module "sync"; deps: util, text, vault, log;
// QtCore + QtNetwork. Everything is single-threaded on the owner's thread and asynchronous
// through callbacks (std::function), never exceptions; tests drive it without a network through
// an in-memory WebDAV server (tests/core/sync/fakewebdavserver.h).
//
// API overview (namespace brulion::sync)
//
//   HTTP                                                                         sync/http.h
//     HttpRequest{method, url, headers, body, timeoutMs}, HttpResponse{status, headers (lower-
//     case), body, networkError, timedOut, protocol}; HttpTransport::send(request, done) — the
//     seam for tests; NetworkHttpTransport (QNetworkAccessManager: any verb, time limit, same-host
//     redirects only, HttpRequest::onProgress from upload/download progress, "h2"/"http/1.1").
//     HttpExchange{method, url, status, timedOut, elapsedMs, bytes, contentType, protocol}: one
//     finished request for statistics and logs (never headers or bodies).
//
//   WebDAV                                              sync/webdavclient.h, sync/errors.h
//     WebDavClient(baseUrl, user, password, transport): test(cb), list(ListOptions, cb)
//     (recursive Depth-1 PROPFINDs; the server's NFD spelling of listed names is kept for
//     requests, also for new files inside such folders), stat(path, cb), get(path, GetOptions, cb),
//     put(path, data, PutOptions{ifMatch, ifNoneMatch, mtime}, cb) (creates parent folders),
//     remove(path, RemoveOptions, cb), move(from, to, MoveOptions{folder, overwrite}, cb) (WebDAV
//     MOVE, "Overwrite: F" by default, parents of the target created), listFolder(path, cb) (one
//     folder's children), mkcol(path, cb), mkdirs(path, cb), ensureBase(cb), identity().
//     Instrumentation: signal exchanged(HttpExchange), setLogTag("#42") (brulion.net: a debug line
//     per request, info for answers ≥ 5 s, warnings for timeouts / network failures),
//     requestsInFlight(), inFlight() ("PROPFIND Dziennik/"), idleMs(), relativePath(url),
//     ListOptions::onProgress(done, known, entries). The password and the Basic token are
//     registered with logging::addSecret (masked in every log line).
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
//     stateOf(path), mergeBaseOf(path), knowsFolder(path);
//     setClock/setConcurrency/setMaxFileSize/setMaxBaseSize.
//     Folders travel too (empty ones, deletions of emptied ones, a folder renamed in the vault
//     moved on the server with one MOVE); moved content is not counted as deleted by the guards.
//     SyncReport{uploaded, downloaded, deletedLocal, deletedRemote, merged, foldersCreated,
//     foldersDeleted, foldersMoved, folderMoves, conflicts,
//     conflictCopies, errors (SyncFileError{path, message, code, status}), skipped, changes,
//     started, finished, cancelled, remoteError, syncError, needsConfirmation, pendingDeletions,
//     runNumber, trigger, phases (SyncPhaseTime: connect, list-remote, list-local, compare,
//     transfer, save-state), stats (SyncStats: requests per method, bytes, listing bytes, the
//     slowest requests, timeouts, 412 retries, protocol), durationMs}. SyncProgress{phase
//     Connecting|Listing|ListingLocal|Comparing|Syncing|Saving|Done, done, total, path, active,
//     remoteEntries, bytesDone, changes, errors, elapsedMs}. Run numbers ("#42" in log lines):
//     setNextRunNumber(); activity() -> SyncActivity{running, runNumber, idleMs,
//     requestsInFlight, waitingFor}. Log (brulion.sync): "#42 started (edit)", per-file
//     warnings with their code, one summary line per run with phase times and request stats.
//     Mass-deletion guard: > 25 % and >= 10 files of a side (or all of them) -> no deletion
//     applied, needsConfirmation; confirm with SyncRunOptions::confirmDeletions. A vault whose
//     folder is missing / on another storage is refused (SyncErrorCode::VaultUnavailable). Pure
//     helpers: planSync(SyncFacts), isSyncablePath, isSyncTextPath, decodeSyncText, groupByCase,
//     remoteVersion.
//
//   Scheduling                                                             sync/scheduler.h
//     SyncScheduler(SyncRunner*): start()/stop()/syncNow(cb)/syncNowConfirmingDeletions(cb)/
//     notifyLocalChange()/
//     notifyChanges(vault changes)/networkOnline()/networkOffline()/setAppActive(bool);
//     status() -> SyncStatus{state Disabled|Idle|Syncing|Error|Offline, lastSync, lastError,
//     lastErrorCode, lastReport, progress, nextSync, retrying, failures, trigger, runStarted,
//     stalled, stalledMs, waitingFor, abandoning}, signals statusChanged(), progressChanged()
//     (per path: throttle it), runStarted(trigger), runFinished(report, stalled) for every run;
//     every run carries a SyncTrigger (start, timer, retry, manual, edit, network, foreground,
//     background, confirm). Stall watchdog (a poll timer only while syncing): no answer or
//     progress for 20 s -> stalled (+ a log warning), for 180 s -> the run is cancelled and
//     reported as failed with code "stalled", then retried with the back-off; a "still running"
//     line every 30 s. Injectable SchedulerTimers and online check, or
//     useSystemNetworkInformation(), else the state last reported through
//     networkOnline()/networkOffline().
//
//   History                                                                 sync/synchistory.h
//     SyncHistory(LocalStore*) — the last 20 runs (SyncRunRecord: outcome ok | conflicts | files
//     | needs-confirmation | refused | failed | cancelled, error code and redacted message,
//     counts, phases, stats, at most 20 file errors), quiet runs folded together, persisted under
//     "runs" (the app: its own sync-history.json); outcomeOf(), needsUser(), nextNumber().
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
#include "sync/synchistory.h"
#include "sync/syncthing.h"
#include "sync/synctypes.h"
#include "sync/url.h"
#include "sync/webdavclient.h"
