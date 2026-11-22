---
tags: [documentation]
brulion: document
---
# Search

**Ctrl+Shift+F** (or **Search** in the icon bar, on a phone in the bottom navigation) searches the whole notebook: the titles and text of your notes, and also the **text of PDFs and pictures** in it. Accents are optional — “cafe” finds “café”, “zolw” finds “żółw” — and the last word may be the start of a word: “wyzn” finds “wyznacznik”. A small typo is forgiven too, but what you typed exactly always comes first. With **search by meaning** on (below), notes close in meaning come in too.

## Results
- A note opens at the line with the words.
- A **PDF** shows its page — “Page 3 of 12” — and how many other pages match. It opens in Brulion's PDF viewer **at that page, with the words highlighted**; the arrows of the viewer's search go to the other places.
- A **picture** (a photo of a whiteboard, a screenshot) opens with the places of the words framed. **Recognized text** in its bar shows everything that was read in it — you can select and copy it.
- “Recognized text” under a result means the words were read from a picture or a scan (text recognition can make mistakes, so search for a few words if one is not found).
- A PDF or picture can also be found by its file name: “wykład” finds “Wykład 3.pdf”.

## Narrowing the search
- **All / Pages / Journals / Attachments** under the field.
- Words in the query itself: `in:pdf`, `in:pictures`, `in:attachments`, `in:notes`, `in:pages`, `in:journals` — e.g. `in:pdf integral`. Polish works too: `w:załączniki`, `w:obrazy`, `w:notatki`, `w:dziennik` (also `type:` and `typ:`). Several of them add up: `in:pdf in:pictures`.
- The **Folder** button (or “Search in folder” in a folder's menu) — only one folder and its subfolders.

## Search by meaning
Search can also find notes by **what they mean**, not only by their words: “apple cake” finds “Szarlotka babci”, an English query finds a Polish note, and “kupiłem opony” finds a note about “kupić oponę” — other forms of a word, other languages, related words. It is **off by default**: turn it on in **Settings → Search → Search by meaning**. The first time it downloads a small model (Multilingual E5 Small, 126 MB, best over Wi-Fi), which then runs **on this device, offline** — nothing about your notes is sent anywhere.

- Results by meaning join the results by words, in Search and in **Search the notes for “…”** of the quick switcher (Ctrl+O). What you typed exactly still counts: a note with your words stays among the results.
- A note that holds none of your words but is close in meaning is marked **Close in meaning** ✨; it opens at the passage closest to your query.
- Brulion reads the notebook by meaning **in the background**, at the lowest priority, newest journal days first; meanwhile a line under the search filters says how far it is (“Reading notes by meaning: 120 of 900”) and the notes not read yet are found by their words. The first reading of a big notebook takes a few minutes on a computer, longer on a phone; after that only the notes you change are read again, a moment after you stop typing.
- What was read is kept **on this device** (not in the notebook, so it doesn't go through sync), and opening the notebook again starts from there.
- **Remove** next to the model deletes it and what was read of every notebook; search is by words again.

The model is small, so it is good at related words and other languages, and weaker at whole paraphrases. Encrypted text is never read.

## How PDFs and pictures are read
Brulion reads attachments **in the background, on this device, offline** — nothing is sent anywhere. The text of a PDF is ready a moment after the notebook opens; a long one is read page by page, so even a 100 MB lecture scan doesn't fill the memory. While reading, a line under the search filters says so (“Reading attachments… 3 of 40”), with **Pause**. A sync in progress and typing come first: reading waits.

What was read is kept on this device (not in the notebook, so it doesn't go through sync): an unchanged file is never read again, and a renamed or moved one keeps its text. A changed file is read again.

## Text recognition (OCR)
A scanned PDF page or a picture has no text of its own — **text recognition** reads it from the picture. It is **off by default**, because it takes about a second of one processor core per page (at the lowest priority, so the computer stays responsive). Turn it on in **Settings → Search → Recognize text in pictures and scans (OCR)**. Then:
- photos of notes and whiteboards, screenshots and scans become searchable,
- a scanned page in the PDF viewer can be searched too (Ctrl+F), with the words highlighted.

**Recognition languages** are automatic — the language of Brulion and English — or chosen by hand. Changing them reads the recognized pages again.

## Settings → Search
- **Search in attachments** — turns reading PDFs and pictures on and off.
- **Recognize text in pictures and scans (OCR)** and its **languages**.
- **In this notebook** — how many files and pages were read, what is being read now, **Pause** / **Resume**.
- **Text kept on this device** — its size; **Clear** reads every file again.
- **Search by meaning** — on and off (the first time it downloads the model), **The model's file** (Download, Stop, Remove), **In this notebook** — how many notes can be found by meaning.

## On different devices
- **Computer (Linux, Windows)** — PDFs are read everywhere. Text recognition uses Tesseract: the Nix package has it with Polish and English built in; elsewhere install it — on Linux the packages `tesseract-ocr`, `tesseract-ocr-pol` and `tesseract-ocr-eng`, on Windows the Tesseract installer (UB Mannheim) with Polish — and restart Brulion. Settings → Search tells you if it is missing. More languages: put their `*.traineddata` files into the folder Settings names.
- **Android** — the text of PDFs from Android 15; no text recognition on the phone (it would make the app much bigger).
- **Browser** — the web version doesn't read PDFs or pictures; it opens them in a new tab, where the browser's own viewer searches them. It doesn't search by meaning either: search there is by words.
- **Search by meaning** works on the computer and on Android phones (64-bit); on a phone the first reading of the notebook takes longer — about ten minutes for a thousand notes —, and searching is as fast as on a computer.
