// How to get the data out of each app (shown when the user picks the app), what to pick, and
// the card icon. Source texts in English; the Polish translation lives in i18n/brulion_pl.ts
// (context "brulion::ioviews::ImportController").
#include "importcontroller.h"

using namespace Qt::StringLiterals;

namespace brulion::ioviews {

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
    if (id == u"mstodo")
        return {tr("Sign in to Microsoft and Brulion downloads every list: tasks with their steps, notes, due dates, "
                   "reminders, repeats, links and attachments; each list becomes a page. Signing in needs your own "
                   "free app registration in Microsoft Entra – the guide explains it step by step. Without one, "
                   "save the answer of Graph Explorer to …/me/todo/lists?$expand=tasks as a JSON file and choose it "
                   "(without steps and attachments)."),
                u"files"_s, u"list-todo"_s,
                tr("Signing in works on the phone too: tap “Sign in to Microsoft”, open the address in the "
                   "browser, enter the code and come back – the lists download by themselves. A JSON file saved "
                   "from Graph Explorer can be picked with “Choose files…”.")};
    if (id == u"onenote")
        return {tr("Sign in to Microsoft and choose the notebooks: Brulion downloads their sections, section groups "
                   "and "
                   "pages with pictures and attached files; to-do tags become tasks, subpages folders. Signing in "
                   "needs "
                   "your own free app registration in Microsoft Entra (the same one as for Microsoft To Do, with the "
                   "permission Notes.Read) – the guide explains it step by step."),
                u"none"_s, u"layers"_s,
                tr("Signing in works on the phone too: tap “Sign in to Microsoft”, open the address in the browser, "
                   "enter the code, come back and choose the notebooks.")};
    if (id == u"applenotes")
        return {tr("Apple Notes keeps its notes in a database on the Mac. In Finder choose Go → Go to Folder…, enter "
                   "~/Library/Group Containers/group.com.apple.notes, copy that whole folder (or a ZIP of it) to this "
                   "device and choose it. Folders, checklists, tables, pictures, scans and drawings come along; locked "
                   "notes stay out. Notes only in iCloud: open them on the Mac first."),
                u"both"_s, u"sticky-note"_s,
                tr("Copy the group.com.apple.notes folder from the Mac (zipped is easiest: right-click → Compress), "
                   "put "
                   "the ZIP on the phone (Downloads, Google Drive…) and pick it with “Choose files…”.")};
    if (id == u"todoist")
        return {tr("Paste your Todoist API token and Brulion downloads every project: sections, tasks with priorities, "
                   "due dates, repeats, labels, subtasks, descriptions and comments. Without a token, export a project "
                   "as a template (••• → Export as a template, CSV) or download a backup (Settings → Backups) and "
                   "choose the files."),
                u"files"_s, u"square-check-big"_s,
                tr("The token works on the phone too: copy it from Todoist's settings (Integrations → Developer) and "
                   "paste it here. A template's CSV or a backup ZIP can be picked with “Choose files…”.")};
    if (id == u"airtable")
        return {tr("Connect with a personal access token and choose the base and its tables, or download a view as "
                   "CSV in Airtable (••• → Download CSV) and choose the files. Each record becomes a page with its "
                   "fields as properties, linked records become links, attachments files of the notebook."),
                u"files"_s, u"grid-3x3"_s,
                tr("Paste the token from airtable.com/create/tokens, or pick a CSV downloaded from Airtable with "
                   "“Choose files…”.")};
    if (id == u"csv")
        return {tr("Any spreadsheet saved as CSV (Excel, LibreOffice, Google Sheets, Numbers, an export of another "
                   "app): "
                   "each row becomes a page with the columns as properties – you choose the title, the types and what "
                   "is left out, and see the first pages before importing. Or the whole sheet as one table page."),
                u"files"_s, u"table"_s,
                tr("Pick the .csv file with “Choose files…”, e.g. from Downloads or Google Drive (Sheets: File → "
                   "Download → CSV).")};
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
    if (id == u"trilium")
        return {tr("In Trilium right-click a note in the tree (for everything: the root note at the top), choose "
                   "Export, then “This note & all descendants” and the format “HTML” (recommended) or “Markdown”. "
                   "Choose the downloaded ZIP file – there is no need to unpack it. Day notes go to your journal."),
                u"files"_s, u"folder-tree"_s,
                tr("Export on a computer or in the browser on your Trilium server (right-click the note → Export, "
                   "“This note & all descendants”, “HTML”), copy the ZIP file to the phone (Downloads, Google "
                   "Drive…) and pick it with “Choose files…”.")};
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
