# Wydajność Brulionu

Stan na 2026-10-18: obecny kod (gałąź `main` z pakietem optymalizacji z października 2026)
porównany z wydaniem **v0.1.1** (`f61a2a4`) i z **d1141c0** (ostatni commit przed
optymalizacjami). Wszystkie trzy wersje zmierzono w tych samych warunkach i tymi samymi
benchmarkami; jak je uruchomić — w części dla współtwórców (po angielsku, niżej). Uzupełnione
2026-10-29 o falę 5: pamięć indeksu, indeksowanie na kilku rdzeniach i gotowy indeks zapisany na
urządzeniu (porównanie z `82d0357`), a 2026-10-30 o szybsze cofanie (L5, porównanie z `b3d9a3d`),
mniejsze pakiety (L9, porównanie z `ef90077`) i szybsze odświeżanie list po zapisie (L3,
porównanie z `ef90077`).

## Podsumowanie dla użytkowników

### Co się poprawiło

* **Mniejsza aplikacja** (2026-10-30). APK na Androida ma 76,9 zamiast 91,8 MiB (do pobrania
  26,3 zamiast 30,9 MB), a wersja przeglądarkowa pobiera przy pierwszej wizycie 13,5 zamiast
  15,8 MB — przy łączu 20 Mbit/s pierwsza klatka jest o ok. 1,1 s wcześniej (7,4 zamiast 8,5 s).
  Pakiety nie zawierają już pięciu nieużywanych stylów kontrolek Qt i zastępczego okna plików Qt
  Quick: okna wyboru plików są systemowe. Wersja na Windows: ZIP 42,8 zamiast 49,5 MB,
  instalator 29,5 zamiast 32,6 MB; AppImage 53 zamiast 58 MiB.
* **Odświeżanie po zapisie w dużych notatnikach** (2026-10-30). Po każdym zapisie Zadania,
  Kalendarz, przypomnienia, bloki zapytań i widżety przeliczają tylko zmienioną notatkę zamiast
  całego notatnika. W notatniku z 10 000 notatek cała ta praca trwa na komputerze ok. 6 ms zamiast
  ok. 112 ms (prawie 19 razy krócej), więc pisanie przy otwartej liście zadań albo kalendarzu nie
  zacina się przy zapisie. Wyniki są dokładnie takie jak wcześniej (sprawdzają to testy z losowymi
  zmianami). Zapytania są przy okazji o połowę szybsze także wtedy, gdy liczą się od nowa, a
  Kalendarz i Agenda otwierają się o kilka milisekund szybciej. Na telefonie jeszcze tego nie
  zmierzyliśmy (przedtem ok. 1 s).
* **Cofanie i ponawianie na długich stronach** (2026-10-30). Ctrl+Z i Ctrl+Shift+Z nie wczytują
  już całej strony od nowa, tylko zmieniają bloki, których dotyczy krok. Na stronie z 5000 bloków
  na komputerze cofnięcie widać po ok. 13 ms zamiast ok. 33 ms — tyle co po Enterze — a sama jego
  obsługa trwa 3–4 ms zamiast ok. 25 ms. Zwinięte bloki, przybliżenie i otwarty edytor zostają po
  cofnięciu na miejscu, także gdy cofnięcie zmienia ich tekst. Przy okazji pisanie na takiej
  stronie jest odrobinę szybsze (obsługa znaku 4,1–4,2 zamiast 4,7–5,0 ms).
* **Duże notatniki otwierają się dwa razy szybciej i zajmują o jedną trzecią mniej pamięci**
  (2026-10-29). Notatki indeksują się na kilku rdzeniach naraz, a gotowy indeks zostaje zapisany
  na urządzeniu, więc przy kolejnym otwarciu Brulion analizuje od nowa tylko notatki zmienione
  od ostatniego razu (pliki nadal czyta, żeby to sprawdzić). Notatnik z 10 000 notatek na
  komputerze jest w pełni zaindeksowany po 0,96 s przy pierwszym otwarciu i po 0,83 s przy
  kolejnych (wcześniej ok. 1,8–1,9 s), a sam indeks zajmuje 60 % mniej pamięci (cała aplikacja
  z tym notatnikiem: 340 zamiast 510 MB).
  Na komputerze i telefonie wyszukiwanie jest kompletne od razu, gdy indeks jest gotowy — nie
  dobudowuje się już w tle z krótką przerwą w działaniu interfejsu; w przeglądarce tak jest przy
  kolejnych wizytach (z zapisanego indeksu), a przy pierwszej dobudowuje się po kawałku jak dotąd.
  W przeglądarce pierwsze wczytanie bez zapisanego indeksu trwa nieco dłużej niż wcześniej (1000
  notatek: 1,3 zamiast 1,1 s — dziennik wczytuje się jako pierwszy, a widok odświeża się w
  trakcie), kolejne
  tyle co dawniej, ale już z kompletnym wyszukiwaniem.
* **Duże notatki nie zamrażają już aplikacji.** Po starcie i po każdym autozapisie bardzo dużej
  notatki (od ok. 200 KB, np. długi dziennik w jednym pliku albo zaimportowana książka) Brulion
  przestawał reagować: na komputerze na 0,9–1,6 s, na słabszym telefonie nawet na 13–20 s.
  Teraz najdłuższa przerwa to ok. 0,09 s na komputerze i 0,2 s na telefonie.
* **Szybszy start.** Okno pojawia się wcześniej (notatnik z 10 000 notatek: na komputerze
  0,45 → 0,21 s, na telefonie 2,3 → 0,5 s), treść dziennika z dużym notatnikiem też (komputer
  0,51 → 0,40 s, telefon 2,4 → 1,6 s). Zamrożenie zaraz po wczytaniu dużego notatnika
  (komputer 1,6 s, telefon do 19 s) zniknęło.
* **Pisanie na bardzo długich stronach** (tysiące bloków): na telefonie znak pojawia się po
  ok. 30–40 ms zamiast 70–90 ms (Enter 80 → 38 ms), na komputerze obsługa klawisza kosztuje
  o jedną trzecią mniej.
* **Widoki:** Zadania otwierają się dwa razy szybciej (komputer 0,26 → 0,13 s, telefon 1,1 →
  0,7 s), Kalendarz i Agenda o 10–30 % szybciej. **Graf na telefonie** układa się w 2,6 s
  zamiast 44 s.
* **Import** długich list z HTML (np. eksport Evernote/Notion) — 20 000 punktów w 0,2 s zamiast
  13 s. Podpowiedzi bloków `((` są dwa razy szybsze, podpowiedzi stron `[[` o ok. jedną trzecią.
* **Android:** synchronizacja z Nextcloud przez HTTPS wreszcie działa (wcześniej każde połączenie
  HTTPS kończyło się błędem „TLS initialization failed”). APK jest mniejszy mimo dołączonego
  OpenSSL: 87,1 → 84,9 MB (do pobrania ok. 28,5 MB).
* **Przeglądarka:** notatnik dokańcza indeksowanie bez ruszania myszą (wcześniej stał, dopóki
  nie było żadnego zdarzenia), powroty na stronę są szybsze (przeglądarka zachowuje
  skompilowany kod), plik aplikacji jest o 0,5 MB mniejszy.
* **Przenoszenie i zmiana nazwy** (2026-10-23): plan, które linki, tagi i zapytania trzeba
  poprawić, liczy się 4–12 razy szybciej (folder 500 stron w notatniku 5000 notatek: przeniesienie
  0,12 → 0,03 s, zmiana nazwy 0,49 → 0,11 s; strona z setkami linków do niej 0,11 → 0,01 s).
  Przeciąganie strony lub folderu w drzewie już nie przycina się nad kolejnymi folderami:
  podpowiedź „Zaktualizowane linki: …” liczy się po kawałku, w tle między klatkami.
  Otwarcie strony, którą nazywa popularny tag, jest do 2 razy szybsze.
* **Kopiowanie bloków na komputerze z Linuksem** (2026-10-27): Ctrl+C na całej stronie z 5000 bloków
  zajmuje ok. 4 ms zamiast ok. 40 ms (do odświeżenia ekranu 13–15 zamiast 46–50 ms). Wersja
  sformatowana dla poczty i edytorów dokumentów powstaje dopiero przy wklejeniu do nich.

### Jak jest teraz

**Na komputerze** (laptop z 2023 r., notatnik 10 000 notatek): okno po 0,2 s, treść po 0,4 s,
cały notatnik zaindeksowany po ok. 1 s, przy kolejnym otwarciu po 0,8 s (w tym czasie można już
czytać i pisać). Strona z 5000
bloków otwiera się w ok. 0,1 s, wyszukiwanie zwraca wyniki w kilka–kilkanaście milisekund,
Zadania/Kalendarz/Agenda otwierają się w 0,13–0,17 s. Bezczynny Brulion zużywa ok. 0,1 %
procesora i nie rysuje klatek.

**Na telefonie** (mierzone na Raspberry Pi 4, który odpowiada słabszemu telefonowi, i na
emulatorze Androida): zwykłe notatniki działają płynnie — start z przykładowym notatnikiem ok.
1,3 s, pisanie na zwykłej stronie bez opóźnień. Duży notatnik (10 000 notatek) pokazuje treść po
ok. 1,6 s, ale indeksował się ok. 15 s; od fali 5 robi to na kilku rdzeniach, a przy kolejnym
otwarciu bierze gotowy indeks z urządzenia (nowego czasu na Raspberry Pi i telefonie jeszcze nie
zmierzyliśmy).

**W przeglądarce**: pierwsza wizyta pobiera ok. 13,5 MB (później strona działa z pamięci
podręcznej i offline), powrót do strony z 1000 notatek rysuje pierwszą klatkę po ok. 0,6 s.

### Znane wolne miejsca

* **Indeksowanie dużych notatników na telefonie** — było ok. 15 s dla 10 000 notatek; teraz
  notatki parsują się na kilku rdzeniach, a łączenie ich w indeks (w wątku interfejsu, po
  kawałku) zostało najdłuższą częścią — nowego czasu na telefonie jeszcze nie zmierzyliśmy.
  Pierwsze otwarcie po aktualizacji Brulionu jest zawsze „na zimno” (gotowy indeks z poprzedniej
  wersji nie jest używany).
* **Pamięć** — notatnik 10 000 notatek zajmuje na komputerze ok. 340 MB (wcześniej ok.
  500 MB). Na telefonach z 3–4 GB RAM system może zamykać Brulion w tle (notatki są zawsze
  zapisane na dysku).
* **Kalendarz i Agenda** otwierają się na telefonie przy dużym notatniku po 1–1,3 s.
  (Cofanie na bardzo długiej stronie trwało na telefonie ok. 0,2 s; od 2026-10-30 kosztuje tyle co
  Enter — na komputerze zmierzone, na telefonie jeszcze nie.)
* **Pierwszy start na komputerze z małym notatnikiem** (świeży profil) pokazuje treść ok.
  0,09 s później niż d1141c0 (okno za to 0,1 s wcześniej); przy kolejnych startach treść jest
  tak szybko jak wcześniej (szczegóły niżej).
* **Rozmiar** — APK ma ok. 81 MB (pobranie ok. 26 MB), wersja przeglądarkowa pobiera przy
  pierwszej wizycie ok. 13,5 MB (pierwsza klatka przy łączu 20 Mbit/s po ok. 7,4 s).

**Bateria:** w tle na Androidzie Brulion nie zużywa procesora i nie wybudza telefonu; alarmy są
ustawiane tylko dla przypomnień (do 64 na najbliższe 14 dni).

---

## Performance (for contributors)

### Reference machines and method

| | machine | how |
|---|---|---|
| desktop | x86_64 laptop, Intel i5-13450HX (6 P + 4 E cores), 31 GB, NixOS | core: Release, pinned to the P-cores (`-c 0-11`), quiet-wait (`-q 3`), 5 repetitions; app: Release `-O3 -g`, Xvfb + Mesa llvmpipe (frames are rendered on the CPU — pessimistic), 5 runs (3 for first starts) |
| phone floor | Raspberry Pi 4, Cortex-A72 at 1.8 GHz, 3.7 GB | core: Release, small profile, 5 repetitions (2 rounds); app: Release, `offscreen` + Qt Quick software renderer (painting happens on the GUI thread, so stalls include it), 3 runs, 3 000-note vault (10 000 where noted) |
| Android | emulator API 36 x86_64 (4 vCPU under KVM, SwiftShader GL) on the laptop | release APKs from the launcher (`am start -W`, `dumpsys meminfo`); a debuggable measurement copy with the probe for first frame / content; optimistic — x86 vCPUs are 2–4× faster than phone cores |
| web | headless Chromium on the laptop, SwiftShader WebGL | `wasm-bench.mjs`: repeat visits with 1 000 notes in IndexedDB, 4× CPU throttling as the phone stand-in, 20 Mbit/s first visits |

Trees: **v0.1.1** = `f61a2a4`; **d1141c0** = the commit before the optimisations (its core and
app code equal `7779c04`'s; Pi and mobile binaries of `7779c04` stand for it); **current** = the
working tree of 2026-10-18 (`8a8b742` + the uncommitted N11, N13, S1 and Android fixes). All three
were built with the same toolchain (the flake lock is unchanged since v0.1.1) and measured with
the same `tests/bench` sources (same source hash). Medians; min–max or spread where it matters.

### Results: v0.1.1 → d1141c0 → current

The optimisations between d1141c0 and the current code (N-numbers below): N1 linear diacritic
folding (`text::foldKey`); N2 cheaper keystrokes on pages with tables and calculations; N3 the
vault opens after the first frame and watches folders, not files; N4 Tasks and Calendar built
once per creation; N5 time-boxed graph layout; N6 regex matching from an offset without
re-checking the whole subject; N7 faster block/page suggestions and name sorting; N8 no QML
file selector; N9 LocalStore skips unchanged files on flush; N10 long HTML lists split before
`QTextDocument`; N11 smaller APK and web build (R8, no QML debugging plugins, no developer QML);
N12 benchmark hygiene and these guards; N13 the web index timer (a repeating 0 ms `QTimer`
stalled every Qt timer in the single-threaded WebAssembly build) and the wasm code cache; S1
OpenSSL in the APK.

**Desktop app** (laptop, 10 000-note vault unless noted; `tests/bench/app`):

| scenario | v0.1.1 | d1141c0 | current | current vs d1141c0 |
|---|---:|---:|---:|---:|
| start, sample notebook (warm): first frame | 593 ms | 320 ms | 215 ms | −33 % |
| start, sample notebook (warm): content | 1.49 s | 385 ms | 392 ms | +2 % (see below) |
| start, sample notebook (cold): content | 2.03 s | 888 ms | 848 ms | −4 % |
| start, 10 000 notes (warm): first frame | 728 ms | 452 ms | 209 ms | −54 % |
| start, 10 000 notes (warm): content | 1.14 s | 508 ms | 403 ms | −21 % |
| start, 10 000 notes (first start): content | 1.29 s | 669 ms | 564 ms | −16 % |
| start, 10 000 notes (cold): content | 1.68 s | 1.03 s | 943 ms | −9 % |
| start, 10 000 notes: index ready | 5.53 s | 1.98 s | 1.93 s | −2 % |
| start, 10 000 notes: longest GUI stall after ready | 3.08 s | 1.56 s | 59 ms | −96 % |
| start, 10 000 notes: settled | 11.69 s | 5.23 s | 2.01 s | −62 % |
| heap after start, 10 000 notes | 573 MB | 498 MB | 495 MB | −1 % |
| open the 5000-block outline: frame / settled | 1.44 / 2.05 s | 117 / 283 ms | 113 / 262 ms | −4 / −7 % |
| typing, small page: key → frame | 12.7 ms | 12.7 ms | 12.7 ms | 0 |
| typing, small page: autosave, GUI blocked | 67 ms | 75 ms | 71 ms | −5 % |
| typing, 5000-block outline: key handling | 16.1 ms | 15.4 ms | 9.8 ms | −36 % |
| typing, 5000-block outline: key → frame p95 | 31.1 ms | 26.3 ms | 23.3 ms | −11 % |
| typing, 5000-block outline: Enter / undo → frame | 73 / 98 ms | 24 / 46 ms | 25 / 46 ms | 0 |
| typing, 500 KB page: autosave, longest stall (outline / document) | 0.90 / 1.56 s | 0.91 / 1.56 s | 88 / 88 ms | −90 / −94 % |
| search: key → results (incl. the 180 ms debounce) | 438 ms | 211 ms | 209 ms | −1 % |
| quick switcher: key → frame | 34.6 ms | 33.6 ms | 31.7 ms | −6 % |
| Tasks / Calendar / Agenda, first open: frame | 1.49 s / 805 / 1037 ms | 260 / 201 / 216 ms | 133 / 166 / 158 ms | −49 / −17 / −27 % |
| Graph: settled | 0.94 s | 0.79 s | 0.77 s | −2 % |
| idle: CPU / wakeups per s | 0.2 % / 10.0 | 0.1 % / 9.9 | 0.1 % / 9.9 | 0 |

One regression showed up and is fixed: after N3 the first block of a small vault appeared
~120 ms later (bisection 8f58f94 → 8a8b742, demo warm start: content 355 → 511 ms, first frame
290 → 210 ms; the GUI thread idle for ~116 ms between "index ready" and the journal's first
block). Cause: the vault now opens after the window's first frame, so the shell and its lists
exist before the index is complete; the journal's first refresh saw no notes, and the refresh
after the last index chunk waited for the lists' 120 ms debounce (`WorkspaceListModel`).
The lists now refresh on the next event-loop turn when the index becomes complete
(`tst_content_models` `journalsShowTheCompleteIndexAtOnce`). Re-measured in one session (laptop,
Xvfb, sample notebook, 5 runs; d1141c0 / before the fix / now; content = the frame showing the
first block): warm start content 384 / 506 / 392 ms with the first frame at 322 / 210 / 203 ms;
first start content 540 / 661 / 632 ms (first frame 478 / 369 / 368 ms); cold start content
898 / 825 / 829 ms. 10 000 notes: unchanged (content 396 / 400 ms, index ready 1.93 / 1.92 s).
Left: a first start (fresh home) shows the content ~90 ms later than d1141c0 (not
investigated). Under Xvfb the splash spinner animates unthrottled (no vsync) once the first
frame is up; a test build with the spinner stopped showed the warm-start content ~40 ms
sooner, so part of what is left is this measuring setup. The Pi showed no regression (content
1263 ms before N3, 1255 ms after).

**Core** (laptop, `scripts/bench.sh`, 9 suites): 365 rows compared. d1141c0 vs v0.1.1: no row
changed by more than max(10 %, noise) (geometric mean 1.004). Current vs d1141c0: **53 rows
faster, 0 slower**, geometric mean 0.766. Selected rows:

| operation | v0.1.1 | d1141c0 | current | change |
|---|---:|---:|---:|---:|
| open vault 10 000 notes (scan + read + index) | 1.26 s | 1.26 s | 1.26 s | 0 |
| first search 10 000 notes (full-text index build) | 1.07 s | 1.07 s | 557 ms | −48 % |
| search 10 000 notes, five words | 7.5 ms | 7.5 ms | 7.5 ms | 0 |
| save of a 1 MB note: search re-index | 2.54 s | 2.54 s | 45 ms | 57× |
| `foldKey` of 1 M Polish characters | 3.10 s | 3.10 s | 1.36 ms | 2283× |
| query `tag:praca` / all tasks sorted (10 000 notes) | 95 / 119 ms | 95 / 119 ms | 97 / 71 ms | +2 / −40 % |
| `((` block suggestions / `[[` page suggestions (10 000 notes) | 40 / 4.2 ms | 40 / 4.2 ms | 20.5 / 2.8 ms | −49 / −32 % |
| unlinked mentions on one 20 000-mention line | 732 ms | 732 ms | 4.3 ms | 172× |
| after one save: every consumer of `VaultIndex::changed` (10 000 notes) | 129 ms | 130 ms | 131 ms | 0 |
| keystroke in a 5000-block page (`BlockDocument`) | 1.31 ms | 1.33 ms | 1.33 ms | 0 |
| parse 20 000 × raw inline HTML in one paragraph | 2.04 s | 2.04 s | 3.0 ms | 681× |
| HTML import: a list of 20 000 items | 12.73 s | 12.75 s | 202 ms | 63× |
| Evernote note with 1000 code blocks | 75 ms | 76 ms | 34 ms | −55 % |
| LocalStore flush with 10 000 synced files | 311 ms | 313 ms | 99 ms | −68 % |
| watcher: 1000 files modified behind the app | 148 ms | 148 ms | 72 ms | −51 % |
| sync without changes, 10 000 files | 420 ms | 420 ms | 419 ms | 0 |
| index heap, 10 000 notes (22.9 MB of Markdown) | 545 MiB | 545 MiB | 545 MiB | 0 |

Scaling (`bench.sh scaling`): d1141c0 has 13 steps with k ≥ 1.6 (quadratic: `foldKey`, big-note
re-index, raw inline HTML, `<` without an autolink, unlinked mentions on a long line, HTML lists);
current has one, `calc` `evaluateLongBlock` (k 1.68, a `sum` per line bounded by the work budget,
known and accepted). The 10 000 → 50 000-note steps of search and suggestions are k 1.3–1.6
(cache effects of a 5× bigger working set, unchanged).

**Raspberry Pi 4** (app: 3 000-note vault unless noted; core: `perf-check.sh` rows, small
profile, 2 rounds × 5 repetitions; v0.1.1's core equals d1141c0's except the importers):

| scenario | v0.1.1 | d1141c0 | current | change |
|---|---:|---:|---:|---:|
| start, sample notebook (warm): first frame / content | 1.28 / 2.15 s | 1.21 / 1.46 s | 0.49 / 1.25 s | −60 / −14 % |
| start, sample notebook (cold): content | 2.75 s | 1.98 s | 1.75 s | −12 % |
| start, 10 000 notes (warm): first frame / content | 2.35 / 3.08 s | 2.30 / 2.42 s | 0.48 / 1.57 s | −79 / −35 % |
| start, 10 000 notes: index ready | 53.9 s | 15.4 s | 15.0 s | −2 % |
| start, 10 000 notes: longest stall while indexing / after ready | 4.15 / 11.4 s | 0.59 / 19.2 s | 0.54 / 0.45 s | −9 / −98 % |
| start, 10 000 notes: settled | 74.2 s | 48.2 s | 16.2 s | −66 % |
| open the 5000-block outline: frame / settled | 2.08 / 3.02 s | 712 ms / 1.34 s | 653 ms / 1.22 s | −8 / −9 % |
| typing, small page: key → frame / autosave blocked | 9.7 / 88 ms | 9.1 / 90 ms | 8.8 / 76 ms | −3 / −15 % |
| typing, 5000-block outline: key handling / key → frame p95 | 75 / 91 ms | 70 / 93 ms | 30 / 42 ms | −57 / −55 % |
| typing, 5000-block outline: Enter / undo → frame | 118 / 296 ms | 80 / 242 ms | 38 / 214 ms | −53 / −11 % |
| typing, 500 KB page: autosave, longest stall (outline / document) | 13.5 / 17.9 s | 13.2 / 19.8 s | 0.20 / 0.20 s | −98 / −99 % |
| Tasks / Calendar / Agenda, first open: frame | 1.69 / 1.92 / 1.79 s | 1.11 / 1.42 / 1.26 s | 0.73 / 1.28 / 1.04 s | −34 / −10 / −17 % |
| Graph: settled | 45.1 s | 44.0 s | 2.6 s | −94 % |
| heap after start, 10 000 notes | 475 MB | 416 MB | 434 MB | +4 % (when the search index finishes; at "ready" 373 → 375 MB) |
| core: first search, 10 000 notes | – | 5.39 / 5.43 s | 2.55 / 2.49 s | −53 % |
| core: `((` suggestions / `[[` suggestions, 10 000 notes | – | 161 / 34 ms | 80 / 28 ms | −50 / −18 % |
| core: save of a 200 KB note, search re-index | – | 1.61 s | 53 ms | 30× |
| core: `foldKey` of 300 K Polish characters | – | 8.37–8.74 s | 1.69 ms | ~5000× |
| core: raw inline HTML × 10 000 / HTML list of 5000 items | – | 2.39 s / 1.93 s | 8 / 279 ms | 300× / 7× |
| core: LocalStore flush, 10 000 synced files | – | 2.09 s | 0.95 s | −54 % |
| core: open vault 10 000 notes / after one save, all consumers | – | 8.3 s / 0.94 s | 8.4 s / 0.95 s | 0 |
| core: query `tag:praca` / all tasks sorted, 10 000 notes (3 rounds × 5) | – | 0.84–1.24 s / 817 ms | 0.97–1.27 s / 564 ms | noise (±25 %) / −31 % |

**Android** (sizes: arm64 release APKs; timings: the emulator above, x86_64 APKs, 5 launcher starts
per kind after a discarded one, PSS 10 s / 25 s after the start; probe = the debuggable
measurement copy with the benchmark probe; 5 000-note vault; d1141c0 = the `7779c04` APKs):

| | v0.1.1 | d1141c0 | current | change |
|---|---:|---:|---:|---:|
| APK arm64, `scripts/android-build.sh` (signed) | 86.40 MB | 87.05 MB | 84.94 MB (with OpenSSL, 4.67 MB) | −2.4 % |
| APK arm64 download (every entry deflated) | 28.49 MB | 28.74 MB | 28.49 MB | −0.9 % |
| APK arm64, `nix build .#brulion-android` (unsigned) | – | 87.03 MB | 84.92 MB | −2.4 % |
| APK x86_64, script / flake | 88.67 MB / – | 89.38 / 89.35 MB | 88.38 / 88.36 MB | −1.1 % |
| entries in the APK | 171 | 171 | 161 | |
| launcher start (`am start -W` TotalTime), sample notebook, warm / cold / hot | 315 / 453 / 29 ms | 313 / 464 / 48 ms | 313 / 465 / 31 ms | 0 (stops at Android's first frame) |
| launcher start, 5 000 notes, warm / cold | 277 / 454 ms | 281 / 460 ms | 313 / 458 ms | ranges overlap (266–315 vs 271–358 ms) |
| PSS after start, sample notebook / 5 000 notes | 234 / 517 MB | 161 / 386 MB | 156 / 359 MB | −3 / −7 % |
| PSS with the 5000-block outline open (5 000 notes) | 492 MB | 413 MB | 409 MB | −1 % |
| CPU time of the first 25 s, 5 000 notes | 6.44 s | 5.48 s | 3.12 s | −43 % |
| probe: first frame / content, sample notebook | 949 / 1600 ms | 910 / 1279 ms | 886 / 1298 ms | −3 / +2 % |
| probe: 5 000 notes, first frame / content / index ready | 974 / 3376 / 3242 ms | 963 / 2911 / 2823 ms | 868 / 2862 / 2824 ms | −10 / −2 / 0 % |
| probe: 5 000 notes, longest stall after ready / settled | 1.46 / 6.45 s | 1.43 / 5.71 s | 0.05 / 2.88 s | −97 / −49 % |
| probe: open the 5000-block outline, frame / settled | 387 / 824 ms | 190 / 483 ms | 176 / 411 ms | −7 / −15 % |
| probe: Tasks / Calendar / Agenda, first open | 344 / 377 / 397 ms | 230 / 268 / 261 ms | 152 / 198 / 204 ms | −34 / −26 / −22 % |
| probe: Graph settled | 46.8 s | 47.0 s | 1.54 s | −97 % |
| idle in the foreground / background | 0.03 % CPU, 0.37 wakeups/s / 0 | 0 %, 0.33 / 0 | 0.03 %, 0.37 / 0 | 0 |

After these measurements the R8 rules also keep the few androidx members that Qt's C++ calls by
name (+19 KB of `classes.dex`): the final arm64 APK is 84.96 MB, 28.50 MB to download.

The flake APK of the current code, signed with the same throwaway key, behaves like the script
build (sample notebook warm start 287 ms, PSS 157 MB; 5 000 notes PSS 359 MB). HTTPS works only
in the current APKs (OpenSSL 3 bundled; before, every HTTPS request failed with "TLS
initialization failed").

**Browser** (headless Chromium on the laptop, `wasm-bench.mjs` / `wasm-batch.sh`; 5 loads, 3 with
throttling; MinSizeRel builds, Qt 6.11.2 + Emscripten 4.0.7):

| | v0.1.1 | d1141c0 | current | change |
|---|---:|---:|---:|---:|
| `brulion.wasm` raw / gzip -9 / brotli -q 11 | 40.42 / 13.69 / 9.42 MB | 40.82 / 13.81 / 9.50 MB | 40.28 / 13.68 / 9.41 MB | −1.3 / −1.0 / −1.0 % |
| first visit (empty storage, localhost): first draw | 1106 ms | 1059 ms | 883 ms | −17 % |
| repeat visit, empty storage: first draw | 959 ms | 946 ms | 586 ms | −38 % |
| repeat visit, 1 000 notes: first draw | 935 ms | 950 ms | 606 ms | −36 % |
| repeat visit, 1 000 notes: index ready without input | never (stalls until input) | never | 1.21 s | fixed (N13) |
| repeat visit, 1 000 notes: main thread busy / longest task | 1776 / 516 ms | 1564 / 451 ms | 1255 / 174 ms | −20 / −61 % |
| ×4 CPU, 1 000 notes: first draw / index ready / settled | 3179 ms / never / 6.94 s | 3200 ms / never / 6.20 s | 1635 ms / 4.27 s / 4.91 s | −49 % / fixed / −21 % |
| ×4 CPU: longest task | 1904 ms | 1845 ms | 603 ms | −67 % |
| first visit at 20 Mbit/s, gzip / brotli: first draw | 8.31 / 6.05 s | 8.23 / 7.42 s | 7.69 / 6.93 s | −7 / −7 % (brotli runs noisy: 5.8–7.4 s) |

"Settled" of the older builds stops early because their index stalls without input (N13), so
it understates their real time to a usable search.

### Wave 4: folders, tags as pages, selection (2026-10-23)

The integration of the six features (folders, tags as page links, block links, block selection,
keyboard toolbar, agent CLI) brought new hot paths; their benchmarks are in `tests/bench`
(`bench_index` `planRelocation`, `moveImpact`, `relocationSteps`, `tagPageOpen`, `suggestTags`;
`bench_markdown` `selectAllCopy`; `bench_io` `clipboardHtml`) and the probe's `select-copy`
scenario (`tests/bench/app`). `relocationVault()` (bench_index) is `bench::makeVault()` with a
tenth of the notes in a folder `Projekty` (500 of 5000), path-qualified links, bracketed tags and
query blocks naming it, and the pages tags name.

**Core** (laptop, Release, P-cores, 5 repetitions, the machine shared with other builds: `-q 6`;
before = `c92ab1d`, the benchmarks on the code as merged, after = this branch):

| operation | before | after | change |
|---|---:|---:|---:|
| plan a move: folder of 500 notes moved into another (5000 notes) | 121 ms | 25.5 ms | −79 % |
| … the folder renamed (its path-qualified links, tags and queries rewritten) | 487 ms | 109 ms | −78 % |
| … the most linked page renamed (567 notes rewritten) | 113 ms | 9.1 ms | −92 % |
| … the same, 20 000 notes: folder moved / renamed / page renamed | 839 / 2290 / 462 ms | 133 / 460 / 39 ms | −84 / −80 / −92 % |
| `notebook::relocationImpact` (move dialog, drop), 5000 notes on disk: folder moved / renamed / page moved | 123 / 489 / 25 ms | 25.5 / 110 / 6.9 ms | −79 / −77 / −73 % |
| the dragged row's ghost, GUI thread per event-loop turn (5000 notes, folder moved) | one stall of 123 ms | 3.3 ms, then ≤ 8.5 ms × 3 turns | |
| … 20 000 notes | one stall of 839 ms | 16 ms, then ≤ 8.7 ms × 15 turns | |
| open the page `#praca` names (5000 notes): warm / right after a save | 2.7 / 6.5 ms | 1.2 / 4.8 ms | −57 / −26 % |
| … 20 000 notes | 15.5 / 29.5 ms | 8.1 / 22 ms | −48 / −25 % |
| `#` completion (`suggestTags`), 5000 notes: 'pra' / 'stro' / nothing typed | 0.59 / 1.44 / 0.014 ms | same | |
| select all + copy, 5000 blocks: top rows + Markdown / plain text / HTML flavour | – | 0.42 / 11.4 / 34.5 ms | new |

What was not linear or too slow, and what changed:
* **Move plans** looked at every note linking to a moved name as new text: `findQueryNames()` (a
  case-insensitive search through the whole note, then a parse) and `retargetTags()` (a parse) for
  each, links re-keyed and resolved twice, and every `path:` filter folded every path of the vault
  (O(query notes × notes): folder move k 1.40 between 5000 and 20 000 notes, now 1.19; rename 1.12
  → 1.04). Now the index's own link keys and resolutions are used, only notes with a query block or
  a renamed tag are parsed, `retargetLinks()` reuses the parsed links, `path:` prefixes are a range
  in a sorted list of folded paths, and the table of names after the move is an overlay on the one
  before (no copy of the whole table). `paths::normalize()` / `stripMarkdownExtension()` lost their
  split-and-join and regular expression, the name table lowers ASCII without Qt's Unicode tables.
* **The drag ghost** asked `Workspace.moveImpact()` synchronously 150 ms after each target change:
  `Workspace.requestMoveImpact()` now plans 8 ms per event-loop turn (`RelocationPlanner`), drops a
  request when the target changes and keeps impacts per target until the notebook changes.
* **Tags as pages**: the page-level tags of every tagged note were worked out again on every page
  open (and per note in every query with a tag filter); the index entry keeps them.
* The remaining super-linear rows are memory, not algorithm: `suggestTags` 5000 → 20 000 notes k
  1.36–1.53 (the `suggestPages()` scan, as `suggestPages` itself at 10 000 → 50 000) and the warm
  tag page k 1.4 (per-block reads of every tagged note); "after a save" stays k 1.10.

**App** (laptop, Xvfb + llvmpipe, Release, 10 000-note vault, 5 runs; `select-copy`: Ctrl+A twice,
Ctrl+C, Escape, three rounds per run):

| | outline 5000 blocks | document 5000 blocks |
|---|---:|---:|
| select every block (2nd Ctrl+A): key handling / key → frame | 8.1 / 18 ms (p95 44 ms) | 8.6 / 18 ms (p95 43 ms) |
| copy (Ctrl+C): key handling / key → frame | 3.8 / 15 ms (p95 4.5 / 16 ms) | 4.1 / 13 ms (p95 5.4 / 16 ms) |
| … before the lazy HTML flavour (same laptop and session) | 38 / 46 ms | 44 / 50 ms |
| copied | 478 190 characters | 597 309 characters |

A copy of a whole 5000-block page cost about 55 ms on the desktop when measured first (53 / 62 and
59 / 68 ms then), two thirds of it the HTML flavour (`io::clipboardHtml`, linear: 3.5 / 34.5 /
140 ms for 500 / 5000 / 20 000 blocks). The flavour is now rendered only when another application
asks for it (`LazyHtmlMimeData`, docs/UI.md "Selecting and copying"; a paste into Brulion, a code
editor or a terminal never renders it): Ctrl+C on the same pages takes ~4 ms of handling and
13–15 ms to the frame — 10× / 3–4× less (2026-10-27, `select-copy`, base `102920e` against this
change, two rounds of 5 runs each, the second on an idle laptop: the numbers above). The cost
moves to the first paste into a mail or office app (34.5 ms at 5000 blocks, once). Android, the
browser and Windows take the clipboard's data at once (Windows: `OleSetClipboard` enumerates the
formats, and Qt's CF_HTML converter asks for `html()` to offer its format), so they still render
it while copying.

### Wave 5: index memory (L8), parallel indexing (L1), index cache (L2) (2026-10-29)

Three steps on the index, each committed and measured on its own (laptop, Release, P-cores,
`-q 3`, 5 repetitions; base = `sweep2` at `82d0357`, the same `tests/bench` sources on both trees —
rows the base cannot build are skipped there, `__has_include` in `bench_index.cpp`).

**L8 — memory.** The heap of the index of the 10 000-note bench vault (22.9 MB of Markdown;
`bench_index:memory`, glibc malloc statistics, exact) step by step:

| step (commit) | ParsedNote only | index | + full-text index |
|---|---:|---:|---:|
| base | 352.2 MB | 447.9 MB | 571.7 MB (545.2 MiB) |
| index maps: per-key path sets as sorted lists ≤ 32 paths (`smallsets_p.h`) | 352.2 | 382.1 | 505.9 |
| `Block::task` on the heap (`Boxed<Task>`, Block 440 → 232 bytes) | 287.6 | 317.5 | 441.2 |
| no growth slack (blocks reserved exactly, lists squeezed) | 256.9 | 286.8 | 410.4 |
| `ParsedNote::blockIds` a `QMap` | 246.1 | 276.0 | 399.7 |
| `Properties` a `QMap` | 214.9 | 244.8 | 368.5 |
| `Block::raw` a slice of the note's text (`BlockSource`) | 165.7 | 195.5 | 319.3 |
| full-text documents point into the dictionary, posting lists squeezed | 165.7 | 195.6 | 232.3 |
| **after (final tree)** | **165.7 (158.0 MiB)** | **194.5 (185.5 MiB)** | **231.3 (220.6 MiB), −59.5 %** |

50 000 notes: 2681.7 → 1094.9 MiB (−59.2 %). The model's counts (the `memory` row's info line,
equal on both trees): 244 795 blocks, 44 919 tasks, 11 413 blocks with properties, 18 064 with an
id, 65 815 links (6 104 block refs), 21.5 M characters of raw and 19.2 M of block text. Where it
went (heaptrack on the base, peak of the 50 000-note rows): the `QList<Block>` storage with its
growth (Blocks of 440 bytes, an in-place `std::optional<Task>` in each), the `raw` and `text`
joins (234 and 200 MB), the full-text index's per-document term copies (183 MB of strings +
157 MB of lists), `ParsedNote::tasks` and `links` growth (65 + 54 MB), and nearly empty 48-entry
QHash/QSet spans (a QSet<QString> with one element ≈ 1.4 KB) for every link key, block id, tag and
name of the index, every `blockIds` and every block's `Properties` (`collapsed::`, a Logseq `id::`).
`QMap` was taken for `Properties` and `blockIds` instead of the planned own compact classes: the
same saving within 1 MB, no new API (iteration is by key now, deterministic — QHash's was random).
Times did not suffer: `buildInMemory` 10 000 notes 836 → 797 ms (−4.6 %), `parseTypical` −5 to
−12 %, `firstSearch` 465 → 475 ms (+2 %, within the noise).

**L1 — parse on worker threads.** `notebook::IndexLoader` reads (`Vault::readDetached`),
decodes and parses the notes (`VaultIndex::prepare`) on a `WorkerPool`, builds the full-text index
on one more thread (`SearchIndex::prepare`/`set`, adopted with per-document content keys) and merges
the notes on the owner's thread in slices (`updatePrepared`, the vault's bookkeeping by
`adoptRead`). The app's pool: clamp(cores − 1, 1, 4) low-priority workers + 1; WebAssembly keeps
the slices on the GUI thread. `openFromDisk` = scan + read + index, until every note is merged
(the loader rows: also the full-text index built and adopted):

| | 1 000 notes | 10 000 notes | 50 000 notes |
|---|---:|---:|---:|
| before: one thread (`readText` + `update`, the app's old path, no full-text index) | 91 ms | 965 ms | 5045 ms |
| loader inline (no threads: WebAssembly) | 93 ms | 979 ms | 5065 ms |
| loader, 1 worker | 79 ms | 793 ms | 4126 ms |
| loader, 4 workers (+ full-text index) | 34 ms | 352 ms | 1834 ms |
| `VaultIndex::prepare` of every note on one thread | 62 ms | 621 ms | – |

The owner's merge (resolving and relinking every note's links, the tag and block maps, the vault's
read bookkeeping) is now the larger part: 4 workers give 2.7× of the serial path, not 4×.

**L2 — the index cache.** `<device state>/index-cache.bin` (`index/indexcache.h`; in the browser a
cache mount of its own, docs/WEB.md: a quota failure there loses only the cache, and
`indexCacheBudget()` keeps it within half of the free quota): every note's parse (block texts and
raw lines stored as offsets into the note where they are one piece of it), the full-text index, a
directory checked per note by path, byte size and the content key of the text just read; identity
= format, `markdown::ParserVersion`, `SearchIndex::FormatVersion`, Qt and app versions, the
config's parse fingerprint, the vault id. 10 000 notes: 18.9 MB (0.82× the
vault), encoded in 122 ms (in 8 ms slices on the GUI thread), all notes and the full-text index
decoded in 129 ms on one thread (k 1.03 / 1.06 between 10 000 and 50 000).

| `openFromDisk` from the cache | 1 000 notes | 10 000 notes | 50 000 notes |
|---|---:|---:|---:|
| inline (WebAssembly) | 51 ms | 552–578 ms (−41 % of the cold inline load) | 3136 ms |
| 4 workers | 22 ms | 272–284 ms | 1604 ms |

Every file is still read and hashed (exact invalidation, the vault's bookkeeping unchanged), so a
warm load saves parsing and tokenising, not reading. Heap after a load from the disk with the
loader (vault, texts, index, full-text index): parsed 305.5 MiB, from the cache 307.2 MiB (+0.6 %).

Since the review, decoding also checks every line number of a record against the text's line
count (a record that passed the size and 64-bit content-key check but belongs to another text
must not point past the note's end; one branch-free pass over the text). Laptop, Release, `-c 0-11
-q 3`, `a5f6c57` against the fix, two alternating rounds of 5 runs (medians): `cacheDecode` 10 000
notes 133.5 / 135.3 → 139.4 / 133.9 ms, 50 000 notes 815 / 818 → 841 / 801 ms; `openFromDisk`
warm cache with 4 workers 292 / 288 → 290 / 270 ms, inline 567 / 531 → 573 / 538 ms — within the
noise (a first, per-character version cost +7 % in `cacheDecode`).

**App** (laptop, Xvfb + llvmpipe, Release `-O3 -g`, the 10 000-note bench vault of
`make-bench-vault.py`; `first` = a fresh profile, i.e. no index cache; `warm` = the same profile
again, i.e. the cache written by the warm-up run; 3 runs of `first`, 5 of `warm`, medians, base and head alternated, the second of two rounds on a quiet laptop):

| | before: first / warm | after: first (cold index) / warm (from the cache) |
|---|---:|---:|
| index ready (`Workspace.ready`, from the process start) | 1891 / 1723 ms | **964 / 831 ms** (−49 / −52 %) |
| longest GUI stall while indexing | 29 / 29 ms | 28 / 32 ms |
| longest GUI stall after ready (the first full-text build) | 54 / 54 ms | **0 / 0 ms** |
| frames per second while indexing | 39 / 40 | 47 / 48 |
| malloc heap when settled | 512 / 504 MB | **340 / 332 MB** (−34 %) |
| RSS when settled | 664 / 647 MB | 504 / 490 MB |
| CPU time until settled (all threads) | 4.49 / 4.35 s | 4.48 / 3.56 s |
| first frame / first content | 317 / 591 ms, 184 / 449 ms | unchanged |

The targets were 0.9 s cold and 0.6 s warm: the merge on the GUI thread (12 ms slices between
frames) is now what the index waits for — next step below (L1b). The full-text index no longer
costs the GUI anything: it is built on its own thread (or read from the cache) and adopted at once,
so the first search after "ready" finds a complete index. Not so in the browser on a first visit
(and in any inline load whose cache has no full-text section): there is no thread for it, so the
documents are still built in 8 ms slices after "ready", and a search before they are done builds
the rest at once, as before wave 5.

Not measured in this wave (no quiet window on the Raspberry Pi — another agent built there —, the
emulator needs Android builds of both trees): the Pi and the Android emulator (PSS at 5 000 notes)
numbers below in Budgets are the previous ones, except where noted.

**Browser** (measured after the review, laptop, headless Chromium 155 + SwiftShader,
`wasm-bench.mjs` with the 1 000-note `web1k` vault in IndexedDB, MinSizeRel builds of `82d0357`
and of this branch, two alternating rounds; medians of 5 repeat visits at 1×, 3 at ×4 CPU, the
two rounds side by side; "cold" = no index cache, "warm" = `--wait-cache`, all 1 000 notes from it):

| 1 000 notes | before | after, cold | after, warm |
|---|---:|---:|---:|
| index ready, repeat visit | 1116 / 1117 ms | 1280 / 1315 ms (+16 %) | 1099 / 1128 ms (±0) |
| settled (main thread quiet) | 1498 / 1513 ms | 1625 / 1645 ms (+8 %) | 1295 / 1252 ms (−15 %) |
| main thread busy | 1223 / 1096 ms | 1150 / 1172 ms | 974 / 1131 ms |
| ×4 CPU: index ready | 5292 / 5147 ms | 5786 / 5735 ms (+11 %) | 4767 / 4690 ms (−9 %) |
| ×4 CPU: settled | 5900 / 5779 ms | 6368 / 6237 ms (+8 %) | 4875 / 4776 ms (−17 %) |
| ×4 CPU: long tasks in total / longest | 3458 / 722 ms | 2343 / 927 ms | 2365 / 942 ms |
| ×4 CPU: WebGL draw calls / frames while loading | 2337 / 81 | 5765 / 105 | 3733 / 68 |
| first visit (cold): index ready, 1× / ×4 CPU | 1419–1506 / 5952–6593 ms | 1643–1657 / 7293–7437 ms | – |
| wasm linear memory | 104 MB | 86 MB (−17 %) | 86 MB |

The cold load in the browser got slower to "ready" although the main thread does about the same
work (+0–3 %): the journals are merged first, so the visible journal fills in early and the UI
draws 30 % (×4 CPU: 2.5×) more while the rest loads, and that time is taken from the slices
(first draw unchanged at ~0.6 s; when the journal's content shows was not measured). The
warm load (every later visit once the cache is written, ~5 s after "ready") is as fast as the old
cold one at 1× and faster at ×4 CPU, with the full-text index included. Cutting the redraws while
loading is part of L1b. (wasm-bench closes the page ~1.5 s after it settles, before the cache is
saved: without `--wait-cache` its repeat visits are cold.) `scripts/wasm-smoke.mjs --loads 2 --index-notes 3000` (wasm.yml) checks that
the browser's second load takes the notes from the cache; on a GitHub runner (headless Chromium,
no throttling, times from the navigation): index of 3000 notes complete at 4.99 s on the first
load (cold, in slices on the GUI thread), the cache saved at 10.1 s, and on the reload complete at
2.73 s with 3000 of 3000 notes from the cache (−45 %).

### L5: delta undo/redo (2026-10-30)

An undo step was a delta of the whole page text, and undo / redo turned the page into the other
version by reading it again: `BlockListModel::replaceContent()` parsed the page (`BlockDocument`,
every block's `parseNoteWith()`), matched the old and new blocks (LCS) and removed and inserted the
rows of every changed block. Now a step is a `BlockDocument::Delta` — the run of blocks the command
changed (from the first to the last changed block), a block in both versions with only its changed
range of text — and undo / redo apply it as a document operation (`revert` / `reapply` through
`BlockListModel::apply()`): only the changed blocks are parsed, every block keeps its uid and row
(`dataChanged` for a changed one). Block uids are unique in the process, and `replaceContent()`
gives the blocks it keeps their old uids, so whole-text edits (Markdown pasted, a template, the note
mode, source mode) are small steps too. A command takes `version()` before it and `diffFrom()` after
it instead of serialising the page and comparing two strings; typing merges `composed()` the
keystroke's delta with the step (bounded: a step whose run spans more than 256 blocks — the first
keystroke at the top of a page also renumbers a loose ordered list at its end — stays a step of its
own, or every later keystroke would compose thousands of blocks).

**Core** (laptop, Release, P-cores, `-q 3`, 5 repetitions; `bench_markdown:blockDocOps` on the
5000-block outline, from the branch's perf-check run; the rows before L5 measured alternately with
`sweep2` at `b3d9a3d` in the same session):

| operation | 5000 blocks | 500 blocks |
|---|---:|---:|
| `undo+redo`: an Enter's delta reverted and reapplied (one undo + one redo) | 1.59 ms | 0.111 ms (k 1.16) |
| `re-read`: the page read again, unchanged blocks lending their parse — the core of the old undo, which had no parse to borrow, and matched the blocks and moved the rows on top | 3.44 ms per step | 0.340 ms |
| `version+diff`: a `setBody` with the step's bookkeeping (`version()` + `diffFrom()`) | 1.11 ms (`setBody` alone 1.00 ms) | 0.093 ms |
| `keystroke`, `setBody`, `split+merge`, … against `b3d9a3d` | within noise (+1 to +9 %, no row outside max(10 %, noise)) | |

**App** (`tests/bench/app`, laptop, Release, Xvfb; the `typing` scenario on the 5000-block pages of
the bench notebook; 6 runs per build, base and branch interleaved; medians, min–max in brackets):

| `typing-outline5000` | before | after | change |
|---|---:|---:|---:|
| undo (Ctrl+Z) → frame | 32.1 ms [31.7–32.2] | 13.2 ms [12.6–13.8] | −59 % |
| … its handling | 24.7 ms | 3.6 ms | −85 % |
| redo (Ctrl+Shift+Z) → frame | 34.6 ms [34.0–35.4] | 13.9 ms [13.5–14.1] | −60 % |
| … its handling | 23.7 ms | 3.1 ms | −87 % |
| Enter → frame | 14.6 ms | 14.7 ms | 0 |
| keystroke handling, median / p95 | 4.7 / 9.4 ms | 4.1 / 8.7 ms | −13 / −7 % |
| keystroke → frame, p95 | 15.2 ms | 14.3 ms | −6 % |
| anonymous memory after the scenario | 408 MB | 402 MB | −6 MB |

`typing-document5000` (5000 paragraphs, document mode): undo → frame 33.4 → 12.7 ms (handling
26.2 → 3.4 ms), redo 37.0 → 13.2 ms (26.3 → 3.1 ms), Enter 14.3 → 13.8 ms, keystroke handling
5.0 → 4.2 ms (p95 9.3 → 8.5 ms). The autosave after a burst of typing is unchanged within its noise
(the GUI time it blocks: outline 149 → 139 ms, document 124 → 130 ms, the ranges overlapping).
Undo and redo now cost what Enter costs (the budget below); before the bound on typing merges the
first keystroke on the document page made every later one 1.1 ms slower (above).

What the step holds: `undoStoresChangesNotPages` (3000 blocks, 60 Enter + typing steps: history <
a quarter of the page) and `pasteIsASmallUndoStep` (3 pasted blocks on a 3000-block page: < 1000
characters) guard it; one character typed into a 100 000-character code block is a step of a few
characters (`deltaHoldsChangesOnly`). The Raspberry Pi was not measured (no builds on it in this
sweep; L5b in "Next steps"); its undo was 214 ms before, five times its Enter (38 ms), and the
work an undo does on the laptop fell by 85 %.

`tst_editor_controller` `largePageTouchesOneRow` guards the ratio without timing a machine: the
median of five undos (and five redos) of an Enter on a 5000-block page must stay ≤ 3 × the median
Enter + 2 ms (Debug build: Enter 2.8 ms, undo 2.0 ms, redo 2.0 ms).

### L9: native file pickers, one Qt Quick Controls style (2026-10-30)

The packages carried five Qt Quick Controls styles and QtQuick.Dialogs that the app never shows,
for two reasons found in Qt 6.11.2's sources: qmlimportscanner follows every `import … auto` of a
qmldir, optional ones included, and every deploy step (androiddeployqt, the browser build's
`qt_import_qml_plugins`, windeployqt, linuxdeploy-plugin-qt) packages what it reports. (1)
`QtQuick/Controls/qmldir` has `optional import QtQuick.Controls.{Fusion,Imagine,Material,Universal,
FluentWinUI3} auto`, so five files' `import QtQuick.Controls` (run-time style selection; the app
forces Basic anyway) deployed all five styles; (2) QtQuick.Dialogs' non-native fallback dialog
exists once per style (`quickimpl/qml/+Material/…`) and imports each style and
`Qt.labs.folderlistmodel`. Now the QML imports `QtQuick.Controls.Basic` (compile-time style
selection) and every picker is `FilePicker` (`src/app/ui/filepicker.h`, docs/UI.md "File
pickers"): the platform theme's own dialog helper — the code path QtQuick.Dialogs took on Android
(SAF intents) and Windows (`IFileDialog`) — and Qt Widgets' `QFileDialog` on desktops without one;
the browser picks through `QWasmLocalFileAccess` (`src/platform/wasm/wasmfiles.h`) and has no Qt
Widgets any more. Guards: configure stops on a bare `import QtQuick.Controls` / `QtQuick.Dialogs`
(`src/app/CMakeLists.txt`); `scripts/android-build.sh` / `nix/android.nix` fail on a style or
QtQuick.Dialogs library in the APK, `scripts/wasm-build.sh` on such a module in the app's QML plugin
list.

Measured on the laptop in one session, `sweep2` at `ef90077` (base) against the branch, same
toolchains (Qt 6.11.2, NDK r27c, emsdk 4.0.7):

| | base | L9 | change |
|---|---:|---:|---:|
| APK arm64 release (`android-apk.sh … rel`, signed) | 96 230 481 B (91.77 MiB) | 80 601 163 B (76.87 MiB) | −15 629 318 B (−16.2 %) |
| APK arm64 download (every entry deflated, `size-report.py`) | 30.86 MB | 26.26 MB | −4.60 MB (−14.9 %) |
| APK entries | 191 | 166 | −25 |
| … Qt Quick Controls styles not used (19 entries) | 11.80 MB (3.80 MB deflated) | 0 | |
| … Qt Quick Dialogs (5 entries) | 3.02 MB (0.83 MB deflated) | 0 | |
| … `libbrulion` (FilePicker, the dialog helper's glue) | 30.94 MB | 31.06 MB | +0.12 MB |
| `brulion.wasm` raw (MinSizeRel, `scripts/wasm-build.sh`) | 47 225 371 B | 41 909 609 B | −5 315 762 B (−11.3 %) |
| `brulion.wasm` gzip -9 | 15 803 378 B | 13 498 551 B | −2 304 827 B (−14.6 %) |
| `brulion.wasm` brotli -q 11 | 10 841 274 B | 9 231 654 B | −1 609 620 B (−14.8 %) |
| wasm link map (`wasm-linkmap.sh`, before wasm-opt): code / data | 36.88 / 16.51 MB | 35.25 / 12.64 MB | −1.63 / −3.87 MB |
| … styles not used / QuickDialogs2QuickImpl + Dialogs2 + Utils / Qt Widgets | 2.75 / 2.41 / 0.52 MB | 0 / 0 / 0 | |
| web first visit at 20 Mbit/s, gzip (`wasm-bench.mjs --visits first --encoding gzip --throttle 20000,40`, 2 rounds × 5): `.wasm` transferred | 15.80 MB | 13.50 MB | −2.30 MB |
| … first draw, median per round | 8.42 / 8.49 s | 7.38 / 7.32 s | −1.1 s (−13 %) |
| … main thread busy during the load | 1.64 / 1.58 s | 1.43 / 1.45 s | −0.17 s |
| Windows ZIP / installer (`windows.yml`, GitHub Actions: `sweep2` run at `ef90077` and the branch, same day) | 49.5 / 32.6 MB | 42.8 / 29.5 MB | −6.7 / −3.1 MB (−14 / −10 %) |
| AppImage x86_64 / aarch64 (`appimage.yml`, both trees the same day) | 60 492 280 / 60 529 160 B | 55 859 704 / 55 953 928 B | −4.42 / −4.36 MiB (−7.7 / −7.6 %) |

(The 20 Mbit/s runs are network-bound; another agent's build ran on the laptop during them, the
same for both builds.) The AppImage needed one more step: linuxdeploy-plugin-qt copies the folder of
every QML import whole, so `import QtQuick` brought all of `qml/QtQuick` — the styles and
QtQuick.Dialogs stayed after the QML changes alone (the AppImage was 0.1 MB larger);
`scripts/appimage-build.sh` now deploys, removes them (their QML folders and libraries) and then
packs, and its check fails when one is back. Still packaged: QtTest, QuickTest and `Qt.labs.folderlistmodel`, imported by
the developer and self-test QML under `src/app` (`qml/dev`, `qml/content/*SelfTest.qml`) — APK
0.84 MB, wasm 0.27 MB before wasm-opt. qmlimportscanner scans the target's whole source folder,
compiled or not, and its `-exclude <folder>` does not keep a folder out in 6.11.2 (it also visits
each folder's `.` entry), so they need a folder of their own outside `src/app` (L9b below).

Checks: `tests/app/filepicker` (fake backends, the widget dialog offscreen), `tst_pickedfiles`,
the import self-test's save dialog (`tst_appio_import_qml*`), `scripts/wasm-smoke.mjs
--pick-files` (CI: seven files through the browser's `<input>`, one above the limit per file
never read, the reading stopped at the pick's total, and a download), the Playwright tests' export (`showSaveFilePicker` in Chromium, a download in
Firefox) and `scripts/android-emulator-test.sh --pickers` (DocumentsUI's open, folder and save
pickers on Android 16).

### L3: incremental derived lists (2026-10-30)

Every `VaultIndex` change used to drop all derived lists, and everything listening to
`VaultIndex::changed` computed its view of the whole vault again: the reminder scheduler (up to four
passes per save on desktops), the Tasks view, the Calendar / Agenda and the side panel's month,
every ```query block of the open pages, the Android widgets. Now:

* **The index logs and patches.** Each change records the notes it touched — the note (both paths
  of a rename), notes whose links now resolve elsewhere (relink), notes whose `((id))` show one of
  its block ids — and whether names changed (a note or attachment added, removed or renamed, aliases):
  `changesSince(version)`, a log of 1024 changes / 16 384 paths (each record's paths in a compact
  set: ~60 bytes for the usual single note, not a 1.4 KB hash span), incomplete after `clear()`, a
  re-parsing `setConfig()` or an overflow; `serial()` tells indexes apart. The lists built once are
  patched with the changed note instead of dropped: sorted entries (by path and by relevance),
  `paths()`, `tasks()`, `reminders()` (the note's segment), `journals()`, `notesWithQueries()`; tags,
  missing pages, tag targets and the page-property lists are dropped only when the change touches
  what they list (its tags and their spellings, its unresolved links, names, its page properties).
  Lists nobody read for 32 changes (a bulk load) are dropped and built once at the next read.
* **Consumers follow the log** (`index/notecache.h`: `IndexFollower`, `NoteCache<Value>`): per-note
  values, computed again for the logged notes only; anything resolving names (`page:`, `ref:`) goes
  full when names change, and another key (query, day, range, language) is a full sync.
* **Three core caches** share their per-note code with the free functions, which are the caches
  without history: `QueryCache` (a query's matches in result order — position and sort keys, no
  `Task` copies; the changed notes evaluated, sorted and merged in; `remind:` filters always full),
  `CalendarCache` (each note's task and reminder items of the range), `NotificationCache` (each
  note's occurrences of a window of days; requests reaching outside it are computed directly; the
  local time zone is part of its key — the kept instants are computed again after a change, so
  reminders keep their wall-clock times).
* **The app keeps them**: `ReminderScheduler` (one `NotificationCache` of [today − 8 days,
  today + horizon + 1] for its refresh, the in-app firing checks and the missed-list pruning),
  `TaskListModel` (a `QueryCache`; the changed notes' tasks — taken in one pass over the cache's
  matches, however many notes one change names — merged into its sorted list, page titles compared
  by ranks — a merge bringing a new title rebuilds), `CalendarModel` (a `CalendarCache`;
  the overdue list from an `OverdueCache`: the old scan of open tasks due before today, per note),
  `Workspace` (the last 4 calendar ranges and 8 queries: the side panel's month, query blocks; a
  query matching more than 5000 items is not kept — its cache would hold every match, ~0.2 KB each,
  for the session),
  `WidgetPublisher` (the snapshot's caches kept across publishes).
* **Full syncs stay as cheap as the old full computation.** A first view, another range or the
  date picker's marks compute everything: a `NoteCache` then fills its values from the index's own
  task and reminder lists (contiguous, kept up to date by the index) rather than visiting 10 000
  notes one by one, one-off reminders outside the range are skipped before they are copied, and a
  query run walks the sorted note records, keeps its sort keys inline, sorts through a permutation
  and visits only a note's task (or reminder) blocks. Without that the app's first open of the
  Calendar was ~50 ms slower (it asks for several ranges; each full computation looked up every
  note) — caught by the app benchmark below.

**Core** (laptop, Release, P-cores, `-q 2`, 5 repetitions; `bench_reminders:afterEdit`, a journal of
the mixed 10 000-note vault edited back and forth, the caches synced once before the loop; `sweep2`
at `ef90077` measured right before in the same session):

| after one save, mixed vault 10 000 notes | before | after (the app's path) | free function after |
|---|---:|---:|---:|
| `update` alone | 0.019 ms | 0.020 ms | – |
| `reminders()` / `tasks()` | 6.2 / 13.5 ms | 0.020 / 0.020 ms (patched, shared copy) | – |
| upcoming notifications (scheduler) | 25.5 ms | 0.13 ms (`NotificationCache`) | 18.0 ms |
| calendar month (6 weeks) | 57 ms | 4.2 ms (`CalendarCache`) | 42.5 ms |
| query `status:open due:<=+7d sort:due` | 31 ms | 0.73 ms (`QueryCache`) | 13.8 ms |
| **all consumers** (guard row) | **112 ms** | **6.0 ms** (−95 %) | 75 ms |
| all consumers, 10 000-task vault | 40 ms | 2.8 ms | 38.5 ms |

What remains of the 6 ms is mostly the calendar range's items gathered and sorted again (the month
holds thousands of items in this vault; the changed note's are computed alone). The free functions
got faster too (full syncs fill from the index's lists, runs copy no `Task` per candidate): queries
(`bench_index:query`, 10 000 notes, the same session) `tag:praca` 78 → 35.5 ms, `type:blocks "łódź"
group:page` 74 → 33 ms, `ref:[[Strona 1]]` 89 → 49.5 ms, open due this week 28.5 → 15.8 ms, overdue
27.5 → 12.5 ms, reminders this week 9.3 → 1.5 ms, all tasks sorted 58 → 56 ms; after one edit with a
`QueryCache` (`bench_index:queryCache`): `tag:praca` 0.17 ms (runQuery 43 ms), all tasks sorted 1.9 ms
(64 ms). A perf-check run of the branch passes all 33 checks; the laptop baseline's afterEdit row is
6 ms now (was 138 ms) and its query row 36 ms (was 99 ms).

**App** (`tests/bench/app`, laptop, Release, Xvfb; the `views` scenario on the 10 000-note bench
notebook, `r0` first open, `r1` again; 5 runs per build, medians): Calendar r0 132.0 → 128.8 ms, r1
119.6 → 115.0 ms; Agenda r0 107.4 → 103.3 ms; Tasks r1 82.6 → 82.9 ms, r0 100.1 → 106.7 ms (within
the runs' spread; the query behind it is faster cold, 16–19 → 14–15 ms, and the second open is
equal); Graph, All pages and Tags unchanged. An earlier build of the branch opened the Calendar
~50 ms later (the full-sync costs above), which is why full syncs fill from the index's lists.

Equivalence is the point, so it is tested, not assumed: random edit sequences (Polish names,
renames, removals, `((id))` across notes, recurring series, malformed tokens) check at every step
that `changesSince()` names every note whose tasks, reminders, link targets or shown block texts
changed (`tst_index_consistency`), that a `QueryCache` per query equals `runQuery()` on the edited
and on a fresh index (16 queries: tags, `ref:`, `page:this`, groups, `limit:`, `remind:overdue`, …),
that `CalendarCache` / `NotificationCache` equal the free functions (a month, a week, a day, the
2026-10-25 change to winter time, windows moving across midnight, another language, a change of
the local time zone; `tst_reminders_cache`), and that the Tasks list, the calendar's overdue list (against the old scan)
and Workspace's cached lists equal what a fresh model computes. `tst_reminders_cache`
`oneEditCostsAFraction` guards the gain without timing a machine: after one edit of 3000 notes the
caches must cost ≤ 0.25 × the free functions (Debug: 2.8 vs 16.9 ms). `tst_planning_tasks`
`changeNamingManyNotesStaysCheap` guards the other end: one change naming 3001 notes (the page 3000
notes link to appears or goes) must merge into the Tasks list in ≤ 2 × the time of a list built
afresh + 5 ms (looking each changed note up among all matches took 275 vs 45 ms under the CI's
sanitizers). The Raspberry Pi was not
measured (no builds on it in this sweep): L3b in "Next steps"; it took 0.95 s before.

### L16: the graph on a worker thread and the scene graph (2026-10-30)

The graph view (N5) laid its graph out on the GUI thread — a step per 16 ms timer event, on slow
devices time-boxed bursts of 10–25 ms during which input waited — and painted it with QPainter in
a `QQuickPaintedItem`, rasterised on every frame of the moving layout while the GUI thread waited
(throttled to a frame every 66–600 ms on slow devices). Now:

* **The layout runs on a worker thread** (`GraphSimulation`: one `QThread` "brulion-graph" per open
  graph, started with its first run). The GUI hands it a copy of the `ForceLayout` and keeps a
  mirror of the newest positions — what it draws, hit-tests, fits and follows —, which the worker
  publishes at most once per frame interval (16 ms); the newest frame replaces an unread one, so a
  busy GUI thread gets the newest positions, not a backlog. Commands (pin and move the dragged node,
  the drag's alpha target, reheat, pause while the view is hidden, cancel) apply before the
  worker's next step; the dragged node follows the pointer at once in the mirror. Only value
  copies cross threads (a run in, frames out; one mutex for the frame slot, one for the command
  queue). `rebuild()` (`VaultIndex::graph()` and the filters) stays on the GUI thread.
* **The browser** (single-threaded WebAssembly) runs the same worker on the GUI thread in slices of
  ≤ 16.5 ms per 33 ms timer event (never a 0 ms timer, N13), at least one step per slice.
* **Pacing and fast cooling instead of time-boxing.** A run unfolds over at least `minRunMs`
  (1 s: several steps per frame for small graphs; one step per frame while a node is dragged, like
  d3), a run that cannot keep up runs flat out. When the first three steps of a run cost more than
  5 ms on average it cools down in ~100 steps instead of ~300 — decided at that fixed step, so the
  layout stays deterministic: the tests compare the worker's final positions bit for bit with the
  same `ForceLayout` steps taken by hand (threaded and inline, both coolings, a pause mid-run, a
  GUI thread blocked for 300 ms). The decision no longer depends on painting.
* **Reduced motion**: the worker computes the run and publishes only its final frame; a layout from
  fresh positions shows a spinner (`GraphModel.settling`) instead of a spiral that jumps.
* **Drawing through the scene graph.** `GraphItem` is a `QQuickItem` with its own nodes
  (`GraphSceneNode`): edges, discs and rings as triangle meshes with a one-device-pixel fringe that
  fades to transparent (antialiasing without custom shaders: `QSGVertexColorMaterial`, so no
  shader tools in any toolchain; 16-bit indices in chunks of ≤ 65 535 vertices for GLES2), in world
  units under a camera transform; labels as `QSGTextNode`s on rounded backgrounds in screen units,
  their text layouts cached per node. Panning a settled graph only changes the camera's matrix
  (and places the labels again); a moving layout, zooming, colours and the highlight rebuild the
  meshes during the sync. No culling of discs and edges (every node goes to the GPU: ≤ 4000, 1200
  on phones), so panning never rebuilds. Both renderers place labels with one rule
  (`geometry::placeLabels`). With the software renderer (tests, screenshots, the Pi's offscreen
  runs) the QPainter code paints into an image node — the cost of the `QQuickPaintedItem` before —
  and keeps its throttling of slow frames of a moving layout (now: a paint over 16 ms).

**Unit timings** (`tst_planning_graph` `bigGraphPerformance`, 2000 nodes / 7338 edges at fit zoom;
the Raspberry Pi, Debug — the laptop was unreachable): one step 6.4–8.6 ms, building the whole
graph's meshes 5.2–6.4 ms (93 764 vertices in 2 chunks) against 58–107 ms for one QPainter frame —
a frame of the moving layout costs the GUI thread ~10–20× less, and a settled pan nothing but a
matrix. Zoomed in to 3× (every disc with up to 64 segments, 274 550 vertices in 5 chunks) a frame
with 33 new labels took 31–41 ms (`tst_planning_graph_sg`, the RHI null backend: the real scene
graph without a GPU), Debug on the Pi.

@@APP@@

### Budgets

Desktop = the laptop above, phone = Pi 4 (floor) and the emulator (optimistic), web = headless
Chromium (×4 CPU = phone). ✅ meets, ⚠️ within 1.5×, ❌ misses (current code).

| budget | target | desktop | phone | web |
|---|---|---|---|---|
| start → first content | desktop < 1 s, phone < 2 s | ✅ 0.40 s (10k), 0.39 s demo | ✅ Pi demo 1.25 s, 10k 1.57 s | ✅ repeat 0.61 s, ×4 CPU 1.64 s; ❌ first visit at 20 Mbit/s 7.3–7.4 s (L9; 8.4–8.5 s before in the same session) |
| index ready | desktop < 2 s at 10k; phone < 10 s at 10k | ✅ 0.96 s, 0.83 s from the index cache (wave 5; was 1.89 s) | ❌ Pi 15 s before wave 5, not measured since (L1b) | ✅ 1k cold 1.3 s, warm 1.1 s (×4 CPU 5.8 / 4.7 s; was 1.1 / 5.2 s in the same session) |
| GUI stall while indexing / after ready | < 50 ms desktop, < 100 ms phone | ✅ 28 ms indexing, 0 ms after ready (wave 5) | ❌ Pi 0.54 / 0.45 s before wave 5 (L1b, L18) | – |
| keystroke → frame (≤ 5000 blocks) | p95 < 16 ms desktop, < 33 ms phone | ✅ small page; ⚠️ 5000-block outline p95 23 ms | ✅ small page; ⚠️ outline p95 42 ms | – |
| Enter / undo | < 50 ms desktop, < 100 ms phone | ✅ 15 / 13 ms (L5; base in the same session 15 / 32 ms) | ✅ Enter 38 ms; undo 214 ms before L5, not measured since (L5b) | – |
| autosave stall | < 50 ms desktop, < 100 ms phone | ✅ small page 41 ms; ❌ 500 KB page 88 ms | ✅ small page 29 ms; ❌ 500 KB page 0.2 s | – |
| open page (5000 blocks) | frame < 150 ms desktop, < 300 ms phone | ✅ 113 ms | ❌ Pi 653 ms (L7) | – |
| Tasks / Calendar / Agenda | < 200 ms desktop, < 500 ms phone | ✅ 133–166 ms | ❌ Pi 0.73–1.28 s before L3, not measured since (L3b, L10) | – |
| Graph | settled < 1 s | ✅ 0.77 s | ❌ Pi 2.6 s (was 44 s; L16); emulator 1.5 s | – |
| after a save, all derived lists (10k) | < 30 ms desktop, < 150 ms phone | ✅ 6.0 ms (L3; base in the same session 112 ms) | ❌ Pi 0.95 s before L3, not measured since (L3b) | – |
| idle | < 0.5 % CPU | ✅ 0.1 % | ✅ background 0 % CPU, 0 wakeups/s | – |
| memory | ≤ 100 MB + 10 B per Markdown byte (10k ≤ 330 MB) | ⚠️ 340 MB heap (wave 5; was 512 MB; index alone 221 MiB) | ❌ emulator PSS 359 MB at 5k before wave 5 (< 300 MB wanted; not measured since) | ✅ 86 MB of linear memory at 1k (was 104 MB) |
| download | APK < 40 MB, web < 10 MB | – | ✅ 26.3 MB download (80.6 MB APK; L9: was 30.9 / 96.2 MB) | ❌ 13.5 MB gzip (9.2 MB brotli, not served; L9: was 15.8 / 10.8 MB) |

### Next steps

The L-items of the October 2026 performance plan, with the numbers they should move:

| # | change | expected |
|---|---|---|
| L1b | cheaper merge of prepared notes on the GUI thread: link lookup keys (`cleanTarget`, NFC, lower case) and the name table's keys computed by the loader's workers; fewer redraws while loading (the browser's cold load: +11–16 % to "ready", 30 %–2.5× more draws since journals come first); measure the Pi and the Android emulator (L1/L2 of wave 5 not measured there yet) | laptop 10k index ready 0.96 / 0.83 s → 0.6 s; Pi ≤ 6 s cold, ≤ 3 s warm |
| L5b | measure undo / redo on the Pi (`bench-matrix.sh` typing on the 5000-block outline, Release) and record the `blockDocOps` 5000 blocks undo+redo baseline in `pi.csv` (`perf-check.sh -u` in a quiet window; until then the Pi's check skips the row) | Pi undo 214 ms → < 50 ms |
| L3b | measure the after-a-save fan-out on the Pi (`bench_reminders` afterEdit, small profile) and ratchet its `pi.csv` row (`perf-check.sh -m pi -u` in a quiet window; the row now measures the cached path, so the old 963 ms baseline only passes it loosely) | Pi 0.95 s → < 0.2 s |
| L4 | `BlockDocument` fast path for body-only edits | keystroke core 1.3 ms → < 0.3 ms |
| L7 | cheaper page opening (header summary and linked references from the index) | Pi open 653 ms → ~400 ms |
| L9b | the developer and self-test QML (`qml/dev`, `qml/content/*SelfTest.qml`) in a folder outside `src/app` (qmlimportscanner scans the target's source folder whole, and its `-exclude` does not work there), so that releases leave out QtTest, QuickTest and `Qt.labs.folderlistmodel` | APK −0.84 MB, wasm −0.27 MB (before wasm-opt) |
| L10 | lazy cold views and dialogs, `BIcon` without a `Loader` | Pi demo first content −0.3 s |
| L16b | measure the graph on the Raspberry Pi (`bench-matrix.sh views graph`, offscreen: the software renderer's image node, 3 000-note vault; must not regress) and in the Android emulator probe (`android-batch.sh probe`, the scene graph on GLES); check it by hand on a phone, on Windows with a GPU (Direct3D 11) and in a phone's browser; if `rebuild()` (VaultIndex::graph and the filters, GUI thread) costs a phone > 50 ms, move its pure part onto the worker | phone: GUI stall during the layout < 33 ms, settled < 3 s |
| L12 | sync: size + mtime quick check for unchanged text files | no-change sync ≥ 3× faster (laptop 10k 419 ms, Pi 5k 1.35 s) |
| L18 | search indexing of big documents in bounded slices | no stall > 16 ms after ready |

Done in wave 5 (2026-10-29, above): L1 (parse on worker threads), L2 (the index cache), L8 (the
memory diet); on 2026-10-30 L5 (delta undo/redo, above; the Pi still to be measured: L5b), L9 (native file
pickers, one Qt Quick Controls style: APK −15.6 MB, wasm −2.3 MB gzip, above), L3
(incremental derived lists, above; the Pi: L3b) and L16 (the graph's layout on a worker thread,
drawn through the scene graph, above; the phones: L16b). Also
open: serving brotli for the web build (9.4 instead of 13.7 MB; GitHub Pages serves gzip only) and
the ~90 ms later content on a desktop first start (above).

### How to run the benchmarks

Never time anything on a machine that is building or running another benchmark; use Release
builds; take ≥ 5 repetitions and compare medians with their spread (`(max − min) / median`).

**Core (QtTest `QBENCHMARK`, `tests/bench/`, driven by `scripts/bench.sh`).** One executable per
core module (`bench_text`, `bench_dates`, `bench_calc`, `bench_markdown`, `bench_index`,
`bench_vault`, `bench_sync`, `bench_reminders`, `bench_io`), built only with
`-DBRULION_BUILD_BENCHMARKS=ON` (`bench.sh` configures a Release build dir for you):

```sh
nix develop
JOBS=16 scripts/bench.sh -B -b ../build-bench                  # configure Release + build the suites
scripts/bench.sh -n -b ../build-bench -r 5 -c 0-11 -q 3 -o ../r/new bench_index bench_markdown
scripts/bench.sh -n -b ../build-bench bench_index:search       # one function
scripts/bench.sh -n -b ../build-bench "bench_index:search:10000 notes fuzzy"   # one data row
scripts/bench.sh compare ../r/base ../r/new                    # % change against the noise
scripts/bench.sh scaling ../r/new                              # exponent k between input sizes
scripts/bench.sh -h
```

`-c` pins to CPUs (on a hybrid Intel CPU use the P-cores: `0-11` on the i5-13450HX), `-q` waits
for a quiet machine (1-minute load below the value), `-p small` drops the largest sizes on
Pi/phone-class machines (3.7 GB RAM). `bench.sh` holds the machine-wide build lock for the whole
run. Results: `<dir>/raw/*.csv|txt` (QTest output per repetition), `<dir>/summary.csv` (median,
min, max, spread per row), `<dir>/env.txt` (machine, governor, load, git state, bench source
hash).

* **Stale binaries are refused.** Every suite carries a hash of the `tests/bench` sources it was
  built from (`tests/bench/sourcehash.cmake`, recomputed on every build); `bench.sh` compares it
  with the source tree and stops with exit code 2 on a mismatch. The same step makes Ninja
  rebuild a suite whose source content changed even when a copied tree kept older mtimes than
  the build (the trap behind the wrong "23 ms per keystroke on the Pi").
* **Inputs are identical on every machine.** The generators (`tests/bench/benchdata.h` and the
  suites) draw from the RNG at most once per statement — C++ leaves the order of `a + b`
  operands and of call arguments unspecified, and GCC evaluated them differently on x86_64 and
  aarch64, so the Pi used to measure a 0.7 % different vault. Each generated input prints
  `[bench] input <name>: <bytes> bytes, sha256 <digest>` (and `[bench] vault N notes: … sha256 …`)
  into `raw/*.txt`: compare two machines' lines to prove they measured the same thing.
* To compare two source trees, give both the same `tests/bench` sources (the hash in `env.txt`
  must match), build each in its own directory and run the suites alternately under the same
  conditions (same `-c`/`-q`), then `compare` the two result dirs.

* **Index loading** (wave 5): the app and brulion-cli read the index cache from the device state
  folder; `BRULION_NO_INDEX_CACHE=1` neither reads nor writes it (a cold index on every start),
  `BRULION_INDEX_THREADS=<n>` sets the parse workers (`0`: in slices on the GUI thread, the
  browser's way). The core rows are `bench_index:openFromDisk` "loader …" and "loader warm cache
  …" (the cache written in the row's setup), `cacheEncode`, `cacheDecode`, `prepare`.
* **Before/after on GitHub's runners** (x86_64 and arm64, the flake's toolchain, both trees with
  head's bench sources, two alternating rounds): `gh workflow run perf-compare.yml --ref <branch>
  -f base=<ref> [-f suites="bench_index bench_markdown"] [-f reps=5]` — the comparison lands in
  the job summary and an artifact. Shared runners are noisy: relative changes only. `-f app="views
  graph"` (`-f suites=` for only these) runs app benchmark groups too: both trees' `brulion` in
  Release with Qt from aqt, the probe and the analysis from head, Xvfb + llvmpipe on
  ubuntu-24.04, `app_rounds` × `app_runs` runs per build, alternating (report.py's table in the
  summary). A workflow can call it too (`workflow_call`), e.g. a branch's own push trigger before
  the file reaches the default branch, which `gh workflow run` needs.

**App (the real binary, `tests/bench/app/`).** An `LD_PRELOAD` probe records frames, GUI stalls,
memory and CPU and drives scenarios (startup, open-page, typing, search, switcher, views, graph,
journal-scroll, switch-pages, idle, workload) with real input; no hook in the product code, so
any build (v0.1.1 included) can be measured.

```sh
tests/bench/app/make-bench-vault.py /tmp/vaults/big10k           # deterministic 10k-note vault
brulion --demo --vault /tmp/vaults/demo --screenshot /tmp/x.png  # the sample notebook, once
nix develop -c tests/bench/app/build-probe.sh /tmp/probe
nix develop -c nix shell nixpkgs#xvfb -c env BINS="base=…/brulion cur=build-rel/src/app/brulion" \
  PROBE=/tmp/probe/libbenchprobe.so VAULT_DEMO=/tmp/vaults/demo VAULT_BIG=/tmp/vaults/big10k \
  OUT=/tmp/results RUNS=5 tests/bench/app/bench-matrix.sh startup-big typing views
tests/bench/app/analyze.py /tmp/results --csv /tmp/results.csv
```

On the Pi use `PLATFORM=offscreen` (software renderer), run inside `nix develop` (QML import
paths) and under the build lock, with a 3k-note vault (`make-bench-vault.py … --notes 3000`).
Attribution: `tests/bench/app/profile-cases.sh` (perf with probe windows, qmlprofiler,
heaptrack). Details: [tests/bench/app/README.md](../tests/bench/app/README.md).

**Android and browser (`tests/bench/mobile/`).** APK size reports (`android-apk.sh`,
`size-report.py`), an emulator probe build (a debuggable measurement copy, never shipped),
launcher starts and `dumpsys meminfo` (`android-batch.sh release|probe`), idle and alarms; the
browser through `wasm-bench.mjs` (headless Chromium over CDP: first and repeat visits, an
IndexedDB vault, CPU and network throttling, IDBFS write-back, `index_ready_ms`) and
`wasm-batch.sh`. Details and the full recipe:
[tests/bench/mobile/README.md](../tests/bench/mobile/README.md).

### Regression guards: `scripts/perf-check.sh`

`scripts/perf-check.sh` runs a fixed subset of the core suites through `bench.sh` and fails
(exit 1) when

* a **per-machine guard** row's median exceeds its baseline by more than
  max(10 %, 3 × spread), spread being the larger of the baseline's and this run's — baselines
  are committed per machine class in `tests/bench/baselines/<machine>.csv` (`laptop`, `pi`; the
  file also records the CPU, the bench source hash and the `bench.sh` options it was measured
  with, which the check reuses);
* a **scaling limit** is passed: the exponent k of time ~ n^k between the two largest sizes of
  a series (`tests/bench/baselines/limits.csv`) — a quadratic regression shows k ≈ 2 on any
  machine, while linear code stays at k ≈ 1;
* an **absolute limit** is passed (index heap with 10 000 notes, MiB), or a checked row is
  missing.

```sh
JOBS=16 scripts/perf-check.sh                  # build, run (~5 min on the laptop), check
scripts/perf-check.sh -n -s                    # scaling + memory only (machine-independent; CI)
scripts/perf-check.sh -n -u                    # new baseline after an optimisation (ratchet)
scripts/perf-check.sh -m pi -R <results dir>   # check an existing run
```

Ratchet a baseline down when an optimisation lands (`-u`); never loosen it without a note in
the commit message. A machine without a baseline gets only the limits. The guard rows (keep in
sync with the arrays in the script):

| guard | row | why |
|---|---|---|
| open vault | `bench_index:openFromDisk` 10000 notes scan+read+index | start with a big vault |
| first search | `bench_index:firstSearch` 10000 notes | full-text index build (N1) |
| query | `bench_index:query` 10000 notes tag praca | query blocks |
| suggestions | `bench_index:suggestBlocks` 'kawa ogród', `suggestPages` 'stro' (10000 notes) | `((` and `[[` popups, quick switcher (N7) |
| index memory | `bench_index:memory` 10000 notes + search index | ≈ 10 B of heap per Markdown byte since wave 5 (L8) |
| after a save | `bench_reminders:afterEdit` mixed vault 10000 notes, all consumers | every save's fan-out as the app runs it: the index's patched lists and the consumers' caches (L3; the free functions' "(stateless)" rows are beside it in the suite) |
| keystroke | `bench_markdown:blockDocOps` 5000 blocks keystroke | typing on long pages (L4) |
| undo / redo | `bench_markdown:blockDocOps` 5000 blocks undo+redo | an undo step applied as a delta (L5) |
| UI state store | `bench_vault:localStoreFlush` 10000 synced files | N9 |
| sync | `bench_sync:syncNoChanges` 10000 files (Pi: 5000) | L12 |
| open vault, threads | `bench_index:openFromDisk` 10000 notes loader 4 workers (Pi: 3) | L1 (wave 5) |
| open vault, cache | `bench_index:openFromDisk` 10000 notes loader warm cache 4 workers (Pi: 3) | L2 (wave 5) |

Scaling limits (`limits.csv`): `foldKey` / `foldKeyWithMap` polish k ≤ 1.2 (N1),
`searchLargeNote` k ≤ 1.3 (N1), `parsePathological` inline HTML and one paragraph of `<`
k ≤ 1.3 (N6), `unlinkedLongLine` k ≤ 1.3 (N6), `htmlToMarkdown` flat and nested lists k ≤ 1.3
(N10), and from wave 4 `planRelocation` folder rename and page rename, `tagPageOpen` after a save,
`selectAllCopy` Markdown and plain text, `clipboardHtml` k ≤ 1.3 (measured k 0.98–1.10), from wave
5 `openFromDisk` loader 4 workers and loader warm cache 4 workers, `cacheEncode`, `cacheDecode`, from
L5 `blockDocOps` undo+redo (measured k 1.15) k ≤ 1.3 — 1.5 for all of them in the Pi's small
profile (3 workers); index heap at 10 000 notes
≤ 233 MiB (wave 5: 220.6 MiB measured; it was ≤ 560 MiB before). The Pi baseline has no loader
rows and no undo+redo row yet (record them with `-u` in a quiet window; L1b, L5b).

App-level guards (startup, typing, open-page, views, idle) are not automated: run
`bench-matrix.sh` before a release and compare with the tables above (same rule: medians of 5
runs, max(10 %, 3 × spread)).

**Baselines** (`tests/bench/baselines/laptop.csv`, `pi.csv`) were recorded on 2026-10-18 from a
perf-check run of the current code and checked against an independent second run of the same
code (laptop: 19 of 19 checks pass, every row within ±5 %; Pi: 18 of 18). The same check on
d1141c0 fails as it should (Pi: first search +111 %, `((` suggestions +101 %, LocalStore flush
+119 % and all six scaling limits, k 1.70–4.04), and so does a scratch copy of the current code
with the old quadratic `foldKey` (`src/core/text/text.cpp` of d1141c0; laptop: first search
+91 %, `foldKey` k 2.28, `searchLargeNote` k 2.30 — exactly the three checks it touches).

### CI performance smoke

The `perf` job of `.github/workflows/ci.yml` builds `bench_text`, `bench_index`,
`bench_markdown` and `bench_io` (Release, Qt from aqt) and runs `scripts/perf-check.sh -n -s`
(3 repetitions): only the scaling limits and the index heap, never absolute times, so the
speed of a shared runner does not matter. It is **non-blocking** (`continue-on-error`): a failure
becomes a warning annotation and a job summary, the results are uploaded as an artifact.

Why it can run on shared runners without flaking: on the laptop's 4 E-cores, 16 runs of
`perf-check.sh -s` while other builds ran on the machine — 8 of them with a busy loop competing on
every one of those cores — gave 0 false failures; the largest exponent was k 1.13 against limits
of 1.2–1.3, while 8 of 8 runs of the quadratic `foldKey` copy failed (k 2.24–2.34). The index
heap is deterministic (544.8–544.9 MiB in every run). Absolute timings stay out of CI: runner
speed varies far more than the 10 % a regression check needs. The Pi (small profile, 2–3× size
steps, 1 MB L2 cache) shows k up to 1.27 for linear code, hence its limit of 1.5.

### Pitfalls (learned the hard way)

* **Cold vs warm index** (wave 5): a second run of the same notebook reads the index cache. App
  runs with `--start warm`/`cold` reuse the profile of their warm-up run and so its cache (written
  ~5 s after the index, or at the latest when the app closes); `--start first` is the cold index.
  Compare like with like, and set `BRULION_NO_INDEX_CACHE=1` for a cold index on warm starts.

* **Stale benchmark binaries.** `rsync -a` keeps source mtimes; a changed file that is older than
  the build used not to be recompiled (the "23 ms per keystroke on the Pi" was such a binary).
  Since the source hash it is rebuilt, and `bench.sh` refuses a binary built from other sources.
* **Unsequenced RNG calls** made the x86_64 and aarch64 vaults differ (22 705 922 vs
  22 869 397 bytes for 10 000 notes). Now both machines generate the same 22 869 397 bytes
  (SHA-256 `f638a871803a3058…`), and 24 of 24 inputs compared between the laptop and the Pi are
  byte-identical.
* **Process history matters**: a row measured after other functions of the same suite can differ
  from the same row in a fresh process (`suggestPages 'stro'`: 2.84 vs 3.31 ms). Compare like with
  like: whole-suite runs with whole-suite runs, perf-check runs with perf-check baselines.
* **Xvfb + llvmpipe has no vsync**: an animation (the splash's busy indicator) renders at
  ~800 frames per second and the GUI thread syncs every frame. Frame costs on the laptop are
  pessimistic; check surprising start-up numbers with `QSG_RENDER_LOOP=basic` or on the Pi.
* Pi / emulator / headless-browser numbers are not phone numbers: the Pi paints on the GUI thread
  (software renderer), the emulator's cores are 2–4× faster than phone cores, Chromium's ×4 CPU
  throttling slows the main thread only.
* I/O and allocation-heavy rows (`writeText`, `historySnapshot`, `query` on the Pi, LocalStore on
  the Pi) vary by ±20–100 %: read them as orders of magnitude, and expect wide tolerances there.
* **The emulator's `emu kill` is a power cut**: a vault pushed shortly before came back as 5 000
  empty files after the next boot, and two measurement windows ran on an empty vault without an
  error (PSS 178 MB instead of ~400 MB was the only hint). `emu.sh stop` now syncs the guest
  first and `push-vault` checks for empty files; check the vault after a reboot before trusting
  a big-vault number.
* Profiles (perf DWARF, qmlprofiler) slow the app 1.5–3×: use them for attribution, not timing.
