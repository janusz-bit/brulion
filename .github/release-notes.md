**Brulion {{VERSION}}** — Markdown notes that work offline: block outlines and a daily journal (like Logseq) on plain `.md` files (like Obsidian), tasks, a calendar, reminders with alarms, calculations and tables. Sync through Syncthing or Nextcloud.

**What's new in {{VERSION}}**

Version 0.13.0 was not published, so its news reaches you only in this release.

* **The “Nocny” look** — a new default: dark and dense, on true black (OLED screens save battery) with a **coral** accent (a darker coral in the light theme); reminders and alarms ⏰ stay amber whatever the accent. Coral is a new accent preset; red and the others stay in *Settings → Appearance*.
* **Built-in fonts** — Inter for the interface, Literata for notes and JetBrains Mono for code and dates, the same on every device and in the browser; any installed font can still be chosen.
* **Side panel in sections** — the panel beside the text folds into sections: for a page **Outline**, **Backlinks**, **Unlinked mentions** and a **Local graph**; for a day of the journal **Day plan** (the day's reminders and dates by the hour), **Tasks** (overdue in red), **A year ago** and **Backlinks**.
* **Side panel on phones** — a small tab on the right edge (with the number of backlinks) or the panel button opens the same panel as a drawer from the right; a tap on the dimmed page, a swipe or Back closes it. Android's back gesture leaves the tab alone.
* **Status bar** — on a computer a thin bar at the bottom shows the sync status, the edited block, the word count and the command palette's key; the tabs have a coral edge on top of the active one.
* **The journals folder in All pages** — the journals folder (a journal's breadcrumb leads there) always lists its daily notes, in date order.

From version 0.12.1: subtasks under their task and folding them, the documentation in English, ☰ always first on phones, smoother scrolling and the tray calendar with one click on Linux. From version 0.11.2: the interface compiled to native code. From version 0.10.3: an intro on the first start, reminders from other devices checked in the background on Android, the view bar and the **Search or type a command…** field at the top, the new **More** menu, Brulion's own F-Droid repository and the browser version in a single file. From version 0.9.1: tabs, split view and separate windows, spell checking, link preview, weekly and monthly notes, searching the journal across many days, habits, task dependencies and time tracking, sorting, filtering and CSV in tables, charts, printing and exporting a page to PDF and Word without Pandoc, text selection, the outline and links in the PDF viewer, a page icon and cover, coloured highlights, statistics and focus mode, custom keyboard shortcuts and new drawing tools. From version 0.8.1: Mermaid diagrams, automatic backups, find and replace (in a note and in the whole notebook), clickable parts of a nested tag and deleting with undo. From version 0.7.0: more shapes, handles to resize and rotate and the **Style** panel in drawings, inserting and moving table rows and columns, **Clean up** (notes and attachments that nothing leads to) and a new-version notice on Android and Windows. From version 0.6.3: a custom drawing background colour. From version 0.6.2: any file as a link (`/File…`). From version 0.6.1: PDF preview and a PDF viewer in the app, clickable parts of a link to a page in a folder. From version 0.6.0: a refreshed look, editing page properties, a dark background, shapes, text and lasso in drawings, drawing on slides and PDFs, Obsidian drawings (`*.excalidraw.md`), merging drawing conflicts, "Open with → Brulion" on Windows and Linux, a faster and lighter index, faster undo, a smooth graph, a smaller app. From version 0.5.0: drawings in notes, Android widgets, "Share → Brulion", **Done** in a reminder's notification, native notifications, autostart and a tray calendar on Windows, appearance settings, menus on date tokens and links, import from Microsoft To Do. From version 0.4.5: pictures from the internet in notes and the first (test) version for Windows. From version 0.4.4: export for Pandoc (one `.md` document of a notebook, a folder or a page — for Word, LibreOffice, PDF and EPUB). From version 0.4.2: changing a date from a menu (right-click or long-press a date token). From version 0.4.1: the "Select text" view removed (copy by selecting whole blocks). From version 0.4.0: moving and creating folders (with links updated and folders synced to Nextcloud), tags as links to pages, copying links to bullets and headings, selecting and copying groups of blocks, a toolbar above the keyboard on phones, and the `brulion-cli` command line and an MCP server for AI agents. From version 0.3.7: the list of all changes in the documentation. From version 0.3.6: choosing a task's status from a menu and with the "/" command, emoji in the browser instead of empty squares, and security and stability fixes in formulas, code blocks, the documentation, Nextcloud and images. From version 0.3.5: picking the time on a round clock face. From version 0.3.4: images and attachments shown as pictures. From version 0.3.3: math formulas drawn right in notes. From version 0.3.2: switching notebooks with one click. From version 0.3.1: Nextcloud sync tied to a notebook. From version 0.3.0: documentation written in Brulion and quick notebook switching.

**Highlights**

* Two note modes: an **outline** (indented blocks, folding, zooming in — like Logseq) and a **document** (continuous text with a live preview — like Obsidian). You choose the mode when you create a page and can change it any time; it is stored in the note's frontmatter (`brulion: document`).
* A journal page for every day, `[[…]]` links, tags, block references `((…))`, backlinks, a graph, search (diacritics optional) with replace, version history, a trash and automatic backups.
* Tasks with due dates, a calendar (month, week, agenda), reminders ⏰ and alarms ⏰! — on Android they work with the app closed and after the phone restarts. Dates can be typed in plain English or Polish ("tomorrow 9:00", "next friday 6pm", "jutro o 9").
* Calculations in `calc` blocks (units, currencies, percentages) and tables with formulas.
* Sync: Syncthing (a plain folder, conflict copies merged) or Nextcloud/WebDAV (in the background, three-way merge, a guard against mass deletion).
* Import from Obsidian, Logseq, Notion, Evernote, Google Keep, Microsoft To Do, Joplin, Simplenote, Roam, Bear, Trilium, Markdown, HTML and iCalendar; export to 12 formats (including PDF, Word and a document for Pandoc) and printing.
* English and Polish interface, dark and light themes, your own accent colour and fonts.
* Drawings in notes: a pen with stylus pressure, shapes, text, a dark background, drawing on slides and PDFs; SVG files that Obsidian shows and excalidraw.com opens for editing.
* Android: home-screen widgets and "Share → Brulion".

| Platform | Download |
|---|---|
| Android 9 or later (arm64) | `brulion-{{FILE_VERSION}}-arm64-v8a.apk` — or the F-Droid repository {{PAGES_URL}} (the same APK, updates through F-Droid) |
| Linux x86_64 | `Brulion-{{FILE_VERSION}}-x86_64.AppImage` — Ubuntu 22.04+, Debian 12+, Fedora 36+ (glibc ≥ 2.35) |
| Linux aarch64 (e.g. Raspberry Pi 4/5 with a 64-bit system) | `Brulion-{{FILE_VERSION}}-aarch64.AppImage` — Ubuntu 24.04+, Debian 13+, Fedora 40+ (glibc ≥ 2.39) |
| Windows 10/11 (x86_64) — test version | `Brulion-{{FILE_VERSION}}-windows-x86_64-setup.exe` (installer) or `brulion-{{FILE_VERSION}}-windows-x86_64.zip` (portable) |
| NixOS | flake `github:{{REPO}}/{{TAG}}`, package `brulion` |
| Command line (Linux, Windows) | `brulion-cli` — in the AppImage as `./Brulion-{{FILE_VERSION}}-x86_64.AppImage cli …`, with Nix `nix run github:{{REPO}}/{{TAG}}#brulion-cli -- …`, on Windows `brulion-cli.exe` next to `brulion.exe` |
| Browser | `brulion-web-{{FILE_VERSION}}.html` — a single file you open from disk — or `brulion-web-{{FILE_VERSION}}.zip` for your own server (any directory, HTTPS) |
| Source code | `brulion-{{FILE_VERSION}}-source.tar.gz` |

**Installation**

* **Android:** download the `.apk` file on your phone and open it (the first time, the system asks you to allow installing apps from that source). A new version installs over the previous one without losing your notes. Or add Brulion's repository in F-Droid ({{PAGES_URL}}: the "Add to F-Droid" button or the QR code) — updates then come through F-Droid.
* **Linux (AppImage):** `chmod +x Brulion-*.AppImage`, then `./Brulion-{{FILE_VERSION}}-x86_64.AppImage` (or `…-aarch64.AppImage`). When the system reports a FUSE error (e.g. in a container): `./Brulion-*.AppImage --appimage-extract-and-run`.
* **Windows (test version):** run `Brulion-{{FILE_VERSION}}-windows-x86_64-setup.exe` — it installs for the current user only, without administrator rights (uninstalling keeps your notes and settings). The app is not signed, so SmartScreen warns: "More info" → "Run anyway". Portable version: unpack `brulion-{{FILE_VERSION}}-windows-x86_64.zip` and run `brulion.exe` from the `Brulion` folder. Reminders and alarms are Windows notifications; the settings can start Brulion when you sign in.
* **NixOS:** `nix run github:{{REPO}}/{{TAG}}` or the flake's package in your system configuration.
* **Browser:** download `brulion-web-{{FILE_VERSION}}.html` and open it in a browser (double-click it or drag it into the window) — it works without a server and without internet, and your notes stay in the browser also after you download a newer version of the file. On your own server (`brulion-web-{{FILE_VERSION}}.zip`) choose "Install app" — it works offline too.

SHA-256 checksums: `SHA256SUMS.txt` (`sha256sum -c SHA256SUMS.txt --ignore-missing`).

<details>
<summary>Po polsku</summary>

**Brulion {{VERSION}}** — notatki w Markdown, które działają offline: konspekty z blokami i dziennik (jak w Logseq) na zwykłych plikach `.md` (jak w Obsidianie), zadania, kalendarz, przypomnienia z alarmami, obliczenia i tabele. Synchronizacja przez Syncthing albo Nextcloud.

**Nowości w {{VERSION}}**

Wersja 0.13.0 nie została opublikowana, więc jej nowości trafiają do Ciebie dopiero w tym wydaniu.

* **Wygląd „Nocny”** — nowy domyślny: ciemny i zwarty, na prawdziwej czerni (ekrany OLED oszczędzają baterię) z **koralowym** akcentem (w jasnym motywie ciemniejszy koral); przypomnienia i alarmy ⏰ zostają bursztynowe przy każdym akcencie. Koralowy to nowy kolor akcentu; czerwony i pozostałe są dalej w *Ustawienia → Wygląd*.
* **Wbudowane czcionki** — Inter w interfejsie, Literata w notatkach i JetBrains Mono w kodzie i datach, takie same na każdym urządzeniu i w przeglądarce; nadal można wybrać dowolną zainstalowaną.
* **Panel boczny w sekcjach** — panel obok tekstu składa się z rozwijanych sekcji: dla strony **Konspekt**, **Odnośniki**, **Wzmianki bez linku** i **Lokalny graf**; dla dnia w dzienniku **Plan dnia** (przypomnienia i daty dnia według godzin), **Zadania** (zaległe na czerwono), **Rok temu** i **Odnośniki**.
* **Panel boczny na telefonie** — mała zakładka przy prawej krawędzi (z liczbą odnośników) albo przycisk panelu otwiera ten sam panel jako szufladę z prawej; dotknięcie przyciemnionej strony, przesunięcie albo Wstecz go zamyka. Gest „wstecz” Androida omija zakładkę.
* **Pasek stanu** — na komputerze cienki pasek na dole pokazuje stan synchronizacji, edytowany blok, liczbę słów i klawisz palety poleceń; aktywna karta ma koralową krawędź u góry.
* **Folder dziennika we Wszystkich stronach** — folder dziennika (prowadzi do niego ścieżka wpisu) zawsze pokazuje swoje notatki dzienne, w kolejności dat.

Z wersji 0.12.1: podzadania pod swoim zadaniem i ich zwijanie, dokumentacja po angielsku, ☰ zawsze pierwszy na telefonie, płynniejsze przewijanie i kalendarz z zasobnika jednym kliknięciem na Linuksie. Z wersji 0.11.2: interfejs kompilowany do kodu maszynowego. Z wersji 0.10.3: wprowadzenie przy pierwszym uruchomieniu, przypomnienia z innych urządzeń sprawdzane w tle na Androidzie, pasek widoków i pole **Szukaj albo wpisz polecenie…** u góry, nowe menu **Więcej**, własne repozytorium F-Droid i wersja w przeglądarce w jednym pliku. Z wersji 0.9.1: karty, podział widoku i osobne okna, sprawdzanie pisowni, podgląd linku, notatki tygodniowe i miesięczne, szukanie w dzienniku przez wiele dni, nawyki, zależności i licznik czasu zadań, sortowanie, filtrowanie i CSV w tabelach, wykresy, druk oraz eksport strony do PDF i Worda bez Pandoca, zaznaczanie tekstu, spis treści i linki w przeglądarce PDF, ikona i okładka strony, kolorowe zakreślenia, statystyki i tryb skupienia, własne skróty klawiszowe i nowe narzędzia w rysunkach. Z wersji 0.8.1: diagramy Mermaid, automatyczne kopie zapasowe, znajdź i zamień (w notatce i w całym notatniku), klikalne części tagu zagnieżdżonego i usuwanie z cofaniem. Z wersji 0.7.0: więcej kształtów, uchwyty do zmiany rozmiaru i obracania oraz panel **Styl** w rysunkach, wstawianie i przenoszenie wierszy i kolumn tabel, **Sprzątanie** (notatki i załączniki, do których nic nie prowadzi) i komunikat o nowej wersji na Androidzie i Windows. Z wersji 0.6.3: własny kolor tła rysunku. Z wersji 0.6.2: dowolny plik jako link (`/Plik…`). Z wersji 0.6.1: podgląd i przeglądarka PDF w aplikacji, klikalne części linku do strony w folderze. Z wersji 0.6.0: odświeżony wygląd, edycja właściwości strony, ciemne tło, kształty, tekst i lasso w rysunkach, rysowanie na slajdach i PDF-ach, rysunki z Obsidiana (`*.excalidraw.md`), scalanie konfliktów rysunków, „Otwórz w Brulionie” na Windows i Linuksie, szybszy i lżejszy indeks, szybsze cofanie, płynny graf, mniejsza aplikacja. Z wersji 0.5.0: rysunki w notatkach, widżety na Androidzie, „Udostępnij → Brulion”, „Zrobione” w powiadomieniu przypomnienia, natywne powiadomienia, autostart i kalendarz z zasobnika na Windows, ustawienia wyglądu, menu na znacznikach i linkach, import z Microsoft To Do. Z wersji 0.4.5: zdjęcia z internetu w notatkach i pierwsza (testowa) wersja na Windows. Z wersji 0.4.4: eksport dla programu Pandoc (jeden dokument `.md` z notatnika, folderu albo strony — do Worda, LibreOffice, PDF i EPUB). Z wersji 0.4.2: zmiana daty z menu (prawy przycisk albo przytrzymanie znacznika daty). Z wersji 0.4.1: usunięty widok „Zaznacz tekst” (kopiuj, zaznaczając całe bloki). Z wersji 0.4.0: przenoszenie i tworzenie folderów (z poprawianiem linków i synchronizacją folderów w Nextcloud), tagi jako linki do stron, kopiowanie odnośników do punktów i nagłówków, zaznaczanie i kopiowanie grup bloków, pasek narzędzi przy klawiaturze na telefonie oraz wiersz poleceń `brulion-cli` i serwer MCP dla agentów AI. Z wersji 0.3.7: lista wszystkich zmian w dokumentacji. Z wersji 0.3.6: wybór statusu zadania z menu i poleceniem „/”, emoji w przeglądarce zamiast pustych kwadratów oraz poprawki bezpieczeństwa i stabilności wzorów, bloków kodu, dokumentacji, Nextcloud i obrazów. Z wersji 0.3.5: wybór godziny na okrągłym zegarze. Z wersji 0.3.4: obrazy i załączniki pokazywane jako zdjęcia. Z wersji 0.3.3: wzory matematyczne rysowane od razu w notatkach. Z wersji 0.3.2: przełączanie notatników jednym kliknięciem. Z wersji 0.3.1: synchronizacja Nextcloud przypisana do notatnika. Z wersji 0.3.0: dokumentacja napisana w Brulionie i szybkie przełączanie notatników.

**Najważniejsze**

* Dwa tryby notatki: **konspekt** (bloki z wcięciami, zwijanie, przybliżanie — jak w Logseq) i **dokument** (ciągły tekst z podglądem na żywo — jak w Obsidianie). Tryb wybierasz przy tworzeniu strony i możesz go zmienić w każdej chwili; zapisuje się w nagłówku notatki (`brulion: document`).
* Dziennik na każdy dzień, linki `[[…]]`, tagi, odwołania do bloków `((…))`, odnośniki zwrotne, graf, wyszukiwarka (także bez polskich znaków) z zamianą, historia wersji, kosz i automatyczne kopie zapasowe.
* Zadania z terminami, kalendarz (miesiąc, tydzień, agenda), przypomnienia ⏰ i alarmy ⏰! — na Androidzie działają przy zamkniętej aplikacji i po restarcie telefonu. Daty można wpisywać po polsku („jutro o 9”, „w piątek 18:00”).
* Obliczenia w blokach `calc` (jednostki, waluty, procenty) i tabele z formułami.
* Synchronizacja: Syncthing (zwykły folder, scalanie kopii konfliktów) albo Nextcloud/WebDAV (w tle, scalanie trójstronne, ochrona przed masowym usuwaniem).
* Import z Obsidiana, Logseq, Notion, Evernote, Google Keep, Microsoft To Do, Joplin, Simplenote, Roam, Bear, Trilium, Markdown, HTML i iCalendar; eksport do 12 formatów (m.in. PDF, Word i dokument dla programu Pandoc) oraz druk.
* Interfejs po angielsku i po polsku, ciemny i jasny motyw, własny kolor akcentu i czcionki.
* Rysunki w notatkach: pióro z naciskiem rysika, kształty, tekst, ciemne tło, rysowanie na slajdach i PDF-ach; pliki SVG, które Obsidian pokazuje, a excalidraw.com otwiera do edycji.
* Android: widżety na ekranie głównym i „Udostępnij → Brulion”.

| Platforma | Do pobrania |
|---|---|
| Android 9 lub nowszy (arm64) | `brulion-{{FILE_VERSION}}-arm64-v8a.apk` — albo repozytorium F-Droid {{PAGES_URL}} (ten sam APK, aktualizacje przez F-Droid) |
| Linux x86_64 | `Brulion-{{FILE_VERSION}}-x86_64.AppImage` — Ubuntu 22.04+, Debian 12+, Fedora 36+ (glibc ≥ 2.35) |
| Linux aarch64 (np. Raspberry Pi 4/5 z 64-bitowym systemem) | `Brulion-{{FILE_VERSION}}-aarch64.AppImage` — Ubuntu 24.04+, Debian 13+, Fedora 40+ (glibc ≥ 2.39) |
| Windows 10/11 (x86_64) — wersja testowa | `Brulion-{{FILE_VERSION}}-windows-x86_64-setup.exe` (instalator) albo `brulion-{{FILE_VERSION}}-windows-x86_64.zip` (wersja przenośna) |
| NixOS | flake `github:{{REPO}}/{{TAG}}`, pakiet `brulion` |
| Wiersz poleceń (Linux, Windows) | `brulion-cli` — w AppImage jako `./Brulion-{{FILE_VERSION}}-x86_64.AppImage cli …`, w Nix `nix run github:{{REPO}}/{{TAG}}#brulion-cli -- …`, w Windows `brulion-cli.exe` obok `brulion.exe` |
| Przeglądarka | `brulion-web-{{FILE_VERSION}}.html` — jeden plik, otwierasz go z dysku — albo `brulion-web-{{FILE_VERSION}}.zip` na własny serwer (dowolny katalog, HTTPS) |
| Kod źródłowy | `brulion-{{FILE_VERSION}}-source.tar.gz` |

**Instalacja**

* **Android:** pobierz plik `.apk` na telefonie i otwórz go (pierwszy raz system poprosi o zgodę na instalowanie aplikacji z tego źródła). Nowa wersja instaluje się na poprzednią bez utraty notatek. Albo dodaj w F-Droid repozytorium Brulionu ({{PAGES_URL}}: przycisk „Add to F-Droid” albo kod QR) — wtedy aktualizacje przychodzą przez F-Droid.
* **Linux (AppImage):** `chmod +x Brulion-*.AppImage`, a potem `./Brulion-{{FILE_VERSION}}-x86_64.AppImage` (albo `…-aarch64.AppImage`). Gdy system zgłasza błąd FUSE (np. w kontenerze): `./Brulion-*.AppImage --appimage-extract-and-run`.
* **Windows (wersja testowa):** uruchom `Brulion-{{FILE_VERSION}}-windows-x86_64-setup.exe` — instaluje się tylko dla bieżącego użytkownika, bez uprawnień administratora (odinstalowanie zostawia notatki i ustawienia). Aplikacja nie jest podpisana, więc SmartScreen ostrzega: „Więcej informacji” → „Uruchom mimo to”. Wersja przenośna: rozpakuj `brulion-{{FILE_VERSION}}-windows-x86_64.zip` i uruchom `brulion.exe` z folderu `Brulion`. Przypomnienia i alarmy są powiadomieniami Windows; w ustawieniach włączysz uruchamianie po zalogowaniu.
* **NixOS:** `nix run github:{{REPO}}/{{TAG}}` albo pakiet z flake'a w konfiguracji systemu.
* **Przeglądarka:** pobierz `brulion-web-{{FILE_VERSION}}.html` i otwórz go w przeglądarce (dwuklik albo przeciągnięcie do okna) — działa bez serwera i bez internetu, notatki zostają w przeglądarce także po pobraniu nowszej wersji pliku. Na własnym serwerze (`brulion-web-{{FILE_VERSION}}.zip`) wybierz „Zainstaluj aplikację” — działa też offline.

Sumy kontrolne SHA-256: `SHA256SUMS.txt` (`sha256sum -c SHA256SUMS.txt --ignore-missing`).

</details>
