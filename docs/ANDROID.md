# Brulion on Android

Brulion on Android is the same C++/Qt Quick app as on the desktop (Qt 6.11 for Android), with a
native part in Kotlin (`android/src/io/github/januszbit/brulion/android/`) that:

* gives access to the notes folder on shared storage ("All files access"),
* schedules reminders and alarms in the system (`AlarmManager`) — they work while the app is
  closed and after the phone restarts,
* checks the notes in the background every 15–60 minutes (after a Nextcloud sync, when that is
  on), so that reminders added on other devices ring without opening the app
  ([Checking notes in the background](#checking-notes-in-the-background)),
* opens the right note when a notification is tapped,
* accepts text and `.md`/`.txt` files from the **Share** menu of other apps
  ([Share → Brulion](#share--brulion)),
* provides home-screen **widgets**: today's tasks with quick add, a calendar and notes
  ([Widgets](#widgets)).

C++ talks to it over JNI (`src/platform/android/androidbridge.{h,cpp}`), see
[JNI bridge](#jni-bridge-c--kotlin).

Notes are plain `.md` files in a folder. On a phone the folder can:

* be a **Syncthing folder** on shared storage (e.g. `/storage/emulated/0/Brulion`) — this needs
  the "All files access" permission, described below;
* live in the **app's private directory** (no extra permissions) and be synced with
  **Nextcloud** (WebDAV) from within Brulion — Settings → Sync (over HTTPS, see
  [HTTPS and OpenSSL](#https-and-openssl)).

Everything works offline. Sync (Syncthing or Nextcloud) never blocks writing.

**Android 9 (API 28)** or newer is required — that is what Qt 6.11 supports (target: API 36,
Android 16).

## A Syncthing folder on the phone

1. Install **Syncthing-Fork** (e.g. from F-Droid). The original "Syncthing" app for Android is no
   longer developed — the fork is its continuation.
2. In Syncthing-Fork add the folder shared from the computer (or create a new one) and set its
   path on the phone's storage, e.g. `/storage/emulated/0/Brulion` or
   `/storage/emulated/0/Syncthing/Notatki`. Folder type: "Send & Receive".
3. Open Brulion → notebook choice → pick the same folder.
4. When Brulion asks for **"All files access"**, a system screen opens — turn on the switch for
   Brulion and return to the app (it checks the permission by itself).
   By hand: *Settings → Apps → Special app access → All files access → Brulion* (menu names
   differ between manufacturers).

Why this permission? The Syncthing folder lives on shared storage, and Brulion reads and writes
notes through the plain file system (fast file listing, atomic writes, detecting changes made by
Syncthing). Without this permission Android 11+ only shows media files there. Brulion does not
look outside the chosen folder.

Older Android versions:

* **Android 10** — instead of "All files access" the ordinary file permission is enough (Brulion
  uses `requestLegacyExternalStorage`).
* **Android 9** — Brulion asks for the "Storage" permission (files and media). If "Don't ask
  again" was chosen before, the app's settings open.

Without a folder on shared storage the notebook is created in Brulion's private directory.
Syncthing has no access to it — sync through Nextcloud then.

Syncthing tips:

* It is worth having a `.stignore` file in the folder that skips temporary files (Brulion has a
  ready template). Brulion keeps device data (change history, indexes — including the ready
  notebook index `index-cache.bin`, which speeds up the next opening and can be deleted safely)
  outside the folder, so it causes no conflicts.
* Brulion detects Syncthing conflicts of notes and drawings (`*.sync-conflict-*.md`,
  `*.sync-conflict-*.svg`) and offers to merge them (drawings: **Merge drawings**, the strokes
  and elements of both versions).
* File versioning in Syncthing ("Simple" or "Staggered") is a good extra safeguard.

An AI agent or a script on the computer (`brulion-cli`, [CLI.md](CLI.md)) works on the same
folder on its side of the sync — there is no command line on Android. The phone arms the
reminders it adds once Brulion sees the change: at startup, after changes in the notes, and while
the app is closed — at the next [background check of the
notes](#checking-notes-in-the-background).

## Reminders and alarms

A reminder (`⏰ 2026-10-14 09:00` in a note) becomes a system notification that appears **also
while the app is closed and after the phone restarts**. An **alarm** (`⏰! 2026-10-14 06:30`)
rings like an alarm clock. Syntax: `docs/SYNTAX.md`.

### How it works

* While Brulion runs (at startup and after changes in the notes), it hands the system the list of
  upcoming reminders, replacing the previous one. The native part stores it in the app's storage
  and sets system alarms (`AlarmManager`): alarms through `setAlarmClock` (wakes the phone, the
  status bar shows the alarm clock icon), ordinary reminders through
  `setExactAndAllowWhileIdle`. At most the 400 nearest reminders are scheduled at once.
* **Notification channels**: "Reminders" (high priority, notification sound), "Alarms" (alarm
  sound on the alarm stream, vibration, rings until you react, visible on the lock screen) and
  "Missed alarms" (silent). You choose the sound of reminders and alarms in Brulion itself
  (below: [Sounds](#reminder-and-alarm-sounds)); the system settings of the channels are in
  *Settings → Apps → Brulion → Notifications*.
* **An alarm nobody reacts to** stops ringing by itself after **10 minutes** (the app can set
  another time for a given alarm, from 1 to 60 minutes) — also while the app is closed. It then
  turns into a silent **"Missed alarm"** notification and the alarm screen closes; tapping the
  notification still opens the note.
* **An alarm while the screen is off** opens a full-screen alarm screen over the lock screen with
  the buttons **Snooze 10 min**, **Done** (for a task), **OK** and **Open note**.
* Buttons in the notification: **Snooze 10 min** (the reminder comes back in 10 minutes — this
  also works while the app is closed), **Done** (only a reminder on a task) and **OK**. Tapping
  the notification opens the note at the reminder. For an alarm, each of these reactions silences
  it and closes the alarm screen.
* **Done** marks the task done (`[x]` with `✅` — like a status change in the editor; a
  recurring task gets its next date) without opening the note. The change reaches the note at
  once while Brulion is on screen (also under the pulled-down notification shade); while it runs
  in the background — when you return to it (Android pauses the Qt app then), and while the app
  is closed — at its next start, once the notebook is loaded (message "“…” marked done"); the
  `✅` date is the day of the tap. Until then other devices see the task as open, and the task's
  other reminders may still ring. The tap waits on the phone until its notebook is open and
  loaded — also when you close Brulion before loading ends or first open another notebook (e.g.
  the documentation). When the reminder or the task changed in the meantime (also on another
  device), the note stays unchanged and Brulion says so.
* **After a restart** of the phone (after the first unlock), after an app update and after a
  change of the clock, reminders are set again. Those that fell due while the phone was off (up
  to 24 hours back) appear at once as "Missed".
* **Time zone change**: reminders stick to local time — "09:00" stays at 09:00 in the new zone
  (snoozes are not moved).

### Reminder and alarm sounds

*Settings → Reminders → Reminder sound* and *Alarm sound* (separately for `⏰` and `⏰!`):

* **Default** — the phone's notification sound (reminders) or its alarm sound (alarms);
* **Ringtone** — **Choose ringtone…** opens the system ringtone picker (notification sounds for
  reminders, alarm sounds for alarms; your own ringtones added on the phone show too). Brulion
  remembers the chosen ringtone: after switching to "Silent" and back to "Ringtone" the same one
  returns;
* **Vibrate** — vibration only, no sound;
* **Silent** — neither sound nor vibration (an alarm still shows its screen over the lock
  screen).

**Play sample** plays the chosen sound for about 3 seconds (the alarm sound on the alarm stream —
at the alarm volume; "Vibrate" — vibrates). An alarm plays the chosen sound in a loop
(`FLAG_INSISTENT`) until you react ("OK", "Snooze 10 min", "Done", a tap) or until the ring time
ends. The **System notification settings** button (Reminders / Alarms) opens the channel's
settings in Android — for the exceptions: Do Not Disturb, vibration on a watch, a sound changed
by the phone's manufacturer.

How it works: the sound of a notification channel cannot be changed after the channel is created
(Android remembers a channel's settings, also of a channel deleted and created again with the
same id). So each new sound gets a **new channel version**: `reminders` / `alarms` (version 0 —
the channel from earlier Brulion versions, with the default sound and whatever was set for it in
the system), then `reminders-v1`, `alarms-v2`… with the sound (`AudioAttributes`
`USAGE_NOTIFICATION_EVENT` for reminders, `USAGE_ALARM` for alarms) and vibration; the previous
version is deleted (Brulion's system settings then show a number of "deleted categories"). The
native part stores the choice and the versions in `SharedPreferences` (`…android.sounds`),
because alarms ring while the app is closed: every notification (scheduled, after a restart,
missed, after a snooze) goes to the current channel — changing the sound and posting a
notification share one lock, so a notification never goes to a channel that was just deleted
(Android would reject it). Sound settings are **local to the device** (a ringtone from one phone
does not exist on another) — they are not synced with the notebook. Changing the sound while an
alarm rings closes its notification (the channel is deleted).

### Checking notes in the background

Brulion schedules reminders from the notes it has read. A reminder added or changed on the
computer (also by an AI agent with `brulion-cli`) reaches the phone together with the note —
through Syncthing or Nextcloud. So that it rings even while Brulion is closed, Android runs a
**background check of the notes** every now and then:

* *Settings → Reminders → Check notes in the background*: **Off**, **15 min**, **30 min**
  (default) or **1 h**. Under the setting's name: when the last check ran and how many reminders
  it found.
* The notebook checked is the one whose reminders are set (the one opened last; the
  documentation does not count). When the notebook syncs with **Nextcloud**, one sync runs first
  (the same as in the app, with the same state), then the notes are read. With **Syncthing**,
  Syncthing-Fork downloads the files itself and Brulion only reads them.
* What is in the notes replaces the set reminders: new ones are added, and those deleted or moved
  on the computer disappear or move. Snoozes from a notification stay.
* When the notebook cannot be read (missing or empty folder, e.g. after a card is removed or the
  file permission is revoked), the set reminders stay unchanged. A failed sync, e.g. without a
  network, breaks nothing: the notes the phone already has are what counts.
* While Brulion is on screen the check is skipped, because the app does this itself. When the app
  returns to the front, the alarms get the app's set back.

Limitations:

* Android runs periodic jobs at most every 15 minutes, and in Doze it postpones them to
  "maintenance windows": on a phone unused for a while, even by an hour or more. A reminder added
  on the computer "in 10 minutes" may therefore not make it, while one for tomorrow morning will.
  Battery saving on some manufacturers' phones can stop the job entirely (below:
  [Battery saving](#battery-saving)).
* Force-stopping Brulion also cancels this job. It returns the next time the app starts.
* The background check does not change notes (apart from the sync itself). "Done" tapped in a
  notification while the app is closed still waits for the app to start, and the widgets are
  refreshed only from the app.

Verified on the emulator (Android 16, 2026-11-05): `scripts/android-emulator-test.sh
--background` — 20 checks; checking a notebook with a cached index takes a few dozen
milliseconds.

How it works:

* `BackgroundCheckJob` (a periodic JobScheduler job, kept across restarts, with no network
  requirements) in the app's main process binds `BackgroundCheckService`: a Qt service
  (`QtService`) in a separate `:background` process. Qt does not start a second time in the
  activity's process (`QtServiceBase` refuses), and the service's `onDestroy` ends the process.
  So the separate process lives only for one check.
* In it, Qt starts Brulion's library with the `--background-check` argument. `main()` goes
  straight to `runBackgroundCheck()` (`src/app/background/backgroundentry.h`):
  `QCoreApplication`, no window, QML or app settings, with a separate event log in
  `files/logs/background/brulion.log`.
* `background::Check` (`src/app/background/backgroundcheck.h`) opens the notebook the way
  `brulion-cli` does (`notebook::Notebook`, with the ready index `index-cache.bin`). With
  Nextcloud it runs one sync with the same `SyncEngine` and the same state from the device data
  folder. Then it computes the reminders with `upcomingFor()`, which gives the same set the app
  hands to Android (a 14-day horizon, at most 64 reminders, ring time, language, notebook id).
  This way the alarm ids match.
* The result (JSON as in `scheduleReminders`) goes back through a `Messenger` to the job in the
  main process, which sets the alarms with `AlarmScheduler.replaceAll`. `ReminderStore`
  (SharedPreferences) has one owner: the main process. The result is discarded when the check
  failed, when it concerns a notebook other than the current target, or when the app came to the
  screen or started in the meantime (`BackgroundCheck.verdict`).
* The app passes what to check through `AndroidBridge::setBackgroundCheck`:
  `{root, stateDir, vault, ringMinutes, language, intervalMinutes, nextcloud: {davUrl, user,
  device}}`. The app password is not in it: the background process reads it from the app's file
  `secrets/nextcloud-password`. The app sends the target when its reminders start and when a
  setting the target uses changes. Closing the app, or opening the documentation or another
  notebook, leaves the target (the alarms of that notebook stay set). Turning reminders off,
  choosing "Off" or closing the notebook (returning to the welcome screen) clears it together
  with the job.
* Two processes, one sync: a `SyncEngine` run holds the lock `store.json.sync-lock` (QLockFile;
  the next run takes over a dead process's lock) and reloads the state before it starts
  (`LocalStore::reload`). When the other process holds the lock, the run ends with the code
  `other-process`: in the app the message "The background check is syncing this notebook right
  now. Brulion will try again shortly." and another attempt in 30 s, while the background check
  then skips the sync and reads what is there.
* When the app returns to the front, C++ scans the folder and hands over its set of reminders
  once more, even unchanged (`ReminderScheduler::resendSoon`). This way the alarms always end up
  with the app's set.

### Permissions

* **Notifications** (Android 13+): Brulion asks for consent with a system dialog. When the system
  no longer asks (denied "for good" or notifications turned off by hand), asking again opens
  Brulion's notification settings.
* **Alarms & reminders** (exact alarms):
  * Android 13+ — granted at install time (`USE_EXACT_ALARM`), nothing to do;
  * Android 12 — *Settings → Apps → Special app access → Alarms & reminders* (Brulion opens this
    screen). Once it is on, reminders are switched to exact ones at once;
  * Android 9–11 — no consent needed;
  * without this permission reminders arrive late (minutes; longer in Doze).
* **Full-screen notifications** (Android 14+): needed for the alarm screen over the lock screen —
  *Settings → Apps → Special app access → Full screen notifications* (Brulion can open this
  screen; when the phone does not have it — Brulion's notification settings). Without this
  permission the alarm still rings, but shows as an ordinary notification.
* **Do Not Disturb**: alarms have the "alarm" category. If alarms are silenced in Do Not Disturb,
  allow them in that mode's settings.

### Battery saving

System alarms work in Doze too, but some manufacturers aggressively "kill" apps in the
background, and **a force stop cancels all of the app's alarms** until it is started again. So:

* *Settings → Apps → Brulion → Battery* → **Unrestricted**;
* **Xiaomi / Redmi / POCO** (MIUI, HyperOS): turn on **Autostart** for Brulion and set "Battery
  saver" to "No restrictions";
* **Samsung**: remove Brulion from "Sleeping apps" and "Deep sleeping apps" (*Settings → Battery
  → Background usage limits*);
* **Huawei / Honor**: *App launch* → Brulion → manage manually (all three switches on);
* **OnePlus / OPPO / realme**: allow background activity and autostart.

The same settings let the [background check of the notes](#checking-notes-in-the-background)
run. Do the same for **Syncthing-Fork**, so that it syncs in the background. Details for specific
phones: <https://dontkillmyapp.com>.

## Importing notes (Obsidian, Logseq, Notion…)

Where: **More (⋮) → Import notes**, **Menu → Import notes** (drawer), the **Import notes** button
under the empty journal of a new notebook, and the "Notes in another app?" card on the welcome
screen (it opens the chosen notebook straight on the import screen). On the import screen choose
the app the notes come from — under its description are the steps **on the phone** (where the
files are, what to pick) — or pick the files right away: Brulion recognises the format itself.

* **Choose files…** — the system file picker (Storage Access Framework): ☰ switches between
  Downloads, the phone's storage, Google Drive and other apps; **a long press selects several
  files**. The picker does not filter file types (providers name them differently, and a filter
  would grey out e.g. a Notion ZIP on Drive or an .md file in Downloads) — Brulion recognises the
  format by the content. `.md`, `.txt`, `.html`, `.enex`, `.json`, `.zip` (a whole Obsidian /
  Logseq / Notion / Google Keep / Trilium export), `.jex`, `.ics` and others work.
* **Choose a folder…** — an Obsidian vault folder, a Logseq graph or a folder of `.md` files:
  open it in the system picker, tap **"Use this folder"** and allow access (read only, only this
  folder). Android **does not allow** picking the whole *Downloads* folder, the storage root or
  `Android/data` — pick a folder inside (e.g. *Downloads/Obsidian*) or type the path.
* **Or type a path** — a file or folder in the file system, e.g.
  `/storage/emulated/0/Download/Obsidian`. Outside Brulion's private directory this needs "All
  files access" (the card shows a button to the system settings); picking through the system
  picker needs no permission at all.

Before writing, Brulion shows what it will import (pages, daily notes, attachments, renamed
files, warnings), then the progress and a report with the **"Open the imported folder"** button
(by default `Import/<app>`; daily notes go into the journal).

**Microsoft To Do** works on the phone just like on the computer: after **"Sign in to
Microsoft"** Brulion shows a code — tap **"Open microsoft.com/devicelogin"** (or open that address
on another device), enter the code (**"Copy code"** copies it to the clipboard), sign in and
return to Brulion; the lists download by themselves. You paste the application ID from your own
Microsoft Entra registration once — details in [IMPORT.md](IMPORT.md#microsoft-to-do).

How it works: the system picker returns `content://` addresses — a single document or a
**document tree** (the chosen folder). Brulion walks the tree folder by folder through
`DocumentsContract` (`Documents.kt`, one `ContentResolver` query per folder; JNI bridge
`documentInfo` / `listDocuments`), and reads the files with `QFile` on the `content://` address
(a file descriptor from `ContentResolver`). Listing and reading run off the UI thread, so a large
folder or a slow provider (Google Drive) does not freeze the app. When the JNI call fails, Brulion
falls back to Qt's own handling of `content://` addresses (`QDir`/`QFileInfo`; slower — many
queries per file). Files that exist only online (e.g. Google Docs) are skipped with a warning.

## Share → Brulion

The **Share** menu of other apps (browser, reader, file manager, mail) has **Brulion**. It
accepts:

* **text and links** — e.g. a page from the browser: the page title and its address become
  `[Title](address)`, and the rest of the text follows after " — " ("Wideo" + "Zobacz to
  https://yt.be/abc" → `[Wideo](https://yt.be/abc) — Zobacz to`; Polish for "Video" and "Watch
  this");
* **text files** `.md`, `.markdown`, `.txt` (and any `text/*` type), also **several at once** —
  up to 2 MB each and up to 20 in one share. It recognises the encoding itself: UTF-8 (with and
  without a BOM), UTF-16 with a BOM, and old Notepad files — windows-1250/1252, as in the import.

Brulion writes nothing right away: it opens the **"Save to Brulion"** dialog with a choice of
**"Today's journal"** or **"New page"**:

* text goes to the journal by default (as one block at the end of today's note) — it can be
  edited before saving. **Dates are not recognised** (as in the quick note): "tomorrow" in an
  article title does not create a reminder; a date can be added by hand;
* a single file becomes a **new page** by default — the title comes from the file name
  ("Shopping list.md" → "Shopping list", it can be changed), the content is **exactly as in the
  file** (line endings too); a name already taken gets " 2". A page that differs from an existing
  one only in letter case ("shopping list" next to "Shopping list") **does not overwrite** it: the
  dialog says such a page already exists, and you can change the title or pick the journal;
* several files go to the journal by default: **each file is one block** — a bold title with the
  file's content under it (indented, so a list from the file stays a nested list; frontmatter is
  skipped). As "New pages" — one page per file (a twin that differs only in letter case is skipped
  with a message);
* new pages go to the notebook's folder for new pages (notebook settings);
* files that cannot be saved (an image, too large, unreadable, with zero bytes) are on the list
  with the reason. Text attached to the files (e.g. a message subject) is skipped when at least one
  file can be saved; when none can, the text is saved (a link shared together with an image or
  with an attachment that cannot be read is still that link). A preview thumbnail that an app
  attaches to shared text only in `ClipData` (without `EXTRA_STREAM`) does not make it onto the
  list at all.

"Cancel" saves nothing. When Brulion is still opening a notebook, the dialog appears once the
notebook is ready; when the **documentation** is open, Brulion first returns to your notebook
(shared notes never go into the documentation); with no notebook at all it asks you to open or
create one and shows the dialog afterwards.

The same dialog opens on the computer — **Open With → Brulion** on an `.md` or `.txt` file in
the file manager ([docs/WINDOWS.md](WINDOWS.md#open-with),
[docs/NIXOS.md](NIXOS.md#open-with)) — and in the browser version installed as an app
(Chrome or Edge on Android: text and links, [docs/WEB.md](WEB.md#sharing-to-brulion)).

How it works: a separate, exported activity **`ShareActivity`** (filters `ACTION_SEND` and
`ACTION_SEND_MULTIPLE` for `text/*`; no UI of its own, kept out of "Recents", with its own task —
the system starts it in the sending app's task, where a second Qt activity cannot live). On a
background thread it copies the shared files to `files/share-in/<time>-<n>/<n>/<name>` (the read
permission from the sender ends together with this activity), reads `EXTRA_SUBJECT`,
`EXTRA_TEXT`, `EXTRA_STREAM` and `ClipData`, and then passes the share JSON
(`{subject, text, dir, files: [{path, name, mime, size, error}]}`) to C++ **inside the process**
(`BrulionAndroid.deliverShare` → native `nativeShared`, or `takeShare` on a cold start — like a
notification tap) and brings `BrulionActivity` to the front. The share does not travel as an
intent extra to `BrulionActivity`: that activity is exported (launcher icon), and any app could
forge such an extra. Everything from the sender is untrusted: only `content://` addresses of other
apps are read (not `file://` nor Brulion's own provider — otherwise the sender could make Brulion
read its private files), extras are read with exception handling, copy names are sanitised (`/`,
`..`, control characters, length). C++ (`AppController::receiveShare`,
`src/app/shell/appshare.cpp`; pure functions in `src/core/notebook/share.h`) reads the copies at
once and deletes the copy folder after saving or cancelling — only inside `files/share-in/` —
and `ShareActivity` deletes leftovers older than a day at the next share.

## Widgets

On the home screen (long-press an empty spot → **Widgets** → **Brulion**) there are three
widgets:

* **Tasks — My day** (3×2 by default, any size) — the weekday and date, the number of overdue
  tasks, and a list below: **Overdue** (due date in red: "yesterday", "Mon, Oct 26") and
  **Today** (the reminder time — `⏰ 09:00`, `⏰! 06:30` for an alarm — or the due time, 🔁 on
  recurring tasks, the page name). The widget enlarged to three rows also shows **Tomorrow**. The
  sections and the order are the same as in the **Tasks** view. Tapping a task opens its note at
  that line, tapping the header opens Brulion, and **+** adds a task (below).
* **Calendar** (4×4 by default, down to 3×2) — the month with day dots (overdue tasks and alarms
  in red, reminders, due dates, scheduled, done, journal; at most three), today in a frame, the
  ‹ › buttons, tapping the title goes back to today. Under the month, the agenda of the chosen
  (tapped) day and the following ones: **entries with a time first**, by time, then all-day ones;
  done ones struck through. A short widget (below about 270 dp) shows only the agenda from today.
* **Notes** (3×2 by default) — the notebook name, the buttons **Today's journal** (opens today's
  daily note when it exists; otherwise Brulion) and **Quick note**, and under them the
  **Favourites** and **Recent** pages (journals labelled with the day: "Tuesday, October 27").

The widgets speak the app's language (Settings → General), not the phone's, and have light or dark
colours like the system (the paper and slate colours of Brulion's theme).

**Where the data comes from.** On every change in the notebook (with a 2 s delay), after the
notebook is fully loaded, at midnight and **when you leave the app**, Brulion writes a "snapshot"
for the widgets: open tasks that are overdue or due in the next two weeks (with dates, at most
200), the calendar from a week back to eight weeks ahead, the favourite and recent pages, and
where quick add should write (the journal paths for the next week and the journal template text
for each of those days — also for a day that already has a note: if someone deleted it before
the widget appends something, the new one starts from the template, as in the app), and whether
reminders are on. The widgets draw it themselves, **without starting Brulion**: they show the
state from when Brulion was last open — changes sent by Syncthing appear after the app is opened.
After midnight (and after a time zone change or a phone restart) the widgets move the tasks to the
new day themselves: today's tasks with a due date become overdue, tomorrow's become today's, just
as the Tasks view would do. The snapshot is only text (never HTML) and stays in the app's private
data; in the documentation notebook nothing changes (the widgets keep showing your notebook), and
closing the notebook (returning to the start screen) clears the widgets: "Open Brulion to show
your notebook here".

### Quick add

**+** on the tasks or calendar widget opens a small **New task** window over the home screen
(**Quick note** on the notes widget — a window for a plain list item, also over several lines):

* the text, and under it the day: **Today** (selected), **Tomorrow**, **No date**, **Date…** (a
  calendar); "Today" and "Tomorrow" are computed at the moment **Add** is tapped (a window opened
  before midnight writes the new day after midnight);
* **Time…** adds a reminder at that time on the chosen day, and **Alarm** turns it into an alarm;
* **Add** saves and clears the field, so several tasks can be added in a row; Back or a tap
  outside closes the window.

The task goes at the end of **today's journal** exactly as the app would add it:
`- [ ] Kupić żółty ser 📅 2026-10-28` ("buy yellow cheese"; a day without a time — a due date),
`- [ ] Wizyta u dentysty ⏰ 2026-10-28 09:00` ("dentist appointment"; with a time — a reminder),
`⏰!` for an alarm, a bare `- [ ] …` with no date. **Words in the text are not dates** — "jutro o
9" ("tomorrow at 9") stays text (unlike quick add in the app); the buttons are there for the date.

**Immediately, even while the app is closed.** The window does not start Brulion: it appends the
line to the journal file itself (Kotlin, `JournalAppend.kt`) — the only place where the Android
part writes in the notebook. It does so safely: only in the notebook folder from the snapshot,
and only when that folder exists (it does not recreate a moved, deleted or unavailable notebook —
the entry then waits in the queue; the path is checked after resolving `..` and symlinks), only in
an `.md` file in UTF-8, through a temporary file `.brulion-tmp-…` in the same folder that replaces
the note in one move, and only when nobody changed the note in the meantime; it keeps the note's
line endings and BOM, fills an empty `- ` item from the template, and creates a missing journal
from the journal template. The new task shows in the widgets at once, and a task with a time gets
a system reminder at once (a temporary one — the app replaces it with its own set at the next
start or return to the front; a reminder more than 14 days ahead the app sets, like any other,
when it comes closer), with **Done** like any task reminder (the app finds the task by note,
time, line and text). When reminders are off in Brulion, the widget sets none. Syncthing sends
the changed file right away.

While Brulion runs (in the background), the window also writes by itself, and the app reloads the
notebook when it returns to the front (Qt pauses its event loop in the background) and hands
Android its set of reminders once more — the temporary reminder gives way to it even when the set
has not changed. When the entry cannot be written right away — Brulion has not been opened with a
notebook yet (no snapshot), the snapshot is older than a week, someone was just changing the note,
a write error — the window says "It will be written to the journal when Brulion opens": the entry
waits in the queue and the app appends it to today's journal at the next start with that notebook
(message "1 entry added from the widget…"). If the app cannot append it either (e.g. the journal
is not UTF-8 text — Brulion does not rewrite such a note, so as not to break its characters), the
entry goes back to the queue and waits for the next attempt ("…it is kept and tried again
later"); nothing typed is lost.

Until the app publishes its first snapshot, the widgets and the window speak the language the app
will start in (English, until Polish is chosen in the app). On Android 9–11 the widget picker shows
their sketch (`android:previewImage`, vector drawings from `scripts/gen-widget-previews.py`), from
Android 12 on — a preview from the layout (`previewLayout`).

**Ticking tasks off from the widget** is not possible — you tick a task in the app (or with
**Done** in the reminder's notification).

How it works: C++ (`src/app/widgets/`: `widgetsnapshot.h` builds the snapshot from the same
models as the Tasks and Calendar views, `widgetpublisher.h` publishes it) passes JSON through
`AndroidBridge::updateWidgets`; Kotlin (`WidgetStore`, `WidgetUpdater`) keeps it in
SharedPreferences and refreshes the widgets (the `AppWidgetProvider`s `TasksWidget`,
`CalendarWidget`, `NotesWidget`, list rows through `WidgetListService`; no periodic updates — only
an alarm just after midnight). Quick add is `QuickAddActivity` (an ordinary Android activity,
without Qt) and `QuickAdd`/`JournalAppend`; the queue for the app is `WidgetInbox`, taken like the
"Done" queue (`AndroidBridge::takeWidgetActions`). The rules (the task line, appending to the
journal, moving tasks to the new day) are checked in C++ and in Kotlin with the same cases
(`tests/data/widgets/`).

## Export: "Save to Downloads"

You can save a finished export file (ZIP, HTML, iCalendar…) in two ways:

* **Save…** — the system "Save as" dialog (any place, also Google Drive or an SD card);
* **Save to Downloads** — with one tap into the *Download* folder. On Android 10+ this goes
  through `MediaStore` and **needs no permission** (also without "All files access"); a name
  already taken gets a "(1)" suffix from the system, and the report gives the file name. On
  Android 9 the file is written straight into *Download* (the storage permission is needed).

**Print…** of a page (page menu, palette) makes a PDF the same way as the PDF export (A4 or Letter
paper by the device's region), puts it in the app's cache (`cache/print/`) and hands it to the
system print service (`AndroidBridge::printPdf`, `PdfPrint.kt`): printers, number of copies and
"Save as PDF" belong to the system; the file is deleted after its dialog closes.

## Backups

The default backup folder is in the app's private files (`<data>/…/backups`): it needs no
permissions, but no file manager sees it, and uninstalling Brulion deletes it. That is why every
backup has **Share** (the system sheet, `AndroidBridge::shareFile`: the provider serves only the
app's files, the bytes are streamed, without loading the ZIP into memory).

The system folder picker gives a `content://` identifier, while the backup is written with plain
files. So Brulion accepts only a document that names a folder (`platform::localPathOfDocument`:
the phone's storage, a card) and checks writing with a test file. Without "All files access" it
shows that this needs to be turned on; a cloud drive is rejected with a message instead of failing
silently later. Background: Qt stops the event loop in the background, so the schedule is also
checked when the app returns to the front; the leftover of an interrupted write (`….zip.XXXXXX`)
is deleted by the first write after a day.

## On-screen keyboard

The editing toolbar always sits right above the on-screen keyboard — in the journal and on pages,
also while scrolling — and the bottom navigation hides while the keyboard is open (docs/UI.md
"On-screen keyboard"). When the system hides the keyboard (Back, a key on the keyboard itself),
the bottom navigation returns and the toolbar stays right above it while typing goes on; touching
the text brings the keyboard back, and a navigation button opens its view (typing ends). A
sliding keyboard (Android 11+, frame by frame) first covers the navigation, which hides only
underneath it, and when it slides away it uncovers it: the toolbar neither drops before the
keyboard slides in nor jumps after it hides (`KeyboardInsets.slidesOver`). The `SafeArea` margin
from Qt is sometimes enlarged by the keyboard on Android (336 dp while it is open and for a moment
after it hides — emulator logs), so the bottom navigation and the content count the system
navigation bar without it: a margin higher than 120 dp is the keyboard, and the last lower one
counts (`AppShell.navigationBar`). Bottom sheets, menus and messages also always sit above the
keyboard, and a form field (settings, notebook choice, dialogs) scrolls above it when it gets the
focus.

How it works:

* `BrulionActivity` has `android:windowSoftInputMode="adjustResize|stateUnchanged"` in the
  manifest: the window is never panned — without this Qt chose panning or resizing separately for
  each field, and a panned window tore the toolbar off the keyboard. So nobody scrolls single-line
  fields into view: the app does it (`BKeyboardFocusKeeper`, docs/UI.md).
* On Android 11+ the window is drawn under the system bars (edge-to-edge): on 15+ always, on
  11–14 thanks to `Qt::ExpandedClientAreaHint` set by `src/app/ui/keyboardsources.cpp`. Such a
  window does not resize for the keyboard and gets every frame of its slide; the app's bars keep
  clear of the system bars by the `SafeArea` margins, and the system bar icons follow the window
  colour, that is the app's theme (dark on light, light on dark;
  `BrulionAndroid.setSystemBarsLight`, again after a configuration change, because Qt then sets
  them by the system's day/night mode). The same with the backgrounds from Settings → Appearance
  (OLED black, white): the brightness of the window colour is what counts. Before QML draws the
  first frame, Qt's default startup background shows for a moment, not the chosen background (a
  short flash with black). Android 9–10 keep the window inside the bars.
* `ImeInsets.kt` (installed in `onCreate`) adds an invisible 0×0 view to the window and watches
  the keyboard insets (`WindowInsets` of type `ime()`) from it, without touching Qt's listeners
  (`SafeArea`) and without consuming anything: `OnApplyWindowInsetsListener` gives the final
  value, `WindowInsetsAnimation.Callback` (Android 11+) every frame of the keyboard slide, and a
  layout listener on the decor view the state on older systems, where the Qt view could have
  stopped the insets. A hiding keyboard reports as not visible (`shown` false) from the first
  frame. It computes how many pixels of the Qt view the keyboard covers **on screen**: the bottom
  of the Qt view minus the top edge of the keyboard. When the system shrank the window above the
  keyboard (Android 9–10), this comes out as 0; with an edge-to-edge window (Android 11+) — the
  whole keyboard height. The report goes through `BrulionAndroid.imeChanged` → native
  `nativeImeChanged` (Android's main thread; C++ only queues the signal
  `AndroidBridge::imeChanged` to the Qt thread), only when something changed.
* C++: `KeyboardInsets` (`src/app/ui/keyboardinsets.h`) converts physical pixels to logical ones
  and from the first report on ignores Qt's keyboard rectangle (`QInputMethod::keyboardRectangle`,
  kept as a fallback in case the observer was not installed). While the keyboard slides (Android
  11+) the toolbar follows it frame by frame (without an animation of its own), and the cursor is
  scrolled above the toolbar only once the keyboard stops; on Android 9–10 the window resizes
  once.
* Diagnostics: the detailed event log records the settled value ("On-screen keyboard shown,
  overlap 336 px (platform)") and where the content ends after it stops ("Typing: the
  content ends 84 px above the window's bottom (keyboard 0 px, navigation bar 24 px, bottom
  navigation 84 px), editing toolbar docked" — after Back: the toolbar on the bottom navigation,
  60 dp above the system navigation bar); `adb shell setprop log.tag.BrulionIme DEBUG` (before the
  app starts) prints every observer report in logcat, including the slide frames.

Verified on the emulator (`scripts/android-emulator-test.sh --keyboard`, below; Pixel 6 profile,
1080×2400, density 420, Gboard keyboard), in the journal and on a guide page:

* Android 16 (API 36, gesture navigation): the keyboard's top edge at 1517 px, the Qt view down to
  2400 px, the observer reports 883 px, the app 336 dp (= 883 · 160 / 420); the slide came in 8
  frames; after Back the bottom navigation returns (60 dp above the gesture navigation bar, 24
  dp), the content ends 84 dp above the bottom, and the toolbar sits on the navigation
  (UiAutomator: the middle of the toolbar at 2109 px, of the Calendar button at 2257 px); touching
  the text brings the keyboard back, the toolbar returns onto it, and the navigation buttons
  disappear;
* Android 11 (API 30, three-button navigation): the window is edge-to-edge too (0–2400 px; before,
  the system shrank it above the keyboard without any frames): the keyboard's top edge at 1533 px,
  the observer reports 867 px, the app 330 dp; the slide came in 9 frames; the app's bars sit
  under the status bar and above the navigation bar, the system icons dark in the light theme and
  light in the dark one. This run was still from before the bottom navigation returned after Back
  (the toolbar then stayed 48 dp above the bottom, above the navigation bar); the state after Back
  with the bottom navigation on Android 11 was not checked.

On both the toolbar sits exactly on the keyboard and stays there while the journal is scrolled
with a finger. Android 9–10, a keyboard lower than the bottom navigation (only the suggestion strip
with a physical keyboard — then the toolbar stays in the navigation's place,
`AppShell.keyboardFloor`), a floating keyboard and real phones were not checked.

## Side menu gesture

The drawer with the page tree opens — besides "Menu" on the bottom bar — with a swipe to the right
from the left edge of the screen; a swipe to the left on the drawer or on the dimmed background
closes it (docs/UI.md "Window layout"). With **gesture navigation** (Android 10+) the system takes
a swipe started at the very edge for its "Back" gesture (a strip of ~30 dp, wider with a higher
sensitivity of the "Back" gesture in the system settings), so the finger has to be put down a
little further from the edge: the app's strip (24 dp) starts right after the system's zone. With
button navigation the strip is at the very edge. While typing and selecting blocks, and on the
week and month grids of the calendar (there a swipe to the right from the edge shows the previous
period), the gesture does not open the drawer (use ☰ then). The strip depends on the UI mode, not on the pointer: a mouse or touchpad in the touch UI
(e.g. a phone with a mouse) gets it too. In the browser on a phone (WebAssembly) nobody reports the
system zones: the strip is at the very edge, where the system's or the browser's "Back" gesture
may take over the swipe — ☰ opens the drawer then. The top bar on a phone always starts with
☰; on Android it has no back arrow, since the system's Back key / gesture walks the same history.

How it works:

* `ImeInsets.kt` also reads `WindowInsets` of type `systemGestures()` from the same window insets
  — the width of the system gesture zones at the left and right edge of the Qt view (0 with
  buttons; with a view set off from the edge only the part of the zone that reaches it) — and
  reports them through `BrulionAndroid.gestureInsetsChanged` → native
  `nativeGestureInsetsChanged` (physical pixels, only on a change: rotation, a change of the
  navigation mode or of the sensitivity).
* C++: `GestureInsets` (`src/app/ui/gestureinsets.h`, a QML singleton) converts them to logical
  pixels; `AppShell.qml` sets the drawer's `dragMargin` to `max(SafeArea.left,
  GestureInsets.left) + 24` px. Taps and vertical scrolling in this strip go to the content; the
  drawer opens only on a mostly horizontal swipe longer than 20 px that nothing else holds already
  (a scrolling table). The text selection handles are separate Android windows above the app — the
  strip does not affect them. While typing and selecting blocks (the keyboard or the docked editing
  toolbar, also above the bottom navigation after the keyboard hides) there is no strip: the drawer
  used to take over the drag of the block selection's start handle, which lies at the left edge;
  "Menu" still opens it (with the keyboard open, after Back). For the same reason there is no strip
  on a view that itself swipes sideways from the left edge (the view's `edgeSwipe`: the week and
  month grids of the calendar — a swipe from the edge belongs to them). While the drawer is open,
  swiping the grids is off (`covered`), so that a swipe to the left closes the drawer instead of
  changing the period under it.
* The app does not turn off the "Back" gesture at the edge (`setSystemGestureExclusionRects`):
  Android limits such an exclusion to 200 dp of height, and the user would lose "Back" in that
  spot.

Verified only on the desktop (`tst_shell_qml`, synthetic touch, the gesture zone simulated
through `GestureInsets.simulate`). **To be checked on a phone / emulator** (gesture and button
navigation, Android 10 and 15+, portrait and landscape with a display cutout): whether the observer
reports the zone (`adb shell setprop log.tag.BrulionIme DEBUG` → "system gestures left … px" in
logcat), whether a swipe started just past the zone opens the drawer while one started at the edge
still goes back, and whether the strip gets in the way of scrolling tables or selecting text
(swiping weeks and months in the calendar from the left edge is checked by `tst_shell_qml`).

## Event log: sharing

The event log (*Settings → Application → Event log*, docs/ARCHITECTURE.md "Logging") lives in the
app's files directory (`/data/user/0/io.github.januszbit.brulion/files/logs/brulion.log` and
`brulion.log.1`) — invisible to other apps. Brulion writes it out at once on warnings and when
going to the background (Android can stop the process without closing the app; the next start
notes such a session). It can be sent with:

* **Share** — a copy of the export goes to `files/share/` (the old one is deleted) and through the
  system share sheet (`ACTION_SEND`, `EXTRA_STREAM` with a `content://` address from the
  FileProvider `<package>.qtprovider`; the recipient gets a one-time read right) —
  `AndroidBridge::shareFile()`, JNI without Kotlin code. The provider's roots are set by our
  `android/res/xml/qtprovider_paths.xml` (it replaces Qt's template, which covered only
  `files-path`): the app's files and cache, and shared storage (`external-path`, where the
  notebook usually lives — the PDF viewer's "Open in another app"); the provider is not exported,
  and each address goes to one app, on the user's explicit command;
* **Save to Downloads** (`MediaStore`, like the export) or **Save as…** (the system dialog);
* **Copy** — to the clipboard (the newest 256 KB).

The export starts with a header (version, device, what it contains, what it does not contain —
passwords and tokens are removed, the latest syncs).

## HTTPS and OpenSSL

Nextcloud sync (WebDAV) goes over HTTPS. Qt for Android has no TLS of its own: its TLS plugin
(`libplugins_tls_qopensslbackend`) loads OpenSSL 3 (`libssl_3.so`, `libcrypto_3.so`) at the first
secure connection, and Qt does not ship it. So the APK contains **OpenSSL 3.1.8** — prebuilt
libraries from [KDAB/android_openssl](https://github.com/KDAB/android_openssl), as the Qt
documentation recommends ("Adding OpenSSL Support for Android"). Without them (up to version
0.1.1) every HTTPS connection ended with the error "TLS initialization failed", and the connection
test on the Nextcloud screen reported an untrusted certificate even with a correct server.

* **The server certificate is always checked**; Brulion never sends the password to an untrusted
  server. The system's certificate authorities are trusted **and those the user installed**
  (Settings → Security → Encryption & credentials → Install a certificate → CA certificate;
  `android/res/xml/network_security_config.xml`). A self-hosted Nextcloud with its own
  certificate authority therefore works once that authority's certificate is installed on the
  phone — as on the computer. A self-signed server certificate alone is not enough: the connection
  test then shows "The server's certificate is not trusted: it may have expired, be self-signed or
  be issued for another name than “…”" with a hint on what to do.
* **Where the libraries come from**: a KDAB commit pinned in `src/app/CMakeLists.txt`
  (`_brulion_openssl_commit`), each of the 8 files (`libssl_3.so`, `libcrypto_3.so` for arm64-v8a,
  armeabi-v7a, x86_64 and x86) checked by SHA-256. At configure time CMake downloads only the
  files of the ABIs being built (from `raw.githubusercontent.com` to
  `<build>/android_openssl/<commit>/`), and `androiddeployqt` adds them to `lib/<abi>/`
  (`QT_ANDROID_EXTRA_LIBS`). Without a network:
  `BRULION_ANDROID_OPENSSL_DIR=<android_openssl checkout at that commit>`. The Nix build takes the
  same commit from `nix/android/openssl.json`. The 64-bit libraries have segments aligned to 16 KB.
  The build from source (F-Droid) compiles OpenSSL 3.5.9 (LTS) with the NDK instead of them —
  `scripts/android-openssl-from-source.sh`, [Building from source](#building-from-source-f-droid).
* **Updating** (a new OpenSSL release at KDAB): the new commit and SHA-256 sums
  (`sha256sum ssl_3/*/lib{ssl,crypto}_3.so` in the checkout) in `src/app/CMakeLists.txt`, then
  `nix/android/update-locks.sh --openssl <commit>`.
* **Checked at build time**: `scripts/android-build.sh` and the Nix derivation stop the build when
  `libssl_3.so` or `libcrypto_3.so` is missing from the APK for some ABI; the script prints the
  OpenSSL version.
* Cost: +4.45 MB in the arm64 APK (1.77 MB compressed).
* Verified 2026-10-18 on the emulator (Android 16) with the APK from the script and from
  `nix build .#brulion-android-x86_64`, with a local WebDAV server over HTTPS: OpenSSL 3.1.8
  loaded (`QSslSocket::supportsSsl()`), a self-signed certificate and a certificate from an
  unknown authority reported as untrusted, after installing the authority as a user certificate
  "Connected", sync both ways; before (0.1.1) every HTTPS connection: "TLS initialization
  failed".

**Licence.** OpenSSL 3 and the KDAB package are under the **Apache License 2.0**, compatible with
Brulion's AGPL-3.0 (the GPLv3 and AGPLv3 licences allow combining a program with Apache-2.0 code).
The licence text ships in the APK (`assets/licenses/OpenSSL-LICENSE.txt`, from
`android/assets/licenses/`) — whoever distributes the APK (GitHub releases, F-Droid) passes it on
together with it. The libraries are not modified.

## Pictures from the internet

Pictures from `http(s)://` addresses in notes (docs/SYNTAX.md §2, the guide page
"Links and backlinks", in Polish "Linki i odnośniki zwrotne") are downloaded by Brulion itself
(`webpicture::PictureFetcher` through `QNetworkAccessManager`), never by the QML engine — just as
on the desktop:

* **Permissions**: `android.permission.INTERNET` from the manifest is enough; nothing is written
  outside the app's folder.
* **HTTPS** goes through the same OpenSSL as the sync (section above), with the same certificate
  authorities (the system's and those the user installed).
* **http://**: Qt connects with its own sockets, not through Java's `HttpURLConnection`, so
  `cleartextTrafficPermitted` from Android 9+ (off by default; we do not change it in
  `network_security_config.xml`) does not block pictures from `http://` — verified on the emulator
  (below): a picture from `http://10.0.2.2:…` downloaded and shown with no change to the
  configuration. Android's rule applies only to Java's network stack (e.g. `WebView`,
  `HttpURLConnection`), which Brulion does not use for pictures.
* **The cache** lives in `QStandardPaths::CacheLocation`/`web-pictures`
  (`/data/data/<package>/cache/web-pictures`), up to 200 MB, the least recently viewed go first.
  Android may clear it by itself (low storage, "Clear cache" in the app's settings) — then the
  picture waits for **Show** like a new one; copies saved to the notebook are attachments in the
  notebook folder and do not disappear.
* **The setting** "Show pictures from the internet without asking" (Settings → Editor) is off by
  default and applies only to this device.
* **Touch**: tapping a shown picture opens it full screen (`WebPictureViewer`, the **Back** gesture
  closes it), a long press opens a menu (View picture, Save to notebook, Open in browser, Copy
  picture address); "Open in browser" hands the address to the system (`QDesktopServices`).
* **Pasting**: "Copy image address" in Chrome and pasting with **Paste** from Brulion's text menu
  offers "Save to notebook"; pasting from the system keyboard (its clipboard enters text like
  typing) inserts the bare address with no offer.

Verified on the emulator 2026-10-28 (Android 16, Pixel 6, debuggable x86_64 APK;
`scripts/android-emulator-test.sh --web-pictures`, 33 checks, screenshots `wp-*.png`): in the
notebook's journal a picture from `https://` (the Google logo) and from `http://` (a PNG from a
server the script starts on the host — the emulator sees it at `10.0.2.2`). Both wait for
**Show**; after a tap they go to `cache/web-pictures` and are visible (the server got a `GET`, so
`http://` is not blocked). After a restart in airplane mode both are visible from the cache without
asking. A tap opens the picture viewer (actual size and back), **Back** closes it; a long press
opens the menu "Picture from 10.0.2.2" with View picture, Save to notebook, Open in browser, Copy
picture address; **View picture** and **Close** work. After the cache is cleared with no network,
**Show** ends with the message "No connection to www.google.com." and a **Try again** button,
which downloads the picture once the network is back. **Save to notebook** writes
`assets/gradient.png` and changes the link in the note to `![…](assets/gradient.png)` (the second
link unchanged). **Open in browser** opens Chrome. Also confirmed by hand: "Copy image address"
puts the address on the clipboard (the keyboard offers it for pasting).

Still to be checked on a real phone: panning a zoomed picture with a finger and the **Back**
gesture from the screen edge (the emulator gets only the Back key and simple drags), pasting an
address copied in Chrome with **Paste** from Brulion's text menu, the HTML export with "Include
copies", a real mobile network / Wi-Fi instead of the emulator's network.

## PDF

PDFs are drawn by the system's `android.graphics.pdf.PdfRenderer` (no libraries in the APK,
docs/UI.md "PDF viewer"): thumbnails of PDFs embedded in notes, the PDF viewer, and PDF pages as a
drawing background. `PdfPages.kt` keeps **sessions** — an open file and its `PdfRenderer` — at
most six at once (it closes the oldest, so that a leak on the C++ side does not use up file
descriptors); every call is synchronised on its session (PdfRenderer opens one page at a time). A
page or a part of it (a detail of a zoomed page) is drawn straight into the pixels of the C++
image (`pdfRenderInto`: a direct `ByteBuffer` over the `QImage` memory, one copy through
`Bitmap.copyPixelsToBuffer`).

**Search** works from **Android 15** on (API 35: `PdfRenderer.Page.getTextContents()` and
`searchText()`); `pdfTextAvailable()` checks once whether the system has these methods (an OEM
build without them — the search button disappears). PdfRenderer's own search distinguishes Polish
letters, so Brulion searches the page text its own way (ignoring case and Polish diacritics,
`src/platform/pdfsearch.h`) and asks PdfRenderer only for the rectangles of each spelling found.
On Android 14 and older there is no search button; the rest of the viewer works.

**Open in another app** (`OpenWith.kt`, `AndroidBridge::viewFile`): `ACTION_VIEW` with a
`content://<package>.qtprovider/…` address and a read right for that file only, without a chooser
(the system asks by itself when there is no default app). A file outside the provider's roots (a
notebook on a memory card) is first copied in the background to `cache/open/` (older copies go),
then the app opens. Without a PDF app Brulion says "No app on this device opens PDF files.".

On the emulator (`scripts/android-emulator-test.sh --pdf`, the same debuggable APK): a thumbnail of
`tests/data/pdf/wyklad.pdf` in the journal (the tile's accessible name with the page count), the
password-locked `zaszyfrowany.pdf` with "PDF protected with a password", a touch opens the viewer
("Page 1 of 3"), a swipe up — page 2, a pinch (two fingers injected by
`scripts/android-stylus/BrulionPen.java pinch`) zooms in; on API 36 searching for "gesla" finds
"gęślą" once, on the image `system-images;android-30;google_apis;x86_64`
(`BRULION_EMULATOR_IMAGE`) there is no search button; memory (`dumpsys meminfo`, TOTAL PSS) after
zooming to 400 % and scrolling through all pages — under 350 MB; "Open in another app" is
`START … act=android.intent.action.VIEW dat=content://io.github.januszbit.brulion.qtprovider/…
typ=application/pdf` in logcat (or, with no PDF app in the image, the app's message); "Draw on
this page" opens the drawing editor, the file `assets/wyklad s. 1.svg` (the page the viewer was
on) and its embed right below the PDF's line in the journal; Back → viewer, Back → journal.
Screenshots `pdf-*.png`. The bridge test (test APK) adds the line
`SELFTEST pdf-reader region=1 text=… hits=… rect=… api=…`: a part of a page drawn separately
equals the cut-out of the whole page; on API ≥ 35 `find("GESLA")` finds one hit at the text's
place, below that — none (`text=0`). The line
`SELFTEST pdf-sessions opened=1 oldest-lost=1 newest-draws=1`: with 17 open sessions (the limit `PdfPages.MAX_SESSIONS` is 16) the oldest is
closed, its reader reports `lost()` (the pool drops it instead of reusing it), and the newest still
draws.

Still to be checked on a real phone: smoothness of pinching and the Back gesture from the edge,
search on Android 15+ on a real device, "Open in another app" with real PDF apps, large scans of
lectures.

## Spell checking

Brulion adds no dictionaries to the APK: it asks the **system spell checker** — the same one the
keyboard uses (Gboard, the AOSP keyboard; `TextServicesManager` / `SpellCheckerSession`,
docs/UI.md "Spell checking"). `SpellCheck.kt` opens one session per language (`Locale("pl")`,
`Locale("en")`, without the system's language settings: Brulion chooses the languages) on its own
thread (`HandlerThread`; a session delivers answers on the looper of the thread it was created on),
and the C++ thread waits at most 3 s for an answer. A session that did not answer in time is closed
(a late answer must not reach the next question) and opened again at the next one. Words travel as
JSON, the whole batch in one call (`BrulionAndroid.spellCheck`); a word is wrong only when the
checker marks it as a typo (`RESULT_ATTR_LOOKS_LIKE_TYPO`), so a checker that does not know a given
language underlines nothing. Languages: the subtypes of the current spell checker (Android 12+),
and without them the device's languages and English. When spell checking is off in Android's
settings (Settings → System → Languages → Spell checker), Brulion hides its spelling settings.
Touching an underlined word opens the suggestions (a long press stays the system's text
selection).

Still to be checked on a real phone: Gboard with Polish and English, underlines and suggestions
while typing with a keyboard that corrects words by itself.

## New version

Android does not update an app installed from an APK file, so Brulion tells you about a new
version itself: a message "Brulion X.Y.Z is available" appears under the top bar. **View on
GitHub** opens that release's page in the browser, where you download the APK; installing the new
file over the old one keeps the data, because releases are signed with the same key. The cross
hides the message for that version, and the next release shows it again. Without a network
Brulion works the same: it asks GitHub at startup and at most once a day, and a failed request is
only recorded in the event log and repeated after a few hours. It skips test releases (`-rc`).
GitHub sees the device's address, but no data about the notes. **Settings → Application → Check
for new versions** turns it off. Code: `src/core/appupdate`, `src/app/shell/appupdates.h`,
`qml/shell/ShellUpdateBanner.qml`. A build with the CMake option `BRULION_UPDATE_CHECK=OFF` (for
stores that update the app themselves — [F-Droid and IzzyOnDroid](#f-droid-and-izzyondroid)) has
neither the message nor the switch and never connects to GitHub.

## F-Droid and IzzyOnDroid

The two parts both repositories need are ready; there is no recipe for `fdroiddata` and no build
from source in F-Droid yet (TODO.md, "Stores and repositories").

**Store descriptions** live in the fastlane layout that F-Droid and IzzyOnDroid read:

```text
fastlane/metadata/android/
├── en-US/                      English, the fallback for other languages (also images)
│   ├── title.txt               name (up to 50 characters)
│   ├── short_description.txt   one sentence (up to 80 characters)
│   ├── full_description.txt    description (up to 4000 characters; from packaging/…metainfo.xml)
│   ├── changelogs/<versionCode>.txt   the version's "what's new" (up to 500 characters)
│   └── images/icon.png         512 × 512 icon from assets/logo.svg
│       images/phoneScreenshots/ screenshots from docs/images/phone-*.png
└── pl/                         the same in Polish, with its own Polish phoneScreenshots/
```

`versionCode` = major·1000000 + minor·1000 + patch (`8001.txt` for 0.8.1, as in
`src/app/CMakeLists.txt`). The `changelogs/` files are written by `scripts/release.sh` at every
release (docs/RELEASE.md) from `.github/release-notes.md`: the English one (`en-US`) lists the
bullets of its "What's new in …" section, the Polish one (`pl`) those of "Nowości w …", each with
a link to the full release notes. The other files are changed by hand — the descriptions
together with `metainfo.xml`, the screenshots after `docs/images/phone-*.png` are refreshed
(`cp` with a name carrying the order number; the Polish ones in `pl/` from the same shots with
`LOCALE=pl`), the icon after a logo change:

```sh
nix shell nixpkgs#librsvg nixpkgs#optipng -c sh -c '
  rsvg-convert -w 512 -h 512 assets/logo.svg -o fastlane/metadata/android/en-US/images/icon.png &&
  optipng -o5 -strip all fastlane/metadata/android/en-US/images/icon.png'
```

The limits are checked by `scripts/check-fastlane-metadata.sh [-v X.Y.Z]` (lengths in characters,
both languages, the changelog of the version from `CMakeLists.txt`, a 512 × 512 icon, PNG/JPEG
images); in ctest as `tst_scripts_check_fastlane_metadata`.

**No update check.** An app from F-Droid or IzzyOnDroid is updated by their client, so the
new-version message from GitHub is pointless there, and asking GitHub is a connection such an
install does not need. The CMake option `BRULION_UPDATE_CHECK=OFF` (`ON` by default) turns it off
entirely: `AppController.updates.supported` is false, so there is neither the message nor the
"Check for new versions" switch, and a request to `api.github.com` is never made — not even with
`--update-available`. `scripts/android-build.sh` passes it on from an environment variable:

```sh
BRULION_UPDATE_CHECK=OFF scripts/android-build.sh
```

Test: `tst_shell_appupdates` checks that `AppUpdates::checkBuiltIn()` follows the option, and in a
build with `OFF` — that the app shows nothing and asks nothing.

## Brulion's own F-Droid repository

At `https://janusz-bit.github.io/brulion/` (the project's GitHub Pages) there is Brulion's F-Droid
repository: a page with a QR code and the key fingerprint, and the repository itself in `repo/`.
The same page is Brulion's download page: it opens with the files of the newest release (below,
"The download page").
You add it in F-Droid (or Droid-ify, Neo Store) with the **Add to F-Droid** button on the page,
with the QR code, or by hand (Settings → Repositories → "+": the address
`https://janusz-bit.github.io/brulion/repo` and the fingerprint from the page). After that F-Droid
offers updates by itself.

* **The same APKs as in the GitHub releases**, signed with the release key — Brulion installed
  from GitHub updates from the repository without uninstalling (and the other way round). An APK
  from f-droid.org, if F-Droid signed it itself (below, "Signing: a decision for the owner"),
  would have a different signature.
* The last three releases (no test releases); an APK is about 90 MB, so about 270 MB together out
  of the 1 GB limit of a GitHub Pages site. The description, icon, screenshots and each version's
  "what's new" come from `fastlane/metadata/android` (above), the categories and links from
  `packaging/fdroid-repo/io.github.januszbit.brulion.yml`.
* The new-version message from GitHub (above) stays — it is the same APK; F-Droid usually offers
  the update at the same time.

**How it is made.** `.github/workflows/fdroid.yml` runs `scripts/fdroid-repo.sh` with
fdroidserver 2.4.5 (PyPI): it downloads the APKs of the last three published releases (`gh release
download`), copies the metadata (fastlane as `metadata/<id>/<language>/`, and for each APK a
`Builds:` entry with its `versionCode` from the file name — only this way does F-Droid connect the
version with its `changelogs/<versionCode>.txt`; the newest is `CurrentVersionCode`), reads the
published `repo/index-v2.json` (it keeps the dates when versions already there were added), runs
`fdroid update` (index `index-v1.jar`, `index-v2.json` + `entry.jar` signed with the repository
key, QR code `repo/index.png`), adds the page `packaging/fdroid-repo/index.html` with the address,
the fingerprint and the newest release's files, and the workflow deploys the whole to GitHub Pages. A release (`release.yml`)
calls it after publishing, except for a test release and an APK with a throwaway key; it can also
be run by hand (`gh workflow run fdroid.yml`). Locally (no deployment, APKs from a folder):

```sh
read -r -s -p 'Repository key password: ' FDROID_KEYSTORE_PASS; echo; export FDROID_KEYSTORE_PASS
nix shell nixpkgs#fdroidserver nixpkgs#jdk21_headless nixpkgs#apksigner nixpkgs#gh -c \
  env FDROID_KEYSTORE=~/.local/share/brulion/brulion-fdroid-repo.p12 \
  scripts/fdroid-repo.sh -a dist-android -o dist-fdroid   # dist-fdroid/index.html, dist-fdroid/repo/
```

**The repository key** (separate from the APK key: it signs only the index) is created by
`scripts/fdroid-new-keystore.sh` — outside the repository (by default
`~/.local/share/brulion/brulion-fdroid-repo.p12`); it prints the fingerprint and the
`gh secret set` commands for the secrets `FDROID_KEYSTORE_BASE64`, `FDROID_KEYSTORE_PASS` and
`FDROID_KEY_ALIAS` (by default `brulion-fdroid`). **Back up the key and the password:** F-Droid
accepts the index only signed with this key; a new key means everyone has to remove the
repository and add it again. Once, GitHub Pages also has to be turned on with the "GitHub Actions"
source, and the `github-pages` environment has to allow the `main` branch and the `v*` tags
(docs/RELEASE.md, "One-time repository setup").

**The download page.** The page's first section, **Download Brulion X.Y.Z**, links the files of
the newest release directly (`https://github.com/<repo>/releases/download/vX.Y.Z/<file>` — a click
downloads the file): the APK, both AppImages, the Windows installer and `.zip`, the browser version's
HTML file and `.zip`, with their sizes and the release's date, the `nix run` command for NixOS and
a link to the release page (what's new, installation, SHA-256 sums, the source code); the Polish
section links the same files. `scripts/fdroid-repo.sh` fills them in when it builds the site: the
release is the newest of the repository's (`-r vX.Y.Z` picks another; with `-a` the default is the
latest published one), read with `gh release view` (tag, date, file names and sizes). The script
refuses a draft, a pre-release, a tag other than `vX.Y.Z` and a release without one of the linked
files, so the page never links a file that is not there. `github.com/…/releases/latest/download/…`
is not used, because the file names carry the version. The page is rebuilt with every published
release (`release.yml` → `fdroid.yml`), so it never shows a pre-release. It is static; a short
script on the page only adds a big button for the visitor's system above the list (Android → APK,
Windows → installer, Linux → AppImage x86_64 or aarch64 — Chromium tells the processor only through
`navigator.userAgentData` — another computer → the browser version; none on iPhone/iPad). Without
JavaScript the list is all there is.

Tests: `tests/scripts/tst_fdroid_repo.sh` (ctest `tst_scripts_fdroid_repo`) runs the script with
stand-ins for `fdroid`, `keytool`, `gh` and `curl` — the choice of releases, the fdroid
configuration (passwords only as `{env: …}`, the file readable only by its owner), the `Builds:`
entries, the metadata, the page with the fingerprint, the downloads (every link a file of the
release, the sizes, the date, `-r`, every system of the button with its link) and rejected data;
the real `fdroid update` runs in `fdroid.yml`.

## Building the APK

`scripts/android-build.sh` does it all (locally and in GitHub Actions): it installs missing tools,
configures CMake, builds the `libbrulion_<abi>.so` library, packages the APK with
`androiddeployqt` (Gradle), signs it and checks the result. It can be run repeatedly — an existing
build directory is reused.

```sh
scripts/android-build.sh            # result: dist-android/brulion-<version>-arm64-v8a.apk
adb install -r dist-android/brulion-*.apk
scripts/android-build.sh --help     # all environment variables
```

It needs a **Linux x86_64** host (the NDK and the Qt tools exist only for x86_64) — not the
Raspberry Pi.

### Versions (Qt 6.11.2)

| Component | Version | Source |
|---|---|---|
| Qt for Android | 6.11.2 `android_arm64_v8a` (optionally `armv7`, `x86_64`, `x86`) | aqt (`aqtinstall` 3.3.0) → `~/.cache/brulion-qt` |
| Host Qt (moc, rcc, qmlcachegen, androiddeployqt…) | 6.11.2 `linux_gcc_64` — the same version | aqt |
| Android NDK | r27c (`27.2.12479018`) — required by Qt 6.11 | `sdkmanager` |
| Android SDK | platform `android-36`, build-tools `36.0.0` | `sdkmanager` |
| CMake + Ninja | 3.31.6 (SDK package `cmake;3.31.6`) | `sdkmanager` |
| JDK | 21 (Qt 6.11 recommends 21; 17+ works) | system / nixpkgs `jdk21_headless` |
| Gradle / Android Gradle Plugin | 9.3.1 / 9.0.0 (Qt 6.11 template, built-in Kotlin support) | Gradle wrapper from Qt |
| minSdk / targetSdk / compileSdk | 28 / 36 / 36 | `src/app/CMakeLists.txt` |
| OpenSSL (HTTPS) | 3.1.8, KDAB android_openssl `b71f147` | downloaded by CMake, checked by SHA-256 (`src/app/CMakeLists.txt`) |
| Build from source (`BRULION_ANDROID_QT=source`, F-Droid) | Qt 6.11.2 (6 modules) and OpenSSL 3.5.9 from source | [Building from source](#building-from-source-f-droid) |

Variables (all optional): `QT_ROOT`, `QT_VERSION`, `QT_HOST_PATH`, `ANDROID_SDK_ROOT`
(by default `~/.cache/brulion-android-sdk`; its `cmdline-tools` install missing packages),
`ANDROID_NDK_ROOT`, `JAVA_HOME`, `BRULION_ANDROID_ABIS` (e.g. `"arm64-v8a x86_64"` — one APK
with several architectures), `BRULION_ANDROID_BUILD_DIR` (by default `build-android`),
`BRULION_ANDROID_BUILD_TYPE` (`Release`), `BRULION_ANDROID_AAB=1` (also an `.aab`),
`BRULION_ANDROID_UNIT_TESTS=1` (Kotlin tests), `BRULION_ANDROID_LINT=1` (Android lint),
`BRULION_ANDROID_SELFTEST=1` (also the bridge's test APK), `BRULION_ANDROID_VERSION_CODE`,
`BRULION_ANDROID_CMAKE=system`, `BRULION_ANDROID_OPENSSL_DIR` (OpenSSL from a
KDAB/android_openssl checkout instead of downloading), `BRULION_ANDROID_QT=source` and
`BRULION_ANDROID_OPENSSL=source` (Qt and OpenSSL from source), `BRULION_ANDROID_SIGN=0` (an
unsigned APK), `BRULION_WERROR=ON` (warnings in our C++ are errors), `JOBS`, `SOURCE_DATE_EPOCH`
(the time of files in the Qt resources, rcc; by default the time of the last commit —
reproducible APKs).

`versionCode` = `major·1000000 + minor·1000 + patch` (0.1.0 → 1000) — the same formula as in the
previous version of Brulion (Tauri), so new APKs update old ones (with the same key).

### Signing

The script signs the APK itself (`zipalign -P 16` + `apksigner`; AAB — `jarsigner`) with the key
from the same variables `androiddeployqt` reads:

* `QT_ANDROID_KEYSTORE_PATH` (or `QT_ANDROID_KEYSTORE_BASE64` — the file in base64, a CI secret),
* `QT_ANDROID_KEYSTORE_ALIAS`, `QT_ANDROID_KEYSTORE_STORE_PASS`,
  `QT_ANDROID_KEYSTORE_KEY_PASS` (by default the store password).

The release key is created (outside the repository) by `scripts/android-new-keystore.sh` — it
also prints the `gh secret set` commands for GitHub Actions. **Back up the key and the
password**: every update must be signed with the same key.

**Without a key** the script warns loudly and signs the APK with a **throwaway key**
(`~/.cache/brulion-android/throwaway.jks`, created once per machine). Such an APK can be installed
and updated by later builds from the same machine, but it will not update a version signed with
the release key (that one has to be uninstalled, which deletes the app's private data).

### NixOS

On NixOS the script re-runs itself in `nix shell` (ccache, `jdk21_headless`, unzip). The prebuilt
binaries (the NDK, the Qt tools from aqt, `aapt2` downloaded by Gradle, CMake from the SDK) need
**nix-ld** (`programs.nix-ld.enable = true`); the Qt tools additionally need glib, krb5, brotli
and keyutils — the script adds them to `NIX_LD_LIBRARY_PATH` (from `nix build nixpkgs#…`). CMake
from nixpkgs **is not suitable**: it is patched so that it does not search `/usr` — also in the
NDK sysroot, so it does not find the EGL/GLES headers and Qt6Gui does not configure. So by default
CMake from the Android SDK is used (`BRULION_ANDROID_CMAKE=system` picks the one from `PATH`).

### Building on the laptop (from the Raspberry Pi)

The Pi (aarch64, 3.7 GB RAM) does not build Android. Compilation runs on the x86_64 laptop through
`laptop-run` (it copies the repository's tracked files to `~/work/note-taking` and runs the command
there; new files first have to be marked with `git add -N <paths>`), always under the shared lock.
We keep the build directory outside `~/work` (it is cleaned at every `laptop-run`); caches: Qt in
`~/.cache/brulion-qt`, the SDK in `~/.cache/brulion-android-sdk`, ccache in `~/.cache/ccache`,
Gradle in `~/.gradle`:

```sh
flock <lock> laptop-run bash -c '
  export BRULION_ANDROID_BUILD_DIR=$HOME/.cache/brulion-android-build
  scripts/android-build.sh > $HOME/.cache/brulion-android-build.log 2>&1; echo "exit=$?"
  tail -n 60 $HOME/.cache/brulion-android-build.log'
```

First installation of the tools on the laptop (the script does the same when something is
missing):

```sh
uvx --from aqtinstall==3.3.0 aqt install-qt all_os android 6.11.2 android_arm64_v8a \
  -O ~/.cache/brulion-qt --autodesktop      # + linux_gcc_64 (host Qt)
JAVA_HOME=$(nix build --no-link --print-out-paths nixpkgs#jdk21_headless)/lib/openjdk \
  ~/.cache/brulion-android-sdk/cmdline-tools/latest/bin/sdkmanager \
  --sdk_root=$HOME/.cache/brulion-android-sdk --install "ndk;27.2.12479018" "cmake;3.31.6"
```

### Result (verified 2026-10-14 on the x86_64 laptop, NixOS)

`BRULION_WERROR=ON BRULION_ANDROID_UNIT_TESTS=1 BRULION_ANDROID_LINT=1 scripts/android-build.sh`
(first build: downloading Gradle 9.3.1 + the AGP dependencies, then Gradle ~45 s; later runs with
ccache and the Gradle cache — a few dozen seconds). End of the log:

```
==> Checking dist-android/brulion-0.1.0-arm64-v8a.apk
Signer #1 certificate DN: CN=Brulion unofficial build, O=Throwaway key
package: name='io.github.januszbit.brulion' versionCode='1000' versionName='0.1.0' … compileSdkVersion='36'
minSdkVersion:'28'
targetSdkVersion:'36'
application-label:'Brulion'
native-code: 'arm64-v8a'
permissions: 13 (all required present)
Kotlin classes in dex: 16
native libraries: 72 in lib/{arm64-v8a}
==> Kotlin unit tests (android/tests)          # ReminderJsonTest 5, WallClockTest 4, RingTimeoutTest 3 — 0 failures
==> Android lint
lint: 0 errors, 2 warnings — in Brulion's files: 1
AndroidManifest.xml:59: Warning: Attribute enableOnBackInvokedCallback is only used in API level 33 and higher … [UnusedAttribute]
==> Done
APK: …/dist-android/brulion-0.1.0-arm64-v8a.apk (58M, signing: throwaway)
```

The merged manifest (`aapt2 dump xmltree --file AndroidManifest.xml`) contains `BrulionActivity`
(LAUNCHER, `singleTop`), four receivers (not exported; `BootReceiver` with the actions
BOOT_COMPLETED, QUICKBOOT_POWERON, MY_PACKAGE_REPLACED, TIME_SET, TIMEZONE_CHANGED,
SCHEDULE_EXACT_ALARM_PERMISSION_STATE_CHANGED), `AlarmActivity`, `requestLegacyExternalStorage`,
the icons `@mipmap/ic_launcher`/`ic_launcher_round` and the permissions: ACCESS_NETWORK_STATE,
INTERNET (from Qt Network), MANAGE_EXTERNAL_STORAGE, READ_EXTERNAL_STORAGE (max 32),
WRITE_EXTERNAL_STORAGE (max 29), POST_NOTIFICATIONS, SCHEDULE_EXACT_ALARM (max 32),
USE_EXACT_ALARM, RECEIVE_BOOT_COMPLETED, VIBRATE, WAKE_LOCK, USE_FULL_SCREEN_INTENT. The method
signatures of the compiled `BrulionAndroid` class (`javap -s`) match the JNI calls in C++.

End-to-end test on the emulator (Android 16 / API 36, x86_64, `google_apis`; the same day):
`BRULION_ANDROID_ABIS=x86_64 BRULION_ANDROID_SELFTEST=1 BRULION_WERROR=ON scripts/android-build.sh`
and `scripts/android-emulator-test.sh` — all 14 checks passed:

```
==> App: dist-android/brulion-0.1.0-x86_64.apk
  PASS app is running 15 s after start
  PASS no crash (FATAL EXCEPTION / Fatal signal)
  PASS notification channels reminders, alarms, missed-alarms
==> Bridge self-test: dist-android/brulion-selftest-0.1.0-x86_64.apk
  PASS scheduleReminders round 1: 2 armed (past one skipped)
  PASS scheduleReminders round 2: idempotent
  PASS permissions reported (notifications granted, exact alarms)
  PASS cold-start tap delivered to C++
  PASS tap while running delivered to C++ (Polish path)
  PASS AlarmManager holds Brulion's alarms
  PASS reminder 101 fired on channel 'reminders'
  PASS alarm 102 rings on channel 'alarms'
  PASS alarm 102 became 'missed-alarms' after its ring time (1 min)
  PASS no crash in the self-test
W brulion.selftest: SELFTEST permissions(start) allFiles=0 notifications=1 exact=1 fullScreen=1
W brulion.selftest: SELFTEST schedule round=1 ok=1 scheduled=2 exact=1 error=
W brulion.selftest: SELFTEST schedule round=2 ok=1 scheduled=2 exact=1 error=
W brulion.selftest: SELFTEST activated path=dziennik/zimny-start.md line=5
W brulion.selftest: SELFTEST activated path=strony/Łódź.md line=7
W brulion.selftest: SELFTEST pdf backend=Android PdfRenderer pages=1 width=595 height=842 centre=#ff0000 corner=#ffffff error=
```

`dumpsys alarm` showed the reminder as an exact RTC_WAKEUP alarm (`setExactAndAllowWhileIdle`)
and the alarm as an alarm with a `showIntent` (`setAlarmClock`), both with the action
`…android.action.FIRE` and `exactAllowReason=policy_permission` (`USE_EXACT_ALARM`).

### APK size

What release builds (Release, MinSizeRel) leave out or shrink — measured 2026-10-18 on the
arm64-v8a APK (`tests/bench/mobile/size-report.py apk`):

| Change | Where | arm64 APK |
|---|---|---|
| **R8** shrinks the Java/Kotlin code (the Kotlin standard library, androidx) | `android/settings.gradle`, rules in `android/proguard-rules.pro` | `classes.dex` 5.12 MB → 0.35 MB |
| no QML debugger and profiler plugins (`qmltooling`, 12 libraries) | `src/app/CMakeLists.txt` (`qt_import_plugins(… EXCLUDE_BY_TYPE qmltooling)`) | −1.0 MB |
| no developer QML: the gallery (`--gallery`), the editor playground (`--editor`), the `--editor-script` scripts and self-tests | the `BRULION_DEV_QML` option in `src/app/CMakeLists.txt` (default `AUTO`: off in Release/MinSizeRel on Android and in the browser, decided at every `cmake`), stubs in `src/app/qml/stubs/` | `libbrulion` −0.77 MB |
| OpenSSL 3 (HTTPS, above) | `src/app/CMakeLists.txt` | +4.45 MB |
| PDF pages as a drawing background (2026-10-30) through the system's `PdfRenderer` (`PdfPages.kt`, JNI) instead of Qt PDF — Qt PDF for Android is `libQt6Pdf_arm64-v8a.so` 4.53 MB (2.09 MB archived), and in the Qt repository it exists only as an extension aqt does not know | `src/platform/pdfpages_android.cpp`, `android/…/PdfPages.kt` | about 0 MB (a few KB of code) |
| only the Basic style, no QtQuick.Dialogs (2026-10-30): the system's file pickers through `FilePicker` (choosing files and folders, "Save as" — the same SAF intents as before), QML imports `QtQuick.Controls.Basic` instead of `QtQuick.Controls`, which through optional imports packaged all styles (Material, Universal, Imagine, Fusion, FluentWinUI3) | `src/app/ui/filepicker.h`, a guard in `src/app/CMakeLists.txt` and `scripts/android-build.sh` (`nix/android.nix`) | −15.63 MB (25 files fewer; download −4.6 MB) |
| the C++ generated from QML compiled with `-Oz` (2026-11-07; it was half of `libbrulion`'s code at `-O3`) | the `BRULION_QML_CODE_FOR_SIZE` option in `src/app/CMakeLists.txt` (Release, MinSizeRel, RelWithDebInfo) | `libbrulion` −5.7 MB |
| `libbrulion` exports no symbols of the static libraries (`-Wl,--exclude-libs,ALL`: only Qt's loader opens it and looks up `main`; JNI goes through `RegisterNatives`) and packs its relative relocations (RELR with `--use-android-relr-tags`, read by Android 9+) | `src/app/CMakeLists.txt` (every Android build type) | −2.4 MB and −4.0 MB |
| no QML source text in the resources (`DISCARD_QML_CONTENTS`; the compiled units always match the bundled Qt) | `src/app/CMakeLists.txt` (Release, MinSizeRel) | −1.1 MB |

In total 83.15 MB → 81.02 MB (download size, compressed: 27.46 → 27.18 MB); without OpenSSL it
would be 76.5 MB. Since 2026-10-30 (without the unused styles and QtQuick.Dialogs): 96.23 →
80.60 MB, download 30.86 → 26.26 MB (docs/PERFORMANCE.md, L9). The QML compiled to C++ (0.11.2)
brought it to 105.86 MB; since 2026-11-07 (the last three rows) 92.67 MB, download 36.11 →
34.09 MB (docs/PERFORMANCE.md, L20). What remains is QtTest, QuickTest
and Qt.labs.folderlistmodel (0.84 MB) from the developer QML: `androiddeployqt` scans the whole of
`src/app`, including files the build does not compile. Debug and RelWithDebInfo builds
(measurement APKs) have `qmltooling` and the developer QML.

**R8** runs in every Gradle release build (APK and AAB with `CMAKE_BUILD_TYPE` Release,
RelWithDebInfo, MinSizeRel; Debug — without R8). `android/settings.gradle` turns it on in the
Gradle project that `androiddeployqt` assembles from Qt's template — Qt's `build.gradle` stays
untouched. Classes that C++ calls through JNI by name are kept whole: Qt's Java
(`org.qtproject.qt.**`, also looked up through reflection) and Brulion's Kotlin
(`io.github.januszbit.brulion.android.**`); AGP itself keeps the components from the manifest
(activities, receivers, `FileProvider`). Qt's C++ also calls by name a few androidx methods that
no Java code calls — the rules keep them separately: `FileProvider.getUriForFile` (a local file as
`content://` for `QDesktopServices::openUrl` and dragging) and `FontRequest` /
`FontsContractCompat` (downloading icon fonts for `QIcon::fromTheme`). Brulion does not use them
today (it opens only http/https/mailto links and has its own icons), but without these rules a new
feature would work in Debug and not in release. Names are not obfuscated (`-dontobfuscate`:
readable stack traces in logcat). `scripts/android-build.sh` and the Nix derivation check after the
build that `classes.dex` has Brulion's classes, `QtNative`, `QtActivityBase`, `QtLoader` and these
androidx methods.

## Building with Nix (flake)

The APK can also be built as a flake output — a pure derivation in the Nix sandbox (no network;
everything downloaded has a pinned hash), on an **x86_64-linux** host (the NDK and the SDK tools
exist only for x86_64; NixOS does not need nix-ld for this):

```sh
nix build .#brulion-android           # arm64-v8a: result/brulion-<version>-arm64-v8a-unsigned.apk
nix build .#brulion-android-x86_64    # for the emulator
nix build .#brulion-android-aab       # arm64-v8a: APK + AAB (bundleRelease, about 10 s more)
nix run .#brulion-android-sign -- result/brulion-*-unsigned.apk   # → ./brulion-<version>-arm64-v8a.apk
```

The output is **unsigned** (aligned with `zipalign -P 16` for 16 KB pages), and the signature is
added only by `brulion-android-sign` when it runs — the key and the password never reach
`/nix/store`. The derivation checks the APK the way `scripts/android-build.sh` does (package,
permissions, manifest components, Kotlin classes and Qt's Java after R8, `libbrulion_<abi>.so`,
OpenSSL, no `qmltooling`, 16 KB alignment).

| Component | Source (`nix/android.nix`) |
|---|---|
| Qt for Android 6.11.2 (qtbase, qtdeclarative, qtsvg) | `.7z` archives from the Qt repository (the same ones aqt downloads), separately per module and ABI, SHA-256 hashes published by Qt — `nix/android/qt-android.json`, `nix/android/qt.nix` |
| Host Qt (moc, rcc, qmlcachegen, androiddeployqt, lrelease…) | nixpkgs `qt6` — **the same version** as Qt for Android (checked at evaluation) |
| SDK (platform 36, build-tools 36.0.0), NDK r27c, CMake 3.31.6 + Ninja | nixpkgs `androidenv` (the SDK licence accepted in `flake.nix`) |
| Gradle / AGP | Gradle 9 from nixpkgs (JDK 21) instead of the wrapper from Qt's template; AGP 9.0.0, Kotlin, androidx — from `nix/android/gradle-deps.json` (mitm-cache), shared by all ABIs |
| aapt2 | from the SDK build-tools (`android.aapt2FromMavenOverride`) instead of the binary from Maven |
| OpenSSL 3 (KDAB android_openssl) | pinned commit in `nix/android/openssl.json` — the same as in `src/app/CMakeLists.txt`, passed as `BRULION_ANDROID_OPENSSL_DIR` |

Signing (`nix run .#brulion-android-sign -- FILE.apk|FILE.aab [options]`, also works on aarch64,
e.g. on the Raspberry Pi — apksigner from nixpkgs is Java):

* `--keystore` (or `BRULION_ANDROID_KEYSTORE`), by default
  `~/.local/share/brulion/brulion-release.jks` — the key from `scripts/android-new-keystore.sh`;
* password: `--password-file` (or `BRULION_ANDROID_KEYSTORE_PASSWORD_FILE`), otherwise
  `QT_ANDROID_KEYSTORE_STORE_PASS`, otherwise a file next to the keystore with the suffix
  `.password` instead of `.jks`, otherwise apksigner asks;
* `--alias` (or `BRULION_ANDROID_KEY_ALIAS` / `QT_ANDROID_KEYSTORE_ALIAS`), by default the only
  key in the keystore; `--out` — the output file (by default the current directory, the name
  without `-unsigned`).

After signing the APK is checked (`apksigner verify`, 16 KB alignment); an AAB is signed by
`jarsigner`.

**Updating the locks** — after `nix flake update` (a new Qt in nixpkgs) or after a change of the
Gradle dependencies (Qt's template, `android/`):

```sh
nix/android/update-locks.sh            # Qt for Android for the qt6 version in nixpkgs + Gradle dependencies
nix/android/update-locks.sh --qt       # only qt-android.json (a few seconds)
nix/android/update-locks.sh --gradle   # only gradle-deps.json (~3 min: builds the app and records Gradle's downloads)
nix/android/update-locks.sh --openssl <commit>   # pins KDAB android_openssl (archive hash)
```

The update script records the Gradle dependencies with `gradle.fetchDeps` (nixpkgs): it builds
the app outside the sandbox (in bubblewrap, with network access through a recording proxy) up to
`gradle assembleRelease bundleRelease` and writes the list of files with their hashes.

How it works (details in the comments of `nix/android.nix`): CMake from the SDK configures the
project with Qt for Android's toolchain file; the `brulion_make_apk` target builds the library and
runs `androiddeployqt`, which copies the Gradle template from Qt and calls `./gradlew`. The
derivation writes its own `gradlew` beforehand (Gradle from nixpkgs with the mitm-cache flags) —
`androiddeployqt` does not overwrite a file newer than the template. Qt for Android is provided as
a tree of symlinks with a writable copy of `src/` (the templates), because `androiddeployqt` edits
the files copied from them (e.g. `res/values/libs.xml`), and copies of store files are read-only.

Measured 2026-10-17 on the x86_64 laptop (16 cores):

* from scratch (empty store, 1.3 GiB of downloads, among them the NDK, SDK, Qt, Gradle, JDK):
  **250 s**;
* with the dependencies ready (a change in the sources): **~145 s** (C++ ~95 s, Gradle ~43 s,
  checks);
* the APK is **bit-for-bit reproducible**: `nix build --rebuild` and a build in an empty store gave
  the same file;
* it is the same APK as from `scripts/android-build.sh`: the same source built by the script with
  `SOURCE_DATE_EPOCH=315532800` (the nixpkgs constant) differs only in the 20 bytes of the build ID
  in `libbrulion_<abi>.so` (which depend on the build path); with another `SOURCE_DATE_EPOCH` (the
  script takes the time of the last commit by default) also in the file timestamps in the Qt
  resources (rcc). The `brulion-android-sign` signature gives byte for byte the same as the
  script's signature (apksigner from build-tools 36, scheme v3);
* the x86_64 APK signed with a throwaway key works on the emulator (Android 16, API 36): the sample
  notebook, editing a block, a reminder (a notification in the background, opening the block on a
  tap), export to ZIP, screen rotation; no crashes in logcat. The emulator with
  `-gpu swangle_indirect` draws correctly (`swiftshader_indirect` leaves black stripes on
  buttons).

## Building from source (F-Droid)

F-Droid builds every app itself, **from source only** — prebuilt binaries are not allowed in the
build. The ordinary build (`scripts/android-build.sh`, Nix) takes two things prebuilt: Qt for
Android and host Qt from The Qt Company's archives (aqt), and OpenSSL 3 from KDAB/android_openssl.
The build from source compiles both itself, with the same NDK r27c:

```sh
BRULION_ANDROID_QT=source scripts/android-build.sh                       # signed (release key or throwaway)
BRULION_ANDROID_QT=source BRULION_ANDROID_SIGN=0 scripts/android-build.sh # unsigned, as in F-Droid
# result: dist-android/brulion-<version>-arm64-v8a[-unsigned].apk
```

`BRULION_ANDROID_QT=source` runs in turn (each script can also be called on its own, `--help`):

1. **`scripts/android-openssl-from-source.sh`** — OpenSSL **3.5.9** (the LTS series, supported
   until 04.2030; the archive from the GitHub releases of `openssl/openssl`, SHA-256 pinned in the
   script) for each ABI: `libssl_3.so`, `libcrypto_3.so` (the names Qt's TLS plugin loads: the
   `_3` variant as at KDAB, but through its own `Configure` target instead of a patch), API 28,
   16 KB segments for the 64-bit ABIs, `strip`. The layout `ssl_3/<abi>/…` as at KDAB, so the
   result goes straight into `BRULION_ANDROID_OPENSSL_DIR`; plus `ssl_3/<abi>/include` for the Qt
   build. CMake then does not check the KDAB sums (`-DBRULION_ANDROID_OPENSSL_CHECK_SHA256=OFF`,
   set by the script) — the sources are checked. The script itself verifies the SONAME, the
   dependency `libssl_3.so → libcrypto_3.so`, the 16 KB alignment and the version in the library.
   About 1 min per ABI.
2. **`scripts/android-qt-from-source.sh`** — Qt **6.11.2** from the official source archives
   (`download.qt.io/official_releases/qt/6.11/6.11.2/submodules/`, the SHA-256 of each module
   pinned in the script, matching Qt's `.sha256` files), in the aqt layout
   (`$QT_ROOT/6.11.2/gcc_64` and `…/android_arm64_v8a`, by default
   `QT_ROOT=~/.cache/brulion-qt-source`):
   * **Host Qt** (the tools the cross build runs: moc, rcc, qmlcachegen, qmltyperegistrar,
     qmlimportscanner, qsb, svgtoqml, androiddeployqt, lrelease, lupdate) and Qt's translations
     (`qtbase_pl.qm`…, built into the app): qtbase, qtshadertools, qtsvg, qtdeclarative, qttools
     (only the Linguist tools), qttranslations — without OpenGL, X11, Wayland and TLS;
   * **Qt for Android**: qtbase, qtshadertools, qtsvg, qtdeclarative — exactly the modules the app
     uses (`Core Gui Network Qml Quick QuickControls2 Svg`, `GuiPrivate`; LinguistTools is only a
     host tool, Qt PDF on Android is replaced by the system's `PdfRenderer`), with the OpenSSL TLS
     plugin loading the library at run time (`-openssl-runtime`, headers from step 1) and 16 KB
     pages (`ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES`).
   * Off on both sides: SQL, printing, D-Bus, Qt Widgets (the UI is Qt Quick — which is also why
     there is no `uic`), zstd (rcc would compress resources with it that Qt for Android cannot
     read), in qtdeclarative the Qt Quick Controls styles other than Basic, QtQuick.Dialogs and the
     Qt Design Studio helpers (the app does not use them, and the script rejects an APK with other
     styles anyway — half of qtdeclarative's build time), system libraries (always the bundled
     zlib, PCRE2, libpng, libjpeg, FreeType, HarfBuzz — rcc's output does not depend on the build
     machine's zlib), ICU, glib, qmlls, qdoc (it would need libclang).
   * Each module has a stamp with the version, sources and options (chained: a new qtbase rebuilds
     the modules above it), so running again does nothing. `BRULION_QT_KEEP_BUILD=1` keeps the
     build directories (`$QT_ROOT/build`).

The main script then also checks that `libQt6Core`, `libQt6Quick`, `libssl_3.so` and
`libcrypto_3.so` in the APK have the same ELF build ID as the locally built libraries (no prebuilt
Qt or OpenSSL got into the package), and that **all** 64-bit libraries have 16 KB segments (with
Qt from aqt it only warns).

Needed: a Linux x86_64 host, a host C++ compiler (`g++`), `make`, `perl`, JDK 17+ (21), the SDK
packages `platforms;android-36`, `build-tools;36.0.0`, `cmake;3.31.6` (with Ninja) and NDK r27c —
on NixOS the scripts enter `nix shell` by themselves (gcc, JDK 21, ccache, perl, make). The
sources can also come from ready trees (`BRULION_QT_SOURCE_DIR` — e.g. `qt5.git` after
`init-repository`, `BRULION_OPENSSL_SOURCE_DIR` — a checkout of the `openssl-3.5.9` tag); this is
what F-Droid does (srclibs).

### Result (verified 2026-11-04, x86_64, NixOS)

`BRULION_ANDROID_QT=source BRULION_ANDROID_SIGN=0 scripts/android-build.sh` (SDK, NDK r27c and
CMake 3.31.6 from nixpkgs `androidenv`, as in `nix/android.nix`; for a comparison build with aqt
there was no prebuilt Qt on the machine):

* `dist-android/brulion-0.8.1-arm64-v8a-unsigned.apk`, **88.2 MB** (the same code with Qt from aqt
  and KDAB's OpenSSL: 86.3 MB). The same list of files in the APK as with aqt (49 native libraries,
  the same QML modules: QtQuick, Controls.Basic, Templates, Layouts, Shapes, Effects, Window,
  QtCore, Qt.labs.folderlistmodel, QtTest); the Qt libraries differ in size by less than 85 KB. The
  +1.9 MB difference is OpenSSL 3.5.9 instead of 3.1.8 (`libcrypto_3.so` 5.59 MB instead of
  4.03 MB, `libssl_3.so` 1.03 MB instead of 0.63 MB).
* The script's checks: `aapt2 dump badging` (package, `versionCode` 8001, minSdk 28, targetSdk 36,
  13 permissions), manifest components, Kotlin classes and Qt's Java after R8,
  `zipalign -c -P 16 4`, **all 49** 64-bit libraries with 16 KB segments,
  `libssl_3.so`/`libcrypto_3.so` (OpenSSL 3.5.9 29 Sep 2026), the build IDs of
  `libQt6Core`/`libQt6Quick` and the OpenSSL bytes matching the local builds, the APK unsigned.
* Qt from source: host Qt (qtbase 1239 Ninja steps, qtdeclarative 2437) and Qt for Android —
  about 30 min with 6 jobs on a 16-core x86_64 (measured in the F-Droid container, together with
  OpenSSL and the APK); on a heavily loaded machine several hours. ~13 GB on disk meanwhile (the
  build directories are deleted afterwards), ~400 MB installed. Later runs: seconds (stamps), the
  APK ~5 min.
* Two consecutive APK builds (the same Qt, Gradle from scratch) — **an identical file**. Note: a
  reused build directory does not rebuild the rcc resources when only `SOURCE_DATE_EPOCH` changed
  (a new commit) — APKs for comparisons and releases always come from a clean directory (as in CI
  and F-Droid).
* **Not checked on a device or an emulator** — that needs Qt for x86_64 from source
  (`BRULION_ANDROID_ABIS=x86_64`) and `scripts/android-emulator-test.sh`, or installing the
  (signed) APK on a phone.

### The F-Droid recipe

A draft recipe for [fdroiddata](https://gitlab.com/fdroid/fdroiddata):
`packaging/fdroid/io.github.januszbit.brulion.yml` (checked by
`tests/scripts/tst_fdroid_recipe.sh`, in ctest `tst_scripts_fdroid_recipe`: the versions of the
NDK, the SDK packages, Qt and OpenSSL match the scripts, the `versionCode` follows the formula in
`src/app/CMakeLists.txt`, the APK path, the variables set). The main decisions:

* **srclibs instead of downloading in `prebuild`** — F-Droid prefers sources from git repositories
  described in `fdroiddata/srclibs/`: `Qt5@v6.11.2` (the `qt5.git` super-repository;
  `init-repository --module-subset=…` fetches only the six modules needed) and
  `OpenSSL@openssl-3.5.9`. Both srclibs already exist in fdroiddata; the recipe
  `net.sourceforge.kid3`, for example, builds Qt 6 the same way. Without srclibs the scripts
  download the archives and check their SHA-256.
* `sudo`: only `g++` and `make` (the buildserver image is Debian 13 with JDK 21; perl is there);
  CMake 3.31.6 and Ninja from the SDK package (`sdkmanager` in `prebuild`).
* `build`: `scripts/android-build.sh` with `BRULION_ANDROID_QT=source`, `BRULION_ANDROID_SIGN=0`
  (F-Droid signs itself) and `BRULION_UPDATE_CHECK=OFF` (no asking GitHub for new versions —
  F-Droid provides the updates; the CMake option and passing it through the script come with a
  separate change), `SOURCE_DATE_EPOCH` = the time of the tag's commit (as the script's default).
* `output: dist-android/brulion-$$VERSION$$-arm64-v8a-unsigned.apk`, `ndk: r27c`,
  `timeout: 14400` (Qt twice — host and Android — exceeds the buildserver's default 2 h with fewer
  cores).
* `scandelete: tests/core/io/fixtures/trilium, tests/data/drawing/lzstring` — ZIP archives from the
  import tests and LZ-String test data, which the F-Droid scanner reports as binaries (checked in
  the container); the Android build does not use them.
* The file is exactly as `fdroid rewritemeta` writes it (no comments — the description is in
  `packaging/fdroid/README.md`, categories in alphabetical order); `fdroid lint` passes.
* `AutoUpdateMode: Version`, `UpdateCheckMode: Tags ^v[0-9]+\.[0-9]+\.[0-9]+$` — the F-Droid bot
  adds a build for every new `vX.Y.Z` tag itself (without `-rc`).
* Categories: Note, Calendar & Agenda, Task, Writing. F-Droid takes the description, screenshots
  and icon from `fastlane/metadata/android/` in the repository.
* **The first build is v0.9.1** (`versionCode` 9001, `commit: v0.9.1`) — the first published
  release with these scripts (v0.8.1 does not have them, v0.9.0 was not published). Later versions
  are added by the F-Droid bot (`AutoUpdateMode`); in this repository the recipe changes only
  together with the way of building.

Checking locally in the F-Droid buildserver container (like the "fdroid build" job in fdroiddata's
CI), podman or docker:

```sh
mkdir -p ~/fdroid && cd ~/fdroid
git clone --depth 1 https://gitlab.com/fdroid/fdroidserver.git
git clone --depth 1 https://gitlab.com/fdroid/fdroiddata.git
cp <brulion>/packaging/fdroid/io.github.januszbit.brulion.yml fdroiddata/metadata/
podman run --rm -it -u root --entrypoint /bin/bash \
  -v "$PWD/fdroiddata:/build:z" -v "$PWD/fdroidserver:/home/vagrant/fdroidserver:Z" \
  registry.gitlab.com/fdroid/fdroidserver:buildserver
# in the container:
source /etc/profile.d/bsenv.sh && apt-get update && apt-get install -y fdroidserver sudo
cd /build && export PATH=/home/vagrant/fdroidserver:$PATH
# (fetchsrclibs is a plugin from fdroidserver's examples/ — as in fdroiddata's CI)
export PYTHONPATH=/home/vagrant/fdroidserver:/home/vagrant/fdroidserver/examples
fdroid lint io.github.januszbit.brulion && fdroid readmeta
fdroid fetchsrclibs io.github.januszbit.brulion --verbose
fdroid build --verbose --test --on-server --no-tarball io.github.januszbit.brulion
```

(A change in the build before it is released — a test on a local branch: in a copy of the recipe,
`Repo:` pointing to the local repository, `commit:` to the commit hash.)

Verified 2026-11-04 exactly like this (the image `registry.gitlab.com/fdroid/fdroidserver:buildserver`
— Debian 13, JDK 21 — and the steps of the "fdroid build" job from fdroiddata's CI: `fetchsrclibs`,
then `fdroid build --test --on-server --no-tarball`, with a copy of the recipe pointing to the
local repository): `fdroid lint` and `fdroid readmeta` without remarks, `fdroid rewritemeta`
changes nothing, the srclibs `Qt5@v6.11.2` (+ `init-repository`, about 2 min) and
`OpenSSL@openssl-3.5.9` downloaded, the F-Droid scanner without errors (after `scandelete`), the
build of host Qt and Qt for Android and the APK — **"Successfully built
io.github.januszbit.brulion:8001"**, output `tmp/io.github.januszbit.brulion_8001.apk` (88.2 MB,
the same `android-build.sh` checks as locally). The `build` step (host Qt, Qt for Android,
OpenSSL, APK) with 6 jobs: 29–45 min. The first attempts found things that were fixed in the
scripts and the recipe: the LZ-String test data as "binaries" for the scanner, a path with `..` in
`QT_ROOT` (Qt then did not find the host's `Qt6LinguistTools`) and byte differences between builds
(below).

Again on 2026-11-05 on the **v0.9.1** release — the recipe from `packaging/fdroid/` unchanged
(`commit: v0.9.1` fetched from GitHub), fdroidserver and fdroiddata from the main branch
(fdroidserver from 27.10.2026): `fdroid lint`, `readmeta` and `rewritemeta` without remarks, the
scanner without errors (after `scandelete`), **"Successfully built io.github.januszbit.brulion:9001
from d497fdb8"**, the `android-build.sh` checks as above (49 libraries, 16 KB, OpenSSL 3.5.9, Qt
and OpenSSL from source), APK 91.9 MB, SHA-256
`6a701443aaab7deadb0e209b8d522c7f1a12d774e7e06203139c1c841f25fca4`. All of it, including
downloading the NDK and the srclibs: about 25 min on 16 cores.

To be checked by fdroiddata at submission: the build time on their buildserver (fewer cores than
here; `timeout: 14400` = 4 h) and whether the reviewers accept the `Qt5` srclib with
`init-repository` (network in `prebuild`; `net.sourceforge.kid3` already does this).

### Signing: a decision for the owner

F-Droid can publish Brulion in two ways. **One has to be chosen before the submission** (changing
it later means F-Droid users have to uninstall the app):

**(a) F-Droid signs with its own key** — the recipe as in the draft, nothing more.
* The simplest; the F-Droid build does not have to be identical to the APK from GitHub.
* The APK from F-Droid and the APK from the GitHub releases have **different signatures**: one
  cannot be updated with the other — switching requires uninstalling (this deletes the app's
  private data; notes in the Syncthing folder on shared storage stay, the settings and a notebook
  in the private directory do not).
* The message "Brulion X.Y.Z is available" disappears in the F-Droid build
  (`BRULION_UPDATE_CHECK=OFF`).

**(b) Reproducible build and the GitHub signature** (`Binaries:` + `AllowedAPKSigningKeys:`) —
F-Droid builds the APK itself, compares it bit for bit with the APK from the GitHub release
(without the signature) and, when they are identical, publishes **the APK with the owner's
signature**. One app for both sources; cross-updates work. The recipe would get:

```yaml
Binaries: https://github.com/janusz-bit/brulion/releases/download/v%v/brulion-%v-arm64-v8a.apk
AllowedAPKSigningKeys: <SHA-256 of the release key's certificate, lower case, no colons>
```

but this requires **the GitHub release to be built the same way as in F-Droid**, that is also
with Qt and OpenSSL from source, and without the update check (the APK has to be identical, and
F-Droid will not accept a built-in update mechanism next to its own):

* `.github/workflows/android.yml`: `BRULION_ANDROID_QT=source` and `BRULION_UPDATE_CHECK=OFF` in
  the `build` step; instead of the aqt caches — `actions/cache` for
  `~/.cache/brulion-qt-source/6.11.2` (key with the hash of `scripts/android-qt-from-source.sh`)
  and `~/.cache/brulion-openssl`; the first build ~1 h on the runner (4 cores), then minutes.
  `SOURCE_DATE_EPOCH` from the tag's commit (already the default). JDK 21, NDK r27c, build-tools
  36.0.0 — the same as on the buildserver (the runner has the Temurin JDK, the buildserver
  Debian's OpenJDK: with the same 21.0.x version `javac` produces the same classes, but only the
  first F-Droid comparison will confirm it — it can be done earlier locally, `fdroid build` in the
  container with `Binaries:` in the recipe).
* `release.yml`: no changes to the steps; `android-build.sh` keeps signing the APK with the
  release key (apksigner, scheme v2/v3 — F-Droid copies the signature from this file onto its
  build).
* Every new tag: F-Droid builds and compares; a difference of even one byte = the version does not
  appear (F-Droid shows the diff in the build log), so environment changes (a new JDK, another
  Gradle) then have to be matched on both sides.
* Users of the app from GitHub lose the new-version message (or it stays only in a build for other
  stores — then the GitHub APK ≠ the F-Droid APK and we are back at (a)).

**Reproducibility — verified 2026-11-04:** the APK from the F-Droid buildserver container (Debian
13, Debian's OpenJDK 21, NDK r27c downloaded by `fdroid`, Qt from the `Qt5` srclib = `qt5.git`
+ `init-repository`, OpenSSL from the git srclib, paths `/home/vagrant/build/…`) and the APK built
with `BRULION_ANDROID_QT=source BRULION_ANDROID_SIGN=0 scripts/android-build.sh` in a fresh clone
on NixOS (SDK/NDK from nixpkgs, JDK 21 from nixpkgs, Qt and OpenSSL from the source archives,
other paths) are **identical bit for bit**: `brulion-0.8.1-arm64-v8a-unsigned.apk`, SHA-256
`82bf9d9d563a719e94b76d1ab8ea41c9301efc0c00b71ddf714c2131e9fc5ed7` (commit `3b3c9b5e`). Even
OpenSSL alone comes out identical from both (`libssl_3.so` `88d316f6…`, `libcrypto_3.so`
`3b472705…`). On the way, four sources of differences had to be removed (all fixed in the
scripts):

1. the paths of the Qt sources and of the NDK's libc++ headers in `.rodata` (`__FILE__`) — the NDK
   toolchain file ignores `CFLAGS`/`CXXFLAGS` from the environment, so `-ffile-prefix-map` goes in
   as `CMAKE_C(XX)_FLAGS`, each path also as its real path (SDK symlinks);
2. the file timestamps in Qt's own rcc resources (the checkout or unpacking time) — Qt for Android
   built with a fixed `SOURCE_DATE_EPOCH=315532800`;
3. the time in the gzip header of the MIME database in `libQt6Core` —
   `QT_AVOID_CMAKE_ARCHIVING_API=ON` (`gzip -n`);
4. the build ID of `libbrulion` (which depends on the debug information) — the paths of the
   checkout, the build directory, the NDK and Qt mapped in `CMakeLists.txt` (Android).

Conditions for reproducibility: the same commit (and `SOURCE_DATE_EPOCH` = that commit's time, the
default), a **clean** build directory in the default place (`build-android` in the checkout —
relative paths to headers in the moc files end up in the debug information, so a build directory
outside the checkout gives another build ID), the same versions: NDK r27c, build-tools 36.0.0,
CMake 3.31.6, JDK 21 (checked with 21.0.12 on both sides), the Gradle dependencies from Qt's
template.

**Recommendation.** Technically (b) is ready — the build is reproducible bit for bit between the
F-Droid buildserver and another system (above). The choice depends on one question: **may the APK
from GitHub lose the new-version message?** If yes — **(b)**: one app, one signature, users can
move between GitHub, IzzyOnDroid and F-Droid without uninstalling; the cost is a slower release
build (Qt from source, with the cache ~minutes) and keeping the CI and F-Droid environments from
drifting apart. If the message in the GitHub APK has to stay — **(a)**: F-Droid signs itself,
nothing changes in the releases, and people on F-Droid and on GitHub have two "different" apps
(changing the source = uninstalling). The decision has to be made **before the first release in
F-Droid**: switching from (a) to (b) later changes the signature of the F-Droid version and forces
its users to uninstall.

### Submission (done by the owner)

None of the below has been sent. The order:

1. Ready: a release with these scripts (v0.9.1) and a recipe pointing to it, built in the
   buildserver container (above). What remains is the signing decision (above) — with (b) the
   recipe gets `Binaries:` and `AllowedAPKSigningKeys:`, and the GitHub releases get the build
   from source.
2. **A merge request to fdroiddata** (the fastest way, no RFP): fork
   `gitlab.com/fdroid/fdroiddata`, branch `io.github.januszbit.brulion`, the file
   `metadata/io.github.januszbit.brulion.yml` from the draft, `fdroid lint` and
   `fdroid rewritemeta` locally (formatting), the MR following the "App inclusion" template;
   fdroiddata's CI builds the app. The MR description:

   > **New app: Brulion** (io.github.januszbit.brulion)
   >
   > Offline-first Markdown notes: a block outliner with daily journals, a calendar,
   > reminders and alarms, calculations, and Syncthing/Nextcloud sync. Plain `.md` files in a
   > folder (compatible with Obsidian/Logseq vaults). C++/Qt 6 (Qt Quick) with a small Kotlin
   > part; Qt 6.11.2 and OpenSSL 3.5 are built from source (srclibs Qt5, OpenSSL) by the
   > project's scripts. AGPL-3.0-or-later.
   >
   > * [x] The app complies with the inclusion criteria
   > * [x] The original app author has been notified (I am the author) and supports the inclusion
   > * [x] All related fdroiddata and RFP issues have been referenced in this merge request
   > * [x] Builds with `fdroid build` and all pipelines pass
   >
   > Permissions: `MANAGE_EXTERNAL_STORAGE` (the notes folder synchronised by Syncthing lives in
   > shared storage), `SCHEDULE_EXACT_ALARM`/`USE_EXACT_ALARM` (reminders and alarms),
   > `POST_NOTIFICATIONS`, `RECEIVE_BOOT_COMPLETED`, `USE_FULL_SCREEN_INTENT` (alarm screen),
   > `INTERNET` (Nextcloud/WebDAV sync, images from the web — both opt-in). No trackers, no
   > Google Play Services. The in-app update check is disabled in this build
   > (`BRULION_UPDATE_CHECK=OFF`).

3. Instead of an MR a **Request For Packaging** can be filed (`gitlab.com/fdroid/rfp`, "New issue"
   → the "Request for packaging" template) — slower, a volunteer then writes the recipe; the same
   text as above plus a link to the draft recipe.
4. **IzzyOnDroid** (a repository with the APKs from the GitHub releases, the owner's signature,
   fast updates, independent of the (a)/(b) decision): an issue in `gitlab.com/IzzyOnDroid/repo`
   (the "App inclusion request" template). Requirements: an APK in the GitHub releases (there is
   one: `brulion-<version>-arm64-v8a.apk`), fastlane metadata in the repository; the APK file is
   ~90 MB (v0.9.1: 90.0 MB; native libraries stored uncompressed, aligned to 16 KB), more than
   IzzyOnDroid's default size limit — ask for an exception in the request.
   Text:

   > **App inclusion request: Brulion**
   >
   > * Source: https://github.com/janusz-bit/brulion (AGPL-3.0-or-later)
   > * APK: GitHub releases, `brulion-<version>-arm64-v8a.apk`, signed with the release key
   > * Package: `io.github.januszbit.brulion`
   > * Offline-first Markdown notes (outliner, journals, calendar, reminders/alarms, Syncthing and
   >   Nextcloud sync). No trackers, no proprietary libraries. Fastlane metadata in
   >   `fastlane/metadata/android`.
   > * The app has its own update notice (GitHub releases, can be turned off in the settings).
   > * The APK is ~90 MB (Qt's native libraries are stored uncompressed for 16 KB page alignment);
   >   please allow an exception to the size limit.
   > * I am the author.

## For developers

Driving the running app by AI agents (`brulion --remote-control`, `brulion-cli app …`,
docs/CLI.md "Remote control") does not exist on Android. The app on a phone or emulator is driven
with Android's tools: a debuggable APK accepts command-line options through
`adb shell am start -n io.github.januszbit.brulion/.android.BrulionActivity --es
applicationArguments '…'` (docs/UI.md, the `--scroll-probe` line), `adb shell input tap|swipe|text`
taps, swipes and types, and `adb exec-out screencap -p > ekran.png` takes a screenshot.

### Files

| Path | Contents |
|---|---|
| `android/AndroidManifest.xml` | manifest based on the Qt 6.11.2 template (`androiddeployqt` fills in the `%%INSERT_…%%` markers: package, version, name, library and the permissions of the Qt modules): permissions, `BrulionActivity`, receivers, `AlarmActivity`, `ShareActivity` (filters `ACTION_SEND`/`ACTION_SEND_MULTIPLE` for `text/*`), icons |
| `android/res/` | strings (`values` — English, the default; `values-pl`), the notification icon `ic_stat_brulion`, the app icons (`mipmap-*` from the logo, the adaptive `mipmap-anydpi` with the `#283045` background and a monochrome layer), the alarm screen theme; widgets: layouts `layout/widget_*.xml` (also the previews for the widget picker), `xml/widget_*_info.xml`, colours `widget_colors.xml` (`values` light, `values-night` dark), the corner radius (`values-v31`: the system's), the theme of the quick add window |
| `android/src/io/github/januszbit/brulion/android/` | Kotlin: `BrulionAndroid` (JNI facade), `BrulionActivity` (`QtActivity` + taps/permissions/ringtone choice) + `StartupRelaunch` (recreated before Qt started: restart of the app), `ImeInsets` (on-screen keyboard: how much of the Qt view it covers), `AlarmScheduler` + `WallClock`, `Reminder` + `ReminderStore`, `ReminderNotifier`, `ReminderSounds` (sounds: `SoundChoice`, channel versions `SoundChannel`, `SoundStore`, sample, ringtone picker), `RingTimeout`, `Receivers` (fire, buttons, boot/time change), `DoneQueue` (the "Done" queue for C++), `AlarmActivity`, `Documents` (chosen documents and document trees for the import), `ShareActivity` + `ShareIntent` ("Share → Brulion": receiving, copies in `files/share-in/`, JSON for C++), widgets: `Widgets` (`TasksWidget`, `CalendarWidget`, `NotesWidget`), `WidgetViews` + `WidgetListService` (RemoteViews and list rows), `WidgetSnapshot` (the snapshot, moving tasks to the new day), `WidgetStore` + `WidgetUpdater` + `WidgetReceiver` (storing, refreshing, midnight, month buttons), `WidgetDates` (the app's day and month names), `QuickAddActivity` + `QuickAdd` + `JournalAppend` (quick add, writing to the journal), `WidgetInbox` (the queue of entries for C++), background check of the notes: `BackgroundCheck` (target, job, decision on the result), `BackgroundCheckJob` (JobScheduler job in the main process), `BackgroundCheckService` (`QtService` in the `:background` process) |
| `android/tests/` | Kotlin unit tests (JVM) + a Gradle init script that adds them |
| `android/lint.xml` | Android lint configuration (exceptions for `res/values/libs.xml` generated by Qt) |
| `android/settings.gradle`, `android/proguard-rules.pro` | R8 in release builds and its rules (classes called through JNI) — [APK size](#apk-size) |
| `android/res/xml/network_security_config.xml` | trusted HTTPS certificate authorities: the system's and those the user installed — [HTTPS and OpenSSL](#https-and-openssl) |
| `android/assets/licenses/` | licence texts of the bundled libraries (OpenSSL, Apache-2.0) |
| `src/platform/android/androidbridge.{h,cpp}` | C++: `brulion::platform::AndroidBridge` (compiled only when `ANDROID`) |
| `src/app/background/` | C++ of the background check of the notes: `backgroundcheck.{h,cpp}` (target, reminders, one check — built everywhere, tests in `tests/app/background`), `backgroundentry_android.cpp` (`main()` of the `:background` process) — [Checking notes in the background](#checking-notes-in-the-background) |
| `src/platform/android/selftest/` | `brulion_android_selftest` — an APK that tests the bridge on a device/emulator (only with `-DBRULION_ANDROID_SELFTEST=ON`) |
| `scripts/android-build.sh`, `scripts/android-new-keystore.sh` | building + signing, the release key |
| `fastlane/metadata/android/`, `scripts/check-fastlane-metadata.sh` | descriptions, icon and screenshots for F-Droid and IzzyOnDroid, and their check — [F-Droid and IzzyOnDroid](#f-droid-and-izzyondroid) |
| `scripts/android-emulator-test.sh` | end-to-end test on the emulator (Android 16, x86_64); `--keyboard`: the editing toolbar on the keyboard; `--selection`: selecting and copying with a finger; `--folders`: folders, block links, the parts of a link to a page in a folder, and tags with a finger; `--share`: "Share → Brulion"; `--widgets`: widgets and quick add; `--done`: "Done" in a notification (on screen and while the app is closed); `--background`: background check of the notes (a reminder added and deleted while the app is closed, the job run at once, the new reminder's alarm rings without opening Brulion); `--drawing`: text on a drawing from the on-screen keyboard; `--stylus`: stylus pressure, its eraser and side button in the drawing editor; `--web-pictures`: pictures from the internet; `--pickers`: the system file pickers (importing files and a folder, the export's "Save"); `--pdf`: PDF thumbnails, the PDF viewer, pinching, search from Android 15, "Open in another app", "Draw on this page", memory; `--relaunch`: activity recreated during startup (a restart instead of an empty window); `--web-keyboard dist-wasm`: the browser version in Chrome, Brave and Firefox — the toolbar on the keyboard (docs/WEB.md "On-screen keyboard") |
| `nix/android.nix`, `nix/android/` | APK from the flake: derivation, Qt for Android (`qt.nix`), signing (`sign.nix`), locks (`*.json`) and `update-locks.sh` |

`android/` is `QT_ANDROID_PACKAGE_SOURCE_DIR`: `androiddeployqt` copies it over the Qt template in
`<build>/src/app/android-build/`. We do not override Qt's `build.gradle` — the 6.11 template uses
AGP 9 with built-in Kotlin support (`kotlin.srcDirs` covers `src/`), so the `.kt` files in
`android/src` compile without extra configuration; `androidx.core` (NotificationCompat) is already
in the template too. Release builds are shrunk by R8 (`android/settings.gradle`, [APK
size](#apk-size)): Kotlin classes called from C++ must live in the package
`io.github.januszbit.brulion.android`, which `android/proguard-rules.pro` keeps whole.

### Activity recreated during startup

Configuration changes the app cannot handle itself (`CONFIG_ASSETS_PATHS`: system overlays, e.g. a
change of the system theme's colours, or overlays a freshly started emulator applies for a few
seconds) are handled by Android by recreating the activity. Qt 6.11's `QtActivity` starts the
native app only at the first layout of the first activity's view. When that activity disappears
earlier, the new one finds the Qt libraries already loaded
(`QtLoader.LoadingResult.AlreadyLoaded`) and starts nothing: no Qt layout, an empty window
forever, `finishDrawing of relaunch: Window{… BrulionActivity}` in logcat and no `brulion.*`
entries. (When the activity is recreated after Qt has started, Qt restarts the app itself:
`QtActivityBase.restartApplication` — with the bare launcher intent, so the extras of the original
intent, e.g. the note from a notification or the arguments of a debuggable APK, are lost.)

`BrulionActivity.onCreate` recognises this case after `super.onCreate`: an earlier
`BrulionActivity` of this process already reached `onCreate`, this one has no view in
`android.R.id.content` (the Qt layout) and is not finishing (Qt finishes it when the libraries
could not be loaded). Then — like Qt — it starts the app again: the same intent (extras: the
arguments of a debuggable APK) with `FLAG_ACTIVITY_NEW_TASK | FLAG_ACTIVITY_CLEAR_TASK`, then
`finish()` and the end of the process. At most twice in a row (a counter in the intent extra
`io.github.januszbit.brulion.STARTUP_RESTARTS`), so a cause that keeps coming back does not loop
the startup. In logcat: `BrulionActivity: recreated before Qt started (…): restart N`.

What only memory holds dies with the process (`BrulionAndroid.keepForRestart`): the first activity
has already taken the note of a tapped notification out of the intent (`activationFrom` removes
the extras, so that a tap is not handled twice), and Android hands the recreated activity the same,
already trimmed intent object — so the path and line go back into the restart intent (the alarm is
already silenced, `NOTIFICATION_ID` does not come back). A share from another app (`ShareActivity`,
only in memory — never an intent extra another app could forge) goes to the private file
`no_backup/startup-restart-share.json`; the first activity of the restarted process (counter > 0)
loads it back, any other first activity only deletes the leftover. The decision and these steps
are plain JVM code in `StartupRelaunch` (`needsRestart`, `carriedNote`,
`saveShare`/`takeSavedShare`; test `StartupRelaunchTest`).

### JNI bridge (C++ ↔ Kotlin)

`AndroidBridge::instance()` (GUI thread, after `QGuiApplication` is created) registers the native
methods of the class `io.github.januszbit.brulion.android.BrulionAndroid` and calls its
`attach()`. The app should do this in `main()` right after creating `QGuiApplication`, before
loading QML (so that the tap that started the app reaches the UI); as long as nothing calls
`AndroidBridge`, the linker leaves it out of `libbrulion_<abi>.so`.

| C++ (`AndroidBridge`) | Kotlin (`BrulionAndroid`, `@JvmStatic`) |
|---|---|
| `bool hasAllFilesAccess()` / `void requestAllFilesAccess()` | `hasAllFilesAccess(Context): Boolean` / `requestAllFilesAccess(Context)` |
| `bool notificationPermission()` / `bool requestNotificationPermission()` | `notificationPermission(Context)` / `requestNotificationPermission(Context): Boolean` |
| `bool canScheduleExact()` / `void requestExactAlarms()` | `canScheduleExact(Context)` / `requestExactAlarms(Context)` |
| `bool canUseFullScreenIntent()` / `void requestFullScreenIntent()` | `canUseFullScreenIntent(Context)` / `requestFullScreenIntent(Context)` |
| `ScheduleResult scheduleReminders(const QJsonArray &)` | `scheduleReminders(Context, String): String` — `{"scheduled":n,"exact":b}` or `{"error":…}` |
| `Activation takeActivation()` | `takeActivation(Context): String` — `{"path":…,"line":n}` or `{}` |
| `QByteArray takeShare()` | `takeShare(Context): String` — the share JSON (`notebook/share.h`) or `{}` |
| `QJsonArray takeDoneRequests(vault)` | `takeDoneRequests(Context, String): String` — the "Done" queue (`DoneQueue.kt`): `[{key, local, path, line, title, day, vault}, …]` of notebook `vault` (and entries without a notebook), entries handed out leave the queue, the rest wait; an empty `vault` — all |
| `static DocumentsResult documentInfo(const QString &uri)` | `documentInfo(Context, String): String` — `{uri, name, dir, size, virtual}` or `{"error":…}` |
| `static DocumentsResult listDocuments(const QString &folderUri)` | `listDocuments(Context, String): String` — `{"documents":[…]}` or `{"error":…}` |
| `QJsonObject setReminderSounds(const QJsonObject &)` | `setReminderSounds(Context, String): String` — `{"reminderChannel", "alarmChannel"}` or `{"error":…}` |
| `bool pickSound(int kind, const QJsonObject &current)` | `pickSound(Context, Int, String): Boolean` — opens the ringtone picker (`startActivityForResult`) |
| `bool previewSound(const QJsonObject &)` | `previewSound(Context, String): Boolean` — a ~3 s sample |
| `void openSoundSettings(int kind)` | `openSoundSettings(Context, Int)` — `ACTION_CHANNEL_NOTIFICATION_SETTINGS` |
| signal `activated(QString path, int line)` | native `nativeActivated(String, Int)` |
| signal `shared(QByteArray json)` | native `nativeShared(String)` (from `ShareActivity` through `deliverShare`) |
| signal `permissionsChanged()` | native `nativePermissionsChanged()` + the app returning to the front |
| signal `doneRequestsQueued()` | native `nativeDoneQueued()` (from `ReminderActions.done`) + return to the front + once after startup |
| `QJsonObject setBackgroundCheck(const QJsonObject &)` | `setBackgroundCheck(Context, String): String` — the background check target (`background::Target`, `{}` = off) stored, the JobScheduler job set to its period: `{"interval": min}` or `{"error":…}` |
| `QJsonObject backgroundCheckStatus()` | `backgroundCheckStatus(Context): String` — the last check `{at, ok, applied, verdict, reminders, sync, error, …}` or `{}` |
| `static QJsonObject backgroundCheckTarget()` (`:background` process) | `backgroundCheckTarget(Context): String` — the target to check or `{}` |
| `static void backgroundCheckFinished(const QJsonObject &)` (`:background` process) | `backgroundCheckFinished(Context, String)` — the result of `background::Check` to the job in the main process (`BackgroundCheckService` → `Messenger`) |
| `int updateWidgets(const QJsonObject &)` | `updateWidgets(Context, String): String` — the widget snapshot (`src/app/widgets/widgetsnapshot.h`) stored, the widgets refreshed: `{"widgets": n}` (how many are placed) or `{"error":…}` (the previous snapshot stays) |
| `QJsonArray takeWidgetActions(vault)` | `takeWidgetActions(Context, String): String` — the quick add queue (`WidgetInbox.kt`): `[{id, kind, text, date, time, alarm, vault, added}, …]` of notebook `vault` (and entries without a notebook), entries handed out leave the queue |
| `void requeueWidgetActions(const QJsonArray &)` | `requeueWidgetActions(Context, String)` — entries the app could not write either go back to the queue (the same JSON; no signal) |
| signal `widgetActionsQueued()` | native `nativeWidgetQueued()` (from `QuickAdd`) + return to the front + once after startup |
| signal `widgetNoteWritten(QString path)` | native `nativeWidgetWrote(String)` — quick add appended to the note itself (absolute path): the app hands over its set of reminders once more (`WorkspaceReminders::resendUpcoming`), the temporary one gives way; the notebook reloads after the return to the front anyway |
| signal `soundPicked(int kind, QJsonObject choice)` | native `nativeSoundPicked(Int, String)` (from `BrulionActivity.onActivityResult`) |
| signal `imeChanged(int overlap, bool shown, bool animating)` / `void requestImeState()` | native `nativeImeChanged(Int, Boolean, Boolean)` (from `ImeInsets`, physical pixels, every frame of the slide on Android 11+) / `requestImeState()` — report once more |
| signal `systemGesturesChanged(int left, int right)` | native `nativeGestureInsetsChanged(Int, Int)` (from `ImeInsets`, physical pixels of the system gesture zones at the edges of the Qt view; reported once more also through `requestImeState()`) |
| `void setSystemBarsLight(bool light)` | `setSystemBarsLight(Boolean)` — system bar icons dark over a light window (Android 11+, window under the bars) |
| `platform::PdfReader::open(file)` (`src/platform/pdfpages_android.cpp`, not `AndroidBridge`; any thread) | `pdfOpen(String): String` — `{"session", "pages", "size": [w, h] (first page, points), "locked", "error"}` (`PdfPages.kt`: a session of the system's `android.graphics.pdf.PdfRenderer`; `session` 0 — not opened) |
| `PdfReader::pageSize(page)` (as above, 64 pages at a time) | `pdfPageSizes(Int, Int, Int): String` — `[[w, h], …]` |
| `PdfReader::render(page, full, clip)` (as above) | `pdfRenderInto(Int, Int, Int, Int, Int, Int, Int, Int, ByteBuffer): Boolean` — the page scaled to `full`, its part `clip` on a white background straight into the RGBA_8888 pixels of the C++ image |
| `platform::pdfTextAvailable()` | `pdfTextAvailable(): Boolean` — Android 15+ and PdfRenderer's text methods are in the system |
| `PdfReader::pageText(page)` / `find(page, query)` | `pdfPageText(Int, Int): String?` / `pdfSearch(Int, Int, Array<String>): String` — `[{"start", "rects": [[l, t, r, b], …]}, …]` of each given spelling |
| `~PdfReader` | `pdfClose(Int)` |
| `AndroidBridge::viewFile(path, mime)` (static) | `viewFile(Context, String, String): Int` — `OpenWith.kt`: 0 started, 1 no app, 2 error, 3 copying (memory card) |
| `AndroidBridge::printPdf(path, jobName)` (static) | `printPdf(Context, String, String): Boolean` — `PdfPrint.kt`: a PDF from the cache in the system print dialog (`PrintManager`, a `PrintDocumentAdapter` copying the file, no WebView); the file is deleted when the job ends |

* A `scheduleReminders` element: `{id: int, title, body, at: ms since the epoch, alarm: bool,
  ringMinutes: int (alarms, 1–60, 10 by default), path: the note's path in the notebook, line: int,
  key: the occurrence's id in Brulion, local: its wall-clock time "yyyy-MM-ddTHH:mm", task: bool}`.
  `key` and `local` (unchanged by a snooze, never converted through UTC) point to the occurrence
  that "Done" should complete; the button is there only with `task: true` (a reminder on an open
  task). Reminders stored by an older version (without these fields) do not have it. `id` must be
  stable between calls (setting the same `id` again replaces the alarm). A call replaces the whole
  set; snoozes from a notification stay until the app sends the same `id`. Malformed JSON does not
  clear the set alarms.
* Sounds (`ReminderSounds.kt`, C++: `src/app/notify/androidpayload.h`): `kind` 0 = reminders,
  1 = alarms; a choice is `{mode: "default" | "custom" | "vibrate" | "silent", uri, title}`
  (`uri` — the ringtone from the picker, `content://…`), `setReminderSounds` gets
  `{reminder: {…}, alarm: {…}}` at app startup and after every change in the settings. The picker
  returns the choice through `nativeSoundPicked`, or `{"error": …}` (no picker); cancelling sends
  nothing. The picker's "Default" entry gives `mode: "default"`.
* `documentInfo` / `listDocuments` (import): static, called from a worker thread (JNI attaches the
  thread itself), without a bridge instance. A tree address (`…/tree/<id>`, the result of
  `ACTION_OPEN_DOCUMENT_TREE`) is turned into the document of its root; the children of a folder
  give `…/tree/<id>/document/<child id>` addresses, which `QFile` opens. `bridgeFailed` in the
  result (missing method, exception) = use Qt's file engine.
* The methods take a `Context` (what `QNativeInterface::QAndroidApplication::context()` returns,
  usually the activity); the permission dialogs use the current `BrulionActivity`. Requests
  (`request*`) only post the dialog/settings screen to Android's main thread and return at once —
  the result is checked again after `permissionsChanged()`.
* **Tapping a notification** — the chosen approach: a JNI callback + pickup at startup:
  * the app is running: `BrulionActivity.onNewIntent` (`singleTop` mode) → `nativeActivated` →
    the `activated` signal on the Qt thread (queued connection);
  * cold start from a notification: `BrulionActivity.onCreate` keeps the target, because C++ is
    not listening yet; the `AndroidBridge` constructor picks it up after `attach()` through
    `takeActivation()` and emits `activated` in the first turn of the event loop (after the UI is
    connected);
  * safeguard: every time the app returns to the front (`ApplicationActive`) the bridge checks
    `takeActivation()` once more. Access to the target is guarded by a lock on the Kotlin side, so
    no tap is lost or delivered twice; intents replayed from "Recents" are skipped.
* **A share** from another app takes the same path: `ShareActivity` calls
  `BrulionAndroid.deliverShare` (the same process) — when C++ is listening, `nativeShared` → the
  `shared` signal → `AppController::receiveShareJson`; otherwise the JSON waits for `takeShare()`
  (the `AndroidBridge` constructor and every return to the front). A newer share replaces one not
  yet picked up.
* **"Done"** — the button works in a `BroadcastReceiver`, usually without Qt running, while only
  C++ changes Markdown (one parser): `ReminderActions.done` closes the notification (like "OK"),
  removes the native snooze of this reminder and appends
  `{key, local, path, line, title, day, vault}` (`day` — the local date of the tap, `vault` — the notebook id, `Vault::id()`, from the
  reminder's data) to the queue in SharedPreferences (`DoneQueue`, at most 100 entries, tapping the
  same occurrence again replaces the entry, written with `commit()`), then calls
  `nativeDoneQueued` if C++ is listening. The signal reaches C++ at once only when the app is in
  the foreground: without `android.app.background_running` Qt for Android pauses the event loop
  while the activity is in the background (the process lives, the queue waits), and takes the
  queue when it returns to the front (`applicationStateChanged`). Verified on the emulator
  2026-10-28 (`--done` and the bridge test). `AndroidNotifier` takes the queue
  (`takeDoneRequests(vault)`) only when the notebook is ready (`Notifier::setDoneTarget` from
  `WorkspaceReminders::doneTarget()`: from `start()` after full indexing until the notebook is
  closed; a notebook with paused reminders, e.g. the documentation, takes nothing): when it becomes
  ready, after the signal and on the return to the front — and only the entries of this notebook
  (and entries without `vault`, from an older version); the others stay in the queue. The requests
  go out as `Notifier::doneRequested` and are written at once: none hangs only in memory, so
  ending the process before the notebook is loaded loses nothing. `WorkspaceReminders` finds the
  occurrence by `key` and `local` in the index, changes its text
  (`markdown::setTaskStatus(…, Done, day)`) and writes it only when the file still contains it
  (`Vault::writeTextIfUnchanged`); a change by another program that the scan has not seen yet
  (Syncthing in the background, a write in place) it takes in first and looks for the occurrence
  again — a line number from a stale index never hits another line.
* `nativeActivated` is called on Android's main thread; C++ only queues the signal emission. The
  `AndroidBridge` destructor calls `detach()` (waits for the lock), and only then clears the
  pointer.

### Tests and diagnostics

* Kotlin unit tests (pure logic: moving wall-clock time between zones, ring time, the reminders
  JSON, the "Done" queue (`DoneQueueTest`), sounds — the choice from C++, the ringtone picker's
  answer, channel versions and deleting old ones — with Polish characters; how much of the view
  the keyboard covers — edge-to-edge window, shrunk window, split screen, slide frames — and how
  much of the system gesture strip reaches the Qt view: `ImeInsetsTest`; "Share → Brulion" — which
  files are text, copy names with `..`, `/`, Polish characters and emoji, the age of the
  `share-in` folders, skipped preview thumbnails, the JSON for C++: `ShareIntentTest`; widgets —
  the snapshot from C++ and moving tasks to the new day, list rows, the month grid, dates:
  `WidgetSnapshotTest`; the quick add line, appending to the journal, the atomic write and paths
  outside the notebook (`..`, a symlink), a file not in UTF-8, a missing notebook folder:
  `JournalAppendTest`; the queue for C++ and going back to it: `WidgetInboxTest`; PDFs — the
  session and hits JSON, drawn sizes: `PdfJsonTest`; "Open in another app" — whether a file lies
  in the FileProvider's roots, the copy's name: `OpenWithTest`; background check of the notes —
  the target, which results are applied, the state for the settings: `BackgroundCheckTest`. The
  cases in `tests/data/widgets/*.json` are also checked by C++ — `tst_widgets` — so both sides
  write and move tasks the same way; the init script passes their folder in the
  `brulion.testData` property): `BRULION_ANDROID_UNIT_TESTS=1 scripts/android-build.sh` (runs
  `./gradlew --init-script android/tests/unit-tests.gradle testDebugUnitTest` in the generated
  Gradle project).
* Android lint: `BRULION_ANDROID_LINT=1 scripts/android-build.sh` (`./gradlew lintRelease`, prints
  the remarks about Brulion's files; the full report in
  `<build>/src/app/android-build/build/reports/`).
* End-to-end test on the emulator (needs KVM; on NixOS the emulator runs through `steam-run`):

  ```sh
  BRULION_ANDROID_ABIS=x86_64 BRULION_ANDROID_SELFTEST=1 scripts/android-build.sh
  scripts/android-emulator-test.sh     # logs and screenshots: dist-android/emulator-test/
  ```

  It installs the emulator and the image `system-images;android-36;google_apis;x86_64` (software
  rendering `-gpu swangle_indirect`; `swiftshader_indirect` leaves black stripes on buttons), runs
  the app's APK (whether it works, notification channels), then the bridge's test APK: permissions
  over JNI, `scheduleReminders` twice (reminders on tasks in 45 s and 200 s, an alarm in 75 s
  ringing for 1 min, past ones — skipped), the alarms in `dumpsys alarm`, notifications on the
  `reminders` and `alarms` channels, an alarm turning into "Missed alarm" after its ring time, taps
  (cold start and a running app, a path with Polish characters) reaching C++, "Done" in the
  notification of a reminder on a task: with the app on screen (the notification shade over it)
  the request reaches C++ at once with the day of the tap, the notification disappears; after the
  process is killed (`am kill`, not `force-stop` — the alarms stay) the second reminder comes to a
  new process without Qt, the tap waits in the queue (`shared_prefs/…done.xml`, debuggable APK)
  and C++ gets it at the next start. Verified 2026-10-28 (Android 16, Pixel 6; 24 checks). The
  test APK has the same package as the app (it replaces it) — only on an emulator/test phone. A
  freshly started emulator still applies system overlays for a few seconds (a
  `CONFIG_ASSETS_PATHS` configuration change the app does not handle itself): Android then
  recreates `BrulionActivity`, and an app recreated before Qt started starts itself once more
  ([Activity recreated during startup](#activity-recreated-during-startup)); `start_app` then
  writes "the app restarted itself" and waits longer for it. An app recreated after Qt has started
  is restarted by Qt without its mode arguments: when logcat has `finishDrawing of relaunch` of
  this activity but no entry about the app restarting itself, `start_app` starts it a second time
  ("…Qt restarted the app without its arguments — started again").
* Activity recreated during startup (a debuggable APK as below):
  ```sh
  scripts/android-emulator-test.sh --relaunch
  ```
  It starts the app like a tapped notification (extras `PATH`/`LINE`: the first page of the
  guide). Right after `am start` (in one `adb shell`, with delays of 0–2 s) it turns on the
  framework overlay `com.android.internal.display.cutout.emulation.*` (`cmd overlay`): a
  `CONFIG_ASSETS_PATHS` change, so Android recreates the activity. It checks that at least once
  this happened before Qt started (`BrulionActivity: recreated before Qt started`) and that after
  every attempt the note from the notification is on screen (also after the app restarted itself;
  after a restart by Qt — in English, without arguments and the note — the notebook's bottom
  navigation is enough or, when no notebook had been opened yet, the welcome screen), with Qt's
  `brulion.*` entries in logcat — no empty window; it prints the outcome of each attempt (before
  Qt started / after it / not recreated). Screenshots `rl-*.png`, each attempt's logcat
  `rl-logcat-<n>.txt`. Verified 2026-11-01 on GitHub Actions (Android 16): with the fix 7
  attempts, 2 times recreated before Qt started — each time a restart and the journal; without it
  (`BrulionActivity` from before the fix) 12 attempts, in one an empty window without any
  `brulion.*` entry, in four a restart by Qt (the journal in English). With a note from a
  notification (the same days, four runs): with the fix 8 restarts before Qt started, each time
  the note open; with a `BrulionActivity` that restarted without `keepForRestart`, 7 such
  restarts, in 6 of them the bare notebook instead of the note.
* Selecting and copying with a real finger (`adb shell input`, a debuggable APK as below):
  ```sh
  scripts/android-emulator-test.sh --selection
  ```
  A long press on a block's text in the journal — selection mode (the keyboard does not show, the
  toolbar from the block, handles, the selection tools at the bottom), a tap on the next block
  extends the selection, "Copy" (the message "Copied 2 blocks", and Android 13+ shows its own
  clipboard preview with the Markdown), Back ends the selection. The script finds blocks and
  buttons by name (like `--folders` below; before — by fractions of the screen, which on Android
  11, with a lower status bar, hit the second block, and the checks passed anyway). Screenshots
  `sel-*.png` in `dist-android/emulator-test/`; verified 2026-10-23 (Android 16 and 11, Pixel 6).
* Folders, links and tags with a real finger (the same debuggable APK):
  ```sh
  scripts/android-emulator-test.sh --folders
  ```
  The script finds the app's elements by their accessible names: `uiautomator dump` (the
  UiAutomator service turns accessibility on, and Qt gives it the tree of QML elements;
  `content-desc` is the button label with its shortcut, a block's text, a message — plain text
  labels have no name; `resource-id` contains the `objectName`), the on-screen keyboard's keys — by
  their names in the dump of all windows (`uiautomator dump --windows`), and it reads the notebook
  files through `run-as`. It types words by tapping keys, not through `input text`: Gboard on
  Android 11 composed the injected keys — after the first one or two, which went straight into the
  field — into a word in progress and committed it before them ("Archiwum" → "rchiwumA",
  "archiwum" → "chiwumar"; this may also affect a hardware keyboard with Gboard on Android 11 —
  unchecked). Steps: a tap on `#przewodnik` in the journal opens the page with that alias (with the
  tag strip), and the drawer opened on it shows that page's row in the tree — in the visible part
  of the list; on `#pomysł` — the empty tag page with links to the tagged blocks (it does not create
  the file), Back goes back; "More" on the editing toolbar (scrolled with a finger) opens the block
  menu, "Copy link" → "Block reference ((…))" adds the block id in the note and copies `((id))`
  (the message "Copied ((id))"; on Android 13+ Ctrl+V — `input keycombination` — pastes it at the
  end of another block, which shows in the file); the block
  `- Zapytać się [[Projekty/Remont mieszkania]] o plan` ("ask about the plan for [[Projects/Flat
  renovation]]") appended to today's journal "by another program" (`vault_append`, the app reads it
  on the return to the front) has the parts of the path as separate link nodes `Projekty` and
  `Remont mieszkania` (UiAutomator turns accessibility on, so `BLinkTargets` creates the link
  elements — as for TalkBack): a tap on `Projekty` opens the folder's list of pages, on
  `Remont mieszkania` — the page, a long press on `Projekty` — a menu with "Show folder" and "Copy
  folder path"; the journal file does not change apart from the appended line (screenshots
  `fl-lp-*.png`); "+" on the "Pages" header in the drawer → "New folder": the name typed on the
  keyboard and "Create" tapped while the keyboard is still composing the word, and the drawer
  shows the new folder above the sync footer; "⋯" of a page in the tree → "Move to…" (a sheet, the
  folder searched for, "Updated links: 1 (in 1 note)") moves "Remont mieszkania" from `Projekty` to
  the new folder: the file is in the new place, the `path:Projekty` filter of its query changes to
  `path:Archiwum`, the message has "Undo". The script first scrolls the drawer rows with a finger
  (`drawer_show`/`drawer_tap`) until the whole row lies between the notebook row and the sync
  footer: UiAutomator gives element bounds not clipped to the scrolling list, so a row hidden under
  the footer is also "on screen", and a tap on it hits the footer. After every drag it closes and
  reopens the drawer — on Android 11 the accessibility tree had no nodes for rows a drag brought
  into the list, and left others at their old places until the drawer showed again. Screenshots
  `fl-*.png`; verified 2026-10-29 (Android 16 and 11, Pixel 6; 38 checks on Android 16). On
  2026-10-29 the test found a bug (fixed): the list positioned the row of the open page, of a new
  folder and of "Show in tree" while the drawer was still closed or only opening — with the height
  the list had in the previous layout; the drawer gets the system bar margins and loses the
  keyboard's space only when it opens, so the row landed under the footer, and a tap on it opened
  Sync (before, the test passed by chance: the list was scrolled by 11 px). Now the row is
  positioned again at every change of the list's height until the user scrolls it (docs/UI.md
  "Folders"). Earlier the test found two bugs (fixed): a menu taller than the space above the
  editing toolbar went under the status bar, where a tap goes to the system — "Block reference ((…))" in
  the submenu was out of a finger's reach (`BMenu.topMargin`); and a word the keyboard is still
  composing (Gboard keeps each word outside the field's text until a space) did not count as typed
  — "Create" of a new folder and "Add" of a quick note with a single word stayed disabled
  (`typedText`, docs/UI.md "On-screen keyboard"). Remaining (not fixed): when the block menu opens
  from "More", the keyboard hides (the menu takes the focus), the editing toolbar slides down, and
  the menu stays where it was placed above the keyboard — with a gap above the toolbar.
* "Share → Brulion" (the same debuggable APK):
  ```sh
  scripts/android-emulator-test.sh --share
  ```
  With the app running (the sample notebook) `am start` sends text to `ShareActivity`
  (`EXTRA_SUBJECT` "Artykuł na jutro" — "an article for tomorrow", `EXTRA_TEXT` with Polish
  characters and an address): the "Save to Brulion" dialog, "Save" — the block
  `[Artykuł na jutro](…) — …` in today's journal, no reminder. Then the file `Lista zakupów.md`
  (CRLF) pushed to `Download`, found in `MediaStore` and shared as `content://media/…` after a cold
  start: the dialog with the file, "Save" — a new page byte for byte, the copy folder deleted.
  Then an image: the system does not offer Brulion (`cmd package query-activities`), and sent
  directly it opens no dialog. Screenshots `sh-*.png`. Finally several files at once from the
  system file manager (`ACTION_SEND_MULTIPLE`, which `am` cannot send): in "Downloads"
  `Lista zakupów.md`, `Notatka źdźbło.txt` and an image selected. The selection then has the type
  `*/*`, which Android matches to the `text/*` filter, so the share sheet offers Brulion (on
  Android 16 it does; if it did not, the test deselects the image); the dialog lists both text
  files and the image as "Not a text file", "Save" — one block per text file in today's journal (a
  bold title, the file's lines indented), the image skipped. After a cold start from "Share" the
  app speaks the phone's language (without `--locale pl`), so the test looks for labels in Polish
  and in English. Verified 2026-10-28 (Android 16, Pixel 6). By hand on a phone (APK from GitHub
  Actions `android.yml`):
  1. Chrome → a page → Share → Brulion: a dialog with `[Title](address)`, "Save" → a block in
     today's journal, nothing more (no ⏰ even with "tomorrow" in the title);
  2. Files → an `.md` with Polish characters → Share → Brulion while the app is closed (cold
     start): a "New page" dialog with the title from the file name; the page has the file's
     content unchanged;
  3. the same file once more → the page "… 2"; the title changed to the same name in other letter
     case (e.g. "LISTA …") → the dialog says the page already exists, nothing is overwritten;
  4. three files at once (selected in the file manager, among them a `.jpg` image) → the image
     does not get Brulion's option, or is on the list as "Not a text file"; "Today's journal" → one
     block per file;
  5. a share while the documentation is open → Brulion returns to your notebook and shows the
     dialog there; "Cancel" → nothing saved, `adb shell run-as io.github.januszbit.brulion
     ls files/share-in` empty (debuggable APK).
* The system file pickers (`FilePicker`, docs/UI.md "File pickers"; the same debuggable APK):
  ```sh
  scripts/android-emulator-test.sh --pickers
  ```
  In `Download` the note `Zażółć.md` (CRLF) and the folder `Notatki z telefonu` ("notes from the
  phone") with two notes. Import → **Choose files…** (the button scrolled above the bottom
  navigation): DocumentsUI on top (`ACTION_OPEN_DOCUMENT`), the note picked with a tap, the import
  preview carries its name; **Choose a folder…** (`ACTION_OPEN_DOCUMENT_TREE`, the picker starts at
  the storage root): `Download` → `Notatki z telefonu` → "Use this folder" → "Allow", a preview
  with the folder's name and "Import 2 files"; Export → **Export…** → the system "Save"
  (`ACTION_CREATE_DOCUMENT`) with the export's file name in the name field (`EXTRA_TITLE`),
  "Save" — the ZIP (starts with `PK`) is in `Download` (in the folder used last). No crashes or
  QML errors, no picker refused to open. Screenshots `pk-*.png`. Verified 2026-10-30 (Android 16,
  Pixel 6).
* "Done" in a notification of the app's own reminder (the same debuggable APK):
  ```sh
  scripts/android-emulator-test.sh --done
  ```
  A notebook in the app's storage with two tasks in today's journal, with reminders in 2 and 4
  minutes (the phone's clock): the first "Done" with the app on screen — in the note at once
  `- [x] … ⏰ … ✅ <day of the tap>`, the notification disappears; the second after the process is
  killed — the note unchanged, the tap in the queue, the next start writes `[x]` with `✅`, the rest
  of the note untouched. Screenshots `dn-*.png`. Verified 2026-10-28 (Android 16, Pixel 6).
* Text on a drawing from the on-screen keyboard (the same debuggable APK; `node` on the host):
  ```sh
  scripts/android-emulator-test.sh --drawing
  ```
  A notebook in the app's storage whose today's journal embeds a drawing: a tap on the drawing
  opens the drawing editor; on a phone held upright the toolbar has **Text**, the colour circle,
  **More** and **Done** on screen (two rows); **Text** and a tap on the sheet open the text field
  and the keyboard (Gboard); words typed with keyboard keys with a pause longer than the save after
  1.2 s — the field and the keyboard stay; ✓ saves the text (a text element with that text in the
  drawing file), the keyboard disappears; a second text started right after the first one (while
  the save after 1.2 s is still pending) also stays open and is saved. Screenshots `dr-*.png`.
  Verified 2026-10-29 (Android 16, Pixel 6, Gboard in English — the test does not type Polish
  characters from the on-screen keyboard).
* The stylus in the drawing editor (the same debuggable APK; `node` on the host):
  ```sh
  scripts/android-emulator-test.sh --stylus
  ```
  `adb shell input stylus …` always presses with a force of 1.0 (a stroke with a single pressure
  draws like a mouse), and `sendevent` would need root and an emulator touch device with a stylus.
  So the script compiles `scripts/android-stylus/BrulionPen.java` (`javac` + `d8` from the SDK) and
  runs it through `app_process` as the shell user (it has `INJECT_EVENTS`, no root — like scrcpy):
  the hidden `InputManagerGlobal.injectInputEvent` (Android 14+; before, `InputManager`) gets
  `MotionEvent`s with the tool type of a stylus (or an eraser, or a finger), pressure and buttons.
  A new drawing (a 0-byte file) opened through `--route`: a stylus stroke with pressure 0.1 → 0.9
  is saved with the pressure at every point (`simulatePressure: false`, from the first ≤ 0.15 to
  the last ≥ 0.85 — lifting the stylus with pressure 0 is not the stroke's pressure); a stroke from
  `adb input stylus` (a single pressure) draws without pressure; the eraser tool type
  (`TOOL_TYPE_ERASER`) erases the first stroke, the side button (`BUTTON_STYLUS_PRIMARY`) the
  second; after the stylus a finger pans the sheet instead of drawing. The drawing file is read
  after every step; screenshots `st-*.png`. Verified 2026-10-30 (Android 16, Pixel 6). The test
  (and the tests in the browsers, docs/WEB.md "Stylus (pressure)") found bugs, fixed and checked in
  `tst_drawingcanvas`: the notification host in the popup layer covered the sheet from the stylus
  in the whole app (the stylus drew like a mouse — without pressure — and a finger after the stylus
  kept drawing; this mode with the APK from before the fix, 2026-10-30: a stroke without pressure,
  the eraser and the side button did not erase); Qt for Android gives the pointer type in Qt 5
  values (pen 1, eraser 3 — in Qt 6 that is not `Eraser`), so the eraser drew, and it gives the side
  button (`BUTTON_STYLUS_PRIMARY`) as `Qt::ExtraButton3`; the lift event has pressure 0 and lies a
  pixel next to the last move (Android resamples the moves), so every stroke ended with pressure 0,
  and a stroke with a single pressure counted as one with pressure. GitHub Actions has KVM on
  `ubuntu-24.04` (a udev rule, as in `wasm.yml`'s `keyboard-android` job), so this mode could run
  there; it is run on the laptop (a separate x86_64 Debug APK build).
* Pictures from the internet (the same debuggable APK; the emulator needs internet access and the
  host `python3`):
  ```sh
  scripts/android-emulator-test.sh --web-pictures
  ```
  Description and result — the section [Pictures from the internet](#pictures-from-the-internet).
  Screenshots `wp-*.png`.
* The browser version in Android's browsers (no Brulion APK: the site from
  `scripts/wasm-build.sh`; internet access is needed for the first download of the browsers, and
  `python3` on the host):
  ```sh
  scripts/android-emulator-test.sh --web-keyboard dist-wasm
  ```
  Chrome from the system image, Brave 1.97.59 and Firefox 157.0.1 (APKs pinned by SHA-256 sums, in
  `~/.cache/brulion-web-browsers`): the editing toolbar sits on the keyboard (in Chrome and Brave
  on the browser's autofill bar), a word typed with keys goes into the block, Enter splits the
  block, Back hides the keyboard. Description — docs/WEB.md "On-screen keyboard". Screenshots
  `wk-*.png`, readings `samples-*.jsonl`. The same test runs in the `keyboard-android` job in
  `.github/workflows/wasm.yml`.
* Widgets (the same debuggable APK; a `google_apis` image with `adb root` — the quick add window
  and the widget receiver are not exported):
  ```sh
  scripts/android-emulator-test.sh --widgets
  ```
  The sample notebook publishes the snapshot when the app leaves the screen; the launcher's widget
  picker places the three widgets (long-pressing the screen, "Widgets", Brulion, a tap on the
  preview, "Add"); quick add with the app killed ("Kupić żółty ser" — "buy yellow cheese",
  **Tomorrow**) — the line in the journal at once, the Qt library not loaded; a task for today in
  the widget at once; **Time…** 09:00 + **Tomorrow** + **Alarm** — `⏰!` and a temporary system
  reminder that the app later replaces with its own; with the notebook folder moved the entry
  waits in the queue, and the folder is not recreated; without a snapshot the entry waits (the
  window then speaks the phone's language), and the app appends both at startup; with the app
  alive in the background quick add writes by itself, and after the return to the front the app
  hands over its set of reminders once more and the temporary one disappears; a tap on a widget
  task opens the note (cold start); › and ‹ of the calendar; the tasks widget enlarged with the
  launcher's handles shows **Tomorrow**, the calendar shrunk — only the agenda. Screenshots
  `wg-*.png` (light and dark). Verified 2026-10-28 (Android 16, Pixel 6).
* The on-screen keyboard on the emulator (a debuggable APK — only to such an APK does Qt pass
  arguments and environment variables from the intent):

  ```sh
  BRULION_ANDROID_ABIS=x86_64 BRULION_ANDROID_BUILD_TYPE=Debug scripts/android-build.sh
  scripts/android-emulator-test.sh --keyboard
  BRULION_EMULATOR_IMAGE="system-images;android-30;google_apis;x86_64" \
    scripts/android-emulator-test.sh --keyboard     # older Android
  ```

  It opens the sample notebook (`--demo`), taps a block of today's journal with a finger, waits
  for the keyboard (`dumpsys input_method`) and compares the keyboard's top edge (the source of the
  `ime` insets in `dumpsys window displays`) with the value the app reports (±2 dp); on Android
  11+ it checks that the window did not resize and that the slide came frame by frame; it scrolls
  the journal with a finger, hides the keyboard with the Back button and checks in the log that the
  bottom navigation came back and the toolbar stayed right on it (and in UiAutomator's tree: the
  toolbar above the "Calendar" button), taps the text so that the keyboard comes back (the toolbar
  on it again, no navigation), then the toolbar on the keyboard on a guide page (opened as if from
  a tapped notification), finally a screenshot in the dark theme (system bar icons). Screenshots
  (`keyboard-*.png`) and logs in `dist-android/emulator-test/`.
* After building, the script checks: the signature (`apksigner verify`), the package and all
  required permissions (`aapt2 dump badging`), the components in the merged manifest
  (`aapt2 dump xmltree`), the Kotlin classes and Qt's Java (`QtNative`, `QtActivityBase`,
  `QtLoader`) and the androidx methods Qt calls (`getUriForFile`, `fetchFonts` — R8 rules) in
  `classes.dex`, `lib/<abi>/libbrulion_<abi>.so`, OpenSSL (`libssl_3.so`, `libcrypto_3.so` for
  each ABI), no `qmltooling` plugins in Release, and also compatibility with **16 KB memory
  pages** (Android 15+; phones with such pages will not load other libraries): `zipalign -c -P 16`
  and the alignment of the `LOAD` segments of the 64-bit libraries (`llvm-readelf`) — a badly
  aligned `libbrulion_<abi>.so` stops the build (NDK r27 gets
  `-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON`; OpenSSL likewise), the Qt libraries are only
  reported.
* Logs: `adb logcat -s BrulionAndroid:* brulion.android:* Qt:*`.
* Set alarms: `adb shell dumpsys alarm | grep -B2 -A6 io.github.januszbit.brulion`.
* Testing re-arming without a restart (the receiver is not exported, so `adb root` is needed, e.g.
  an emulator without Google Play):
  `adb shell am broadcast -a android.intent.action.BOOT_COMPLETED -p io.github.januszbit.brulion`.

### A note on distribution

Google Play restricts the `MANAGE_EXTERNAL_STORAGE` (all files access) and `USE_EXACT_ALARM`
permissions to selected app categories. Brulion is meant to be installed from an APK file (e.g.
from the GitHub releases) or from F-Droid.
