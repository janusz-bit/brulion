---
tags: [documentation]
brulion: document
---
# Sync

Brulion has no server of its own. Your notes are an ordinary folder, so you sync it with whatever you like. Saving is always local and instant; syncing happens in the background and never blocks your writing.

## Syncthing (recommended for working offline)
1. Share the notebook folder in Syncthing (on Android: Syncthing-Fork).
2. On every device, point Brulion to the same folder (on Android e.g. `/storage/emulated/0/Brulion` and the “All files access” permission).
3. When the same note changes on two devices, Syncthing leaves a `*.sync-conflict-*` copy. Brulion doesn't show it as a page, but in the Sync center as a **conflict to merge** — also a copy of a drawing, which you merge with the **Merge drawings** button.

Optionally Brulion shows the status from Syncthing's API.

## Nextcloud (WebDAV)
*Settings → Sync* (or the Sync center): the server address, the user name and an **app password** generated in Nextcloud. Changes from several devices are merged three-way (drawings element by element, never line by line), so usually nothing gets lost and you don't have to do anything. When merging is impossible, both versions are kept and you get the merge view.

The server and the account are shared, but **turning sync on and the folder on the server belong to the notebook**: another notebook (this documentation too) starts without sync. That way notes from one notebook never end up in another.

- Brulion suggests a folder on the server for each notebook separately: for the first account `Notatki`, for further notebooks their name (e.g. `Work`, and when that is taken — `Work 2`). On a second device, enter the same folder as on the first.
- One folder on the server belongs to one notebook: Brulion won't save a folder that another notebook on this device already syncs with (nor a folder inside it or above it), and it tells you which notebook that is.
- If two notebooks already sync with one folder (versions 0.3.1–0.3.5 could set that up), neither of them syncs — neither by itself nor after “Sync now” — because their notes would get mixed. The sync status (at the bottom of the sidebar) tells you which notebook shares the folder; in the Sync center choose “Choose another folder” and give a new folder for one of them (or turn sync off in it).
- **Sign out** turns sync off in all notebooks on this device, because the app password is shared; after signing in again, turn it on in each notebook separately. Likewise, saving a different server or user turns it off in the other notebooks — none of them starts sending notes to the new account by itself.

Deletions are held back until you decide when they affect many files at once — a safeguard against mistakes.

### Folders
- An **empty folder** created on one device appears on the server and on the other devices.
- A **deleted folder** disappears on the other side too, but only when nothing is left in it. A folder holding a note that isn't synced yet, or a file Brulion doesn't sync (e.g. `.git`), stays where it is.
- **Moving and renaming** (a folder or a page) is a move for sync, not a deletion: you don't get the question about mass deleting, even when you move a folder with hundreds of notes. A real deletion of many notes still waits for your confirmation.
- A folder or page you rename or move in Brulion is moved on the server with a single command — the files aren't uploaded again. This also applies to a page you keep writing in right after renaming it: it is moved first, and then only the change is uploaded. Changes from another device then go under the new name. When the server doesn't allow this, the files are uploaded under the new name and the old ones deleted — nothing gets lost.
- A name changed outside Brulion (e.g. in a file manager) is recognized by Brulion only by the unchanged content. A page that was changed afterwards is uploaded under the new name and the old one is deleted on the server — nothing gets lost, but on other devices the old one goes to the trash.
- A folder or page that was renamed or moved on another device or in Nextcloud in the browser (also in the Nextcloud desktop app) gets the same new name here. The notes aren't downloaded again and don't go to the trash: their version history, favorites and folded blocks go with them. Brulion recognizes them by the file's id on the server (or by its version when the server doesn't give ids).
- When the folder with the new name is already taken here, or the folder can't be renamed (e.g. on Windows another program holds a file from that folder open), Brulion moves the notes one by one. A note that can't be moved is downloaded under the new name, and the old one goes to the trash — nothing gets lost.
- When someone changed a note in the moved folder in the meantime, no version gets lost: the changes are merged, and when that isn't possible, both are kept.
- Syncthing moves and deletes folders by itself; there is nothing to set up.

> [!note] Documentation pages in your notebook
> In version 0.3.0, opening this documentation with Nextcloud turned on synced it with your folder on the server. Its pages (back then in Polish, e.g. “Spis treści”, “Synchronizacja”) could therefore end up in your notebook on all devices, and copies of your notes in the documentation. You find documentation pages in your notebook in the **Tags** view, under the tag `dokumentacja`, and you can delete them there: the documentation has its own. Your notes that appeared in the documentation are only copies.

## Sync center
The **More** menu → **Notebook** → *Sync center*, or a click on the sync status icon at the bottom of the icon bar on the left (its tooltip tells you the status). Tabs: Overview, Syncthing, Nextcloud, Conflicts.

- You see what is going on as it happens: the stage, the number of files, how long it has been running and the files on their way.
- If the server doesn't answer for a long time, a warning appears, and after 180 s the attempt is stopped and retried — the panel doesn't “sync” forever.
- After an error you see a countdown to the next attempt and a “Try now” button.
- The history of recent runs shows the result, the reason and the time; a click opens the details.
- **Conflicts** of drawings show both versions and the merged result as drawings, never as text. **Merge drawings** combines the lines and shapes of both versions (lines erased on just one device come back), saves the previous version in the history and moves the copy to the trash. A copy of an Obsidian drawing that has different notes than your drawing stays after merging until you move its notes over and delete it. See [[Drawings#Saving and sync|Saving and syncing drawings]].

## Event log
When something doesn't work, look into the **Event log** (*Settings → Application*, or “log” in the command palette). You can filter and search it, and then copy or save it to send. The log contains no passwords, tokens or note content; it stays on the device until you share it yourself. For sync problems, first turn on “Detailed log”, repeat the sync, and only then send the log.

## Version history and trash
- Before a note is overwritten, its previous version goes into the history on the device. Page menu → **History** lets you compare versions and restore any of them.
- Deleted pages go to the **Trash**, from which you can recover them. You delete a page, an attachment or a folder from the **More** menu, from the page tree (also with the `Delete` key), from **All pages** and from the command palette (“Move to trash”), and **Undo** in the notification brings it back right away. Files disappear from the trash for good only when you ask for it — a deletion spreads through sync to your other devices, so the trash is a safeguard.
- **Clean up** (the **More** menu and the command palette) shows notes and attachments that no link, tag or query leads to. You choose what to move to the trash: notes, attachments or single files. Journals, templates and favorites aren't listed there.
- Sync's working data (history, merge traces) lives outside the notebook folder, so it doesn't sync between devices.

See also [[File format|File format]].
