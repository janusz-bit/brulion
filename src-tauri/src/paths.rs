//! Vault path safety and shared DTOs.
//!
//! Every vault command receives `root` (absolute folder of the vault) and a `VaultPath`
//! (relative, `/`-separated). [`resolve`] joins them and rejects anything that could escape
//! the root lexically: absolute paths, `..`, root/prefix components. Errors are formatted
//! like the frontend's `FsError` messages: `"<CODE>: <message>: <path>"` (POSIX-style code
//! first), so `src/core/storage/errors.ts` can recognise e.g. `ENOENT`.

use std::fs::Metadata;
use std::io;
use std::path::{Component, Path, PathBuf};
use std::time::UNIX_EPOCH;

use serde::Serialize;

/// Directory names that are never listed or reported by the watcher.
pub const SKIPPED_DIRS: &[&str] = &[".git", "node_modules"];

/// Prefix of the temp files used for atomic writes (see `fs_cmds::write_atomic`).
pub const TEMP_PREFIX: &str = ".brulion-tmp-";

/// `FileStatDto` of the IPC contract.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct FileStatDto {
    pub path: String,
    pub kind: EntryKind,
    pub size: u64,
    /// Modification time in ms since the Unix epoch.
    pub mtime: f64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum EntryKind {
    File,
    Dir,
}

impl FileStatDto {
    pub fn from_metadata(path: String, meta: &Metadata) -> Self {
        let is_dir = meta.is_dir();
        FileStatDto {
            path,
            kind: if is_dir {
                EntryKind::Dir
            } else {
                EntryKind::File
            },
            size: if is_dir { 0 } else { meta.len() },
            mtime: mtime_ms(meta),
        }
    }
}

/// Modification time in whole milliseconds (0 when unavailable or before 1970).
pub fn mtime_ms(meta: &Metadata) -> f64 {
    meta.modified()
        .ok()
        .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
        .map(|d| d.as_millis() as f64)
        .unwrap_or(0.0)
}

/// Error string `"<CODE>: <message>: <path>"`.
pub fn err(code: &str, message: impl std::fmt::Display, path: &str) -> String {
    format!("{code}: {message}: {path}")
}

/// Maps an io error to the frontend's error format.
pub fn io_err(e: &io::Error, path: &str) -> String {
    err(io_code(e), e, path)
}

/// POSIX-style code for an io error.
pub fn io_code(e: &io::Error) -> &'static str {
    use io::ErrorKind as K;
    match e.kind() {
        K::NotFound => "ENOENT",
        K::AlreadyExists => "EEXIST",
        K::IsADirectory => "EISDIR",
        K::NotADirectory => "ENOTDIR",
        K::DirectoryNotEmpty => "ENOTEMPTY",
        K::InvalidInput | K::InvalidData | K::InvalidFilename => "EINVAL",
        K::PermissionDenied => "EACCES",
        K::StorageFull => "ENOSPC",
        K::ReadOnlyFilesystem => "EROFS",
        K::CrossesDevices => "EXDEV",
        _ => "EIO",
    }
}

/// Validates the vault root: non-empty, absolute, no `..` components.
pub fn validate_root(root: &str) -> Result<PathBuf, String> {
    if root.is_empty() {
        return Err(err("EINVAL", "vault root must not be empty", root));
    }
    if root.contains('\0') {
        return Err(err("EINVAL", "vault root contains a NUL byte", root));
    }
    let path = Path::new(root);
    if !path.is_absolute() {
        return Err(err("EINVAL", "vault root must be an absolute path", root));
    }
    if path.components().any(|c| matches!(c, Component::ParentDir)) {
        return Err(err("EINVAL", "vault root must not contain '..'", root));
    }
    Ok(path.to_path_buf())
}

/// Normalises a relative vault path into its components: `.` and empty segments are dropped;
/// absolute paths, `..`, root and prefix components are rejected.
pub fn normalize_rel(rel: &str) -> Result<Vec<&str>, String> {
    if rel.contains('\0') {
        return Err(err("EINVAL", "path contains a NUL byte", rel));
    }
    let path = Path::new(rel);
    if path.is_absolute() || path.has_root() {
        return Err(err("EINVAL", "path must be relative to the vault", rel));
    }
    let mut parts = Vec::new();
    for c in path.components() {
        match c {
            Component::Normal(s) => match s.to_str() {
                Some(s) => parts.push(s),
                None => return Err(err("EINVAL", "path is not valid UTF-8", rel)),
            },
            Component::CurDir => {}
            Component::ParentDir => {
                return Err(err("EINVAL", "path must not contain '..'", rel));
            }
            Component::RootDir | Component::Prefix(_) => {
                return Err(err("EINVAL", "path must be relative to the vault", rel));
            }
        }
    }
    Ok(parts)
}

/// A resolved vault entry: absolute path plus the normalised `VaultPath`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Resolved {
    pub abs: PathBuf,
    /// Normalised relative path (`""` for the root itself).
    pub rel: String,
}

/// Joins `root` and `rel` safely. An empty `rel` resolves to the root itself.
pub fn resolve(root: &str, rel: &str) -> Result<Resolved, String> {
    let root_path = validate_root(root)?;
    let parts = normalize_rel(rel)?;
    let mut abs = root_path;
    for p in &parts {
        abs.push(p);
    }
    Ok(Resolved {
        abs,
        rel: parts.join("/"),
    })
}

/// Like [`resolve`], but `rel` must name an entry inside the root (not the root itself).
/// Used by every operation that reads, writes, removes or renames a single entry.
pub fn resolve_entry(root: &str, rel: &str) -> Result<Resolved, String> {
    let r = resolve(root, rel)?;
    if r.rel.is_empty() {
        return Err(err("EINVAL", "path must not be empty", rel));
    }
    Ok(r)
}

/// `abs` relative to `root` as a `/`-separated VaultPath. `None` when outside the root,
/// equal to the root, or not valid UTF-8.
pub fn rel_from_abs(root: &Path, abs: &Path) -> Option<String> {
    let rest = abs.strip_prefix(root).ok()?;
    let mut parts = Vec::new();
    for c in rest.components() {
        match c {
            Component::Normal(s) => parts.push(s.to_str()?),
            Component::CurDir => {}
            _ => return None,
        }
    }
    if parts.is_empty() {
        None
    } else {
        Some(parts.join("/"))
    }
}

/// True for file names of temp files written by Brulion or Syncthing while saving.
pub fn is_temp_name(name: &str) -> bool {
    name.starts_with(TEMP_PREFIX)
        || (name.starts_with("~syncthing~") && name.ends_with(".tmp"))
        || (name.starts_with(".syncthing.") && name.ends_with(".tmp"))
}

/// True for VaultPaths the watcher never reports: anything inside (or being) `.git` /
/// `node_modules`, and temp files.
pub fn is_ignored_rel(rel: &str) -> bool {
    let mut segments = rel.split('/').peekable();
    while let Some(seg) = segments.next() {
        if SKIPPED_DIRS.contains(&seg) {
            return true;
        }
        if segments.peek().is_none() && is_temp_name(seg) {
            return true;
        }
    }
    false
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn resolves_normal_paths() {
        let r = resolve("/vault", "notes/a.md").unwrap();
        assert_eq!(r.abs, PathBuf::from("/vault/notes/a.md"));
        assert_eq!(r.rel, "notes/a.md");
    }

    #[test]
    fn normalises_redundant_segments() {
        let r = resolve("/vault/", "./notes//dzień ąćęłńóśźż.md").unwrap();
        assert_eq!(r.abs, PathBuf::from("/vault/notes/dzień ąćęłńóśźż.md"));
        assert_eq!(r.rel, "notes/dzień ąćęłńóśźż.md");
        assert_eq!(resolve("/vault", "a/./b/").unwrap().rel, "a/b");
    }

    #[test]
    fn empty_rel_is_the_root_but_not_an_entry() {
        let r = resolve("/vault", "").unwrap();
        assert_eq!(r.abs, PathBuf::from("/vault"));
        assert_eq!(r.rel, "");
        assert_eq!(resolve("/vault", ".").unwrap().rel, "");
        assert!(resolve_entry("/vault", "")
            .unwrap_err()
            .starts_with("EINVAL"));
        assert!(resolve_entry("/vault", "./").is_err());
        assert!(resolve_entry("/vault", "a.md").is_ok());
    }

    #[test]
    fn rejects_escapes() {
        for bad in [
            "../etc/passwd",
            "notes/../../x",
            "notes/..",
            "..",
            "/etc/passwd",
            "/",
            "a/\0b",
        ] {
            let e = resolve("/vault", bad).unwrap_err();
            assert!(e.starts_with("EINVAL: "), "{bad:?} -> {e}");
        }
    }

    #[test]
    fn rejects_bad_roots() {
        assert!(resolve("", "a.md").unwrap_err().contains("empty"));
        assert!(resolve("vault", "a.md").unwrap_err().contains("absolute"));
        assert!(resolve("./vault", "a.md").is_err());
        assert!(resolve("/home/../vault", "a.md")
            .unwrap_err()
            .contains(".."));
    }

    #[test]
    fn dot_names_that_are_not_traversal_are_fine() {
        assert_eq!(resolve("/v", "..md").unwrap().rel, "..md");
        assert_eq!(resolve("/v", ".trash/a.md").unwrap().rel, ".trash/a.md");
        assert_eq!(resolve("/v", "a..b/c").unwrap().rel, "a..b/c");
    }

    #[test]
    fn rel_from_abs_works() {
        let root = Path::new("/vault");
        assert_eq!(
            rel_from_abs(root, Path::new("/vault/a/b.md")).as_deref(),
            Some("a/b.md")
        );
        assert_eq!(rel_from_abs(root, Path::new("/vault")), None);
        assert_eq!(rel_from_abs(root, Path::new("/other/a.md")), None);
        assert_eq!(rel_from_abs(root, Path::new("/vaultx/a.md")), None);
    }

    #[test]
    fn temp_and_ignored_names() {
        assert!(is_temp_name(".brulion-tmp-0123abcd"));
        assert!(is_temp_name("~syncthing~notatka.md.tmp"));
        assert!(is_temp_name(".syncthing.notatka.md.tmp"));
        assert!(!is_temp_name("notatka.md"));
        assert!(!is_temp_name(".syncthing.md"));
        assert!(is_ignored_rel(".git"));
        assert!(is_ignored_rel(".git/HEAD"));
        assert!(is_ignored_rel("projekt/node_modules/x/index.js"));
        assert!(is_ignored_rel("notes/.brulion-tmp-1"));
        assert!(!is_ignored_rel("notes/a.md"));
        assert!(!is_ignored_rel(".gitignore"));
        assert!(!is_ignored_rel(".brulion-tmp-dir/a.md"));
    }

    #[test]
    fn io_errors_carry_posix_codes() {
        let e = io::Error::from(io::ErrorKind::NotFound);
        let s = io_err(&e, "a.md");
        assert!(s.starts_with("ENOENT: "));
        assert!(s.ends_with(": a.md"));
        assert_eq!(
            io_code(&io::Error::from(io::ErrorKind::AlreadyExists)),
            "EEXIST"
        );
        assert_eq!(io_code(&io::Error::other("x")), "EIO");
    }

    #[test]
    fn dto_serialises_like_the_contract() {
        let dto = FileStatDto {
            path: "a.md".into(),
            kind: EntryKind::File,
            size: 3,
            mtime: 1_700_000_000_123.0,
        };
        let v = serde_json::to_value(&dto).unwrap();
        assert_eq!(
            v,
            serde_json::json!({ "path": "a.md", "kind": "file", "size": 3, "mtime": 1_700_000_000_123.0 })
        );
    }
}
