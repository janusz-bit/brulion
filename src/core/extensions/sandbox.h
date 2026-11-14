#pragma once
// An extension tried in a sandbox (docs/EXTENSIONS.md "Written by an AI agent"): loaded in an
// engine of its own over the notebook, with a dry-run NotebookHost (notebookhost.h), so that its
// top-level code runs as when the app loads it, but nothing is written and nothing stays
// registered. `brulion-cli extension check / write` and b.checkExtension (the AI assistant's
// check_extension) report the same.
//
//   const QVariantMap r = checkExtensionPage(vault, index, u"Rozszerzenia/Licznik.md"_s, text, options);
//   r["ok"], r["error"], r["line"]  (the page's line, 1-based), r["commands"] …
//
// The report (a QVariantMap; also what b.checkExtension returns):
//   ok        it loads
//   error     why not (first line), `line` the page's line it names (1-based), `traceback`,
//             `status` (error | timeout | out_of_memory | stack_overflow)
//   commands  [{name, label, block}], views [{name, label, icon, actions}], hooks [event]
//   printed   what print / b.log wrote while loading
//   writes    [{path, created, before, after}]: what its top-level code would have written
// checkExtensionPage() adds what the page says: isExtension, id, title, path, permissions,
// description, overrides, problems, blocks, codeHash.
//
// A check never nests: called while one runs (code being checked calls b.checkExtension), it
// reports an error instead.

#include "extensions/engine.h"
#include "extensions/notebookhost.h"
#include "types.h"

#include <QVariantMap>

namespace brulion {
class Vault;
class VaultIndex;
}

namespace brulion::extensions {

/// Loads `extension` in a fresh engine over `vault` and `index` (`host` with dryRun forced on).
QVariantMap checkExtension(Vault &vault, VaultIndex &index, const Extension &extension,
                           NotebookHost::Options host, const Limits &limits = Limits{});

/// The same for a page text: `content` as the extension page at `path` would be — not tagged as an
/// extension, or without code, it does not load (isExtension, problems). A page that overrides a
/// built-in extension runs under the built-in's id, with its data, as in the app.
QVariantMap checkExtensionPage(Vault &vault, VaultIndex &index, const VaultPath &path, const QString &content,
                               const NotebookHost::Options &host, const Limits &limits = Limits{});

} // namespace brulion::extensions
