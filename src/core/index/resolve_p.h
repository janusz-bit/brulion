// Link resolution (docs/SYNTAX.md §2) for the vault index: target -> vault file. Internal.
//
// Order: 1) exact vault path (with or without `.md`, case-insensitive, NFC; among case variants
// the exact spelling wins); 2) path relative to the linking file's folder; 3) base-name match
// (case-insensitive, NFC; for targets with folders a path-suffix match) — several candidates:
// shortest path, then alphabetical (code units); 4) frontmatter `aliases`. Explicitly relative
// targets (`./x`, `../x`) try the relative path first; a leading `/` anchors the target at the
// vault root. A target with a real file extension (`photo.png`, see isAttachmentExtension())
// prefers attachments, anything else (`Dom`, `Dr. Smith`, `Spotkanie 14.10`) prefers notes.
//
// NameTable keeps the lookup maps incrementally. Every file a target can resolve to shares the
// target's linkKey() (lower-cased base name without `.md`) as its note/file name key or as the
// linkKey() of one of its aliases, which lets the index re-resolve only the links that a file
// change can affect.
#pragma once

#include "types.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

namespace brulion::index_detail {

/// Case-insensitive key of a vault path: NFC, lower-cased ("Projekty/Dom.md" -> "projekty/dom.md").
QString pathKey(const QString &path);
/// Name key of a note: lower-cased title ("Projekty/Dom.md" -> "dom").
QString noteNameKey(const VaultPath &path);
/// Name key of an attachment: lower-cased file name ("assets/Foto.PNG" -> "foto.png").
QString fileNameKey(const VaultPath &path);
/// Key of an alias: NFC, trimmed, lower-cased.
QString aliasKey(const QString &alias);
/// How a link target is written.
enum class TargetForm {
    /// As typed by a user or API caller: may carry `[[ ]]`, a `|display` part and a `#fragment`.
    Raw,
    /// Link::target from the Markdown parser: display text and fragment are already split off,
    /// and a Markdown link target is percent-decoded — so '#' and '|' in it are part of the
    /// file name (`[C#](C%23.md)` -> "C#.md").
    Parsed,
};

/// Strip what is not part of the file name: for Raw targets surrounding `[[ ]]`, a `|display`
/// part (also the table-escaped `\|`) and a `#heading` / `#^block` fragment; trim; backslashes
/// become slashes.
QString cleanTarget(const QString &target, TargetForm form = TargetForm::Raw);
/// Invalidation key of a link target: its base name without `.md`, NFC, lower-cased.
QString linkKey(const QString &target, TargetForm form = TargetForm::Raw);
/// Invalidation keys under which links that may resolve through `alias` are filed (the alias as
/// a Raw and as a Parsed target).
QStringList aliasLinkKeys(const QString &alias);
/// Does the target carry a URL scheme (`https:`, `mailto:`, …)? Windows drives excluded.
bool isExternalTarget(QStringView target);
/// Is `ext` (as returned by paths::extension(), with the dot) a real file extension: 1-10 ASCII
/// letters/digits with at least one letter, not Markdown? ".png", ".pdf", ".mp3" are; "",
/// ". smith" ("Dr. Smith"), ".10" ("Spotkanie 14.10"), ".2" ("v1.2") are not.
bool isAttachmentExtension(QStringView ext);
/// Is an unresolved target a page (no real extension, or `.md`) rather than a missing attachment?
bool isPageTarget(const QString &target, TargetForm form = TargetForm::Raw);

/// Shortest path, then smallest by code units; empty for an empty set.
VaultPath pickBest(const QSet<VaultPath> &paths);

/// Incrementally maintained lookup tables for link resolution.
class NameTable {
public:
    void addNote(const VaultPath &path, const QStringList &aliases);
    void removeNote(const VaultPath &path, const QStringList &aliases);
    void addAttachment(const VaultPath &path);
    void removeAttachment(const VaultPath &path);
    void clear();

    /// Resolve `target` written in `from` (see the file comment). An empty target (`[[#^id]]`)
    /// resolves to `from`. Returns an empty path when nothing matches.
    VaultPath resolve(const QString &target, const VaultPath &from = {}, TargetForm form = TargetForm::Raw) const;

private:
    VaultPath resolveBare(const QString &target, const VaultPath &from) const;
    /// File at exactly `path` (case-insensitive, NFC), also with `.md` / `.markdown` added;
    /// notes first unless `attachmentFirst`. Among case variants the exact spelling wins.
    VaultPath lookupPath(const QString &path, bool attachmentFirst) const;
    /// lookupPath() of one key; `spelled` is the NFC path as written.
    VaultPath lookupExact(const QString &key, const QString &spelled) const;
    VaultPath lookupName(const QString &lowerPath) const;
    VaultPath lookupAlias(const QString &target) const;

    QHash<QString, QSet<VaultPath>> m_byPath;    ///< pathKey -> notes and attachments
    QHash<QString, QSet<VaultPath>> m_noteNames; ///< noteNameKey -> notes
    QHash<QString, QSet<VaultPath>> m_fileNames; ///< fileNameKey -> attachments
    QHash<QString, QSet<VaultPath>> m_aliases;   ///< aliasKey -> notes
};

/// Add `value` to the set stored under `key`.
template<typename K, typename V>
void multiAdd(QHash<K, QSet<V>> &map, const K &key, const V &value)
{
    map[key].insert(value);
}

/// Remove `value` from the set under `key`, dropping empty sets.
template<typename K, typename V>
void multiRemove(QHash<K, QSet<V>> &map, const K &key, const V &value)
{
    const auto it = map.find(key);
    if (it == map.end())
        return;
    it->remove(value);
    if (it->isEmpty())
        map.erase(it);
}

/// Add 1 to the count of `value` under `key`.
template<typename K, typename V>
void countAdd(QHash<K, QHash<V, int>> &map, const K &key, const V &value)
{
    ++map[key][value];
}

/// Subtract 1 from the count of `value` under `key`, dropping zero counts and empty maps.
template<typename K, typename V>
void countRemove(QHash<K, QHash<V, int>> &map, const K &key, const V &value)
{
    const auto it = map.find(key);
    if (it == map.end())
        return;
    const auto inner = it->find(value);
    if (inner != it->end() && --(*inner) <= 0)
        it->erase(inner);
    if (it->isEmpty())
        map.erase(it);
}

} // namespace brulion::index_detail
