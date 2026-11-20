---
tags: [documentation]
brulion: document
---
# Search

**Ctrl+Shift+F** (or **Search** in the icon bar, on a phone in the bottom navigation) searches the whole notebook: the titles and text of your notes, and also the **text of PDFs and pictures** in it. Accents are optional — “cafe” finds “café”, “zolw” finds “żółw” — and the last word may be the start of a word: “wyzn” finds “wyznacznik”. A small typo is forgiven too, but what you typed exactly always comes first.

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

## On different devices
- **Computer (Linux, Windows)** — PDFs are read everywhere. Text recognition uses Tesseract: the Nix package has it with Polish and English built in; elsewhere install it — on Linux the packages `tesseract-ocr`, `tesseract-ocr-pol` and `tesseract-ocr-eng`, on Windows the Tesseract installer (UB Mannheim) with Polish — and restart Brulion. Settings → Search tells you if it is missing. More languages: put their `*.traineddata` files into the folder Settings names.
- **Android** — the text of PDFs from Android 15; no text recognition on the phone (it would make the app much bigger).
- **Browser** — the web version doesn't read PDFs or pictures; it opens them in a new tab, where the browser's own viewer searches them.
