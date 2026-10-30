// brulion index module — the in-memory vault index and the query language. Include this header
// to get the whole module (namespace brulion; never reads the clock — callers pass `today`).
//
// API overview:
//
//   vaultindex.h   class VaultIndex : QObject — incremental index of a vault's notes
//                    VaultIndex(VaultConfig = {}, QObject *parent = nullptr); setConfig(config)
//                    update(path, content) (Markdown parsed with markdown::parseNote, other files
//                    remembered as attachments; ignored paths / conflict copies skipped),
//                    remove(path), rename(old, new, content), clear(),
//                    beginBatch()/endBatch() (+ RAII VaultIndex::Batch), version(), flushSearch()
//                    Q_SIGNAL changed() — coalesced: once per event-loop turn (zero timer) or at
//                    the endBatch() closing the outermost batch
//                  change log: changesSince(version) -> IndexChanges{complete, notes (changed,
//                           relinked, showing a changed block's ((id))), names}, serial(); the
//                           derived lists below are patched per change, not built again
//                  notes:   note(path) -> const ParsedNote* | nullptr, paths(), attachments(),
//                           contains(path), content(path)
//                  links:   resolveLink(target, from = {}) -> optional<VaultPath> (SYNTAX §2:
//                           exact path ± .md case-insensitive NFC, relative to the source folder,
//                           base name — shortest path, then alphabetical —, aliases; attachments
//                           for ![[img.png]]), outgoingLinks(path) -> ResolvedLink{link, target},
//                           backlinks(path) -> Backlink{path, link, blockIndex} (incl. ((id)) refs
//                           to blocks of the page; self excluded; for a page not written yet the
//                           links that will resolve to it), linksTo(path) (files to rewrite on
//                           rename), unlinkedReferences(path) -> UnlinkedRef{path, line,
//                           excerpt} (title/aliases as whole words, ≤ 200), missingPages(),
//                           linkTarget(path, from) (the shortest [[target]] reaching `path`)
//                  moves:   planRelocation([FileMove{from, to}]) -> RelocationPlan{files{path,
//                           newPath, before, after, links, tags}, rewrite(path, content)} —
//                           the notes to rewrite so every link and tag keeps its target
//                           (relocate.cpp; SYNTAX §2), tagTarget(path) (the shortest tag naming it)
//                  tags as page links (SYNTAX §3): resolveTag(tag) -> optional<VaultPath> (like
//                           [[tag]] at the vault root, notes only), tagsNaming(path) (the tag
//                           keys naming it; for a missing page those that would), tagReferences(path) ->
//                           TagReference{path, blockIndex (-1 = whole page), tag} (blocks and
//                           pages tagged with a tag naming `path`; self excluded; a missing page
//                           by its name), tagsTo(path) (files to rewrite on rename)
//                  blocks:  blockById(id) -> optional<BlockRef{path, blockIndex}>
//                  lists:   tags() -> TagCount{tag, count, paths, display}, tasks() -> TaskRef{path,
//                           blockIndex, task}, reminders() -> ReminderRef{path, blockIndex,
//                           reminder, task?}, journals() -> JournalRef{date, path} newest first;
//                           tasksOf(path), remindersOf(path) (one note's)
//                  search:  search(query, limit = 50) -> SearchHit{path, title, score, line, excerpt}
//                           suggestPages(prefix, limit = 20) -> PageSuggestion{path, title, alias}
//                           suggestBlocks(text, limit = 20) -> BlockRef
//                  page properties: propertyKeys() -> PropertyKeyInfo{key, count, type} (keys
//                           of all notes' page properties, most used first),
//                           propertyValues(key, prefix, limit) -> PropertyValueCount{value, count}
//                           (the values written for a key; the "Właściwości" panel's suggestions)
//                  graph:   graph(includeTags, includeMissing) -> GraphData{nodes{id, title, degree,
//                           isTag, isMissing}, edges{source, target}} (a tag naming a note is an
//                           edge to it; "#tag" nodes only for the others)
//
//   searchindex.h  class SearchIndex — the inverted index behind VaultIndex::search(): fields
//                  title (x3) / aliases (x2) / content, tokens folded with text::foldKey ("zolw"
//                  finds "żółw"), BM25+, prefix matching for the last term, fuzzy (edit distance 1
//                  for terms of 4+ chars, 2 for 8+), AND then OR fallback.
//
//   query.h        parseQuery(source) -> variant<Query, QString error> (SYNTAX §10: type, status,
//                  tag / -tag, page, path, ref, text, due / scheduled / start / done / created /
//                  remind / date with date expressions and <, <=, >, >=, priority, sort, group,
//                  limit; Polish aliases)
//                  runQuery(query, index, today, weekStart = Monday, QueryOptions{currentPath,
//                  now, defaultLimit}) -> QueryResult{type, items (QueryItem{path, blockIndex,
//                  task?, reminder?}), groups{key, label, items}, total}
//   querycache.h   QueryCache — runQuery() kept across index changes: update(query, index, today,
//                  weekStart, options) evaluates only the notes changed since (everything for
//                  another query / day / page, page: and ref: filters after name changes, and
//                  remind: filters), result(index), run(), itemsOf(path, index), total(),
//                  lastSync()
//   notecache.h    following the index (header-only): IndexFollower::follow(index, key,
//                  dependsOnNames) -> NoteCacheSync{full, changed}; NoteCache<Value>::sync(index,
//                  key, dependsOnNames, compute, keep), values() (by path) — what QueryCache,
//                  reminders' CalendarCache and NotificationCache are built on
//
//   loading elsewhere (notebook::IndexLoader): VaultIndex::prepare(path, content, config) /
//                  prepareParsed() -> PreparedNote (any thread), updatePrepared(prepared,
//                  sequence), reserveSequence(n), adoptSearch(SearchIndex) (documents kept by
//                  content key), setSearchBuiltElsewhere(on), contentKey(text), parseFingerprint;
//                  SearchIndex::prepare(title, aliases, content) -> Document, set(id, doc, key)
//   indexcache.h   the index cache on the device (docs/PERFORMANCE.md L2): Identity::current(config,
//                  vaultId), Reader::open(bytes, identity, &why) -> find(path) -> Record{size,
//                  contentKey, …}, decodeNote(record, path, content, config) (any thread),
//                  searchDecoder() (in steps); Writer(index, identity, stat).step(ms, &bytes)
//                  (slices; Aborted when the index changes), encode(index, identity, stat)
//
// Not thread-safe: use a VaultIndex from one thread (the static prepare functions and the cache's
// decoder excepted). Results are snapshots (copies); pointers from note() stay valid until the
// next change.
#pragma once

#include "index/indexcache.h"
#include "index/query.h"
#include "index/searchindex.h"
#include "index/vaultindex.h"
