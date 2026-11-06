---
tags: [documentation]
brulion: document
---
# Reminders and alarms

A reminder is a `⏰` marker with a date and time in any block — not only in a task.

| Notation | Effect |
|---|---|
| `⏰ 2026-10-20 09:00` | a notification at that moment |
| `⏰ 2026-10-20` | a notification at the default time from the settings |
| `⏰! 2026-10-20 06:30` | an **alarm** — loud, high priority, keeps going until you turn it off |

The time is local time, written without a time zone; “9:00” means 9:00 wherever you are.

## Adding
- Type `⏰` and write it the way you'd say it: “tomorrow at 7:30”, “in 2 hours”, “friday 18:00”.
- Or use “Reminder…” or “Alarm…” from the `/` menu: a dialog with quick choices, a calendar, a time, an alarm switch and recurrence.
- Clicking the ⏰ sign in a note opens a dialog where you can change it, snooze it or delete it.
- A right click on the ⏰ sign (on a phone, a long press) opens a menu: “Change date and time…”, “An hour later”, “Tomorrow at this time”, “A week later”, the “Alarm” switch, the **Repeat** submenu (ready-made rules, “Change rule…”, “Don't repeat”) and — on a task — **Priority**, plus “Delete reminder”. You can undo every change (`Ctrl+Z`). The same menu is in query results, in embedded blocks, in tables, in the Tasks view and in the calendar.
- In the calendar a recurring reminder shows on every day of its series. The menu of such a day changes the whole series (“Change the whole series…”). When that occurrence has already passed, “An hour later” and “Tomorrow at this time” postpone only that one — just like **Snooze** in the notification. For a date that is still to come, the menu doesn't have these items, because the reminder would ring anyway.
- Changing a 🔁 rule changes only the rule itself: words written after it stay. When you change only the time, the rule stays written as it was (e.g. “every week”).
- In a quick capture (Ctrl+Shift+N) it is enough to write the date in the text.

## Recurrence
If the block also has a `🔁` rule (see [[Tasks|Tasks]]), the reminder repeats with it. A reminder on a task that is done or cancelled doesn't ring.

## Notifications
- Tapping a notification opens the note at the right block.
- The notification's title is the block's text without markers.
- A reminder on a task has a **Done** button in its notification (next to **Snooze** and **OK**): it marks the task as done — `[x]` with a `✅` date — without opening the note, just like changing the status in the editor. A recurring task gets its next date right away. When you have changed that reminder or task in the meantime (also on another device), Brulion changes nothing and tells you so. The button is in notifications on Linux, Windows and Android — there also on the alarm screen.
- On Android **Done** also works when Brulion is in the background or closed: the change goes into the note when you return to Brulion, or at its next start once the notebook with that task is loaded (it then shows a message), and the `✅` date is the day you tapped the button. Until then the tap waits on the phone, so it isn't lost, even if you close Brulion earlier or first open another notebook. Until then other devices see the task as open.
- On Android reminders and alarms work with the app closed, also after the phone restarts; they need permission for notifications and exact alarms, which the app asks for.
- On a computer Brulion runs in the system tray if you turn on autostart, and notifies you from there.
- On Windows reminders and alarms are Windows notifications with **Snooze** and **OK** buttons (on a task also **Done**); a click opens the note. An alarm stays on the screen, and its sound repeats until you respond (at most for the alarm's ringing time). You choose the sound from the Windows notification sounds in *Settings → Reminders*. Notifications work as long as Brulion is running — also after you close the window, in the tray. When you turn off Brulion's notifications in the Windows settings, the tray icon shows the reminders.
- On Windows *Settings → Application → Start when you sign in* starts Brulion hidden in the tray right after you sign in, so that reminders ring from the start of the day. The installer offers the same.
- Reminders that passed while the device was off wait in the journal, where a banner shows the reminders to deal with: **Open**, **Snooze**, on a task **Done**, and dismissing.

## Reminders from other devices
A reminder added on a computer (or by an AI agent with `brulion-cli`) reaches the phone together with the note, through Syncthing or Nextcloud. On Android it rings even when Brulion is closed: every now and then the phone checks the notes in the background by itself — with Nextcloud it syncs them first — and sets up what it finds. New reminders get added, and the ones deleted or moved on the computer disappear or move.
- How often is chosen in *Settings → Reminders → Check notes in the background*: every 15 minutes, every 30 minutes (the default), every hour or never. Below this setting you see when the last check happened.
- Android may postpone the check when the phone has been lying unused for a long time (even by an hour or more), so a reminder “in 10 minutes” may not make it, while one for tomorrow morning will. Setting Brulion's battery usage to “Unrestricted” helps.
- When the notebook can't be read (e.g. the memory card is removed), the reminders already set stay unchanged.

## Which notebook reminds you
Reminders and alarms come from the open notebook. When you switch to another of your notebooks, Brulion counts down only its reminders, and the previous notebook's wait until you return to it:
- on Android the previous notebook's alarms are switched off, and those whose time passes before you return are lost — they won't ring or show up later;
- on a computer, when you return, reminders up to 6 hours late and alarms late by no more than an alarm's ringing time show up right away; older ones go to the list of missed ones.

The exception is this documentation: it has no reminders of its own, so while you read it, on Android your notebook's alarms keep ringing, and tapping a notification opens the note in your notebook. On a computer, reminders that passed in the meantime show up when you return, as above.

## Where to find them
Reminders and alarms show up in the **Calendar** (month, week, agenda) and in queries: `type:reminders remind:this-week` — see [[Queries|Queries]]. You set the default time in *Settings → Notebook*.
