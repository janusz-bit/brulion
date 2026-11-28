---
tags: [documentation]
aliases: [Encryption, Encrypted notes, Password, App lock, PIN, Meld Encrypt, Privacy]
brulion: document
---
# Privacy and encryption

Brulion has two kinds of protection, for two different worries:

- **Encrypted notes** — a page, or a piece of a page, readable only with a password. The file on disk, the copies on your other devices, the sync server and the backups hold only scrambled text. Use it for passwords, PINs, a diary, anything nobody else should read even if they get the notebook's folder.
- **App lock** — a PIN (or the phone's fingerprint, face or screen lock) in front of the whole app on this device. It keeps someone holding your unlocked phone or computer out of Brulion; the notes themselves stay ordinary files.

## Encrypted text in a page
Select the text, then **Encrypt selection…** in the text's menu (right-click or long press), or type `/encrypt` for new encrypted text at the cursor. Choose a password, type it twice and, if you like, a **hint** — the hint is not encrypted, everyone can read it.

The text becomes a locked chip: 🔒 with the hint, or “Encrypted”. Click it and type the password to read it. In the window that opens you can:
- change the text and **Save** — it is encrypted again with the same password;
- **Copy** it (on Android 13 and newer and in clipboard managers that honour it, the copy is marked as sensitive — still, it is not encrypted on the clipboard);
- under **…**: **Change password…** or **Decrypt permanently…** (the plain text goes back into the page).

In the file the chip is written as `%%🔐γ 💡hint💡…🔐%%` — hidden in Obsidian's reading view, like a comment. Switch on **Show a lock in Obsidian's reading view** when encrypting to get `🔐γ … 🔐` instead, which Obsidian shows as a 🔐 to click.

## Encrypted pages
A whole page can be encrypted too: the page's menu **…** → **Encrypt page…** (or the command palette). The page becomes `Name.mdenc` and every link to it follows; its version history on this device (the earlier, unencrypted versions) is deleted unless you untick that. A new encrypted page: the palette's **New encrypted page…**, or a folder's menu in the sidebar.

An encrypted page shows a lock until you type its password; then it opens in the editor like any other page, and everything you type is saved encrypted. The page's menu has **Change password…**, **Decrypt permanently…** and **Lock page**. An encrypted page is not exported, printed or shown as a QR code — decrypt it permanently first if you need that.

## Remembering passwords
A password you typed is remembered for a while, so the next encrypted text or page with the same password opens without asking. After **5 minutes** without using encrypted notes (Settings → Privacy: 1 minute to 1 hour) every password is forgotten and open encrypted pages lock again — also when the app lock locks, when you close the notebook and with the command **Lock encrypted notes**. Passwords are never written anywhere.

> [!warning] A forgotten password cannot be recovered
> Nobody — not Brulion, not its author — can open the text without the password. Keep it somewhere safe.

## Works with Obsidian
Brulion reads and writes the format of Obsidian's **Meld Encrypt** plugin: the same notes open in both apps with the same password. Brulion reads every format Meld Encrypt has ever written (also the older `α` and `β` markers and `.mdenc` files of versions 1.0 and 2.0) and writes the current one (`γ`, file version 3.0: AES-256-GCM with a key from PBKDF2-SHA512, 220 000 rounds). An older text is upgraded when you save it.

## What stays readable
- The page's name and folder, the hint, and that something is encrypted. Search does not find words inside encrypted text, links and tags inside it do not show up as backlinks or tags, and reminders and tasks inside it never ring or appear in Tasks.
- Copies made **before** you encrypted something — backups, versions on other devices, a sync server's history — stay as they were.
- While a page is unlocked, its text is in the app's memory (that is how you read it).

## Sync
Encrypted text is ordinary text in the file, so sync merges it like any line: changes in different places of a page merge; when two devices changed the same encrypted text, both versions are kept as a conflict, as for any other line. A whole encrypted page changed on two devices at the same time is kept in both versions (a conflict copy), never mixed. An encrypted page changed on another device with **another** password locks here and is never overwritten — unlock it with the new password.

The whole notebook can also be kept unreadable on the Nextcloud server: [[Sync#End-to-end encryption|End-to-end encryption]]. Every name is encrypted with AES-256-SIV and every file with AES-256-GCM in 64 KiB pieces under a key of its own; the notebook's key is random and is kept on the server sealed with your password (PBKDF2-HMAC-SHA-512, 600 000 rounds) and with the recovery key, and on each device in a file only Brulion may read.

## App lock
Settings → **Privacy** → **Set a PIN…** (4 to 16 digits). On a phone you can use the phone's own lock instead: **Use the phone's lock** — your fingerprint, face or the phone's PIN, pattern or password.

You choose when Brulion locks: when it starts, after you leave it (at once, after 1, 5 or 15 minutes, or an hour), and on a computer also after a while without a key, click or touch. **Lock now** (Settings, or **Lock Brulion** in the command palette) locks at once. While locked:
- nothing of your notes is shown, also not in windows of their own; the encrypted notes lock too;
- reminders say only “Reminder” and the time (**Hide the note's text in notifications**);
- on Android, the home-screen widgets say “Brulion is locked” (quick add still works) and Brulion's screen is left out of the recent apps' preview; **Hide Brulion's screen** → **Always** also blocks screenshots and screen recording.

After five wrong PINs Brulion makes you wait — 30 seconds, then longer, up to 15 minutes. A forgotten PIN can only be removed by clearing Brulion's settings on that device; your notes are not affected. In the browser only a PIN is available.

See also: [[Sync|Sync]], [[Backups|Backups]], [[File format|File format]].
