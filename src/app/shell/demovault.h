// The sample vault "Przykładowy brulion": a Polish guide page — a document (`brulion: document`)
// — that shows every feature with live examples (the two note modes, outliner, links, tags, block
// references, tasks, reminders and alarms, calc blocks, tables with formulas, queries, embeds,
// callouts, templates, calendar, sync, import/export, shortcuts), daily notes for today /
// yesterday / a week ago (outlines), project pages with tables and formulas (the renovation one
// an outline: `brulion: outline`), templates and an image. Dates are relative to `now`, so tasks,
// reminders and the calendar look alive whenever it is created.
#pragma once

#include "types.h"

#include <QDateTime>
#include <QList>
#include <QString>

namespace brulion {
class Vault;
}

namespace brulion::shell {

struct DemoFile {
    VaultPath path;
    QString content;
};

/// The guide page every sample vault opens with.
QString demoWelcomePage();

/// Files of the sample vault (journal paths use the default "journals/yyyy-MM-dd.md").
QList<DemoFile> demoVaultFiles(const QDateTime &now);

/// The first page of the documentation notebook "Dokumentacja Brulionu" and all its files.
QString docsWelcomePage();
QList<DemoFile> docsVaultFiles();

/// What updateDocsVault() did.
struct DocsUpdate {
    int added = 0;   ///< pages the notebook did not have (on the first open: all of them)
    int updated = 0; ///< pages nobody changed, replaced by their new version
    int removed = 0; ///< pages nobody changed that are no longer part of the documentation (trash)
    int kept = 0;    ///< pages left as they are: changed by the user (or the user's own page)
    bool ok = true;  ///< false when a write failed (Vault::errorString())
};

/// Write the documentation into an open vault and keep it current with the app. The first time
/// every page; later the pages new in this version, and the new version of every page whose
/// content is still exactly what was written last time. Pages the user changed stay as they are,
/// pages the user deleted stay deleted, unchanged pages dropped from the documentation go to the
/// trash. What was written (per page, the hash of its content) is recorded in the vault's
/// device-local LocalStore, never among the notes. Notebooks written by 0.3.0–0.3.5 have no
/// record: their unchanged pages are recognised by the hashes of those versions' pages.
DocsUpdate updateDocsVault(Vault &vault, const QList<DemoFile> &pages = docsVaultFiles());

/// Write the sample files into an open vault; files that already exist are left alone.
/// Returns how many files were written (-1 when a write failed).
int seedDemoVault(Vault &vault, const QDateTime &now);

} // namespace brulion::shell
