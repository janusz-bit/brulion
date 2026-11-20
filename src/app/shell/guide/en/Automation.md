---
tags: [documentation]
aliases: [brulion links, Tasker, Shortcuts, URI]
brulion: document
---
# Automation

Brulion understands **brulion:// links**. Such a link opens a page, today's journal or the calendar, or adds a note, a task with a reminder or a new page — from a bookmark in the browser, a launcher, a keyboard shortcut of your desktop, a script, or an automation app on the phone (Tasker, Automate, MacroDroid). The phone also has **Quick Settings tiles** and **shortcuts on the app icon**.

## Try it
Open **Settings → Automation**. Under "Make a link" choose what the link should do, fill in the fields and press **Try it** — or **Copy link** and paste it wherever a link can go.

```text
brulion://daily                                  today's journal
brulion://open?page=Shopping&heading=Today       a page, at its heading
brulion://capture?text=Gift%20idea               a quick note into today's journal
brulion://task?text=Call%20mum&remind=tomorrow%206pm
brulion://append?page=Shopping&heading=Today&text=Milk
brulion://new?title=Meeting%20with%20Anna&template=Meeting
brulion://search?query=holiday
brulion://view?name=calendar
```

Spaces and Polish letters are written as `%20`, `%C5%BC` and so on — every app that builds links does it for you. A plus sign `+` in a link is a space too; a real plus is `%2B`.

## What a link can do
- **open** — a page (`page=`: its name, path, alias or a date), at a heading (`heading=`), a block (`block=`) or a line (`line=`). Also `page=Shopping#Today`, like a link in a note.
- **daily** — a day's journal: `date=yesterday`, `date=next friday`, `date=2026-12-24`; today without it.
- **capture** — a quick note into today's journal; a date in the text makes it a task with a reminder, as in the app's quick capture. Without `text=` it only opens the quick capture window.
- **append**, **prepend** — text at the end or the start of a page (`page=`), of a section (`heading=`), under a block (`block=`), or in a day's journal (`date=`; today without `page` and `date`).
- **task** — a task: `text=`, and `due=`, `remind=`, `alarm=`, `every=` (e.g. `every week`), `priority=` — in words, in English or Polish. Without them a date in the text is used, as in quick capture.
- **new** — a new page: `title=`, `folder=`, `template=` (a page from your templates folder), `text=`. A page with that name is never written over — it simply opens.
- **search**, **view** — search for something, or open a view: `journals`, `calendar`, `tasks`, `habits`, `graph`, `pages`, `tags`, `settings`.
- **run** — a command of an extension you approved on this device (`extension=` and `command=`).

With several notebooks, `vault=` picks one by its name or folder. The full list of parameters is in the documentation for developers (docs/UI.md, "Automation").

## Safety: the automation key
A link can come from anywhere — a web page, a message, a note someone synced to you. So:
- A link that **opens** something works at once: it changes nothing.
- A link that **writes** first shows **"Write from a link?"**: where it would write and exactly what. Nothing is saved before you press **Write**.
- Your own automations skip that question with the notebook's **automation key**: **Settings → Automation → Create a key**, then add `&key=…` to the link (or tick "With the key" in the link builder). Keep the key private, like a password. It stays on this device and is never synced; **New key** makes the old one stop working.
- **Links without the key** in the same settings: **Ask** (the default), **Only open** (writing only with the key) or **Off**.
- A link never deletes anything and never writes over a page: it only adds a block or creates a page with a free name. An extension's command runs only when you approved the extension.

## On a computer
- **Linux**: Brulion's desktop entry takes brulion:// links, so `xdg-open 'brulion://daily'` works, and so does a keyboard shortcut of GNOME or KDE with that command. A running Brulion comes to the front and does it.
- **Windows**: the installer registers brulion:// links: `start brulion://daily`, the Run dialog (Win+R), a desktop shortcut with a brulion:// address as its target.
- **Command line**: `brulion-cli uri run 'brulion://append?…'` writes without the app at all, `brulion-cli uri open '…'` hands a link to the running app, `brulion-cli uri build …` writes one for you — see [[Command line and AI agents]].

## On the phone (Android)
- **Quick Settings tiles**: open the Quick Settings panel, edit the tiles (the pencil) and drag **Quick note** and **New task** among them. A tap opens a small window over whatever is on the screen; the note or task goes into today's journal at once, without opening Brulion.
- **Shortcuts on the icon**: hold Brulion's icon: **Quick note**, **New task**, **Today's journal**, **Search** — drag one onto the home screen to keep it.
- **Tasker, Automate, MacroDroid**: send the intent with the action `io.github.januszbit.brulion.action.RUN` to the package `io.github.januszbit.brulion`. Put the link in the intent's *Data* and the text and the key in extras:

```text
Action:   io.github.januszbit.brulion.action.RUN
Data:     brulion://append?page=Shopping&heading=Today
Extra:    text:%CLIP
Extra:    key:%BRULION_KEY
Target:   Broadcast Receiver   (class io.github.januszbit.brulion.android.AutomationReceiver)
```

With the key the note is written in the background, even when Brulion is closed, and a task with a time gets its reminder at once. Brulion answers with the broadcast `io.github.januszbit.brulion.action.RESULT` (`ok`, `error`, and with the key also `path` and `line`) — Tasker's event "Intent Received". A request that opens something, or has no key, needs the *Activity* target instead (`io.github.januszbit.brulion.android.AutomationActivity`): Brulion comes up and asks first.

## In the browser
In **Settings → Automation → Links in this browser** press **Allow**: links starting with `web+brulion:` then open this Brulion (`web+brulion://daily`). The rest works the same.
