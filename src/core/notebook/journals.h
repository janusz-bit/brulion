// Daily notes as the app and the command line write them (docs/SYNTAX.md §1, §12): where the
// note of a day is (also under another name the journal folder accepts), creating it from the
// journal template, and appending a block to a note. Part of the notebook module — see
// notebook/notebook.h.
#pragma once

#include "dates/names.h"
#include "types.h"

#include <QDate>
#include <QDateTime>
#include <QString>

namespace brulion {
class Vault;
}

namespace brulion::notebook {

/// The language note content is written in ({{weekday}} of templates, quick capture): "en" is
/// English, anything else Polish.
dates::UiLocale contentLocale(QStringView language);

/// The app's language, "pl" or "en", for the value of its setting ui/language: that value, else
/// (never chosen, or unknown) the system's — English for an English locale, Polish otherwise.
/// AppSettings::language() and brulion-cli (which only reads the settings) both use it, so they
/// write notes in the same language.
QString appLanguage(const QString &saved);

/// A daily note of `date` under any name markdown::journalDateFromPath() accepts (Logseq's
/// "2026_10_14.md", "Journals/" capitalised on another device, an earlier file name format), the
/// configured folder's spelling first; "" when there is none.
VaultPath existingJournal(const Vault &vault, QDate date);

/// The note of the day `date`: its existing file (also one that just arrived on disk and the vault
/// has not listed yet, or one under another accepted name), else where it will be created. Never
/// writes; "" for an invalid date.
VaultPath journalFileFor(const Vault &vault, QDate date);

/// The template at `templatePath` filled in for a page titled `title` at `when` (markdown::
/// applyTemplate: {{date}}, {{weekday}} in `locale`, …); "" when it cannot be read.
QString templateText(Vault &vault, const VaultPath &templatePath, const QDateTime &when, const QString &title,
                     dates::UiLocale locale);

/// The text a new daily note of `date` starts with (ensureJournal()): the journal template filled
/// in for `date` at `now`'s time, else "". Never writes.
QString newJournalText(Vault &vault, QDate date, const QDateTime &now, dates::UiLocale locale);

struct EnsuredJournal {
    VaultPath path;       ///< the daily note ("" on failure)
    bool created = false; ///< written now (from the journal template, or empty)
    QString error;        ///< why it failed (Vault::errorString(); "" for an invalid date)
};

/// The daily note of `date`, created from the journal template (filled for `date` at `now`'s time)
/// when missing. A note another program creates at the same moment is kept, never overwritten
/// (Vault::writeTextIfUnchanged with an empty expected text).
EnsuredJournal ensureJournal(Vault &vault, QDate date, const QDateTime &now, dates::UiLocale locale,
                             ChangeSource source = ChangeSource::User);

/// Pure: `content` with `block` (lines joined with "\n") appended as its last block, written with
/// the note's own line ending. Trailing blank lines go, and a template's empty last bullet ("- ")
/// is filled instead of left above the block. Every other byte (a BOM, other line endings) stays.
QString appendBlock(const QString &content, const QString &block);

/// Pure: free text as one block (captureToJournal): its first line gets "- " unless it starts
/// with a list marker, further lines are indented by two spaces; blank lines at the edges drop.
/// "" for blank text.
QString blockFromText(const QString &text);

} // namespace brulion::notebook
