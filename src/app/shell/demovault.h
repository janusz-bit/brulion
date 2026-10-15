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

/// Write the sample files into an open vault; files that already exist are left alone.
/// Returns how many files were written (-1 when a write failed).
int seedDemoVault(Vault &vault, const QDateTime &now);

} // namespace brulion::shell
