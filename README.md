<p align="center">
  <img src="assets/logo.svg" width="96" alt="Logo Brulionu">
</p>

<h1 align="center">Brulion</h1>

<p align="center">
  <b>Notatki w Markdownie, które są Twoje.</b><br>
  Dziennik i outliner jak w Logseq, zwykłe pliki jak w Obsidianie, kalendarz, przypomnienia
  z alarmami, obliczenia i tabele z formułami. Działa bez internetu — na NixOS, Androidzie
  i w przeglądarce. Synchronizacja przez Syncthing albo Nextcloud.
</p>

<p align="center">
  <a href="https://github.com/janusz-bit/brulion/actions/workflows/ci.yml"><img src="https://github.com/janusz-bit/brulion/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="https://github.com/janusz-bit/brulion/releases"><img src="https://img.shields.io/github/v/release/janusz-bit/brulion?include_prereleases&label=wydanie" alt="Wydanie"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/licencja-AGPL--3.0-blue" alt="Licencja AGPL-3.0"></a>
</p>

<!-- SCREENSHOTS -->

## Dlaczego Brulion?

* **Twoje pliki, zawsze.** Każda notatka to zwykły plik `.md` w folderze. Żadnej bazy danych,
  żadnego konta, żadnej chmury pośrednika. Ten sam folder możesz otworzyć w Obsidianie,
  Logseq czy dowolnym edytorze tekstu.
* **Offline przede wszystkim.** Wszystko działa bez sieci. Synchronizacja dzieje się w tle,
  gdy sieć jest dostępna, i nigdy nie blokuje pisania. Konflikty są scalane automatycznie
  (scalanie trójstronne), a gdy się nie da — obie wersje zostają i dostajesz wygodny widok
  scalania.
* **Dziennik + outliner.** Codzienna strona dziennika, bloki z wcięciami (Tab / Shift+Tab),
  zwijanie, przybliżanie bloku, przenoszenie bloków z dziećmi, odwołania do bloków
  `((…))`, linki `[[…]]`, tagi `#…`, odnośniki zwrotne.
* **Zadania, kalendarz, przypomnienia.** `- [ ] Zadanie 📅 2026-10-20 ⏰ 2026-10-19 18:00`,
  powtarzanie `🔁 co tydzień`, alarmy `⏰!` (głośne, aż do wyłączenia — na Androidzie działają
  także przy zamkniętej aplikacji i po restarcie telefonu), widok kalendarza i listy zadań.
  Daty wpisujesz po polsku: „jutro o 9”, „w piątek 18:00”, „za 2 godziny”.
* **Obliczenia.** Bloki ` ```calc ` liczą jak kalkulator w notatniku (zmienne, procenty,
  jednostki `5 km to mi`, waluty `12 zł + 5 zł`, `suma`), tabele z formułami jak w arkuszu
  (`=sum(above)`, `=[Cena]*[Ilość]`), obliczenia w tekście `` `= 2*21` ``.
* **Zapytania.** Blok ` ```query ` pokazuje na żywo np. wszystkie otwarte zadania z tagiem
  `#praca` na najbliższy tydzień.
* **Import i eksport.** Import z Obsidiana, Logseq, Notion, Evernote, Google Keep, Joplin,
  Simplenote, Roam, Bear, plików Markdown/TXT, HTML i kalendarzy iCalendar. Eksport do ZIP
  z Markdownem, formatu Obsidiana i Logseq, statycznej strony HTML, JSON, iCalendar, OPML
  i CSV zadań.
* **Kompatybilność.** Składnia zadań zgodna z wtyczką Obsidian Tasks, właściwości `klucz::
  wartość` jak w Logseq/Dataview — szczegóły w [docs/SYNTAX.md](docs/SYNTAX.md).
* **Historia wersji i kosz.** Każda zmiana jest zapisywana lokalnie; dowolną wcześniejszą
  wersję możesz porównać i przywrócić.

## Instalacja

| Platforma | Jak |
|---|---|
| **NixOS** | flake + moduł `programs.brulion` (z autostartem w zasobniku) — [docs/NIXOS.md](docs/NIXOS.md) |
| **Inne Linuksy** | AppImage z [Wydań](https://github.com/janusz-bit/brulion/releases) |
| **Android** (9+) | plik APK z [Wydań](https://github.com/janusz-bit/brulion/releases) — [docs/ANDROID.md](docs/ANDROID.md) |
| **Przeglądarka** | <https://janusz-bit.github.io/brulion/> (działa offline po pierwszym otwarciu) — [docs/WEB.md](docs/WEB.md) |

### NixOS w skrócie

```nix
# flake.nix systemu
inputs.brulion.url = "github:janusz-bit/brulion";

# w modules = [ … ]
brulion.nixosModules.default
{ programs.brulion = { enable = true; autostart = true; }; }
```

Albo bez instalowania: `nix run github:janusz-bit/brulion`.

## Synchronizacja

**Syncthing (polecane do pracy offline):** wskaż w Brulionie folder synchronizowany przez
Syncthing (na Androidzie: Syncthing-Fork, np. `/storage/emulated/0/Brulion`, i nadaj
aplikacji „Dostęp do wszystkich plików”). Brulion zapisuje pliki atomowo, wykrywa kopie
konfliktów Syncthinga (`*.sync-conflict-*`) i pozwala je scalić. Opcjonalnie pokazuje stan
synchronizacji z API Syncthinga.

**Nextcloud (WebDAV):** w *Synchronizacja* podaj adres serwera, użytkownika i **hasło
aplikacji** (Nextcloud → Ustawienia → Bezpieczeństwo → „Utwórz nowe hasło aplikacji”) oraz
folder (domyślnie `Notatki`). Brulion synchronizuje w tle co kilka minut i po zmianach;
równoległe dopiski w dzienniku z dwóch urządzeń scalają się same. Ochrona przed masowym
usuwaniem wstrzymuje synchronizację, jeśli miałaby skasować dużą część notatek.

## Skróty klawiszowe

| Skrót | Działanie |
|---|---|
| `Ctrl+K` | paleta poleceń |
| `Ctrl+O` | szybkie przejście do strony |
| `Ctrl+J` | dzisiejszy dziennik |
| `Ctrl+Shift+F` | wyszukiwanie |
| `Enter` / `Shift+Enter` | nowy blok / nowa linia w bloku |
| `Tab` / `Shift+Tab` | wcięcie / wysunięcie bloku z dziećmi |
| `Alt+↑` / `Alt+↓` | przeniesienie bloku |
| `Ctrl+Enter` | zmiana stanu zadania |
| `Ctrl+.` | zwiń / rozwiń |
| `Ctrl+E` | tryb źródła Markdown |
| `/` | menu poleceń (zadanie, przypomnienie, alarm, tabela, obliczenia, zapytanie…) |

## Dla programistów

C++20 + Qt 6.11 (Qt Quick/QML), CMake, testy QtTest. Logika (parser Markdown, indeks
i wyszukiwarka, kalkulator, daty, synchronizacja, przypomnienia, import/eksport) to
biblioteki bez GUI w `src/core`, wspólne dla Linuksa, Androida i WebAssembly.

```sh
nix develop                      # Qt, CMake, Ninja, ccache, clang-tools
scripts/dev-build.sh -t .        # zbuduj i uruchom wszystkie testy
./build/src/app/brulion          # uruchom aplikację
```

Dokumentacja: [architektura](docs/ARCHITECTURE.md) · [składnia](docs/SYNTAX.md) ·
[interfejs](docs/UI.md) · [Android](docs/ANDROID.md) · [przeglądarka](docs/WEB.md) ·
[NixOS](docs/NIXOS.md) · [wydania](docs/RELEASE.md). Poprzednia wersja (TypeScript +
Tauri) jest zachowana na gałęzi `archive/web-v1`.

## Licencja

[AGPL-3.0-or-later](LICENSE). Ikony: [Lucide](https://lucide.dev) (ISC). Kompresja ZIP:
[miniz](https://github.com/richgel999/miniz) (MIT).

---

## English

**Brulion** (Polish for “rough notebook”) is an offline-first Markdown notes app: Logseq-style
daily journals and block outliner on top of plain Obsidian-compatible `.md` files, with a
calendar, reminders and real alarms, calculation blocks and spreadsheet-like table formulas,
live queries, version history, and import/export for Obsidian, Logseq, Notion, Evernote,
Google Keep, Joplin, Simplenote, Roam, Bear, HTML and iCalendar.

* Runs on **NixOS/Linux** (flake + NixOS module, AppImage), **Android** 9+ (APK; native exact
  alarms that fire while the app is closed) and in the **browser** (Qt for WebAssembly,
  works offline).
* Sync through **Syncthing** (it's just a folder; conflict copies are detected and merged in
  the app) or **Nextcloud/WebDAV** (built-in background sync with 3-way merge and a
  mass-deletion guard).
* Polish UI by default, English available. Natural-language dates in Polish and English.
* Built with C++20 and Qt 6.11 Quick; GUI-free core libraries with thousands of unit tests.

Install: see the table above. Build: `nix develop`, then `scripts/dev-build.sh -t .`.
License: AGPL-3.0-or-later.
