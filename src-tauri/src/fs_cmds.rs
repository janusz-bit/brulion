//! Vault file-system commands (see "Tauri IPC contract" in docs/ARCHITECTURE.md).
//!
//! The logic lives in plain functions (unit-tested below); the `#[tauri::command]` wrappers
//! only move the work off the main thread with `spawn_blocking`.

use std::collections::hash_map::RandomState;
use std::fs::{self, File, OpenOptions};
use std::hash::{BuildHasher, Hasher};
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

use percent_encoding::percent_decode_str;
use tauri::ipc::{InvokeBody, Request, Response};
use walkdir::WalkDir;

use crate::paths::{
    err, io_err, rel_from_abs, resolve, resolve_entry, validate_root, FileStatDto, SKIPPED_DIRS,
    TEMP_PREFIX,
};

// ---------------------------------------------------------------------------
// Plain implementation
// ---------------------------------------------------------------------------

/// Recursive listing of `root` (files and directories, root excluded). Symlinks are not
/// followed: a symlink to a file is listed with the target's size/mtime, symlinks to
/// directories and broken links are skipped. `.git` and `node_modules` are skipped entirely.
pub fn list(root: &str) -> Result<Vec<FileStatDto>, String> {
    let root_path = validate_root(root)?;
    let meta = fs::metadata(&root_path).map_err(|e| io_err(&e, root))?;
    if !meta.is_dir() {
        return Err(err("ENOTDIR", "vault root is not a directory", root));
    }
    let walker = WalkDir::new(&root_path)
        .follow_links(false)
        .min_depth(1)
        .sort_by_file_name()
        .into_iter()
        .filter_entry(|e| {
            e.depth() == 0
                || !e
                    .file_name()
                    .to_str()
                    .is_some_and(|name| SKIPPED_DIRS.contains(&name))
        });
    let mut out = Vec::new();
    for entry in walker {
        let entry = match entry {
            Ok(e) => e,
            Err(e) => {
                // An unreadable sub-folder must not hide the rest of the vault.
                log::warn!("fs_list: skipping entry in {root}: {e}");
                continue;
            }
        };
        let Some(rel) = rel_from_abs(&root_path, entry.path()) else {
            log::warn!("fs_list: skipping non-UTF-8 path {:?}", entry.path());
            continue;
        };
        if let Some(meta) = listable_metadata(&entry) {
            out.push(FileStatDto::from_metadata(rel, &meta));
        }
    }
    Ok(out)
}

/// Metadata of a walked (not followed) entry as `fs_list` reports it: files and directories
/// as they are, a symlink to a file with the target's metadata. `None` for symlinks to
/// directories, broken links and special files (sockets, FIFOs, devices). The watcher uses
/// the same rule, so it never reports what a listing would not contain.
pub fn listable_metadata(entry: &walkdir::DirEntry) -> Option<fs::Metadata> {
    let ft = entry.file_type();
    if ft.is_symlink() {
        fs::metadata(entry.path()).ok().filter(|m| m.is_file())
    } else if ft.is_file() || ft.is_dir() {
        entry
            .metadata()
            .map_err(|e| log::warn!("cannot stat {}: {e}", entry.path().display()))
            .ok()
    } else {
        None
    }
}

/// Stat of an entry (`""` = the root itself); `None` when missing. Follows symlinks.
pub fn stat(root: &str, path: &str) -> Result<Option<FileStatDto>, String> {
    let r = resolve(root, path)?;
    match fs::metadata(&r.abs) {
        Ok(meta) => Ok(Some(FileStatDto::from_metadata(r.rel, &meta))),
        Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(None),
        Err(e) if e.kind() == io::ErrorKind::NotADirectory => Ok(None),
        Err(e) => Err(io_err(&e, &r.rel)),
    }
}

/// Reads a text file. Decodes like the browser's `TextDecoder('utf-8')` used by the other
/// storage backends: a leading BOM is dropped and invalid sequences become U+FFFD.
pub fn read_text(root: &str, path: &str) -> Result<String, String> {
    let bytes = read_binary(root, path)?;
    Ok(decode_utf8(bytes))
}

pub fn decode_utf8(bytes: Vec<u8>) -> String {
    let mut s = match String::from_utf8(bytes) {
        Ok(s) => s,
        Err(e) => String::from_utf8_lossy(e.as_bytes()).into_owned(),
    };
    if s.starts_with('\u{feff}') {
        s.drain(..'\u{feff}'.len_utf8());
    }
    s
}

pub fn read_binary(root: &str, path: &str) -> Result<Vec<u8>, String> {
    let r = resolve_entry(root, path)?;
    fs::read(&r.abs).map_err(|e| io_err(&e, &r.rel))
}

/// Atomic write inside the vault; returns the new stat.
pub fn write(root: &str, path: &str, data: &[u8]) -> Result<FileStatDto, String> {
    let r = resolve_entry(root, path)?;
    let written = write_atomic(&r.abs, data).map_err(|e| io_err(&e, &r.rel))?;
    let meta = fs::metadata(&written).map_err(|e| io_err(&e, &r.rel))?;
    Ok(FileStatDto::from_metadata(r.rel, &meta))
}

/// Removes a file (or symlink) or a directory recursively. Missing entries are fine,
/// including paths below a file (ENOTDIR), which cannot exist either.
pub fn remove(root: &str, path: &str) -> Result<(), String> {
    let r = resolve_entry(root, path)?;
    let missing = |e: &io::Error| {
        matches!(
            e.kind(),
            io::ErrorKind::NotFound | io::ErrorKind::NotADirectory
        )
    };
    let meta = match fs::symlink_metadata(&r.abs) {
        Ok(m) => m,
        Err(e) if missing(&e) => return Ok(()),
        Err(e) => return Err(io_err(&e, &r.rel)),
    };
    let res = if meta.is_dir() {
        fs::remove_dir_all(&r.abs)
    } else {
        fs::remove_file(&r.abs)
    };
    match res {
        Err(e) if !missing(&e) => Err(io_err(&e, &r.rel)),
        _ => Ok(()),
    }
}

/// Renames/moves an entry, creating the parent directories of `to`. Same semantics as
/// POSIX `rename` (an existing file at `to` is replaced).
pub fn rename(root: &str, from: &str, to: &str) -> Result<(), String> {
    let a = resolve_entry(root, from)?;
    let b = resolve_entry(root, to)?;
    if a.rel == b.rel {
        return Ok(());
    }
    if b.rel.starts_with(&format!("{}/", a.rel)) {
        return Err(err("EINVAL", "cannot move a directory into itself", &b.rel));
    }
    fs::symlink_metadata(&a.abs).map_err(|e| io_err(&e, &a.rel))?;
    if let Some(parent) = b.abs.parent() {
        fs::create_dir_all(parent).map_err(|e| io_err(&e, &b.rel))?;
    }
    fs::rename(&a.abs, &b.abs).map_err(|e| io_err(&e, &a.rel))
}

/// Creates a directory and its parents (`""` creates the vault root itself).
pub fn mkdir(root: &str, path: &str) -> Result<(), String> {
    let r = resolve(root, path)?;
    fs::create_dir_all(&r.abs).map_err(|e| io_err(&e, &r.rel))
}

static TMP_COUNTER: AtomicU64 = AtomicU64::new(0);

fn random_suffix() -> String {
    let mut h = RandomState::new().build_hasher();
    h.write_u64(TMP_COUNTER.fetch_add(1, Ordering::Relaxed));
    h.write_u32(std::process::id());
    if let Ok(d) = SystemTime::now().duration_since(UNIX_EPOCH) {
        h.write_u128(d.as_nanos());
    }
    format!("{:016x}", h.finish())
}

/// Creates `<dir>/.brulion-tmp-<random>` exclusively.
fn create_temp(dir: &Path) -> io::Result<(PathBuf, File)> {
    let mut last_err = None;
    for _ in 0..16 {
        let path = dir.join(format!("{TEMP_PREFIX}{}", random_suffix()));
        match OpenOptions::new().write(true).create_new(true).open(&path) {
            Ok(f) => return Ok((path, f)),
            Err(e) if e.kind() == io::ErrorKind::AlreadyExists => last_err = Some(e),
            Err(e) => return Err(e),
        }
    }
    Err(last_err.unwrap_or_else(|| io::Error::other("cannot create a temp file")))
}

/// Writes `data` to `target` atomically: temp file in the same directory, fsync, rename over
/// the target, fsync of the directory (best effort). Parent directories are created. When
/// `target` is a symlink the file it points to is replaced (the link is kept); permissions of
/// an existing target are preserved. Returns the path actually written.
pub fn write_atomic(target: &Path, data: &[u8]) -> io::Result<PathBuf> {
    let mut target = target.to_path_buf();
    match fs::symlink_metadata(&target) {
        Ok(m) if m.file_type().is_symlink() => {
            if let Ok(real) = fs::canonicalize(&target) {
                target = real;
            }
        }
        Ok(m) if m.is_dir() => {
            return Err(io::Error::new(
                io::ErrorKind::IsADirectory,
                "is a directory",
            ));
        }
        _ => {}
    }
    let dir = match target.parent() {
        Some(d) if !d.as_os_str().is_empty() => d.to_path_buf(),
        _ => {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "target has no parent directory",
            ))
        }
    };
    fs::create_dir_all(&dir)?;
    let existing_perms = fs::metadata(&target).ok().map(|m| m.permissions());
    let (tmp, file) = create_temp(&dir)?;
    if let Err(e) = fill_and_rename(file, &tmp, &target, data, existing_perms) {
        let _ = fs::remove_file(&tmp);
        return Err(e);
    }
    sync_dir(&dir);
    Ok(target)
}

fn fill_and_rename(
    mut file: File,
    tmp: &Path,
    target: &Path,
    data: &[u8],
    perms: Option<fs::Permissions>,
) -> io::Result<()> {
    file.write_all(data)?;
    file.sync_all()?;
    drop(file);
    if let Some(perms) = perms {
        // Some file systems (Android FUSE, vfat) refuse chmod — not worth failing for.
        let _ = fs::set_permissions(tmp, perms);
    }
    fs::rename(tmp, target)
}

/// Makes the rename durable (best effort; not supported everywhere).
fn sync_dir(dir: &Path) {
    #[cfg(unix)]
    if let Ok(d) = File::open(dir) {
        let _ = d.sync_all();
    }
    #[cfg(not(unix))]
    let _ = dir;
}

/// Bytes of a raw IPC body. Desktop sends `InvokeBody::Raw`; Android's postMessage IPC
/// serialises a `Uint8Array` payload as a JSON array of numbers, which Tauri parses into one
/// `serde_json::Value` per byte (~32 bytes of memory each) before the command runs. For large
/// files the caller can send `{ base64: "<standard, padded base64>" }` instead.
pub fn body_bytes(body: &InvokeBody) -> Result<Vec<u8>, String> {
    use base64::Engine as _;
    use serde_json::Value;
    match body {
        InvokeBody::Raw(bytes) => Ok(bytes.clone()),
        InvokeBody::Json(Value::Array(items)) => items
            .iter()
            .map(|v| {
                v.as_u64()
                    .and_then(|n| u8::try_from(n).ok())
                    .ok_or_else(|| err("EINVAL", "body must be bytes", "(body)"))
            })
            .collect(),
        InvokeBody::Json(Value::Object(map)) if map.len() == 1 => match map.get("base64") {
            Some(Value::String(b64)) => base64::engine::general_purpose::STANDARD
                .decode(b64)
                .map_err(|e| err("EINVAL", format!("invalid base64 body ({e})"), "(body)")),
            _ => Err(err("EINVAL", "expected { base64: string }", "(body)")),
        },
        InvokeBody::Json(_) => Err(err(
            "EINVAL",
            "expected a raw byte body (Uint8Array/ArrayBuffer) or { base64 }",
            "(body)",
        )),
    }
}

/// Percent-decoded (`encodeURIComponent`) header value.
pub fn header(request: &Request<'_>, name: &str) -> Result<String, String> {
    let value = request
        .headers()
        .get(name)
        .ok_or_else(|| err("EINVAL", "missing header", name))?;
    let value = value.to_str().map_err(|_| {
        err(
            "EINVAL",
            "header is not ASCII (use encodeURIComponent)",
            name,
        )
    })?;
    percent_decode_str(value)
        .decode_utf8()
        .map(|s| s.into_owned())
        .map_err(|_| err("EINVAL", "header is not valid UTF-8 after decoding", name))
}

/// Validates the absolute target of `write_export` (a path chosen in the save dialog).
pub fn export_target(path: &str) -> Result<PathBuf, String> {
    let p = validate_root(path)?;
    if p.file_name().is_none() {
        return Err(err("EINVAL", "export target must be a file path", path));
    }
    Ok(p)
}

// ---------------------------------------------------------------------------
// Tauri commands
// ---------------------------------------------------------------------------

/// Runs blocking file-system work on Tauri's blocking thread pool.
pub async fn blocking<T, F>(f: F) -> Result<T, String>
where
    T: Send + 'static,
    F: FnOnce() -> Result<T, String> + Send + 'static,
{
    tauri::async_runtime::spawn_blocking(f)
        .await
        .map_err(|e| format!("EIO: background task failed: {e}"))?
}

#[tauri::command]
pub async fn fs_list(root: String) -> Result<Vec<FileStatDto>, String> {
    blocking(move || list(&root)).await
}

#[tauri::command]
pub async fn fs_stat(root: String, path: String) -> Result<Option<FileStatDto>, String> {
    blocking(move || stat(&root, &path)).await
}

#[tauri::command]
pub async fn fs_read_text(root: String, path: String) -> Result<String, String> {
    blocking(move || read_text(&root, &path)).await
}

#[tauri::command]
pub async fn fs_read_binary(root: String, path: String) -> Result<Response, String> {
    let bytes = blocking(move || read_binary(&root, &path)).await?;
    Ok(Response::new(bytes))
}

#[tauri::command]
pub async fn fs_write_text(
    root: String,
    path: String,
    content: String,
) -> Result<FileStatDto, String> {
    blocking(move || write(&root, &path, content.as_bytes())).await
}

/// Raw body = bytes; headers `x-root`, `x-path` (encodeURIComponent).
#[tauri::command]
pub async fn fs_write_binary(request: Request<'_>) -> Result<FileStatDto, String> {
    let root = header(&request, "x-root")?;
    let path = header(&request, "x-path")?;
    let data = body_bytes(request.body())?;
    blocking(move || write(&root, &path, &data)).await
}

#[tauri::command]
pub async fn fs_remove(root: String, path: String) -> Result<(), String> {
    blocking(move || remove(&root, &path)).await
}

#[tauri::command]
pub async fn fs_rename(root: String, from: String, to: String) -> Result<(), String> {
    blocking(move || rename(&root, &from, &to)).await
}

#[tauri::command]
pub async fn fs_mkdir(root: String, path: String) -> Result<(), String> {
    blocking(move || mkdir(&root, &path)).await
}

/// Raw body = bytes; header `x-path` = absolute target chosen in the save dialog
/// (encodeURIComponent). On Android the dialog returns a `content://` URI, which is
/// written through the ContentResolver.
#[tauri::command]
pub async fn write_export<R: tauri::Runtime>(
    app: tauri::AppHandle<R>,
    request: Request<'_>,
) -> Result<(), String> {
    let target = header(&request, "x-path")?;
    let data = body_bytes(request.body())?;
    #[cfg(target_os = "android")]
    if target.starts_with("content://") {
        return blocking(move || android_export::write_content_uri(&app, &target, &data)).await;
    }
    let _ = &app;
    blocking(move || {
        let path = export_target(&target)?;
        write_atomic(&path, &data)
            .map(|_| ())
            .map_err(|e| io_err(&e, &target))
    })
    .await
}

/// Lets the webview load files of the vault through the asset protocol (`convertFileSrc`),
/// e.g. images embedded in notes. Read-only, recursive, dot-folders excluded by Tauri.
#[tauri::command]
pub async fn allow_vault_assets<R: tauri::Runtime>(
    app: tauri::AppHandle<R>,
    root: String,
) -> Result<(), String> {
    use tauri::Manager;
    let path = validate_root(&root)?;
    app.asset_protocol_scope()
        .allow_directory(&path, true)
        .map_err(|e| err("EINVAL", e, &root))
}

/// `default_vault_dir` — desktop: `~/Brulion`; Android: `/storage/emulated/0/Brulion` when
/// shared storage is writable (all-files access granted), else `<app data>/vault`.
#[tauri::command]
pub async fn default_vault_dir<R: tauri::Runtime>(
    app: tauri::AppHandle<R>,
) -> Result<String, String> {
    blocking(move || {
        let dir = default_vault_path(&app)?;
        dir.into_os_string()
            .into_string()
            .map_err(|p| err("EINVAL", "path is not valid UTF-8", &p.to_string_lossy()))
    })
    .await
}

#[cfg(not(target_os = "android"))]
fn default_vault_path<R: tauri::Runtime>(app: &tauri::AppHandle<R>) -> Result<PathBuf, String> {
    use tauri::Manager;
    let home = app
        .path()
        .home_dir()
        .map_err(|e| err("ENOENT", format!("home directory not found ({e})"), "~"))?;
    Ok(home.join("Brulion"))
}

#[cfg(target_os = "android")]
fn default_vault_path<R: tauri::Runtime>(app: &tauri::AppHandle<R>) -> Result<PathBuf, String> {
    use tauri::Manager;
    let shared = PathBuf::from("/storage/emulated/0/Brulion");
    if dir_is_writable(&shared) {
        return Ok(shared);
    }
    let private = app
        .path()
        .app_data_dir()
        .map_err(|e| err("EIO", e, "app data dir"))?
        .join("vault");
    fs::create_dir_all(&private).map_err(|e| io_err(&e, &private.to_string_lossy()))?;
    Ok(private)
}

/// True when files can be created in `dir` (created if missing; a directory created only
/// for the probe is removed again when it turns out to be unusable).
#[cfg_attr(not(target_os = "android"), allow(dead_code))]
pub fn dir_is_writable(dir: &Path) -> bool {
    let existed = dir.is_dir();
    if !existed && fs::create_dir_all(dir).is_err() {
        return false;
    }
    let ok = match create_temp(dir) {
        Ok((tmp, file)) => {
            drop(file);
            fs::remove_file(&tmp).is_ok()
        }
        Err(_) => false,
    };
    if !ok && !existed {
        let _ = fs::remove_dir(dir);
    }
    ok
}

#[cfg(target_os = "android")]
mod android_export {
    use std::io::Write;

    use tauri_plugin_fs::{FilePath, FsExt, OpenOptions};

    use crate::paths::err;

    pub fn write_content_uri<R: tauri::Runtime>(
        app: &tauri::AppHandle<R>,
        uri: &str,
        data: &[u8],
    ) -> Result<(), String> {
        let path: FilePath = uri
            .parse()
            .map_err(|_| err("EINVAL", "invalid content URI", uri))?;
        let mut opts = OpenOptions::new();
        opts.write(true).truncate(true).create(true);
        let mut file = app.fs().open(path, opts).map_err(|e| err("EIO", e, uri))?;
        file.write_all(data).map_err(|e| err("EIO", e, uri))?;
        file.sync_all().ok();
        Ok(())
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::paths::EntryKind;

    fn vault() -> (tempfile::TempDir, String) {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path().to_str().unwrap().to_string();
        (dir, root)
    }

    fn temp_files(dir: &Path) -> Vec<String> {
        fs::read_dir(dir)
            .unwrap()
            .filter_map(|e| e.ok())
            .map(|e| e.file_name().to_string_lossy().into_owned())
            .filter(|n| n.starts_with(TEMP_PREFIX))
            .collect()
    }

    #[test]
    fn write_creates_parents_and_returns_stat() {
        let (dir, root) = vault();
        let st = write(&root, "dzienniki/2026_10_14.md", "- zadanie ąę".as_bytes()).unwrap();
        assert_eq!(st.path, "dzienniki/2026_10_14.md");
        assert_eq!(st.kind, EntryKind::File);
        assert_eq!(st.size, "- zadanie ąę".len() as u64);
        assert!(st.mtime > 1_600_000_000_000.0);
        assert_eq!(
            fs::read_to_string(dir.path().join("dzienniki/2026_10_14.md")).unwrap(),
            "- zadanie ąę"
        );
        assert!(temp_files(&dir.path().join("dzienniki")).is_empty());
    }

    #[test]
    fn atomic_write_replaces_content_and_keeps_permissions() {
        let (dir, root) = vault();
        let file = dir.path().join("a.md");
        fs::write(&file, "old content that is longer").unwrap();
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(&file, fs::Permissions::from_mode(0o600)).unwrap();
        }
        write(&root, "a.md", b"new").unwrap();
        assert_eq!(fs::read_to_string(&file).unwrap(), "new");
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let mode = fs::metadata(&file).unwrap().permissions().mode() & 0o777;
            assert_eq!(mode, 0o600);
        }
        assert!(temp_files(dir.path()).is_empty());
    }

    #[cfg(unix)]
    #[test]
    fn atomic_write_through_symlink_keeps_the_link() {
        let (dir, root) = vault();
        fs::create_dir(dir.path().join("real")).unwrap();
        fs::write(dir.path().join("real/n.md"), "old").unwrap();
        std::os::unix::fs::symlink(dir.path().join("real/n.md"), dir.path().join("link.md"))
            .unwrap();
        write(&root, "link.md", b"via link").unwrap();
        assert!(fs::symlink_metadata(dir.path().join("link.md"))
            .unwrap()
            .file_type()
            .is_symlink());
        assert_eq!(
            fs::read_to_string(dir.path().join("real/n.md")).unwrap(),
            "via link"
        );
    }

    #[test]
    fn write_refuses_directories_and_bad_paths() {
        let (dir, root) = vault();
        fs::create_dir(dir.path().join("folder")).unwrap();
        assert!(write(&root, "folder", b"x")
            .unwrap_err()
            .starts_with("EISDIR"));
        assert!(write(&root, "../x.md", b"x")
            .unwrap_err()
            .starts_with("EINVAL"));
        assert!(write(&root, "", b"x").unwrap_err().starts_with("EINVAL"));
        assert!(write("relative", "a.md", b"x").is_err());
        assert!(temp_files(dir.path()).is_empty());
    }

    #[test]
    fn write_atomic_cleans_up_on_failure() {
        let dir = tempfile::tempdir().unwrap();
        // The parent "file.md" is a regular file, so the directory cannot be created.
        fs::write(dir.path().join("file.md"), "x").unwrap();
        assert!(write_atomic(&dir.path().join("file.md/child.md"), b"x").is_err());
        assert!(temp_files(dir.path()).is_empty());
    }

    #[test]
    fn read_text_decodes_like_text_decoder() {
        let (dir, root) = vault();
        fs::write(
            dir.path().join("bom.md"),
            b"\xEF\xBB\xBFza\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87",
        )
        .unwrap();
        assert_eq!(read_text(&root, "bom.md").unwrap(), "zażółć");
        fs::write(dir.path().join("bad.md"), b"a\xFFb").unwrap();
        assert_eq!(read_text(&root, "bad.md").unwrap(), "a\u{fffd}b");
        let e = read_text(&root, "missing.md").unwrap_err();
        assert!(e.starts_with("ENOENT: "), "{e}");
        assert!(e.contains("os error 2"), "{e}");
        assert!(e.ends_with(": missing.md"), "{e}");
    }

    #[test]
    fn read_binary_roundtrip() {
        let (_dir, root) = vault();
        let data: Vec<u8> = (0..=255).collect();
        write(&root, "assets/obraz.png", &data).unwrap();
        assert_eq!(read_binary(&root, "assets/obraz.png").unwrap(), data);
        assert!(read_binary(&root, "assets")
            .unwrap_err()
            .starts_with("EISDIR"));
    }

    #[test]
    fn list_is_recursive_and_skips_git_and_node_modules() {
        let (dir, root) = vault();
        let p = dir.path();
        fs::create_dir_all(p.join("notes/sub")).unwrap();
        fs::create_dir_all(p.join(".git/objects")).unwrap();
        fs::create_dir_all(p.join("app/node_modules/x")).unwrap();
        fs::write(p.join("notes/sub/Łódź.md"), "abc").unwrap();
        fs::write(p.join("top.md"), "x").unwrap();
        fs::write(p.join(".git/HEAD"), "ref").unwrap();
        fs::write(p.join("app/node_modules/x/i.js"), "").unwrap();
        fs::write(p.join(".gitignore"), "").unwrap();
        let listed = list(&root).unwrap();
        let paths: Vec<(&str, EntryKind)> =
            listed.iter().map(|s| (s.path.as_str(), s.kind)).collect();
        assert_eq!(
            paths,
            vec![
                (".gitignore", EntryKind::File),
                ("app", EntryKind::Dir),
                ("notes", EntryKind::Dir),
                ("notes/sub", EntryKind::Dir),
                ("notes/sub/Łódź.md", EntryKind::File),
                ("top.md", EntryKind::File),
            ]
        );
        let lodz = listed.iter().find(|s| s.path.ends_with(".md")).unwrap();
        assert_eq!(lodz.size, 3);
        assert!(listed
            .iter()
            .filter(|s| s.kind == EntryKind::Dir)
            .all(|s| s.size == 0));
    }

    #[cfg(unix)]
    #[test]
    fn list_does_not_follow_directory_symlinks() {
        let (dir, root) = vault();
        let outside = tempfile::tempdir().unwrap();
        fs::write(outside.path().join("secret.md"), "s").unwrap();
        fs::write(dir.path().join("real.md"), "abcd").unwrap();
        std::os::unix::fs::symlink(outside.path(), dir.path().join("linked-dir")).unwrap();
        std::os::unix::fs::symlink(dir.path().join("real.md"), dir.path().join("alias.md"))
            .unwrap();
        std::os::unix::fs::symlink(dir.path().join("gone.md"), dir.path().join("broken.md"))
            .unwrap();
        // A loop must not hang the walk.
        std::os::unix::fs::symlink(dir.path(), dir.path().join("loop")).unwrap();
        let listed = list(&root).unwrap();
        let paths: Vec<&str> = listed.iter().map(|s| s.path.as_str()).collect();
        assert_eq!(paths, vec!["alias.md", "real.md"]);
        assert_eq!(listed[0].size, 4);
    }

    #[test]
    fn list_errors_for_missing_root() {
        let (dir, root) = vault();
        let missing = format!("{root}/nie-ma");
        assert!(list(&missing).unwrap_err().starts_with("ENOENT"));
        fs::write(dir.path().join("f"), "").unwrap();
        assert!(list(&format!("{root}/f"))
            .unwrap_err()
            .starts_with("ENOTDIR"));
        assert!(list("").is_err());
    }

    #[test]
    fn stat_handles_root_files_and_missing() {
        let (_dir, root) = vault();
        write(&root, "a/b.md", b"12345").unwrap();
        let st = stat(&root, "a/b.md").unwrap().unwrap();
        assert_eq!(
            (st.path.as_str(), st.kind, st.size),
            ("a/b.md", EntryKind::File, 5)
        );
        let d = stat(&root, "a").unwrap().unwrap();
        assert_eq!((d.kind, d.size), (EntryKind::Dir, 0));
        let r = stat(&root, "").unwrap().unwrap();
        assert_eq!((r.path.as_str(), r.kind), ("", EntryKind::Dir));
        assert_eq!(stat(&root, "nope.md").unwrap(), None);
        assert_eq!(stat(&root, "a/b.md/child").unwrap(), None);
        assert!(stat(&root, "../x").is_err());
    }

    #[test]
    fn remove_files_dirs_and_missing() {
        let (dir, root) = vault();
        write(&root, "d/e/f.md", b"x").unwrap();
        write(&root, "g.md", b"x").unwrap();
        remove(&root, "g.md").unwrap();
        assert!(!dir.path().join("g.md").exists());
        remove(&root, "d").unwrap();
        assert!(!dir.path().join("d").exists());
        remove(&root, "missing/x.md").unwrap();
        assert!(remove(&root, "").unwrap_err().starts_with("EINVAL"));
        assert!(dir.path().exists());
    }

    #[test]
    fn remove_below_a_file_is_missing_not_an_error() {
        // MemoryFs and the adapter contract: "No error if missing". A path below a file
        // cannot exist (ENOTDIR), exactly like fs_stat reporting null for it.
        let (dir, root) = vault();
        write(&root, "plik.md", b"x").unwrap();
        remove(&root, "plik.md/dziecko.md").unwrap();
        remove(&root, "plik.md/a/b").unwrap();
        assert!(dir.path().join("plik.md").is_file());
    }

    #[test]
    fn rename_moves_and_creates_parents() {
        let (dir, root) = vault();
        write(&root, "a.md", b"A").unwrap();
        rename(&root, "a.md", "archiwum/2026/a.md").unwrap();
        assert_eq!(
            fs::read_to_string(dir.path().join("archiwum/2026/a.md")).unwrap(),
            "A"
        );
        // Replacing an existing file follows POSIX semantics.
        write(&root, "b.md", b"B").unwrap();
        rename(&root, "b.md", "archiwum/2026/a.md").unwrap();
        assert_eq!(
            fs::read_to_string(dir.path().join("archiwum/2026/a.md")).unwrap(),
            "B"
        );
        // Same path is a no-op.
        rename(&root, "archiwum", "archiwum").unwrap();
        assert!(rename(&root, "archiwum", "archiwum/2026/x")
            .unwrap_err()
            .starts_with("EINVAL"));
        assert!(rename(&root, "nope.md", "x.md")
            .unwrap_err()
            .starts_with("ENOENT"));
        assert!(rename(&root, "archiwum", "../escape").is_err());
        rename(&root, "archiwum", "stare").unwrap();
        assert!(dir.path().join("stare/2026/a.md").exists());
    }

    #[test]
    fn mkdir_recursive_and_root() {
        let (dir, root) = vault();
        mkdir(&root, "x/y/z").unwrap();
        assert!(dir.path().join("x/y/z").is_dir());
        mkdir(&root, "x/y/z").unwrap();
        write(&root, "plik", b"").unwrap();
        assert!(mkdir(&root, "plik").unwrap_err().starts_with("EEXIST"));
        let fresh = format!("{root}/nowy-sejf");
        mkdir(&fresh, "").unwrap();
        assert!(dir.path().join("nowy-sejf").is_dir());
    }

    #[test]
    fn body_bytes_accepts_raw_and_json_arrays() {
        assert_eq!(
            body_bytes(&InvokeBody::Raw(vec![1, 2, 255])).unwrap(),
            vec![1, 2, 255]
        );
        assert_eq!(
            body_bytes(&InvokeBody::Json(serde_json::json!([0, 128, 255]))).unwrap(),
            vec![0, 128, 255]
        );
        assert!(body_bytes(&InvokeBody::Json(serde_json::json!([256]))).is_err());
        assert!(body_bytes(&InvokeBody::Json(serde_json::json!({ "a": 1 }))).is_err());
    }

    #[test]
    fn body_bytes_accepts_compact_base64_objects() {
        // Android: a big attachment as `{ base64 }` instead of a JSON number per byte.
        let ok = |v: serde_json::Value| body_bytes(&InvokeBody::Json(v));
        assert_eq!(
            ok(serde_json::json!({ "base64": "AAH/" })).unwrap(),
            vec![0, 1, 255]
        );
        assert_eq!(
            ok(serde_json::json!({ "base64": "xYLDs2TFug==" })).unwrap(),
            "łódź".as_bytes()
        );
        assert_eq!(
            ok(serde_json::json!({ "base64": "" })).unwrap(),
            Vec::<u8>::new()
        );
        for bad in [
            serde_json::json!({ "base64": "nie base64!" }),
            serde_json::json!({ "base64": 12 }),
            serde_json::json!({ "base64": "AAH/", "extra": 1 }),
            serde_json::json!("AAH/"),
        ] {
            let e = ok(bad.clone()).unwrap_err();
            assert!(e.starts_with("EINVAL: "), "{bad} -> {e}");
        }
    }

    #[test]
    fn export_target_must_be_absolute_file() {
        assert!(export_target("/home/u/Eksport.zip").is_ok());
        assert!(export_target("Eksport.zip").is_err());
        assert!(export_target("/").is_err());
        assert!(export_target("/home/../etc/x").is_err());
    }

    #[test]
    fn writable_probe() {
        let dir = tempfile::tempdir().unwrap();
        let fresh = dir.path().join("Brulion");
        assert!(dir_is_writable(&fresh));
        assert!(fresh.is_dir());
        assert!(temp_files(&fresh).is_empty());
        assert!(dir_is_writable(&fresh), "existing folder");
        fs::write(dir.path().join("plik"), "").unwrap();
        assert!(!dir_is_writable(&dir.path().join("plik/x")));
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let ro = dir.path().join("ro");
            fs::create_dir(&ro).unwrap();
            fs::set_permissions(&ro, fs::Permissions::from_mode(0o555)).unwrap();
            // Root ignores permission bits; only assert when the probe can tell.
            if fs::write(ro.join("t"), "").is_err() {
                assert!(!dir_is_writable(&ro.join("sub")));
                assert!(!ro.join("sub").exists());
            }
            fs::set_permissions(&ro, fs::Permissions::from_mode(0o755)).unwrap();
        }
    }

    #[test]
    fn decode_utf8_variants() {
        assert_eq!(decode_utf8(b"plain".to_vec()), "plain");
        assert_eq!(decode_utf8(b"\xEF\xBB\xBF".to_vec()), "");
        assert_eq!(decode_utf8(Vec::new()), "");
    }
}
