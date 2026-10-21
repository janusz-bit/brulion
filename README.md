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
<p align="center">
  <img src="docs/images/dziennik.png" width="880" alt="Dziennik w Brulionie: dzisiejsza strona z zadaniem, terminem, przypomnieniem, linkami i tagami, poniżej wczorajsza z blokiem obliczeń wydatków">
  <br><sub><b>Dziennik</b> — strona na każdy dzień: zadania z terminami, przypomnienia, linki, tagi i obliczenia</sub>
</p>

<table>
  <tr>
    <td width="50%" valign="top">
      <img src="docs/images/strona-odnosniki.png" alt="Strona „Remont mieszkania” z tabelą kosztorysu liczoną formułami i blokiem obliczeń; w panelu bocznym odnośniki z dziennika i innych stron">
      <br><sub><b>Strona i odnośniki</b> — tabela z formułami, blok obliczeń, a z boku wszystko, co linkuje do strony</sub>
    </td>
    <td width="50%" valign="top">
      <img src="docs/images/kalendarz.png" alt="Kalendarz miesięczny z zadaniami, terminami, przypomnieniami i alarmem oraz planem wybranego dnia">
      <br><sub><b>Kalendarz</b> — miesiąc, tydzień albo agenda: terminy, przypomnienia, alarmy i zadania cykliczne</sub>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <img src="docs/images/przypomnienie.png" alt="Okno nowego przypomnienia: szybkie terminy, kalendarz, godzina, przełącznik alarmu i powtarzanie">
      <br><sub><b>Przypomnienia i alarmy</b> — szybkie terminy, data po polsku („jutro 9:00”), powtarzanie</sub>
    </td>
    <td width="50%" valign="top">
      <img src="docs/images/zadania-ciemny.png" alt="Lista zadań w ciemnym motywie, pogrupowana na jutro, ten tydzień i później, z terminami, priorytetami i przypomnieniami">
      <br><sub><b>Zadania</b> (motyw ciemny) — wszystkie zadania z notatek, filtry i szybkie dodawanie</sub>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <img src="docs/images/graf-ciemny.png" alt="Graf powiązań między stronami i dniami dziennika w ciemnym motywie">
      <br><sub><b>Graf</b> (motyw ciemny) — linki między stronami i dniami dziennika; na życzenie także tagi</sub>
    </td>
    <td width="50%" valign="top">
      <img src="docs/images/synchronizacja.png" alt="Centrum synchronizacji: notatka zmieniona na dwóch urządzeniach i podgląd automatycznego scalenia obu wersji">
      <br><sub><b>Centrum synchronizacji</b> — konflikt z dwóch urządzeń scalony wiersz po wierszu</sub>
    </td>
  </tr>
</table>

<p align="center">
  <img src="docs/images/telefon-dziennik.png" width="270" alt="Dziennik na telefonie z dolnym paskiem nawigacji i przyciskiem szybkiej notatki">
  &nbsp;&nbsp;&nbsp;
  <img src="docs/images/telefon-edycja-ciemny.png" width="270" alt="Edycja strony na telefonie w ciemnym motywie z paskiem narzędzi nad klawiaturą">
  <br><sub><b>Na telefonie</b> — dziennik z dolną nawigacją oraz edycja z paskiem narzędzi nad klawiaturą (motyw ciemny)</sub>
</p>

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
* **Dokument albo konspekt.** Każda notatka ma swój tryb: **dokument** jak w Obsidianie
  (akapity, nagłówki i zwykłe listy, czytane jak tekst; klikasz i piszesz w miejscu) albo
  **konspekt** jak w Logseq (każdy punkt to blok). Tryb wybierasz przy tworzeniu strony
  i przełączasz w każdej chwili; zapisuje się w notatce jako `brulion: document` /
  `brulion: outline`, a notatki z Obsidiana i Logseq same otwierają się we właściwym trybie.
* **Zadania, kalendarz, przypomnienia.** `- [ ] Zadanie 📅 2026-10-20 ⏰ 2026-10-19 18:00`,
  powtarzanie `🔁 co tydzień`, alarmy `⏰!` (dzwonią jak budzik, aż je wyłączysz albo minie
  ustawiony czas — na Androidzie także przy zamkniętej aplikacji i po restarcie telefonu),
  widok kalendarza i listy zadań. Przypomnienia, które minęły przy wyłączonym urządzeniu,
  czekają w dzienniku. Daty wpisujesz po polsku: „jutro o 9”, „w piątek 18:00”, „za 2 godziny”.
* **Obliczenia.** Bloki ` ```calc ` liczą jak kalkulator w notatniku (zmienne, procenty,
  jednostki `5 km to mi`, waluty `12 zł + 5 zł`, `suma`), tabele z formułami jak w arkuszu
  (`=sum(above)`, `=[Cena]*[Ilość]`), obliczenia w tekście `` `= 2*21` ``.
* **Wzory matematyczne.** LaTeX w notatkach (`$E=mc^2$`, `$$\frac{a}{b}$$`, blok
  ` ```math `) jest od razu rysowany jako wzór — greckie litery, ułamki, pierwiastki,
  macierze, układy równań. Plik zachowuje źródło LaTeX, a eksport HTML pokazuje gotowy wzór.
* **Zapytania.** Blok ` ```query ` pokazuje na żywo np. wszystkie otwarte zadania z tagiem
  `#praca` na najbliższy tydzień: `status:open tag:praca due:<=+7d sort:due` (klucze także po
  polsku: `stan:otwarte termin:jutro`).
* **Import i eksport.** Import z Obsidiana, Logseq, Notion, Evernote, Google Keep, Joplin,
  Simplenote, Roam, Trilium, Bear, plików Markdown/TXT, HTML i kalendarzy iCalendar — także na
  telefonie (*Więcej → Importuj notatki*: pliki, ZIP albo cały folder skarbca, z podglądem przed
  zapisem; [docs/IMPORT.md](docs/IMPORT.md) — m.in. jak wyeksportować notatki z Trilium i co się
  z czym zamienia; [docs/ANDROID.md](docs/ANDROID.md#import-notatek-obsidian-logseq-notion)).
  Eksport do ZIP z Markdownem, formatu Obsidiana i Logseq, statycznej strony HTML lub jednego
  pliku HTML, JSON, iCalendar, OPML i CSV zadań.
* **Kompatybilność.** Składnia zadań zgodna z wtyczką Obsidian Tasks, właściwości `klucz::
  wartość` jak w Logseq/Dataview — szczegóły w [docs/SYNTAX.md](docs/SYNTAX.md).
* **Historia wersji i kosz.** Zanim notatka zostanie nadpisana, jej poprzednia wersja trafia
  do historii na urządzeniu (przy pisaniu najwyżej co 5 minut, zawsze przed zmianami
  z synchronizacji i importu). Każdą wersję możesz porównać i przywrócić, a usunięte strony
  odzyskać z kosza.
* **Na start: przykładowy brulion.** Strona „Witaj w Brulionie” pokazuje wszystkie funkcje na
  żywych przykładach — do klikania, edytowania i psucia.
* **Dokumentacja w środku.** Pełny poradnik po polsku to osobny notatnik „Dokumentacja
  Brulionu” (na komputerze i Androidzie): przycisk „Przełącz notatnik” obok nazwy notatnika
  albo `Ctrl+Shift+B` i z powrotem. Aktualizuje się razem z aplikacją, a Twoje zmiany w nim
  zostają; w przeglądarce te same strony są w repozytorium
  ([src/app/shell/guide](src/app/shell/guide)).

## Instalacja

| Platforma | Jak |
|---|---|
| **NixOS** | flake + moduł `programs.brulion` (z autostartem w zasobniku) — [docs/NIXOS.md](docs/NIXOS.md) |
| **Inne Linuksy** | AppImage (x86_64, aarch64) z [Wydań](https://github.com/janusz-bit/brulion/releases) |
| **Android** (9+) | plik APK (arm64) z [Wydań](https://github.com/janusz-bit/brulion/releases) — [docs/ANDROID.md](docs/ANDROID.md) |
| **Przeglądarka** | <https://janusz-bit.github.io/brulion/> (działa offline po pierwszym otwarciu) — [docs/WEB.md](docs/WEB.md); na własnym serwerze: `brulion-web-<wersja>.zip` z Wydań albo moduł `services.brulion-web` |

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

**Nextcloud (WebDAV):** w *Centrum synchronizacji* (menu *Więcej* ⋮ lub *Ustawienia →
Synchronizacja*) podaj adres serwera, użytkownika i **hasło aplikacji** (Nextcloud →
Ustawienia osobiste → Bezpieczeństwo → „Utwórz nowe hasło aplikacji”) oraz folder (przy
pierwszym koncie proponowany `Notatki`, dla kolejnych notatników ich nazwa); „Testuj
połączenie” wyjaśnia ewentualne błędy. Serwer i konto są wspólne, ale włączenie synchronizacji
i folder na serwerze należą do notatnika: inny notatnik (także dokumentacja) startuje bez
synchronizacji, a folderu, z którym synchronizuje się już inny notatnik na tym urządzeniu,
Brulion nie zapisze. Brulion synchronizuje w tle
(domyślnie co 15 minut), chwilę po zmianach i po powrocie sieci; równoległe dopiski
w dzienniku z dwóch urządzeń scalają się same. Ochrona przed masowym usuwaniem wstrzymuje
kasowanie, jeśli synchronizacja miałaby usunąć dużą część notatek (ponad 25%, co najmniej
10 plików) — dopóki nie potwierdzisz listy.

W zakładce *Nextcloud* widać na żywo, co robi trwająca synchronizacja (etap, „folder 12 z
ok. 40”, aktualne pliki, czas trwania, ilość danych), ostrzeżenie, gdy serwer przestaje
odpowiadać (po 3 minutach bez odpowiedzi synchronizacja jest przerywana i ponawiana — nigdy nie
„synchronizuje się” w nieskończoność), odliczanie do kolejnej próby oraz **historię ostatnich 20
synchronizacji**: kiedy i dlaczego się zaczęły, ile trwały, co przesłały, z błędami po polsku.
„Szczegóły” pokazują, na co poszedł czas (łączenie, sprawdzanie plików na serwerze i na
urządzeniu, przesyłanie, zapis), żądania do serwera i pliki, których nie udało się
zsynchronizować.

## Gdy coś nie działa — rejestr zdarzeń

Brulion zapisuje, co robi — zwłaszcza synchronizacja — w **rejestrze zdarzeń**: *Ustawienia →
Aplikacja → Rejestr zdarzeń*, *Ustawienia → Synchronizacja*, przycisk „Rejestr zdarzeń”
w zakładce *Nextcloud* centrum synchronizacji albo paleta poleceń (Ctrl+K, „rejestr”, „logi”).
Wpisy można filtrować (Synchronizacja, Sieć, Notatnik…, tylko ostrzeżenia i błędy) i
przeszukiwać (bez polskich znaków też: „zazolc” znajdzie „Zażółć”); widać też wcześniejsze
uruchomienia aplikacji.

**Jak wysłać nam rejestr:** na telefonie *Udostępnij* (np. do poczty lub komunikatora) albo
*Zapisz w Pobranych*; na komputerze *Zapisz jako…* (pliki leżą też w
`~/.local/state/Brulion/Brulion/logs/brulion.log`, „Otwórz folder”); w przeglądarce *Pobierz*;
wszędzie *Kopiuj*. Gdy synchronizacja sprawia kłopot, włącz najpierw **Szczegółowy rejestr**
(zapisuje też każde żądanie do serwera), powtórz synchronizację i dopiero wtedy wyślij rejestr.

**Co zawiera:** czas, rodzaj i przebieg zdarzeń — adres serwera i nazwę użytkownika
Nextcloud, nazwy i ścieżki notatek, których dotyczą zmiany i błędy, folder notatnika, wersję
aplikacji i systemu. **Czego nie zawiera:** haseł, haseł aplikacji, tokenów, ciasteczek ani
treści notatek — Brulion usuwa je, zanim cokolwiek trafi do rejestru (także z adresów w
rodzaju `https://jan:hasło@…`). Rejestr zostaje na urządzeniu (dwa pliki po 1 MB, 3 MB w
trybie szczegółowym) i nigdy nie jest nigdzie wysyłany — udostępniasz go tylko Ty. *Wyczyść*
usuwa go w całości.

## Skróty klawiszowe

| Skrót | Działanie |
|---|---|
| `Ctrl+K` | paleta poleceń |
| `Ctrl+O` | szybkie przejście do strony |
| `Ctrl+J` | dzisiejszy dziennik |
| `Ctrl+N` / `Ctrl+Shift+N` | nowa strona / szybka notatka do dziennika |
| `Ctrl+Shift+B` | dokumentacja Brulionu / powrót do poprzedniego notatnika |
| `Ctrl+Shift+F` | wyszukiwanie |
| `Alt+←` / `Alt+→` | wstecz / dalej |
| `Enter` / `Shift+Enter` | nowy blok / nowa linia w bloku |
| `Tab` / `Shift+Tab` | wcięcie / wysunięcie bloku z dziećmi |
| `Alt+↑` / `Alt+↓` | przeniesienie bloku z dziećmi |
| `Ctrl+Enter` | zmiana stanu zadania (do zrobienia → w toku → zrobione) |
| `Ctrl+.` | zwiń / rozwiń |
| `Ctrl+Z` / `Ctrl+Y` | cofnij / ponów |
| `Ctrl+E` | tryb źródła Markdown |
| `F2` / `Ctrl+,` | zmiana nazwy strony / ustawienia |
| `/` | menu poleceń (zadanie, przypomnienie, alarm, tabela, obliczenia, zapytanie, obraz…) |

W trybie dokumentu `Tab` / `Shift+Tab` działa tylko na punktach listy, `Enter` na pustym
punkcie kończy listę (w liście zagnieżdżonej: wysuwa punkt o poziom), a `Backspace` na początku
punktu usuwa znacznik listy, jak w Obsidianie.

Na telefonie wcięcia, przenoszenie bloków, zadania, linki, tagi, polecenia, terminy,
przypomnienia i cofanie są w pasku narzędzi nad klawiaturą, a widoki bez własnego przycisku
(centrum synchronizacji, import i eksport, kosz, ustawienia) — w menu *Więcej* (⋮).
Wszystkie skróty: [docs/UI.md](docs/UI.md#keyboard-shortcuts-desktop).

## Dla programistów

C++20 + Qt 6.11 (Qt Quick/QML), CMake, testy QtTest. Logika (parser Markdown, indeks
i wyszukiwarka, kalkulator, daty, synchronizacja, przypomnienia, import/eksport) to
biblioteki bez GUI w `src/core`, wspólne dla Linuksa, Androida i WebAssembly.

```sh
nix develop                      # Qt, CMake, Ninja, ccache, clang-tools
scripts/dev-build.sh -t .        # zbuduj i uruchom wszystkie testy (JOBS=8 — więcej wątków)
./build/src/app/brulion          # uruchom aplikację
./build/src/app/brulion --demo   # … z przykładowym brulionem
```

APK Androida z flake'a (na hoście x86_64-linux, niepodpisany): `nix build .#brulion-android`
(arm64-v8a; `.#brulion-android-x86_64` dla emulatora, `.#brulion-android-aab` z AAB), podpis:
`nix run .#brulion-android-sign -- result/*.apk` — patrz
[docs/ANDROID.md](docs/ANDROID.md#budowanie-przez-nix-flake).

Zrzuty ekranu i testy interfejsu działają bez okna (`--screenshot`, `--journey`, skrypty
`tests/app/*/screenshots.sh`) — patrz [docs/UI.md](docs/UI.md#dev-tools-screenshots-and-ui-tests).

Dokumentacja: [architektura](docs/ARCHITECTURE.md) · [składnia](docs/SYNTAX.md) ·
[interfejs](docs/UI.md) · [Android](docs/ANDROID.md) · [przeglądarka](docs/WEB.md) ·
[NixOS](docs/NIXOS.md) · [wydania](docs/RELEASE.md). Poprzednia wersja (TypeScript +
Tauri) jest zachowana na gałęzi `archive/web-v1`.

Wydajność (liczby na komputerze, telefonie i w przeglądarce, benchmarki, strażnicy regresji): [docs/PERFORMANCE.md](docs/PERFORMANCE.md).

## Licencja

[AGPL-3.0-or-later](LICENSE). Ikony: [Lucide](https://lucide.dev) (ISC). Kompresja ZIP:
[miniz](https://github.com/richgel999/miniz) (MIT).

---

## English

**Brulion** (Polish for “rough notebook”) is an offline-first Markdown notes app: Logseq-style
daily journals and block outliner on top of plain Obsidian-compatible `.md` files (every note is
either an Obsidian-like document or a Logseq-like outline, switchable at any time), with a
calendar, reminders and real alarms, calculation blocks and spreadsheet-like table formulas,
live queries, version history, and import/export for Obsidian, Logseq, Notion, Evernote,
Google Keep, Joplin, Simplenote, Roam, Trilium, Bear, HTML and iCalendar.

* Runs on **NixOS/Linux** (flake + NixOS module, AppImage), **Android** 9+ (APK; native exact
  alarms that fire while the app is closed) and in the **browser** (Qt for WebAssembly,
  works offline).
* Sync through **Syncthing** (it's just a folder; conflict copies are detected and merged in
  the app) or **Nextcloud/WebDAV** (built-in background sync with 3-way merge and a
  mass-deletion guard).
* Two note modes: a **document** (Obsidian-like live preview: paragraphs, headings and plain
  lists, edited in place) or an **outline** (Logseq-like: every bullet is a block). Chosen when
  a page is created and switchable at any time; stored in the note as `brulion: document` /
  `brulion: outline` frontmatter, and notes from Obsidian or Logseq open in the right mode
  without it.
* Polish UI by default, English available. Natural-language dates in Polish and English.
* Something not working? Settings → Application → **Event log** shows what the app and the sync
  did (live sync progress and the history of runs are in the sync center); share or save it
  and send it to us — it stays on the device and never contains passwords, tokens or note
  contents.
* Built with C++20 and Qt 6.11 Quick; GUI-free core libraries with thousands of unit tests.

The screenshots above show the Polish UI with the bundled sample notebook (`brulion --demo`).
Install: see the table above (`nix run github:janusz-bit/brulion`, AppImage, APK, or
<https://janusz-bit.github.io/brulion/>). Build: `nix develop`, then
`scripts/dev-build.sh -t .`; the Android APK: `nix build .#brulion-android` (x86_64-linux hosts,
unsigned; sign with `nix run .#brulion-android-sign -- result/*.apk`). Developer docs (in
English): [architecture](docs/ARCHITECTURE.md), [Markdown dialect](docs/SYNTAX.md),
[UI](docs/UI.md). License: AGPL-3.0-or-later.

Performance (numbers per platform, budgets, how to benchmark, regression guards): [docs/PERFORMANCE.md](docs/PERFORMANCE.md).
