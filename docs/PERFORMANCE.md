# Wydajność Brulionu

Stan na 2026-10-18: obecny kod (gałąź `main` z pakietem optymalizacji z października 2026)
porównany z wydaniem **v0.1.1** (`f61a2a4`) i z **d1141c0** (ostatni commit przed
optymalizacjami). Wszystkie trzy wersje zmierzono w tych samych warunkach i tymi samymi
benchmarkami; jak je uruchomić — w części dla współtwórców (po angielsku, niżej).

## Podsumowanie dla użytkowników

### Co się poprawiło

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

### Jak jest teraz

**Na komputerze** (laptop z 2023 r., notatnik 10 000 notatek): okno po 0,2 s, treść po 0,4 s,
cały notatnik zaindeksowany po ok. 2 s (w tym czasie można już czytać i pisać). Strona z 5000
bloków otwiera się w ok. 0,1 s, wyszukiwanie zwraca wyniki w kilka–kilkanaście milisekund,
Zadania/Kalendarz/Agenda otwierają się w 0,13–0,17 s. Bezczynny Brulion zużywa ok. 0,1 %
procesora i nie rysuje klatek.

**Na telefonie** (mierzone na Raspberry Pi 4, który odpowiada słabszemu telefonowi, i na
emulatorze Androida): zwykłe notatniki działają płynnie — start z przykładowym notatnikiem ok.
1,3 s, pisanie na zwykłej stronie bez opóźnień. Duży notatnik (10 000 notatek) pokazuje treść po
ok. 1,6 s, ale indeksuje się ok. 15 s (wyszukiwanie w tym czasie najpierw dokańcza indeks,
więc pierwsze wyniki mogą chwilę potrwać).

**W przeglądarce**: pierwsza wizyta pobiera ok. 14 MB (później strona działa z pamięci
podręcznej i offline), powrót do strony z 1000 notatek rysuje pierwszą klatkę po ok. 0,6 s.

### Znane wolne miejsca

* **Indeksowanie dużych notatników na telefonie** — ok. 15 s dla 10 000 notatek (praca
  w tle, w krótkich kawałkach; w tym czasie interfejs czasem przycina się na ok. 0,5 s).
* **Pamięć** — notatnik 10 000 notatek zajmuje ok. 430–500 MB. Na telefonach z 3–4 GB RAM
  system może częściej zamykać Brulion w tle (notatki są zawsze zapisane na dysku).
* **Po każdym zapisie** listy zadań, kalendarz, przypomnienia i bloki zapytań przeliczają się od
  nowa: przy 10 000 notatkach to ok. 0,13 s pracy na komputerze i ok. 1 s na telefonie.
* **Cofanie (Ctrl+Z) na bardzo długiej stronie** trwa na telefonie ok. 0,2 s; Kalendarz
  i Agenda otwierają się na telefonie przy dużym notatniku po 1–1,3 s.
* **Pierwszy start na komputerze z małym notatnikiem** (świeży profil) pokazuje treść ok.
  0,09 s później niż d1141c0 (okno za to 0,1 s wcześniej); przy kolejnych startach treść jest
  tak szybko jak wcześniej (szczegóły niżej).
* **Rozmiar** — APK ma ok. 85 MB (pobranie ok. 28,5 MB), wersja przeglądarkowa pobiera ok.
  14 MB.

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

### Budgets

Desktop = the laptop above, phone = Pi 4 (floor) and the emulator (optimistic), web = headless
Chromium (×4 CPU = phone). ✅ meets, ⚠️ within 1.5×, ❌ misses (current code).

| budget | target | desktop | phone | web |
|---|---|---|---|---|
| start → first content | desktop < 1 s, phone < 2 s | ✅ 0.40 s (10k), 0.39 s demo | ✅ Pi demo 1.25 s, 10k 1.57 s | ✅ repeat 0.61 s, ×4 CPU 1.64 s; ❌ first visit at 20 Mbit/s 7.7 s |
| index ready | desktop < 2 s at 10k; phone < 10 s at 10k | ✅ 1.93 s | ❌ Pi 15 s (L1, L2) | ✅ 1k 1.2 s (×4 CPU 4.3 s) |
| GUI stall while indexing / after ready | < 50 ms desktop, < 100 ms phone | ✅ 31 ms indexing; ⚠️ 59 ms after ready | ❌ Pi 0.54 / 0.45 s (L1, L18) | – |
| keystroke → frame (≤ 5000 blocks) | p95 < 16 ms desktop, < 33 ms phone | ✅ small page; ⚠️ 5000-block outline p95 23 ms | ✅ small page; ⚠️ outline p95 42 ms | – |
| Enter / undo | < 50 ms desktop, < 100 ms phone | ✅ 25 / 46 ms | ✅ Enter 38 ms; ❌ undo 214 ms (L5) | – |
| autosave stall | < 50 ms desktop, < 100 ms phone | ✅ small page 41 ms; ❌ 500 KB page 88 ms | ✅ small page 29 ms; ❌ 500 KB page 0.2 s | – |
| open page (5000 blocks) | frame < 150 ms desktop, < 300 ms phone | ✅ 113 ms | ❌ Pi 653 ms (L7) | – |
| Tasks / Calendar / Agenda | < 200 ms desktop, < 500 ms phone | ✅ 133–166 ms | ❌ Pi 0.73–1.28 s (L3, L10) | – |
| Graph | settled < 1 s | ✅ 0.77 s | ❌ Pi 2.6 s (was 44 s; L16); emulator 1.5 s | – |
| after a save, all derived lists (10k) | < 30 ms desktop, < 150 ms phone | ❌ 131 ms (L3) | ❌ Pi 0.95 s (L3) | – |
| idle | < 0.5 % CPU | ✅ 0.1 % | ✅ background 0 % CPU, 0 wakeups/s | – |
| memory | ≤ 100 MB + 10 B per Markdown byte (10k ≤ 330 MB) | ❌ 495 MB (L8) | ❌ emulator PSS 359 MB at 5k (< 300 MB wanted) | ✅ 104 MB of linear memory at 1k |
| download | APK < 40 MB, web < 10 MB | – | ✅ 28.5 MB download (84.9 MB installed) | ❌ 13.7 MB gzip (9.4 MB brotli, not served) |

### Next steps

The L-items of the October 2026 performance plan, with the numbers they should move:

| # | change | expected |
|---|---|---|
| L1 | parse notes on worker threads while indexing, merge into `VaultIndex` in batches | Pi 10k index 15 s → ~5 s, no indexing stall > 50 ms |
| L2 | persistent parsed-index + postings cache keyed by path, size, mtime, hash, parser version | warm start ≈ the scan; non-blocking first search |
| L3 | incremental derived lists (Tasks, Calendar, reminders, query blocks) | after a save 131 ms → < 30 ms (Pi 0.95 s → < 0.2 s) |
| L4 | `BlockDocument` fast path for body-only edits | keystroke core 1.3 ms → < 0.3 ms |
| L5 | delta undo/redo | Pi undo 214 ms → < 50 ms |
| L7 | cheaper page opening (header summary and linked references from the index) | Pi open 653 ms → ~400 ms |
| L8 | `ParsedNote` memory diet (`Block::raw` as offsets, `Task` out of line) | heap −40 % (10k ≈ 300 MB) |
| L9 | native file pickers instead of QtQuick.Dialogs (its per-style QML pulls in every style) | APK −15 MB, wasm −5 MB |
| L10 | lazy cold views and dialogs, `BIcon` without a `Loader` | Pi demo first content −0.3 s |
| L16 | graph layout on a worker, scene-graph rendering | smooth graph on phones |
| L12 | sync: size + mtime quick check for unchanged text files | no-change sync ≥ 3× faster (laptop 10k 419 ms, Pi 5k 1.35 s) |
| L18 | search indexing of big documents in bounded slices | no stall > 16 ms after ready |

Also open: serving brotli for the web build (9.4 instead of 13.7 MB; GitHub Pages serves gzip
only) and the ~90 ms later content on a desktop first start (above).

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

**App (the real binary, `tests/bench/app/`).** An `LD_PRELOAD` probe records frames, GUI stalls,
memory and CPU and drives scenarios (startup, open-page, typing, search, switcher, views,
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
| index memory | `bench_index:memory` 10000 notes + search index | ≈ 25 B of heap per Markdown byte today (L8) |
| after a save | `bench_reminders:afterEdit` mixed vault 10000 notes, all consumers | every save's fan-out (L3) |
| keystroke | `bench_markdown:blockDocOps` 5000 blocks keystroke | typing on long pages (L4) |
| UI state store | `bench_vault:localStoreFlush` 10000 synced files | N9 |
| sync | `bench_sync:syncNoChanges` 10000 files (Pi: 5000) | L12 |

Scaling limits (`limits.csv`): `foldKey` / `foldKeyWithMap` polish k ≤ 1.2 (N1),
`searchLargeNote` k ≤ 1.3 (N1), `parsePathological` inline HTML and one paragraph of `<`
k ≤ 1.3 (N6), `unlinkedLongLine` k ≤ 1.3 (N6), `htmlToMarkdown` flat and nested lists k ≤ 1.3
(N10) — 1.5 for all of them in the Pi's small profile; index heap at 10 000 notes ≤ 560 MiB.

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
