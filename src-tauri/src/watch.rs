//! Native vault watcher: one recursive `notify` watcher per vault root. A background thread
//! batches raw events for ~300 ms, maps them to the frontend's `FsEvent` model and emits
//! `brulion://fs-events` with `{ root, events, rescan? }`.
//!
//! Mapping rules (pure, unit-tested in [`map_events`]):
//! * paths are relative to the root, `/`-separated; anything inside `.git`/`node_modules` and
//!   temp files (`.brulion-tmp-*`, `~syncthing~*.tmp`, `.syncthing.*.tmp`) are dropped;
//! * a rename from a temp file onto a real name (atomic save by Brulion or Syncthing) is
//!   reported as `create` of the target ("the path appeared"; it may replace a known file, so
//!   consumers treat `create`/`modify` as upserts), a rename into an ignored name as `delete`;
//! * inotify's From/To halves are paired via the tracker (cookie); unpaired halves mean the
//!   entry moved out of / into the vault (`delete` / `create`);
//! * events for one path inside a batch collapse into its net effect (created+modified =
//!   create, deleted+recreated = modify, created+deleted = nothing), never across a rename;
//! * `modify` of directories (attribute changes) is dropped;
//! * `rescan: true` is set when events may have been lost (queue overflow, watcher errors, the
//!   root itself removed/moved) — the frontend should re-list the vault.
//!
//! Directories that appear are expanded: their existing children are reported as `create`
//! too (files written before the recursive watch on the new folder was installed).

use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender, TryRecvError};
use std::sync::Mutex;
use std::time::{Duration, Instant};

use notify::event::{ModifyKind, RenameMode};
use notify::{Config, Event, EventKind, RecommendedWatcher, RecursiveMode, Watcher};
use serde::Serialize;
use tauri::{AppHandle, Emitter, Runtime, State};
use walkdir::WalkDir;

use crate::fs_cmds::{blocking, listable_metadata};
use crate::paths::{err, io_err, is_ignored_rel, rel_from_abs, validate_root, SKIPPED_DIRS};

/// Event name emitted to the webview.
pub const EVENT: &str = "brulion://fs-events";

/// Batching window, measured from the first event of a batch.
pub const BATCH_WINDOW: Duration = Duration::from_millis(300);

/// Upper bound of queued events drained after the window closes.
const MAX_DRAIN: usize = 10_000;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum FsEventType {
    Create,
    Modify,
    Delete,
    Rename,
}

/// `FsEvent` of `src/core/types.ts`.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FsEventDto {
    #[serde(rename = "type")]
    pub kind: FsEventType,
    pub path: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub old_path: Option<String>,
}

impl FsEventDto {
    pub fn new(kind: FsEventType, path: impl Into<String>) -> Self {
        FsEventDto {
            kind,
            path: path.into(),
            old_path: None,
        }
    }

    pub fn rename(from: impl Into<String>, to: impl Into<String>) -> Self {
        FsEventDto {
            kind: FsEventType::Rename,
            path: to.into(),
            old_path: Some(from.into()),
        }
    }
}

/// Payload of `brulion://fs-events`.
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FsEventsPayload {
    /// The root exactly as passed to `watch_start`.
    pub root: String,
    pub events: Vec<FsEventDto>,
    /// Events may have been lost: re-list the vault. Omitted when false.
    #[serde(skip_serializing_if = "is_false")]
    pub rescan: bool,
}

fn is_false(b: &bool) -> bool {
    !*b
}

/// What is at a path right now (injected so [`map_events`] stays pure).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Probe {
    Missing,
    File,
    Dir,
}

/// Probes the real file system without following symlinks.
pub fn probe_fs(path: &Path) -> Probe {
    match fs::symlink_metadata(path) {
        Ok(m) if m.is_dir() => Probe::Dir,
        Ok(_) => Probe::File,
        Err(_) => Probe::Missing,
    }
}

// ---------------------------------------------------------------------------
// Pure mapping
// ---------------------------------------------------------------------------

/// Intermediate per-path change.
#[derive(Debug, Clone, PartialEq, Eq)]
enum Change {
    /// The path did not exist before (inotify IN_CREATE).
    Created(String),
    /// The path appeared by a move; it may have replaced an existing entry.
    MovedIn(String),
    Modified(String),
    Removed(String),
    Renamed(String, String),
}

/// Maps a batch of raw notify events to deduplicated vault events. Returns the events and
/// whether a rescan is needed.
pub fn map_events(
    root: &Path,
    raw: &[Event],
    probe: &dyn Fn(&Path) -> Probe,
) -> (Vec<FsEventDto>, bool) {
    let rel = |p: &Path| rel_from_abs(root, p).filter(|r| !is_ignored_rel(r));
    let paired: HashSet<usize> = raw
        .iter()
        .filter(|e| {
            matches!(
                e.kind,
                EventKind::Modify(ModifyKind::Name(RenameMode::Both))
            )
        })
        .filter_map(|e| e.tracker())
        .collect();

    let mut rescan = false;
    let mut changes = Vec::new();
    for ev in raw {
        if ev.need_rescan() {
            rescan = true;
        }
        let touches_root = ev.paths.iter().any(|p| p == root);
        if touches_root
            && matches!(
                ev.kind,
                EventKind::Remove(_) | EventKind::Modify(ModifyKind::Name(_))
            )
        {
            rescan = true;
        }
        let each = |make: fn(String) -> Change, out: &mut Vec<Change>| {
            out.extend(ev.paths.iter().filter_map(|p| rel(p)).map(make));
        };
        match ev.kind {
            EventKind::Create(_) => each(Change::Created, &mut changes),
            EventKind::Remove(_) => each(Change::Removed, &mut changes),
            EventKind::Modify(ModifyKind::Name(mode)) => match mode {
                RenameMode::Both => {
                    if let [from, to, ..] = ev.paths.as_slice() {
                        match (rel(from), rel(to)) {
                            (Some(f), Some(t)) => changes.push(Change::Renamed(f, t)),
                            (None, Some(t)) => changes.push(Change::MovedIn(t)),
                            (Some(f), None) => changes.push(Change::Removed(f)),
                            (None, None) => {}
                        }
                    }
                }
                RenameMode::From => match ev.tracker() {
                    Some(t) if paired.contains(&t) => {}
                    // Moved out of the vault (or the other half got lost).
                    Some(_) => each(Change::Removed, &mut changes),
                    // inotify MOVE_SELF of a watched directory: stale unless it is gone.
                    None => {
                        for p in &ev.paths {
                            if let Some(r) = rel(p) {
                                if probe(p) == Probe::Missing {
                                    changes.push(Change::Removed(r));
                                }
                            }
                        }
                    }
                },
                RenameMode::To => match ev.tracker() {
                    Some(t) if paired.contains(&t) => {}
                    _ => each(Change::MovedIn, &mut changes),
                },
                RenameMode::Any | RenameMode::Other => {
                    for p in &ev.paths {
                        if let Some(r) = rel(p) {
                            changes.push(if probe(p) == Probe::Missing {
                                Change::Removed(r)
                            } else {
                                Change::MovedIn(r)
                            });
                        }
                    }
                }
            },
            // Data, metadata, Any, Other.
            EventKind::Modify(_) => each(Change::Modified, &mut changes),
            EventKind::Any => {
                for p in &ev.paths {
                    if let Some(r) = rel(p) {
                        changes.push(if probe(p) == Probe::Missing {
                            Change::Removed(r)
                        } else {
                            Change::Modified(r)
                        });
                    }
                }
            }
            EventKind::Access(_) | EventKind::Other => {}
        }
    }

    let mut events = coalesce(changes);
    events.retain(|e| e.kind != FsEventType::Modify || probe(&root.join(&e.path)) != Probe::Dir);
    (events, rescan)
}

#[derive(Debug)]
struct PathState {
    path: String,
    /// Existed before the batch: Some(true/false), None = unknown (moved in).
    before: Option<bool>,
    after: bool,
}

#[derive(Debug)]
enum Slot {
    Path(PathState),
    Rename { from: String, to: String },
    Dropped,
}

#[derive(Default)]
struct Coalescer {
    slots: Vec<Slot>,
    /// Path -> index of its open `Slot::Path` (closed by renames involving the path).
    open: HashMap<String, usize>,
    /// Paths renamed away in this batch (a later stale "removed" for them is noise).
    renamed_away: HashSet<String>,
}

impl Coalescer {
    fn apply(&mut self, path: String, before: Option<bool>, after: bool) {
        if !after && !self.open.contains_key(&path) && self.renamed_away.contains(&path) {
            return;
        }
        if after {
            self.renamed_away.remove(&path);
        }
        if let Some(&i) = self.open.get(&path) {
            if let Slot::Path(st) = &mut self.slots[i] {
                st.after = after;
            }
            return;
        }
        self.open.insert(path.clone(), self.slots.len());
        self.slots.push(Slot::Path(PathState {
            path,
            before,
            after,
        }));
    }

    fn take_open(&mut self, path: &str) -> Option<PathState> {
        let i = self.open.remove(path)?;
        match std::mem::replace(&mut self.slots[i], Slot::Dropped) {
            Slot::Path(st) => Some(st),
            other => {
                self.slots[i] = other;
                None
            }
        }
    }

    fn rename(&mut self, from: String, to: String) {
        if from == to {
            return;
        }
        // Pending changes of the source move with it.
        let src = self.take_open(&from);
        let prefix = format!("{from}/");
        let mut children: Vec<(usize, String)> = self
            .open
            .iter()
            .filter(|(p, _)| p.starts_with(&prefix))
            .map(|(p, &i)| (i, p.clone()))
            .collect();
        children.sort();
        let children: Vec<PathState> = children
            .into_iter()
            .filter_map(|(_, p)| self.take_open(&p))
            .collect();

        if let Some(st) = &src {
            if st.before == Some(false) {
                // Created within this batch and renamed: it simply appeared at `to`.
                self.renamed_away.insert(from);
                self.apply(to, None, true);
                return;
            }
        }
        let duplicate = matches!(self.slots.last(), Some(Slot::Rename { from: f, to: t }) if *f == from && *t == to);
        if !duplicate {
            // Pending changes of a replaced `to` stay where they are (before the rename).
            self.open.remove(&to);
            self.slots.push(Slot::Rename {
                from: from.clone(),
                to: to.clone(),
            });
        }
        self.renamed_away.insert(from);
        self.renamed_away.remove(&to);
        if src.is_some_and(|st| st.after) {
            self.apply(to.clone(), Some(true), true);
        }
        for child in children {
            let rebased = format!("{to}/{}", &child.path[prefix.len()..]);
            self.apply(rebased, child.before, child.after);
        }
    }

    fn finish(self) -> Vec<FsEventDto> {
        let mut out = Vec::new();
        for slot in self.slots {
            match slot {
                Slot::Path(st) => {
                    let kind = match (st.before, st.after) {
                        (Some(false), false) => continue,
                        (_, false) => FsEventType::Delete,
                        (Some(true), true) => FsEventType::Modify,
                        (_, true) => FsEventType::Create,
                    };
                    out.push(FsEventDto::new(kind, st.path));
                }
                Slot::Rename { from, to } => out.push(FsEventDto::rename(from, to)),
                Slot::Dropped => {}
            }
        }
        out
    }
}

fn coalesce(changes: Vec<Change>) -> Vec<FsEventDto> {
    let mut c = Coalescer::default();
    for change in changes {
        match change {
            Change::Created(p) => c.apply(p, Some(false), true),
            Change::MovedIn(p) => c.apply(p, None, true),
            Change::Modified(p) => c.apply(p, Some(true), true),
            Change::Removed(p) => c.apply(p, Some(true), false),
            Change::Renamed(from, to) => c.rename(from, to),
        }
    }
    c.finish()
}

/// Adds `create` events for the existing contents of directories that were created / moved
/// in (their children may predate the watch on the new folder).
pub fn expand_created_dirs(root: &Path, events: &mut Vec<FsEventDto>) {
    let mut known: HashSet<String> = events.iter().map(|e| e.path.clone()).collect();
    let mut extra = Vec::new();
    for e in events.iter().filter(|e| e.kind == FsEventType::Create) {
        let abs = root.join(&e.path);
        if probe_fs(&abs) != Probe::Dir {
            continue;
        }
        let walker = WalkDir::new(&abs)
            .follow_links(false)
            .min_depth(1)
            .sort_by_file_name()
            .into_iter()
            .filter_entry(|d| {
                !d.file_name()
                    .to_str()
                    .is_some_and(|n| SKIPPED_DIRS.contains(&n))
            });
        for entry in walker.flatten() {
            // Same rule as `fs_list`: no symlinked folders, broken links or special files.
            if listable_metadata(&entry).is_none() {
                continue;
            }
            if let Some(rel) = rel_from_abs(root, entry.path()) {
                if !is_ignored_rel(&rel) && known.insert(rel.clone()) {
                    extra.push(FsEventDto::new(FsEventType::Create, rel));
                }
            }
        }
    }
    events.extend(extra);
}

// ---------------------------------------------------------------------------
// Watcher thread
// ---------------------------------------------------------------------------

enum Msg {
    Event(notify::Result<Event>),
    Stop,
}

/// Collects events into batches until stopped (or the watcher is dropped).
fn run_batcher(
    rx: Receiver<Msg>,
    root: PathBuf,
    window: Duration,
    emit: impl Fn(Vec<FsEventDto>, bool),
) {
    loop {
        let mut raw = Vec::new();
        let mut rescan = false;
        let mut push = |res: notify::Result<Event>, raw: &mut Vec<Event>| match res {
            Ok(ev) => raw.push(ev),
            Err(e) => {
                log::warn!("watcher error in {}: {e}", root.display());
                rescan = true;
            }
        };
        match rx.recv() {
            Ok(Msg::Event(res)) => push(res, &mut raw),
            Ok(Msg::Stop) | Err(_) => return,
        }
        let deadline = Instant::now() + window;
        loop {
            let left = deadline.saturating_duration_since(Instant::now());
            if left.is_zero() {
                break;
            }
            match rx.recv_timeout(left) {
                Ok(Msg::Event(res)) => push(res, &mut raw),
                Ok(Msg::Stop) | Err(RecvTimeoutError::Disconnected) => return,
                Err(RecvTimeoutError::Timeout) => break,
            }
        }
        // Take what is already queued so both halves of a rename land in one batch.
        for _ in 0..MAX_DRAIN {
            match rx.try_recv() {
                Ok(Msg::Event(res)) => push(res, &mut raw),
                Ok(Msg::Stop) | Err(TryRecvError::Disconnected) => return,
                Err(TryRecvError::Empty) => break,
            }
        }
        let (mut events, lost) = map_events(&root, &raw, &probe_fs);
        expand_created_dirs(&root, &mut events);
        if !events.is_empty() || rescan || lost {
            emit(events, rescan || lost);
        }
    }
}

/// A running watcher; dropping it stops the watcher and ends its batching thread.
pub struct WatchHandle {
    _watcher: RecommendedWatcher,
    tx: Sender<Msg>,
}

impl Drop for WatchHandle {
    fn drop(&mut self) {
        let _ = self.tx.send(Msg::Stop);
    }
}

fn notify_err(e: notify::Error, root: &str) -> String {
    match &e.kind {
        notify::ErrorKind::Io(io) => io_err(io, root),
        notify::ErrorKind::PathNotFound => err("ENOENT", "vault folder not found", root),
        notify::ErrorKind::MaxFilesWatch => err(
            "ENOSPC",
            "inotify watch limit reached (fs.inotify.max_user_watches)",
            root,
        ),
        _ => err("EIO", e, root),
    }
}

/// Starts a recursive watcher on `root`; `emit` receives every batch.
pub fn start_watcher(
    root: &str,
    window: Duration,
    emit: impl Fn(FsEventsPayload) + Send + 'static,
) -> Result<WatchHandle, String> {
    let root_path = validate_root(root)?;
    let meta = fs::metadata(&root_path).map_err(|e| io_err(&e, root))?;
    if !meta.is_dir() {
        return Err(err("ENOTDIR", "vault root is not a directory", root));
    }
    let (tx, rx) = mpsc::channel::<Msg>();
    let handler_tx = tx.clone();
    let mut watcher = RecommendedWatcher::new(
        move |res: notify::Result<Event>| {
            let _ = handler_tx.send(Msg::Event(res));
        },
        Config::default().with_follow_symlinks(false),
    )
    .map_err(|e| notify_err(e, root))?;
    watcher
        .watch(&root_path, RecursiveMode::Recursive)
        .map_err(|e| notify_err(e, root))?;
    let label = root.to_string();
    std::thread::Builder::new()
        .name("brulion-watch".into())
        .spawn(move || {
            run_batcher(rx, root_path, window, |events, rescan| {
                emit(FsEventsPayload {
                    root: label.clone(),
                    events,
                    rescan,
                })
            })
        })
        .map_err(|e| err("EIO", e, root))?;
    Ok(WatchHandle {
        _watcher: watcher,
        tx,
    })
}

/// Managed state: running watchers by normalised root.
///
/// Starting a watcher takes a while (inotify watches are added for every folder) and runs off
/// the lock, so calls for one root can overlap. Every `watch_start`/`watch_stop` bumps the
/// root's generation; a start only installs its watcher when no later call for the root came
/// in meanwhile — the last call wins (a stop during a start is not lost).
#[derive(Default)]
pub struct Watchers(Mutex<WatcherMap>);

#[derive(Default)]
struct WatcherMap {
    handles: HashMap<PathBuf, WatchHandle>,
    generations: HashMap<PathBuf, u64>,
}

impl Watchers {
    fn key(root: &str) -> Result<PathBuf, String> {
        // `components()` drops trailing and duplicate slashes.
        Ok(validate_root(root)?.components().collect())
    }

    fn map(&self) -> std::sync::MutexGuard<'_, WatcherMap> {
        self.0.lock().unwrap_or_else(|e| e.into_inner())
    }

    /// Registers a new call for `key`; returns its generation.
    fn begin(&self, key: &Path) -> u64 {
        let mut map = self.map();
        let generation = map.generations.entry(key.to_path_buf()).or_default();
        *generation += 1;
        *generation
    }

    /// Installs the watcher of the start registered as `generation` unless a later call for
    /// `key` superseded it. Returns the handle to drop (outside the lock): the replaced
    /// watcher, or `handle` itself when it is stale.
    fn install(&self, key: PathBuf, generation: u64, handle: WatchHandle) -> Option<WatchHandle> {
        let mut map = self.map();
        if map.generations.get(&key) == Some(&generation) {
            map.handles.insert(key, handle)
        } else {
            Some(handle)
        }
    }

    /// Stops `key` (also any start still in flight); returns the handle to drop.
    fn stop(&self, key: &Path) -> Option<WatchHandle> {
        self.begin(key);
        self.map().handles.remove(key)
    }

    #[cfg(test)]
    fn is_watching(&self, key: &Path) -> bool {
        self.map().handles.contains_key(key)
    }
}

/// Starts (or restarts) watching `root`.
#[tauri::command]
pub async fn watch_start<R: Runtime>(
    app: AppHandle<R>,
    watchers: State<'_, Watchers>,
    root: String,
) -> Result<(), String> {
    let key = Watchers::key(&root)?;
    let generation = watchers.begin(&key);
    let handle = blocking(move || {
        start_watcher(&root, BATCH_WINDOW, move |payload| {
            if let Err(e) = app.emit(EVENT, payload) {
                log::warn!("cannot emit {EVENT}: {e}");
            }
        })
    })
    .await?;
    drop(watchers.install(key, generation, handle));
    Ok(())
}

/// Stops watching `root` (no-op when not watched).
#[tauri::command]
pub async fn watch_stop(watchers: State<'_, Watchers>, root: String) -> Result<(), String> {
    let key = Watchers::key(&root)?;
    drop(watchers.stop(&key));
    Ok(())
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use notify::event::{
        AccessKind, AccessMode, CreateKind, DataChange, Flag, MetadataKind, RemoveKind,
    };
    use FsEventType::*;

    const ROOT: &str = "/vault";

    fn p(rel: &str) -> PathBuf {
        Path::new(ROOT).join(rel)
    }
    fn create(rel: &str) -> Event {
        Event::new(EventKind::Create(CreateKind::File)).add_path(p(rel))
    }
    fn create_dir(rel: &str) -> Event {
        Event::new(EventKind::Create(CreateKind::Folder)).add_path(p(rel))
    }
    fn modify(rel: &str) -> Event {
        Event::new(EventKind::Modify(ModifyKind::Data(DataChange::Any))).add_path(p(rel))
    }
    fn attrib(rel: &str) -> Event {
        Event::new(EventKind::Modify(ModifyKind::Metadata(MetadataKind::Any))).add_path(p(rel))
    }
    fn remove(rel: &str) -> Event {
        Event::new(EventKind::Remove(RemoveKind::File)).add_path(p(rel))
    }
    fn close_write(rel: &str) -> Event {
        Event::new(EventKind::Access(AccessKind::Close(AccessMode::Write))).add_path(p(rel))
    }
    fn name(mode: RenameMode, rel: &str, cookie: Option<usize>) -> Event {
        let e = Event::new(EventKind::Modify(ModifyKind::Name(mode))).add_path(p(rel));
        match cookie {
            Some(c) => e.set_tracker(c),
            None => e,
        }
    }
    /// The three events inotify produces for one rename inside the vault.
    fn inotify_rename(from: &str, to: &str, cookie: usize) -> Vec<Event> {
        vec![
            name(RenameMode::From, from, Some(cookie)),
            name(RenameMode::To, to, Some(cookie)),
            Event::new(EventKind::Modify(ModifyKind::Name(RenameMode::Both)))
                .set_tracker(cookie)
                .add_path(p(from))
                .add_path(p(to)),
        ]
    }

    fn all_files(_: &Path) -> Probe {
        Probe::File
    }
    fn nothing(_: &Path) -> Probe {
        Probe::Missing
    }

    fn map(raw: &[Event]) -> Vec<FsEventDto> {
        let (events, rescan) = map_events(Path::new(ROOT), raw, &all_files);
        assert!(!rescan);
        events
    }

    fn ev(kind: FsEventType, path: &str) -> FsEventDto {
        FsEventDto::new(kind, path)
    }

    #[test]
    fn basic_kinds() {
        assert_eq!(map(&[create("a.md")]), vec![ev(Create, "a.md")]);
        assert_eq!(map(&[modify("a.md")]), vec![ev(Modify, "a.md")]);
        assert_eq!(map(&[attrib("a.md")]), vec![ev(Modify, "a.md")]);
        assert_eq!(map(&[remove("a.md")]), vec![ev(Delete, "a.md")]);
        assert_eq!(map(&[close_write("a.md")]), vec![]);
        assert_eq!(
            map(&inotify_rename("a.md", "dir/b.md", 7)),
            vec![FsEventDto::rename("a.md", "dir/b.md")]
        );
    }

    #[test]
    fn deduplicates_and_collapses_per_path() {
        // Typical editor save: create + several writes + close.
        let raw = [
            create("nowa notatka.md"),
            modify("nowa notatka.md"),
            modify("nowa notatka.md"),
            close_write("nowa notatka.md"),
            modify("b.md"),
            modify("b.md"),
            attrib("b.md"),
        ];
        assert_eq!(
            map(&raw),
            vec![ev(Create, "nowa notatka.md"), ev(Modify, "b.md")]
        );
        // Deleted and recreated -> modified; created and deleted -> nothing.
        assert_eq!(
            map(&[
                remove("a.md"),
                create("a.md"),
                create("tmp.md"),
                remove("tmp.md")
            ]),
            vec![ev(Modify, "a.md")]
        );
        // Modified, then deleted -> delete.
        assert_eq!(
            map(&[modify("a.md"), remove("a.md")]),
            vec![ev(Delete, "a.md")]
        );
    }

    #[test]
    fn temp_file_saves_become_creates_of_the_target() {
        // Brulion's own atomic write.
        let mut raw = vec![
            create(".brulion-tmp-0011223344556677"),
            modify(".brulion-tmp-0011223344556677"),
        ];
        raw.extend(inotify_rename(".brulion-tmp-0011223344556677", "a.md", 1));
        assert_eq!(map(&raw), vec![ev(Create, "a.md")]);
        // Syncthing writes ".syncthing.<name>.tmp" and renames it into place.
        let raw = inotify_rename("notes/.syncthing.Zakupy.md.tmp", "notes/Zakupy.md", 2);
        assert_eq!(map(&raw), vec![ev(Create, "notes/Zakupy.md")]);
        let raw = inotify_rename("~syncthing~x.md.tmp", "x.md", 3);
        assert_eq!(map(&raw), vec![ev(Create, "x.md")]);
        // Moving a note into a temp name is a deletion.
        let raw = inotify_rename("a.md", ".brulion-tmp-1", 4);
        assert_eq!(map(&raw), vec![ev(Delete, "a.md")]);
        // Pure temp traffic produces nothing.
        assert_eq!(
            map(&[create("~syncthing~a.tmp"), remove("~syncthing~a.tmp")]),
            vec![]
        );
    }

    #[test]
    fn ignores_git_node_modules_and_outside_paths() {
        let raw = [
            create(".git/objects/ab/cdef"),
            modify(".git/index"),
            create("web/node_modules/x/index.js"),
            Event::new(EventKind::Create(CreateKind::File))
                .add_path(PathBuf::from("/elsewhere/a.md")),
            create_dir(".git"),
            create(".gitignore"),
        ];
        assert_eq!(map(&raw), vec![ev(Create, ".gitignore")]);
        // Moving a note into .git is a deletion; moving it out is a creation.
        assert_eq!(
            map(&inotify_rename("a.md", ".git/a.md", 5)),
            vec![ev(Delete, "a.md")]
        );
        assert_eq!(
            map(&inotify_rename(".git/a.md", "a.md", 6)),
            vec![ev(Create, "a.md")]
        );
    }

    #[test]
    fn unpaired_halves_are_moves_across_the_vault_boundary() {
        assert_eq!(
            map(&[name(RenameMode::From, "a.md", Some(9))]),
            vec![ev(Delete, "a.md")]
        );
        assert_eq!(
            map(&[name(RenameMode::To, "z importu.md", Some(10))]),
            vec![ev(Create, "z importu.md")]
        );
    }

    #[test]
    fn move_self_of_a_renamed_directory_is_ignored() {
        // Renaming folder "a" to "b": parent sees the rename, the folder's own watch reports
        // MOVE_SELF (From without tracker) for the old path, which no longer exists.
        let mut raw = inotify_rename("a", "b", 11);
        raw.push(name(RenameMode::From, "a", None));
        let (events, rescan) = map_events(Path::new(ROOT), &raw, &nothing);
        assert!(!rescan);
        assert_eq!(events, vec![FsEventDto::rename("a", "b")]);
        // Same for DELETE_SELF-style removals of a renamed path.
        let mut raw = inotify_rename("a", "b", 12);
        raw.push(remove("a"));
        assert_eq!(map(&raw), vec![FsEventDto::rename("a", "b")]);
    }

    #[test]
    fn rename_any_uses_the_probe() {
        let raw = [
            name(RenameMode::Any, "gone.md", None),
            name(RenameMode::Any, "here.md", None),
        ];
        let probe = |p: &Path| {
            if p.ends_with("here.md") {
                Probe::File
            } else {
                Probe::Missing
            }
        };
        let (events, _) = map_events(Path::new(ROOT), &raw, &probe);
        assert_eq!(events, vec![ev(Delete, "gone.md"), ev(Create, "here.md")]);
    }

    #[test]
    fn changes_are_not_merged_across_renames() {
        // modify a, rename a -> b, a recreated: the old pending modify of "a" moves to "b".
        let mut raw = vec![modify("a.md")];
        raw.extend(inotify_rename("a.md", "b.md", 20));
        raw.push(create("a.md"));
        assert_eq!(
            map(&raw),
            vec![
                FsEventDto::rename("a.md", "b.md"),
                ev(Modify, "b.md"),
                ev(Create, "a.md"),
            ]
        );
        // Created and renamed within one batch: it just appears under the new name.
        let mut raw = vec![create("Bez tytułu.md"), modify("Bez tytułu.md")];
        raw.extend(inotify_rename("Bez tytułu.md", "Zażółć.md", 21));
        assert_eq!(map(&raw), vec![ev(Create, "Zażółć.md")]);
        // Pending children of a renamed directory are rebased.
        let mut raw = vec![modify("dir/x.md"), create("dir/new.md")];
        raw.extend(inotify_rename("dir", "renamed", 22));
        assert_eq!(
            map(&raw),
            vec![
                FsEventDto::rename("dir", "renamed"),
                ev(Modify, "renamed/x.md"),
                ev(Create, "renamed/new.md"),
            ]
        );
        // Renaming onto a file with pending changes keeps them before the rename.
        let mut raw = vec![modify("b.md")];
        raw.extend(inotify_rename("a.md", "b.md", 23));
        assert_eq!(
            map(&raw),
            vec![ev(Modify, "b.md"), FsEventDto::rename("a.md", "b.md")]
        );
    }

    #[test]
    fn directory_modifications_are_dropped() {
        let probe = |p: &Path| {
            if p.ends_with("folder") {
                Probe::Dir
            } else {
                Probe::File
            }
        };
        let raw = [attrib("folder"), modify("folder/a.md"), create_dir("nowy")];
        let (events, _) = map_events(Path::new(ROOT), &raw, &probe);
        assert_eq!(events, vec![ev(Modify, "folder/a.md"), ev(Create, "nowy")]);
    }

    #[test]
    fn rescan_flags() {
        let overflow = Event::new(EventKind::Other).set_flag(Flag::Rescan);
        let (events, rescan) = map_events(Path::new(ROOT), &[overflow], &all_files);
        assert!(events.is_empty());
        assert!(rescan);
        let root_gone = Event::new(EventKind::Remove(RemoveKind::Folder)).add_path(p(""));
        let (events, rescan) = map_events(Path::new(ROOT), &[root_gone], &nothing);
        assert!(events.is_empty());
        assert!(rescan);
    }

    #[test]
    fn payload_serialises_camel_case() {
        let payload = FsEventsPayload {
            root: "/vault".into(),
            events: vec![FsEventDto::rename("a.md", "b.md"), ev(Delete, "c.md")],
            rescan: false,
        };
        assert_eq!(
            serde_json::to_value(&payload).unwrap(),
            serde_json::json!({
                "root": "/vault",
                "events": [
                    { "type": "rename", "path": "b.md", "oldPath": "a.md" },
                    { "type": "delete", "path": "c.md" }
                ]
            })
        );
        let with_rescan = FsEventsPayload {
            rescan: true,
            ..payload
        };
        assert_eq!(serde_json::to_value(&with_rescan).unwrap()["rescan"], true);
    }

    #[test]
    fn expands_created_directories() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        fs::create_dir_all(root.join("import/sub")).unwrap();
        fs::create_dir_all(root.join("import/.git")).unwrap();
        fs::write(root.join("import/a.md"), "a").unwrap();
        fs::write(root.join("import/sub/b.md"), "b").unwrap();
        fs::write(root.join("import/.git/HEAD"), "").unwrap();
        fs::write(root.join("import/.brulion-tmp-1"), "").unwrap();
        let mut events = vec![
            ev(Create, "import"),
            ev(Create, "import/a.md"),
            ev(Modify, "other.md"),
        ];
        expand_created_dirs(root, &mut events);
        assert_eq!(
            events,
            vec![
                ev(Create, "import"),
                ev(Create, "import/a.md"),
                ev(Modify, "other.md"),
                ev(Create, "import/sub"),
                ev(Create, "import/sub/b.md"),
            ]
        );
    }

    #[cfg(unix)]
    #[test]
    fn expansion_skips_what_fs_list_skips() {
        // A folder moved into the vault containing a symlinked folder, a broken link and a
        // link to a file: report exactly what `fs_list` would list for it.
        let dir = tempfile::tempdir().unwrap();
        let outside = tempfile::tempdir().unwrap();
        fs::write(outside.path().join("sekret.md"), "s").unwrap();
        let root = dir.path();
        fs::create_dir(root.join("przeniesiony")).unwrap();
        fs::write(root.join("przeniesiony/notatka.md"), "n").unwrap();
        std::os::unix::fs::symlink(outside.path(), root.join("przeniesiony/link-do-folderu"))
            .unwrap();
        std::os::unix::fs::symlink(root.join("brak.md"), root.join("przeniesiony/zerwany.md"))
            .unwrap();
        std::os::unix::fs::symlink(
            root.join("przeniesiony/notatka.md"),
            root.join("przeniesiony/alias.md"),
        )
        .unwrap();
        let mut events = vec![ev(Create, "przeniesiony")];
        expand_created_dirs(root, &mut events);
        let mut expected: Vec<String> = crate::fs_cmds::list(root.to_str().unwrap())
            .unwrap()
            .into_iter()
            .map(|s| s.path)
            .collect();
        expected.sort();
        let mut got: Vec<String> = events.into_iter().map(|e| e.path).collect();
        got.sort();
        assert_eq!(got, expected);
        assert_eq!(
            got,
            vec![
                "przeniesiony",
                "przeniesiony/alias.md",
                "przeniesiony/notatka.md"
            ]
        );
    }

    // -- real watcher (inotify) -------------------------------------------------------------

    use std::sync::mpsc::Receiver as Rx;

    /// Accumulates events until `done` holds, then waits a little for stragglers.
    fn collect(rx: &Rx<FsEventsPayload>, done: impl Fn(&[FsEventDto]) -> bool) -> Vec<FsEventDto> {
        let mut acc = Vec::new();
        let deadline = Instant::now() + Duration::from_secs(5);
        while !done(&acc) {
            let left = deadline.saturating_duration_since(Instant::now());
            match rx.recv_timeout(left) {
                Ok(payload) => acc.extend(payload.events),
                Err(_) => panic!("timed out; got {acc:?}"),
            }
        }
        while let Ok(payload) = rx.recv_timeout(Duration::from_millis(400)) {
            acc.extend(payload.events);
        }
        acc
    }

    #[test]
    fn real_watcher_end_to_end() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path().to_path_buf();
        fs::create_dir_all(root.join(".git/objects")).unwrap();
        let (tx, rx) = mpsc::channel::<FsEventsPayload>();
        let root_str = root.to_str().unwrap().to_string();
        let handle = start_watcher(&root_str, Duration::from_millis(100), move |payload| {
            let _ = tx.send(payload);
        })
        .unwrap();

        fs::write(root.join("a.md"), "x").unwrap();
        let got = collect(&rx, |acc| !acc.is_empty());
        assert_eq!(got, vec![ev(Create, "a.md")]);

        crate::fs_cmds::write_atomic(&root.join("a.md"), b"nowa tre\xC5\x9B\xC4\x87").unwrap();
        let got = collect(&rx, |acc| !acc.is_empty());
        assert_eq!(
            got,
            vec![ev(Create, "a.md")],
            "atomic save: temp file hidden"
        );

        fs::rename(root.join("a.md"), root.join("b.md")).unwrap();
        let got = collect(&rx, |acc| !acc.is_empty());
        assert_eq!(got, vec![FsEventDto::rename("a.md", "b.md")]);

        fs::create_dir(root.join("sub")).unwrap();
        fs::write(root.join("sub/c.md"), "c").unwrap();
        let got = collect(&rx, |acc| acc.iter().any(|e| e.path == "sub/c.md"));
        let mut paths: Vec<(FsEventType, &str)> =
            got.iter().map(|e| (e.kind, e.path.as_str())).collect();
        paths.sort_by_key(|(_, p)| *p);
        assert!(
            paths == vec![(Create, "sub"), (Create, "sub/c.md")]
                || paths == vec![(Create, "sub"), (Create, "sub/c.md"), (Modify, "sub/c.md")],
            "{got:?}"
        );

        fs::write(root.join(".git/objects/obj"), "ignored").unwrap();
        fs::remove_file(root.join("b.md")).unwrap();
        let got = collect(&rx, |acc| !acc.is_empty());
        assert_eq!(got, vec![ev(Delete, "b.md")]);

        // Dropping the handle ends the batching thread, which drops the emit closure.
        drop(handle);
        let deadline = Instant::now() + Duration::from_secs(3);
        loop {
            match rx.recv_timeout(Duration::from_millis(100)) {
                Err(mpsc::RecvTimeoutError::Disconnected) => break,
                _ if Instant::now() > deadline => panic!("watcher thread did not stop"),
                _ => {}
            }
        }
    }

    #[test]
    fn start_watcher_validates_root() {
        let noop = |_: FsEventsPayload| {};
        assert!(start_watcher("", BATCH_WINDOW, noop).is_err());
        assert!(start_watcher("relative/dir", BATCH_WINDOW, noop).is_err());
        let dir = tempfile::tempdir().unwrap();
        let missing = dir.path().join("brak");
        let e = start_watcher(missing.to_str().unwrap(), BATCH_WINDOW, noop)
            .err()
            .unwrap();
        assert!(e.starts_with("ENOENT"), "{e}");
    }

    /// A real watcher whose batching thread can be observed: the receiver disconnects once
    /// the handle has been dropped (the thread ends and drops the emit closure).
    fn observed_watcher(dir: &Path) -> (WatchHandle, Rx<FsEventsPayload>) {
        let (tx, rx) = mpsc::channel();
        let handle = start_watcher(dir.to_str().unwrap(), BATCH_WINDOW, move |p| {
            let _ = tx.send(p);
        })
        .unwrap();
        (handle, rx)
    }

    fn stopped(rx: &Rx<FsEventsPayload>) -> bool {
        let deadline = Instant::now() + Duration::from_secs(3);
        while Instant::now() < deadline {
            if let Err(mpsc::RecvTimeoutError::Disconnected) =
                rx.recv_timeout(Duration::from_millis(50))
            {
                return true;
            }
        }
        false
    }

    #[test]
    fn a_stop_during_a_start_is_not_lost() {
        let dir = tempfile::tempdir().unwrap();
        let watchers = Watchers::default();
        let key = Watchers::key(dir.path().to_str().unwrap()).unwrap();
        // watch_start registers itself and starts adding inotify watches (slow) ...
        let generation = watchers.begin(&key);
        // ... watch_stop arrives meanwhile (e.g. the user switched vaults) ...
        assert!(watchers.stop(&key).is_none());
        // ... and only then the start finishes: its watcher must not be installed.
        let (handle, rx) = observed_watcher(dir.path());
        drop(watchers.install(key.clone(), generation, handle));
        assert!(!watchers.is_watching(&key));
        assert!(stopped(&rx), "stale watcher keeps running");
    }

    #[test]
    fn the_latest_start_wins_and_replaced_watchers_stop() {
        let dir = tempfile::tempdir().unwrap();
        let watchers = Watchers::default();
        let key = Watchers::key(dir.path().to_str().unwrap()).unwrap();
        let first = watchers.begin(&key);
        let second = watchers.begin(&key);
        // The second start finishes first, the first one late: the second must stay.
        let (h2, rx2) = observed_watcher(dir.path());
        assert!(watchers.install(key.clone(), second, h2).is_none());
        let (h1, rx1) = observed_watcher(dir.path());
        drop(watchers.install(key.clone(), first, h1));
        assert!(stopped(&rx1));
        assert!(watchers.is_watching(&key));
        // A later restart replaces (and stops) the running watcher.
        let third = watchers.begin(&key);
        let (h3, rx3) = observed_watcher(dir.path());
        drop(watchers.install(key.clone(), third, h3));
        assert!(stopped(&rx2));
        assert!(watchers.is_watching(&key));
        drop(watchers.stop(&key));
        assert!(stopped(&rx3));
        assert!(!watchers.is_watching(&key));
    }

    #[test]
    fn watcher_keys_ignore_trailing_slashes() {
        assert_eq!(
            Watchers::key("/vault/").unwrap(),
            Watchers::key("/vault").unwrap()
        );
        assert_eq!(
            Watchers::key("//vault//x").unwrap(),
            Watchers::key("/vault/x").unwrap()
        );
        assert!(Watchers::key("vault").is_err());
    }
}
