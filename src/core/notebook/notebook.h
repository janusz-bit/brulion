// brulion::notebook — what the app and the command line do to a notebook, GUI-free, so the two
// behave identically (Workspace delegates here; brulion-cli and its MCP server are built on it).
// Include this header to get the whole module.
//
// API overview (namespace brulion::notebook; never reads the clock — callers pass `now`):
//
//   capture.h    buildCaptureLine(text, now, weekStart, locale) -> CaptureLine{kind note|reminder|
//                due, block, text, when} ("Kupić mleko jutro o 18" -> "- [ ] Kupić mleko ⏰ …")
//   journals.h   journalFileFor(vault, date) (also under another accepted name), existingJournal,
//                ensureJournal(vault, date, now, locale) (from the journal template, never over a
//                note another program just created), newJournalText (what it starts with),
//                templateText, pure appendBlock(content, block) (fills a template's empty last
//                bullet, the note's own line ending) and blockFromText(text), contentLocale("pl" |
//                "en"), appLanguage(saved ui/language) (else the system's)
//   pages.h      newPagePath, newPageText, createPage (letter-case twins are one page),
//                createFolder, folderPaths, folderInfo, specialsWithin; renames and moves through
//                relocate() (VaultIndex::planRelocation(): every link and tag keeps its target,
//                special folders rebase .brulion/config.json), relocationImpact (counts and the
//                notes' rewritten texts, for a dry run), renamePage, renameFolder,
//                movePath; deletePath (to .trash/); problems as PathProblem kinds
//   links.h      pageLinkTarget(path, config, index) (a daily note's file name, else
//                VaultIndex::linkTarget(): the one page part of every copied link) and
//                blockLinks(content, line, page, isTaken) -> {id, ref ((id)), wikiLink
//                [[Page#^id]], embed, headingLink} — the links the editor copies;
//                copiedLink(url, from, …) -> {text, path}: a link menu's "Kopiuj link"
//   indexloader.h IndexLoader(vault, index, WorkerPool{threads, start}, {cachePath, firstFolder}):
//                start(files), step(budgetMs) -> Done | More | Waiting (+ resultsReady()), finish(),
//                forget(path), configChanged(), stats() — reads and parses on the pool's threads
//                (or takes notes from the index cache), merges in slices on the owner's thread;
//                inline without threads (WebAssembly)
//   indexcachekeeper.h IndexCacheKeeper(vault, index, pool, path, Policy): loaded(stats),
//                saveNow(), saved(bytes, ms) / saveFailed(error) — <device state>/index-cache.bin
//                written in slices after a load that parsed and after quiet changes
//   itemref.h    ItemRef "Dom.md#L12@3f9a01bc" / "Dom.md#^id" / "^id" / "((id))": parse, format,
//                lineHash, resolve(content, ref) -> Ok | Moved | Stale | NotFound
//   json.h       QJsonObject of tasks, reminders, blocks, query results, search hits, calendar
//                items, notifications and the vault config (the keys of the app's maps and of
//                the JSON export); status / priority / kind names and their Polish words
//   session.h    Notebook: open(root, {stateDir, watch, historyReason, workers, indexCache}) without
//                creating a missing folder, a lazily built VaultIndex kept up to date (loaded with
//                IndexLoader, its cache written at exit after a load that parsed many notes),
//                indexStats(), lock() (the device's writers),
//                edit(path, intent, {expectHash, dryRun, mayCreate, initialText}) — compare-and-swap writes
//                with retries and History; deviceStatePath(appData, root), contentHash()
//   share.h      what another app shares ("Udostępnij → Brulion", the web share target, files
//                opened with Brulion on a desktop): sharedContentFromJson / sharedContentToJson (the
//                platform's JSON), sharedContentFromQuery (the share target's query),
//                splitOpenArguments / openedFilesShare / mergeOpenedShares (`--open` files),
//                pageForOpenedFile (a file that is a page of the open notebook),
//                composeSharedText(subject, text, url) ("[Tytuł](url) — tekst"), checkSharedFile /
//                looksBinary (text types only, SharedFileMaxBytes), decodeSharedBytes,
//                sharedPageTitle / sharedPageContent (a new page) and sharedFileBlock (one
//                journal block per file)
#pragma once

#include "notebook/capture.h"
#include "notebook/indexcachekeeper.h"
#include "notebook/indexloader.h"
#include "notebook/itemref.h"
#include "notebook/journals.h"
#include "notebook/json.h"
#include "notebook/links.h"
#include "notebook/pages.h"
#include "notebook/session.h"
#include "notebook/share.h"
