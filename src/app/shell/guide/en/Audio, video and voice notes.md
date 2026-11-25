---
tags: [documentation]
aliases: [Audio, Video, Voice notes, Recordings, Transcription]
brulion: document
---
# Audio, video and voice notes

A recording or a video in a note plays right where it is, and a voice note you record in Brulion can become text — on the device, without sending a sound anywhere.

## Players in notes
Embed a sound or a video like a picture — paste it, drop it onto the page or choose it with `/Image…` / `/File…`: `![[Lecture.mp3]]`, `![[Trip.mp4]]`. Brulion plays MP3, M4A, WAV, Ogg, Opus, FLAC and 3GP recordings and MP4, WebM, OGV, MOV and MKV videos — the formats Obsidian plays, so the same notes work in both.

The player has:
- **Play / Pause** and the time — where it is and how long the recording is (known before you press anything);
- the **bar** to jump anywhere: drag it or tap where you want to be;
- the **speed** — tap **1×** for 1.25×, 1.5×… up to 2× and back to 0.5×;
- the **sound** — mute, and on a computer the volume beside it;
- for a video, its first frame until it plays, and **Full screen** (`Esc` or `F` leaves; `Space` pauses, `←` / `→` jump by 5 seconds, `M` mutes);
- **More**: *Open in the player*, *Transcribe*, *Copy link to 1:23* (the moment it is at), *Open in another app*.

**Only one plays at a time**: start another and the first one pauses — and remembers where it was, so the next **Play** goes on from there. A recording **keeps playing when you scroll away**, open another page or go to the calendar: a small bar at the bottom shows what plays, with pause and **×** — tap its name to open the recording. A video plays only where you see it: scrolled away, it pauses.

Clicking the file in the tree, in a link `[[Lecture.mp3]]` or in *Open in the player* opens the **player view**: the video large, the bar with ±10 seconds, and below the notes that embed the recording or link to it.

## Linking to a moment
Add `#t=` and the time to the name: `![[Lecture.mp3#t=1:23]]` starts the player at 1:23, `[[Lecture.mp3#t=83]]` opens the player view at 83 seconds, and `![[Lecture.mp3#t=10,20]]` plays from 0:10 and pauses at 0:20. Seconds (`#t=83`), minutes and seconds (`#t=1:23`), hours (`#t=1:02:03`) and `#t=1m23s` all work — the same notation as web browsers and Obsidian's media plugins. *More → Copy link to 1:23* copies such a link to the moment the recording is at, ready to paste into your notes.

## Recording a voice note
Type **/Voice note** (or `/record`, `/nagranie`) in a block, tap the **microphone** on the toolbar above the keyboard on a phone, use *Record voice note* in the command palette (`Ctrl+K`), or **Voice note** in quick capture. The first time, Brulion asks for the microphone (on the phone, the system asks). Recording starts at once:
- the time recorded and a meter of how loud you speak (on Android the system records by itself, so instead of the meter a microphone breathes while it records);
- with a speech model downloaded (below), **the words as you speak them** — a preview that grows every few seconds;
- **Pause** and **Resume**;
- **Save** puts the recording into the note — where the cursor was, or at the end of the page — and from quick capture or the palette (off a note) into **today's journal**;
- **Cancel** (or `Esc`, the back gesture) throws it away.

The recording is an ordinary file in the attachments folder, named by its time: `Recording 2026-11-20 18.30.m4a`. Brulion records AAC in M4A where the device can — it plays everywhere, also in Obsidian on an iPhone — else Opus in Ogg or WAV. A recording stops by itself after three hours.

## Turning speech into text
Brulion can write down what was said — in Polish, English and many other languages — **on the device**: no sound leaves it, and no account or internet is needed once the model is downloaded. It uses OpenAI's Whisper (MIT licence).

1. Open *Settings → Extensions → Speech* and choose a model:
   - **Small** (252 MB) — Polish heard well enough to edit, a minute of speech in seconds on a computer; the default;
   - **Base** (78 MB) — fast on any device, but rough in Polish;
   - **Large Turbo** (547 MB) — the most accurate, also with names and numbers; slower, on a phone a minute of speech takes a while.
2. Tap **Download** (best over Wi-Fi; you can leave the page, and a download that stops goes on from where it was).
3. Choose the **language**: *Automatic* recognizes it from the first half minute; *Polish* or *English* is better for short notes.

With **Transcribe voice notes** on (the default), a voice note you record is written down by itself. Any recording in a note — also one you got from elsewhere — is written down from its menu: **More → Transcribe**. While it works the player says how far it is, with **Stop**; when it is done, the text appears **right under the recording**, as ordinary blocks you can edit, move or delete. Several recordings are written down one after another.

A short note becomes one paragraph. A longer recording — a lecture, a meeting — becomes paragraphs of whole sentences, each starting with **the moment it was said**: `[[Recording 2026-11-20 18.30.m4a#t=83|1:23]]`. Click the time and the recording above plays from there, so you can listen again to the part you are reading.

**Text while recording** (on by default, in the same settings) shows the words under the recorder while you speak, and in the AI chat's field while you dictate. It is a quick preview: once you save, the whole recording is heard again, more carefully, and that is the text that goes into the note. If the device cannot keep up with you — the words come more than ten seconds late — it stops for that recording and stays off with that model until you tap **Try again** in the settings; a smaller model may keep up.

> [!tip] Where it works
> Players work on the computer (Linux, Windows) and on Android; in the browser a recording or a video opens in a new tab, where the browser plays it. Turning speech into text works where the model on the device does: the NixOS package and Android (not yet in the AppImage, on Windows or in the browser).

## In the file
```markdown
- Lecture ![[Lecture.mp3]]
- From the important part: ![[Lecture.mp3#t=12:40]]
- ![[Recording 2026-11-20 18.30.m4a]]
	- Buy milk and bread tomorrow morning, then call the dentist.
- ![[Lecture 2026-11-24 10.00.m4a]]
	- [[Lecture 2026-11-24 10.00.m4a#t=0|0:00]] Good morning. Today we talk about how speech becomes text.
	- [[Lecture 2026-11-24 10.00.m4a#t=34|0:34]] First the recording is cut into short windows…
```
