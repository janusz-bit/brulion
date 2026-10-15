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
        return { tr("Obsidian keeps notes as Markdown files in a folder (the vault). Choose that folder – the one "
                    "with the .obsidian folder inside – or a ZIP of it. Links, attachments and daily notes are kept."),
                 u"both"_s, u"notebook-pen"_s };
    if (id == u"logseq")
        return { tr("Choose your Logseq graph folder (it contains pages, journals and logseq) or a ZIP of it. "
                    "Journal pages become daily notes, block references are kept."),
                 u"both"_s, u"list-indent-increase"_s };
    if (id == u"notion")
        return { tr("In Notion open Settings → Export all workspace content (for one page: ••• → Export), choose "
                    "“Markdown & CSV” and include subpages. Choose the downloaded ZIP file – there is no need to "
                    "unpack it."),
                 u"files"_s, u"files"_s };
    if (id == u"evernote")
        return { tr("In Evernote select the notes (or a notebook), choose Export and the format “ENEX (.enex)”. "
                    "Choose the .enex files – several at once are fine."),
                 u"files"_s, u"bookmark"_s };
    if (id == u"keep")
        return { tr("Open takeout.google.com, click “Deselect all”, select only “Keep” and create the export. "
                    "Choose the downloaded ZIP file."),
                 u"files"_s, u"list-checks"_s };
    if (id == u"joplin")
        return { tr("In Joplin choose File → Export all → “JEX – Joplin Export File” and choose that .jex file. "
                    "A folder exported as “MD – Markdown + Front Matter” works too."),
                 u"both"_s, u"book-open"_s };
    if (id == u"simplenote")
        return { tr("In Simplenote open Settings → Tools → Export notes. Choose the downloaded ZIP file (or the "
                    "notes.json inside it)."),
                 u"files"_s, u"file-text"_s };
    if (id == u"roam")
        return { tr("In Roam Research open ••• (top right) → Export All, format “JSON”. Choose the downloaded "
                    "ZIP or JSON file. Daily pages go to your journal."),
                 u"files"_s, u"network"_s };
    if (id == u"bear")
        return { tr("In Bear choose File → Backup Notes (a .bear2bk file), or select notes and export them as "
                    "TextBundle. Choose the backup or the exported files."),
                 u"files"_s, u"pencil"_s };
    if (id == u"markdown")
        return { tr("Any folder or ZIP of Markdown (.md) and text (.txt) files with their pictures and "
                    "attachments. Subfolders are kept."),
                 u"both"_s, u"hash"_s };
    if (id == u"html")
        return { tr("Web pages and HTML exports (.html files or a ZIP of them) become Markdown pages; their "
                    "pictures become attachments."),
                 u"both"_s, u"code"_s };
    if (id == u"ics")
        return { tr("Calendar files (.ics) from Google Calendar, Outlook, Thunderbird or a phone. Events go to the "
                    "daily notes of their days with ⏰ reminders, to-dos become tasks."),
                 u"files"_s, u"calendar-days"_s };
    return { QString(), u"both"_s, u"file-input"_s };
}

} // namespace brulion::ioviews
