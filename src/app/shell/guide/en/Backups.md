---
tags: [documentation]
brulion: document
---
# Backups

Brulion can back up your notebook by itself: ordinary ZIP archives with your files. Version history helps when you break one note; a backup helps when more things break at once. You set everything up in **Settings → Backups** (or “backups” in the command palette).

## What a backup contains
All notes and attachments byte for byte, the notebook settings (`.brulion/config.json`) and empty folders. It doesn't contain the trash, `.git`, temporary files or other folders that Brulion skips (for example `.obsidian` and `logseq`). You can unpack a backup with any program — you don't need Brulion for that.

## Schedule
- **Automatic backups** are on by default; in the browser they are off by default (see below).
- **How often:** every day or every week. Brulion checks this when it opens the notebook and every hour while it runs. If nothing has changed since the last backup, it doesn't make a new one.
- **How many to keep:** the oldest backups go away only after a new one has been saved successfully. Brulion deletes only backups of this notebook (every backup records which notebook it came from); it leaves other files in the folder alone.
- **Incomplete backup:** when some file can't be read, the backup is made without it, and the list and the settings say so. Such a backup doesn't push out complete ones, and the next day Brulion tries again. When the whole drive with the notebook disappears during a backup, there is no backup at all.
- **Create a backup now** makes a backup right away, even when nothing has changed.

## Where backups are kept
By default in the app's private folder on this device, separately for each notebook. Sync doesn't spread them, so **a backup on the same drive doesn't protect you from a drive failure**. On a computer and on Android you can choose another folder, for example on a USB stick. A folder inside the notebook isn't allowed: sync would spread the backups, and each new one would contain the previous ones.

If two notebooks with the same name keep backups in one folder, the second one's backups are marked “not from this notebook” in the list: Brulion doesn't delete them by itself, and restoring or deleting such a backup needs a separate confirmation. Backups of a notebook from before it was moved to another folder look the same.

When the folder isn't there (an unplugged drive), Brulion shows that in the settings and in the event log, and leaves older backups alone. The backup is made at the next check after the drive is connected.

### Android
In the app's private folder you see the backups only in Brulion; the **Share** button next to a backup lets you send it on. Uninstalling the app deletes that folder together with the backups. Another folder needs the “All files access” permission. Cloud drives aren't supported — Brulion tells you so when you choose one.

### Browser
**The backups are in this browser's storage: clearing its data deletes them together with your notes.** To have a backup somewhere else, use the **Download** button. Every backup takes up space allotted to the site and memory every time it opens, which is why automatic backups are off by default here, and you can keep at most three. A backup that doesn't fit simply isn't made; the older ones stay.

## Restoring
Next to a backup in the list choose **Restore**. Brulion first checks the whole backup and shows how many files it will replace, how many will come back and how many will go to the trash because the backup doesn't have them. It skips files with the same content. After you confirm:

1. It saves the open notes and waits for a running Nextcloud sync to finish; no new one starts until the restore is done.
2. It makes a **safety backup** of the current state. If that fails or doesn't cover a file the restore would change, it restores nothing.
3. It writes the files from the backup; replaced notes stay in the version history, other files only in the safety backup. It doesn't overwrite a file it can't read.
4. It moves surplus files to the trash — only if all the writes succeeded. With an incomplete backup, surplus files stay where they are.

When closing the notebook or the app interrupts a restore, the notebook is only partly restored. Brulion tells you so the next time it opens and points you to the safety backup; until you confirm that you have dealt with it, it makes no automatic backups. Many deleted files don't disappear from the server right away: Nextcloud, as always, asks for confirmation in the Sync center. Brulion can't pause Syncthing. You can restore a backup from the list in the chosen folder; importing any ZIP file is a separate feature — see [[Import and export|Import and export]].

Brulion won't restore a backup with only a folder or only settings, because it would just empty the notebook. On a phone (and other drives that don't tell letter case apart) it also refuses a backup with names that differ only in letter case, for example `Plan.md` and `plan.md` — unpack such a backup by hand.

A file larger than 1 GB is in the backup, but Brulion won't restore it: it stays as it is, and the restore dialog says so. You extract such a file from the ZIP by hand.
