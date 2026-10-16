// How to get the data out of each app (shown when the user picks the app), what to pick, and
// the card icon. Source texts in English; the Polish translation lives in i18n/brulion_pl.ts
// (context "brulion::ioviews::ImportController").
#include "importcontroller.h"

using namespace Qt::StringLiterals;

namespace brulion::ioviews {

ImporterInfo importerInfo(const QString &id)
{
    return ImportController::importerInfo(id);
}

ImporterInfo ImportController::importerInfo(const QString &id)
{
    if (id == u"obsidian")
        return {tr("Obsidian keeps notes as Markdown files in a folder (the vault). Choose that folder – the one "
                   "with the .obsidian folder inside – or a ZIP of it. Links, attachments and daily notes are kept."),
                u"both"_s, u"notebook-pen"_s,
                tr("On a phone the vault is a folder, often in Documents (or where you created it in Obsidian). Tap "
                   "“Choose a folder…”, open the vault folder (the one with .obsidian inside), tap “Use this folder” "
                   "and allow access. A ZIP of the vault works too.")};
    if (id == u"logseq")
        return {tr("Choose your Logseq graph folder (it contains pages, journals and logseq) or a ZIP of it. "
                   "Journal pages become daily notes, block references are kept."),
                u"both"_s, u"list-indent-increase"_s,
                tr("On a phone the graph is the folder you chose in Logseq (with pages and journals inside), often in "
                   "Documents. Tap “Choose a folder…”, open it, tap “Use this folder” and allow access.")};
    if (id == u"notion")
        return {tr("In Notion open Settings → Export all workspace content (for one page: ••• → Export), choose "
                   "“Markdown & CSV” and include subpages. Choose the downloaded ZIP file – there is no need to "
                   "unpack it."),
                u"files"_s, u"files"_s,
                tr("Export in the Notion app (••• → Export, “Markdown & CSV”) or in the browser; the ZIP lands in "
                   "Downloads. Tap “Choose files…” and pick it there – Google Drive works too.")};
    if (id == u"evernote")
        return {tr("In Evernote select the notes (or a notebook), choose Export and the format “ENEX (.enex)”. "
                   "Choose the .enex files – several at once are fine."),
                u"files"_s, u"bookmark"_s,
                tr("Evernote exports .enex files on a computer or at evernote.com. Copy them to the phone (Downloads, "
                   "Google Drive…) and pick them with “Choose files…”.")};
    if (id == u"keep")
        return {tr("Open takeout.google.com, click “Deselect all”, select only “Keep” and create the export. "
                   "Choose the downloaded ZIP file."),
                u"files"_s, u"list-checks"_s,
                tr("Takeout works in the phone's browser too: open takeout.google.com, export only Keep and download "
                   "the ZIP. Pick it with “Choose files…” (in Downloads or Google Drive).")};
    if (id == u"joplin")
        return {tr("In Joplin choose File → Export all → “JEX – Joplin Export File” and choose that .jex file. "
                   "A folder exported as “MD – Markdown + Front Matter” works too."),
                u"both"_s, u"book-open"_s,
                tr("Create the JEX file in Joplin on a computer, copy it to the phone (Downloads, Google Drive…) and "
                   "pick it with “Choose files…”.")};
    if (id == u"simplenote")
        return {tr("In Simplenote open Settings → Tools → Export notes. Choose the downloaded ZIP file (or the "
                   "notes.json inside it)."),
                u"files"_s, u"file-text"_s,
                tr("Export the notes at app.simplenote.com (Settings → Tools → Export notes) in the phone's browser "
                   "and pick the downloaded ZIP with “Choose files…”.")};
    if (id == u"roam")
        return {tr("In Roam Research open ••• (top right) → Export All, format “JSON”. Choose the downloaded "
                   "ZIP or JSON file. Daily pages go to your journal."),
                u"files"_s, u"network"_s,
                tr("Export in the browser (••• → Export All, format “JSON”) and pick the downloaded file with “Choose "
                   "files…”.")};
    if (id == u"bear")
        return {tr("In Bear choose File → Backup Notes (a .bear2bk file), or select notes and export them as "
                   "TextBundle. Choose the backup or the exported files."),
                u"files"_s, u"pencil"_s,
                tr("Bear runs on Apple devices: make the backup there, copy it to the phone (Google Drive, a cable…) "
                   "and pick it with “Choose files…”.")};
    if (id == u"markdown")
        return {tr("Any folder or ZIP of Markdown (.md) and text (.txt) files with their pictures and "
                   "attachments. Subfolders are kept."),
                u"both"_s, u"hash"_s,
                tr("Tap “Choose a folder…” for a folder of .md files (then “Use this folder”), or “Choose files…” for "
                   "single files or a ZIP – a long press selects several.")};
    if (id == u"html")
        return {tr("Web pages and HTML exports (.html files or a ZIP of them) become Markdown pages; their "
                   "pictures become attachments."),
                u"both"_s, u"code"_s,
                tr("Pick the .html files or a ZIP of them with “Choose files…” – a long press selects several.")};
    if (id == u"ics")
        return {tr("Calendar files (.ics) from Google Calendar, Outlook, Thunderbird or a phone. Events go to the "
                   "daily notes of their days with ⏰ reminders, to-dos become tasks."),
                u"files"_s, u"calendar-days"_s,
                tr("Pick the .ics file with “Choose files…”, e.g. from Downloads or Google Drive (a calendar's export "
                   "or an e-mail attachment saved on the phone).")};
    return {QString(), u"both"_s, u"file-input"_s, QString()};
}

} // namespace brulion::ioviews
