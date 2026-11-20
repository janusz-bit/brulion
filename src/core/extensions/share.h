#pragma once
// Extensions shared between notebooks (docs/EXTENSIONS.md "Sharing between notebooks"): an
// extension page is copied into another notebook at the same path — with the extension pages its
// code requires (b.require with a page's name written out), so that it runs there as here. The
// copy is a page of that notebook from then on: synced, edited and versioned with it. Settings →
// Extensions → "Copy to another notebook" (the app approves the copy there for this device: the
// user chose to run that code) and `brulion-cli extension copy` (which approves nothing).
//
//   Vault target(root, deviceStatePath(dataDir, root)); target.open();
//   const CopyReport r = copyExtension(index, u"Rozszerzenia/Licznik słów.md"_s, target, false);
//   r.written, r.unchanged, r.conflicts (a page there with other text: left alone unless `replace`)
//
// Approvals of this device in a notebook's device state (pageextension.h's keys) are written by
// approveInStore(): the hash, and the code for the next approval's diff.

#include "extensions/pageextension.h"
#include "types.h"

#include <QStringList>

namespace brulion {
class LocalStore;
class Vault;
class VaultIndex;
} // namespace brulion

namespace brulion::extensions {

/// The pages `code` requires with a literal name: b.require("Wspólne") and b.require 'Wspólne'
/// (a name computed at run time is not found), each once, in order.
QStringList requiredNames(const QString &code);

struct CopyReport {
    QString error;              ///< why nothing was copied ("" = it was)
    QStringList written;        ///< paths written in the target notebook
    QStringList unchanged;      ///< already there with the same text
    QStringList conflicts;      ///< there with another text and not replaced
    QList<PageExtension> pages; ///< the extension pages copied (written or unchanged), the first one first
};

/// Copies the extension page `path` of `index` and the extension pages its code requires (their
/// requirements too) into `target` at the same paths. `replace`: a page there with other text is
/// written over (its old text stays in the target's history), else it is a conflict.
CopyReport copyExtension(const VaultIndex &index, const VaultPath &path, Vault &target, bool replace);

/// The engine's id of an extension page: its own, or the built-in's it overrides.
QString runIdOf(const PageExtension &page);

/// Records in `store` (a notebook's device state) that this device approved `page`: its hash among
/// the approved ones (the newest 300 kept) and its code as approved (approvedCodeStoreKey()).
void approveInStore(LocalStore *store, const PageExtension &page);

/// The code `store` keeps as last approved for the extension `id`: {hash, code, permissions, title},
/// or an empty map.
QVariantMap approvedCode(const LocalStore *store, const QString &id);

/// How many approvals a notebook's device state keeps (the newest).
constexpr int kMaxApprovals = 300;

} // namespace brulion::extensions
