// Weekly and monthly notes in a vault (docs/SYNTAX.md §1 "Weekly and monthly notes"): where the note
// of a period is (also under another letter case of its folder), its starting text from the period's
// template and creating it — as notebook/journals.h does for the daily notes. The names themselves:
// periodic/periodic.h. Part of the notebook module — see notebook/notebook.h.
#pragma once

#include "notebook/journals.h"
#include "periodic/periodic.h"

namespace brulion::notebook {

/// The note of the period holding `date` under any spelling periodic::dateFromPath() accepts (the
/// folder in another letter case), the configured spelling first; "" when there is none.
VaultPath existingPeriodicNote(const Vault &vault, periodic::Period period, QDate date);

/// The note of the period holding `date`: its existing file (also one on disk the vault has not
/// listed yet), else where it will be created. Never writes; "" for an invalid date.
VaultPath periodicFileFor(const Vault &vault, periodic::Period period, QDate date);

/// The text a new note of the period starts with: its template (weeklyTemplate / monthlyTemplate)
/// filled in with `{{date}}` the period's first day (at `now`'s time), `{{end}}` its last day and
/// `{{title}}` the file's name; "" without a template. Never writes.
QString newPeriodicText(Vault &vault, periodic::Period period, QDate date, const QDateTime &now,
                        dates::UiLocale locale);

/// The note of the period holding `date`, created from its template when missing (a note another
/// program creates meanwhile is kept, as ensureJournal()).
EnsuredJournal ensurePeriodicNote(Vault &vault, periodic::Period period, QDate date, const QDateTime &now,
                                  dates::UiLocale locale, ChangeSource source = ChangeSource::User);

} // namespace brulion::notebook
