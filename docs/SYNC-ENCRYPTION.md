# End-to-end encrypted sync (design)

Status (2026-11-28): design decided and built — the formats, the key file and the encrypting
transport (core `sync`), the setting in the sync settings (`SyncEncryptionCard`,
`WorkspaceSync::setUpEncryption()`, `changeEncryptionPassword()`), the guide's page. Left
(TODO.md "Privacy and security"): a check on real devices. A device whose
setting is off looks for the key file before its first run and stays locked when there is one. This page says what is encrypted, how,
what is not, and why each choice was made. docs/ARCHITECTURE.md "Security" is the overview of
what protects what; this page is the sync's part of it.

## The goal and its limits

A notebook synced through Nextcloud or any WebDAV server should be unreadable on that server
without the user's password: its notes, their titles, the attachments. The server stays what it is
for Brulion today — a meeting point of the user's devices that keeps the last version of each file
— and it may be someone else's machine (a hosting company, a friend's Nextcloud, a cloud drive's
WebDAV).

**The plain-files principle stays.** Brulion's notebook is a folder of Markdown files on every
device, readable by any editor, by Obsidian, by `grep`, backed up by whatever the user backs up
with. Encryption of the sync must not change that: notes are plain on the devices and encrypted
only where they leave them, on the server. This is the difference to Joplin (whose sync target is
a database of items, encrypted or not) and the likeness to Obsidian Sync (plain vault, encrypted
server copy) and Cryptomator (plain files in the virtual drive, encrypted ones in the cloud).

What it does not protect:

* **The devices.** A stolen unlocked phone has the plain notebook; encrypted notes (Meld Encrypt,
  docs/ARCHITECTURE.md "Security") and the app lock are for that.
* **What the server sees anyway**: how many files and folders there are and how they nest, each
  file's size (to within 16 bytes per 64 KiB), when files change, the account's name and IP
  addresses. Names are hidden, but two files of the same name in different folders have the same
  encrypted name (see "Names").
* **A hostile server's choices**: it can delete files, keep an older version and give it back
  (roll back), or swap two files' contents. It cannot read them, change a byte of them, or make up
  a file. The sync's guards against mass deletions and empty sides stay in front of the first.
* **Syncthing** never goes through a server: its devices are the user's own and hold the plain
  notebook. (Syncthing's own "untrusted device" encryption is for a device that should not read
  it.) A **folder of the device** (docs/ANDROID.md "A folder picked in the system's dialog") is
  another plain copy on purpose — it is there for other apps — and is not encrypted either.

## Decisions

### 1. Where: a layer between the sync engine and the server

`EncryptingTransport` (core `sync`) is an `HttpTransport` around the network's: every request the
`WebDavClient` makes passes through it. It encrypts each segment of a request's path (and the
`Destination` of a MOVE), the body of a PUT, decrypts the body of a GET, and rewrites the answers
of PROPFIND: each `href` decrypted, each `getcontentlength` the plain size, entries it cannot
decrypt left out. The engine, its three-way merges, conflict copies, history, guards and the
client's retries see a plain WebDAV server and stay as they are; nothing above the transport knows
about encryption.

*Why not inside the engine or the client?* Both work on paths and bodies in many places; the
transport is the one place every byte for the server goes through, so one decision sits in one
place and nothing can bypass it (a new request type gets encrypted or is refused). It also keeps
the unencrypted path byte-for-byte the code of today: an unencrypted notebook does not go through
the layer at all.

### 2. Content: AES-256-GCM in chunks (the STREAM construction)

A file on the server is a 32-byte header and the content in chunks of 64 KiB, each sealed with
AES-256-GCM:

```
header   "BRLN" | version 1 | chunk size: 16 (2^16 = 64 KiB) | 0 0 | file nonce (24 random bytes)
chunk i  AES-256-GCM(file key, nonce = i as 11 bytes big-endian | last ? 1 : 0, aad = header)
         of up to 64 KiB of the file: the ciphertext and its 16-byte tag
file key HKDF-SHA-256(content key, salt = file nonce, info = "brulion-sync file v1")
```

Every chunk but the last is full; the last carries the flag, so a file cut short or extended by a
chunk fails as surely as a changed byte (STREAM, Hoang, Reyhanitabar, Rogaway and Vizár 2015 — the
construction of `age` and Tink's streaming AEAD). An empty file is one empty last chunk. A fresh
file nonce on every upload gives every version its own key, so GCM's nonces (counters) never repeat
under one key however many times a file is saved.

*Why chunks?* Downloads and uploads of attachments are streamed in bounded memory
(docs/PERFORMANCE.md "Sync and import in bounded memory"); chunks keep that true — each one is
checked before its plaintext is handed on, so a download is decrypted as it arrives, never held
whole. *Why GCM?* It is what Brulion's crypto already has (BearSSL's constant-time AES and GHASH,
the same code as encrypted notes), it is standard (NIST SP 800-38D), and Cryptomator encrypts
content the same way. *Why is the path not authenticated?* A MOVE on the server renames without
re-encrypting, and folders are moved that way; binding a file to its path would break it. The
price is the swap named above.

The plain size follows from the encrypted one (32 + size + 16 per chunk), so a listing's sizes are
exact without reading anything.

### 3. Names: AES-SIV, base32

Each segment of a path — a folder's or a file's name — is encrypted on its own with AES-256-SIV
(RFC 5297, the name key, no associated data) and written in lowercase base32 without padding
(RFC 4648): `Dziennik/2026-11-28.md` becomes `k3n…q/7fz…a`. SIV is deterministic: the same name
always gives the same encrypted name, which is what lets the client ask for a path it knows and
the server list a folder without either knowing the other's state; it still authenticates the name
(a changed or made-up name does not decrypt) and hides everything but equality and length.

*Why per segment, without the parent as associated data?* Folders can then be moved and renamed
on the server with one MOVE, as today. Cryptomator binds names to their folder (and pays for it with
directory ids and an indirection file per folder); Brulion keeps the tree's shape visible instead
— it is visible in Cryptomator too, by the files' count and sizes — and leaks that two folders hold
a file of the same name. *Why base32?* A server may store names on a file system that ignores
case (Nextcloud on macOS, SMB external storage, IIS); base64's upper and lower case would collide
there. *The cost*: an encrypted name is 16 bytes longer and 8/5 as long, so a name of up to 143
bytes of UTF-8 fits the 255 bytes servers allow (about 140 characters of English, fewer with Polish
letters). A longer name is a per-file error of the sync ("the name is too long to encrypt"), shown
like any other; Cryptomator's shortening (a hash and a side file) can come later if needed.

### 4. Keys: one random notebook key, wrapped by the password and by a recovery key

```
notebook key   32 random bytes (systemRandom)
name key       HKDF-SHA-256(notebook key, info = "brulion-sync names v1"), 64 bytes (AES-256-SIV)
content key    HKDF-SHA-256(notebook key, info = "brulion-sync content v1"), 32 bytes
password key   PBKDF2-HMAC-SHA-512(password, 16 random bytes, 600 000 rounds), 32 bytes
recovery key   32 random bytes, shown once as 52 base32 characters in groups of four
```

The notebook key, sealed (AES-256-GCM) once with the password key and once with
HKDF-SHA-256(recovery key, info = "brulion-sync recovery v1"), is kept on the server in the
notebook's folder as `.brulion/encryption.json` — the one file there whose name and content are
plain (they reveal nothing without the password or the recovery key). `.brulion/` is a folder no
sync lists, so a device that does not encrypt never downloads it; its presence is what tells a
device that the folder is encrypted (checked once when the sync is set up or changed).

On a device the notebook key is kept where the Nextcloud app password already is: a file of its
own under the app's `secrets/` folder, readable by the owner only (docs/ARCHITECTURE.md; Brulion
does not depend on an OS keychain). The password is asked once per device, when the sync is set up
there; it is never stored.

*Why a random key, not one derived from the password?* The password can then change without
re-encrypting anything (the key file is sealed again), and a recovery key can open the same
notebook. *Why PBKDF2 and these rounds?* It is the key derivation Brulion's crypto has (a
standard: RFC 8018, NIST SP 800-132); Argon2id or scrypt would cost an attacker more per guess but
are not among BearSSL's primitives, and the rule is to use only proven ones. The key file sits on
the server, so the rounds are what an attacker who has it pays per guess: 600 000 rounds of
SHA-512 (OWASP asks for at least 210 000) take about 0.3 s on a laptop and 1–2 s on a phone, once
per device. A long passphrase is still what really protects the notebook, and the setup asks for
one of at least 12 characters.

### 5. Recovery: the recovery key, and the plain copies on the devices

A forgotten password does not lose the notebook: every device has it in plain files, and a new
encrypted folder can be made from any of them. What it loses is the way into the server's copy for
a *new* device. The recovery key, shown once at setup ("write it down or print it"), opens the key
file instead of the password and lets a new password be set. Without both, the server's copy is
unreadable — by design, as in Obsidian Sync, Joplin and Cryptomator; the setup says so.

### 6. Turning it on: into an empty folder, never over plain files

Encryption is switched on for a sync account by choosing an empty server folder for it (offered:
"<folder> (encrypted)"). The first sync uploads every note encrypted, as a first sync into an empty
folder does today. The old plain folder is left alone — Brulion never deletes it; the settings say
it is there and that it can be deleted on the server once every device uses the new one — and gets
a note, `Brulion — this notebook moved to an encrypted folder.md`, naming the new folder and what
to do: another device still syncing with the old folder downloads it like any note, and its user
sees it in the notebook. (A hidden marker that makes such a device stop would need that device to
know about it — an older version does not.)

Turning it off is the same in reverse: a new plain folder.

*Why not encrypt the folder in place?* A device that has not switched yet would read encrypted
files as foreign ones, or upload plain ones next to them; a half-done switch (a phone going
offline) would leave a folder of both. A new folder is all-or-nothing, and the engine's guards
(an empty side is never taken for deletions) stay meaningful.

### 7. Conflicts and merges: unchanged

Merging happens on the device, on plain text: the engine downloads the server's version (decrypted
by the transport), merges it with the device's and the merge base (kept on the device, in its sync
state, as today) and uploads the result (encrypted). A conflict copy is a new file with an
encrypted name like any other. Nothing about merges, conflict copies, history or the mass-deletion
guard changes.

### 8. Compatibility

* A notebook without encryption syncs exactly as before: the layer is not in its path.
* An encrypted folder synced by an older Brulion is a folder of files with strange names to it:
  it would download them as attachments, and nothing on the server can stop it. So the folder gets
  a plain `README.txt` ("This folder is a Brulion notebook encrypted end to end … Brulion 0.33 or later"),
  which the layer hides from newer versions like every name it cannot decrypt, and the setup says
  that every device needs a version that encrypts before the switch.
* The formats carry a version (the header's byte, the key file's `version`): a later format can be
  read next to this one.

## Compared

| | Brulion | Joplin | Obsidian Sync | Cryptomator |
|---|---|---|---|---|
| on the devices | plain Markdown files | its database (notes as items) | plain Markdown files | plain files in a virtual drive |
| on the server | each file encrypted, same tree | encrypted items with ids for names | Obsidian's own server | each file encrypted, the tree hidden by directory ids |
| content | AES-256-GCM, 64 KiB chunks | AES-256-GCM (formerly CCM) per item | AES-256-GCM | AES-256-GCM, 32 KiB chunks |
| names | AES-256-SIV per segment, base32 | not needed (ids) | encrypted | AES-256-SIV with the folder's id, base64url, long names shortened |
| key from the password | PBKDF2-SHA-512, 600 000 rounds | PBKDF2-SHA-512 | scrypt | scrypt |
| recovery | recovery key + plain copies on the devices | none (local copies) | none | recovery key (words) |
| any WebDAV server | yes | yes (and others) | no | any cloud drive |

## Built as

core `crypto`: `hkdf()` (RFC 5869), `aescmac`, `aessiv` (RFC 4493, RFC 5297), tested with the RFCs'
vectors and checked against OpenSSL. core `sync`: the content format and the names (pure
functions, tested with fixed keys and nonces), the key file (create, unlock, change the password,
recover), `EncryptingTransport` (tested over the fake WebDAV server: a whole notebook synced
between two devices, renames, conflicts, large attachments in pieces, a server that changes or
cuts files, foreign files in the folder). The app: `SyncEncryptionCard` in the sync settings
(the folder, the password, the recovery key shown once), `WorkspaceSync::setUpEncryption()`
(`tst_shell_workspace_sync_encryption`), the notebook key in `secrets/sync-key-<notebook id>`
(AppSettings), the guide's "Sync" and "Privacy and encryption" pages (en/pl).
