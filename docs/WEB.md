# Brulion w przeglądarce (WebAssembly)

Wersja przeglądarkowa to ta sama aplikacja C++/QML co na NixOS i Androidzie, skompilowana
przez **Qt for WebAssembly** (jednowątkowo). Po pierwszym wczytaniu działa **offline** jako
PWA (można ją „zainstalować” z menu przeglądarki), a notatki trzyma w **IndexedDB**
przeglądarki.

* Wdrożenie docelowe: GitHub Pages pod `https://janusz-bit.github.io/brulion/` (działa też
  dowolny serwer plików statycznych, pod dowolną ścieżką — wszystkie adresy są względne).
* Ograniczenia wobec aplikacji natywnych: przypomnienia tylko gdy karta/PWA jest otwarta,
  brak dostępu do folderów na dysku (import/eksport plików zamiast tego), synchronizacja z
  Nextcloud wymaga CORS (niżej), Syncthing niedostępny. Qt for WebAssembly nie ma bazy stref
  czasowych: przy imporcie kalendarza (`.ics`) godziny ze strefą (`TZID`) są brane jako czas
  lokalny (godziny w UTC przelicza się normalnie). Przeglądarka ma jeden notatnik, więc nie ma
  w niej notatnika „Dokumentacja Brulionu”: strona powitalna przykładowego brulionu odsyła do
  dokumentacji w repozytorium (`src/app/shell/guide/`).

## Wersje narzędzi

| Składnik | Wersja | Skąd |
|---|---|---|
| Qt for WebAssembly | 6.11.2 `wasm_singlethread` | `aqt install-qt all_os wasm 6.11.2 wasm_singlethread` (aqtinstall 3.3.0) |
| Qt hosta (moc, rcc, qmlcachegen, lrelease…) | 6.11.2 `linux_gcc_64` | `aqt install-qt linux desktop 6.11.2 linux_gcc_64` |
| Emscripten | **4.0.7** | `emsdk install 4.0.7 && emsdk activate 4.0.7` |

Qt 6.11 wymaga **dokładnie** Emscriptena 4.0.7 (dokumentacja
[doc.qt.io/qt-6.11/wasm.html](https://doc.qt.io/qt-6.11/wasm.html): „The supported version of
Emscripten for 6.11.2 is Emscripten 4.0.7”; to samo w `QT_EMCC_RECOMMENDED_VERSION` w
`lib/cmake/Qt6/QtPublicWasmToolchainHelpers.cmake`). Emscripten nie gwarantuje zgodności ABI
między wersjami, więc inny emsdk (np. `emscripten` z nixpkgs, obecnie 6.0.x) się nie nadaje.
Skrypt budujący sprawdza wersję `emcc` i przerywa przy niezgodności. Przy podbiciu Qt zmień
razem `QT_VERSION` i `EMSDK_VERSION` w `scripts/wasm-build.sh`.

Paczka `all_os/wasm` z aqt jest budowana na Windows: jej skrypty `bin/qt-cmake` nie są
wykonywalne i wskazują na `C:/…`. Dlatego budujemy zwykłym `cmake` z plikiem
`lib/cmake/Qt6/qt.toolchain.cmake`, który znajduje Emscripten przez zmienną `EMSDK`.

## Budowanie

```sh
scripts/wasm-build.sh --setup   # pierwszy raz: emsdk 4.0.7 + Qt 6.11.2 (wasm + host) do ~/.cache
scripts/wasm-build.sh           # konfiguracja + build + złożenie strony w dist-wasm/
scripts/wasm-build.sh --clean   # od zera
scripts/wasm-build.sh --assemble  # tylko ponowne złożenie dist-wasm/ z istniejącego buildu
```

Skrypt jest idempotentny (ponowne uruchomienie przebudowuje tylko zmiany, `ccache` jeśli jest)
i konfigurowalny zmiennymi środowiska:

| Zmienna | Domyślnie |
|---|---|
| `BRULION_CACHE` | `${XDG_CACHE_HOME:-~/.cache}` |
| `EMSDK` | `$BRULION_CACHE/brulion-emsdk/emsdk` |
| `QT_ROOT` | `$BRULION_CACHE/brulion-qt` (wspólne z buildem Androida) |
| `QT_WASM` / `QT_HOST` | `$QT_ROOT/6.11.2/wasm_singlethread` / `$QT_ROOT/6.11.2/gcc_64` |
| `BUILD_DIR` / `DIST_DIR` | `build-wasm` / `dist-wasm` |
| `BUILD_TYPE` | `MinSizeRel` (najmniejszy `.wasm`) |
| `JOBS` | `nproc` |

Na NixOS skrypt sam wchodzi do `nix shell` (cmake, ninja, ccache, python3) i dokłada do
`NIX_LD_LIBRARY_PATH` biblioteki potrzebne prekompilowanym narzędziom Qt z aqt (glib, krb5,
brotli) — wymaga `programs.nix-ld`. Na innych systemach potrzebne są: cmake ≥ 3.24, ninja,
python3, git, a do `--setup` także `uvx` (uv) albo `aqt`.

**Raspberry Pi (maszyna deweloperska) nie buduje WebAssembly** — linkowanie aplikacji Qt
(wasm-ld + wasm-opt) potrzebuje kilku GB RAM; skrypt odmawia przy < 6 GB RAM
(`BRULION_FORCE_LOCAL=1` wymusza). Buduj na laptopie (nowe pliki najpierw `git add -N`):

```sh
flock <lock> laptop-run bash -c 'BUILD_DIR=~/.cache/brulion-wasm-build scripts/wasm-build.sh'
```

(katalog roboczy na laptopie jest czyszczony przy każdym `laptop-run`, dlatego katalog buildu
leży w `~/.cache`).

### Co powstaje w `dist-wasm/`

| Plik | Opis |
|---|---|
| `index.html` | nasza powłoka (z `wasm/index.html`, na bazie `wasm_shell.html` Qt): ekran ładowania z logo i postępem pobierania, „Wczytywanie Brulionu…”, ekrany błędów, rejestracja service workera |
| `brulion.js`, `brulion.wasm` | aplikacja (glue Emscriptena + moduł WebAssembly) |
| `qtloader.js` | loader Qt (`qtLoad()`) |
| `sw.js` | service worker (z `wasm/sw.js`): offline, kliknięcia w powiadomienia |
| `manifest.webmanifest` | manifest PWA (z `wasm/`) |
| `icons/` | ikony PWA (z `wasm/icons/` + `assets/logo.svg`) |
| `.nojekyll` | GitHub Pages bez przetwarzania przez Jekyll |

Skrypt wstawia do `index.html` rozmiar `.wasm` (pasek postępu) i hash plików aplikacji
(`?v=<hash>` w adresach — przeglądarka nigdy nie złoży `brulion.js` i `brulion.wasm` z dwóch
różnych wydań), a do `sw.js` wersję cache (hash całej strony) i listę plików do zapamiętania.

Wygenerowany przez Qt `build-wasm/src/app/brulion.html` to tylko domyślna strona Qt (bez PWA)
— do szybkich prób wystarczy, do publikacji służy `dist-wasm/`.

## Uruchamianie lokalnie

Strony nie da się otworzyć z `file://` — potrzebny serwer HTTP:

```sh
python3 -m http.server 8000 --bind 127.0.0.1 --directory dist-wasm
# http://127.0.0.1:8000/
```

Service worker (offline) działa tylko na `https://` oraz na `localhost`/`127.0.0.1`.
Parametry adresu (do debugowania):

* `?nosw` — nie rejestruj service workera.
* `?selftest=storage`, `?selftest=notify`, `?selftest=glyphs`, `?selftest=keyboard`,
  `?selftest=storage,notify` — testy (niżej).
* `?keyboard=visual` — klawiatura ekranowa zmniejsza tylko widoczny obszar, nie stronę
  (domyślne zachowanie przeglądarek, `interactive-widget=resizes-visual`; niżej, „Klawiatura
  ekranowa”) — do testów i diagnozy.
* `?report=/__report/<nazwa>` — wyniki testów (`?selftest=…`) idą też żądaniem POST pod tę
  ścieżkę tej samej witryny (`{name, json, t}`, bez ciasteczek; serwer testowy
  `scripts/web-test-server.py`) — dla przeglądarek bez DevTools, np. na emulatorze Androida.
  Inne ścieżki i adresy, które parser adresów zamienia na inną witrynę (np. `/%09/host`: tabulator
  i znaki nowej linii parser usuwa, zostaje `//host`), są pomijane — sprawdza to
  `scripts/wasm-smoke.mjs --keyboard`. Wynik `?selftest=keyboard` zawiera początek tekstu
  edytowanego pola (`focusText`), nigdy pola hasła.

Ścieżkę GitHub Pages da się sprawdzić lokalnie, kopiując `dist-wasm/` do `site/brulion/` i
serwując `site/` (`http://127.0.0.1:8000/brulion/`).

## Test dymny w przeglądarce bez okna

`scripts/wasm-smoke.mjs` (Node ≥ 22, bez zależności) steruje Chromium przez DevTools
Protocol: otwiera stronę, czeka aż aplikacja zastąpi ekran ładowania, robi zrzut ekranu,
sprawdza service workera. Z `--selftest` uruchamia `?selftest=storage`: aplikacja zwiększa
licznik w ukrytym pliku `.brulion-selftest` w każdym zamontowanym katalogu, czeka na
automatyczny zapis do IndexedDB i publikuje wynik (`window.brulionSelfTest`). Kolejne
wczytania muszą widzieć licznik +1.

```sh
python3 -m http.server 8123 --bind 127.0.0.1 --directory site &
node scripts/wasm-smoke.mjs --url http://127.0.0.1:8123/brulion/ --profile /tmp/prof \
  --loads 2 --selftest --expect-first 1 --screenshot online.png    # zapis + przeładowanie
node scripts/wasm-smoke.mjs --url http://127.0.0.1:8123/brulion/ --profile /tmp/prof-index \
  --loads 2 --index-notes 3000                                     # indeks bez wejścia
kill %1                                                            # serwer wyłączony
node scripts/wasm-smoke.mjs --url http://127.0.0.1:8123/brulion/ --profile /tmp/prof \
  --loads 1 --selftest --expect-first 3 --screenshot offline.png   # offline z cache SW
```

Drugie uruchomienie (nowy proces Chromium, ten sam profil, serwer wyłączony) dowodzi, że
dane przetrwały restart przeglądarki, a aplikacja startuje offline z cache service workera.

Pozostałe opcje:

* `--notify` — nadaje stronie zgodę na powiadomienia, aplikacja (`?selftest=notify`) pokazuje
  powiadomienie testowe, skrypt je „klika” i sprawdza, że kliknięcie (id, ścieżka, linia)
  dotarło do C++ (`WasmNotifier::activated`).
* `--glyphs` — `?selftest=glyphs`: aplikacja sprawdza, czy zapasowe czcionki (niżej,
  „Czcionki: emoji i symbole”) są zainstalowane i rysują wszystkie emoji i symbole Brulionu
  bez pustych kwadratów (`window.brulionGlyphSelfTest`: dla każdej czcionki znaki rysowane jako
  kwadraty przed i po instalacji). CI uruchamia go przy pierwszym teście.
* `--keyboard` — podstawia stronie własny `window.visualViewport` (przed jej skryptami) i
  uruchamia `?selftest=keyboard`: aplikacja przy każdej zmianie publikuje
  `window.brulionKeyboardSelfTest` (`{overlap, shown, animating, source, mode, sample, …}`),
  a skrypt „otwiera” klawiaturę 300 px, 250 px przy stronie przewiniętej o 20 px (jak robi iOS),
  podaje wartość błędną (NaN → 0), powiększa stronę gestem (`scale` 2: to nie klawiatura, 0),
  przewija stronę o 40 px (aplikacja musi ją przewinąć z powrotem na górę) i chowa klawiaturę —
  za każdym razem aplikacja musi zgłosić właściwą wysokość (niżej, „Klawiatura ekranowa”).
  Wcześniej sprawdza samą powłokę (skrypty aplikacji wstrzymane, więc magazyn i liczniki testu
  zostają nietknięte): z `?report=/__report/smoke` wynik idzie POST-em tylko pod ten adres tej
  samej witryny, a z `/%09/`, `/%0A/`, `/%0D/`, `//`, `/\`, pełnym adresem innej witryny
  i innymi ścieżkami — nigdzie. Ze `--screenshot` dochodzi zrzut `…-keyboard.png`. CI uruchamia go
  przy pierwszym teście.
* `--keyboard-phone` — to samo na udawanym telefonie (412×915 pikseli CSS, gęstość 2,625, ekran
  dotykowy) z dzisiejszym dziennikiem zapisanym do IndexedDB (jak `--copy`) i blokiem edytowanym
  stuknięciem nisko na ekranie: strona prosi o `interactive-widget=resizes-content` (a z
  `?keyboard=visual` o `resizes-visual`), ukryte pole klawiatury Qt zostaje w lewym górnym rogu
  okna, klawiatura zmniejszająca tylko widoczny obszar (300 px; 180 px przy widoku przesuniętym
  o 120 px) podnosi pasek narzędzi dokładnie na swoją górną krawędź, a strona zmniejszona przez
  przeglądarkę (ekran udawanego telefonu niższy o 300 px) daje zero zakrycia, klawiatura liczy się
  jako widoczna, a pasek kończy się na dole okna — także przy nieaktualnym `innerHeight` (stary
  wzór liczyłby klawiaturę drugi raz); kontenery Qt nie zostają przewinięte. Położenie paska
  i treści aplikacja podaje w `brulionKeyboardSelfTest` (`windowHeight`, `contentBottom`,
  `toolbarBottom`, `bottomNavTop`); gdy strona znów urośnie (klawiatura schowana Wstecz), wraca
  dolna nawigacja, a pasek leży tuż na niej. Ze `--screenshot` dochodzą zrzuty `…-edit.png`, `…-visual.png`,
  `…-content.png`. CI uruchamia go przy pierwszym teście.
* `--two-tabs` — otwiera drugą kartę: musi czekać z komunikatem „Brulion jest otwarty w innej
  karcie…”, a po opuszczeniu aplikacji w pierwszej karcie przejąć dane.
* `--share` — cel udostępniania (niżej, „Udostępnianie do Brulionu”): manifest odczytany przez
  Chrome (`Page.getAppManifest`) ma `share_target` GET `./` z `share-title`, `share-text`,
  `share-url` i żadnych błędów; start z udostępnionym tytułem i tekstem (`?selftest=share`:
  aplikacja publikuje dane okna, `window.brulionShareSelfTest`, z `dialog` — czy okno miało się
  otworzyć; przed startem w IndexedDB notatnik z dzisiejszym dziennikiem, jak przy `--copy`)
  otwiera okno **Zapisz w Brulionie** z „[Przepis na żurek](https://kuchnia.pl/żurek) — Zobacz
  to”, a pasek adresu traci parametry udostępnienia; druga karta z udostępnieniem przy otwartej
  pierwszej przekazuje je (sama nie wczytuje aplikacji, pokazuje komunikat i prosi o zamknięcie),
  a pierwsza pokazuje okno z „Zażółć gęślą jaźń”. Ze `--screenshot` dochodzi zrzut
  `…-share.png`. CI uruchamia go osobno (profil `profile-share`).
* `--loader-screenshot plik.png` — przy pierwszym wczytaniu dławi sieć do ~3 MiB/s i robi
  zrzut ekranu ładowania (pasek postępu).
* `--lang pl-PL` (domyślnie) — język przeglądarki.
* `--index-notes N` — przed pierwszym wczytaniem zapisuje do IndexedDB skarbiec z N
  wygenerowanymi notatkami (tak jak przechowuje je montaż IDBFS, z `settings.ini` wskazującym
  go jako bieżący); każde wczytanie musi potem zgłosić kompletny indeks
  (`brulionShell.indexed()`, czas w `brulionShell.indexedAt`) **bez żadnego wejścia** (myszy,
  klawiatury) — test regresji zatrzymanych timerów (niżej, „Ograniczenia techniczne”). Od 200
  notatek pierwsze wczytanie musi też zapisać gotowy indeks (`brulionShell.indexCacheSavedAt`),
  a każde kolejne wziąć z niego prawie wszystkie notatki (`brulionShell.indexStats()`:
  `indexedFromCache` ≥ N − 1). CI uruchamia go z 3000 notatek.
* `--copy` — kopiowanie bloków prawdziwym wejściem: przed wczytaniem zapisuje do IndexedDB (jak
  `--index-notes`) notatnik z dzisiejszym dziennikiem (trzy bloki, drugi z dzieckiem, polskie
  znaki, pogrubienie, link), potem klika tekst pierwszego bloku, wciska Escape (blok zaznaczony),
  Shift+↓ (następny z dzieckiem) i Ctrl+C, a strona (z nadaną zgodą na schowek) czyta schowek
  przeglądarki: `navigator.clipboard.readText()` musi dać dokładnie Markdown tych bloków,
  a `navigator.clipboard.read()` — HTML Brulionu (`text/html` ze znacznikiem `generator`). Ze
  `--screenshot` dochodzi zrzut `…-copy.png`. Wynik i miejsce kliknięcia zależą od układu strony
  1280×800 (pierwszy wiersz dnia ok. 153 px od góry; skrypt próbuje kilku wysokości).

Rysunki w motywie aplikacji sprawdza osobny skrypt `tests/app/drawing/wasm-drawing-check.mjs`
(ten sam minimalny klient DevTools, bez zmian w `wasm-smoke.mjs`): zapisuje do IndexedDB
notatnik z dzisiejszym dziennikiem osadzającym `Rysunek.svg` (złoty plik testów,
`tests/data/drawing/auto_theme.svg`, „Jak motyw”) i `settings.ini` z motywem `ui/themeMode`
jasnym, a potem ciemnym; po starcie aplikacji zrzut ekranu dekoduje w osobnej karcie (płótna
WebGL Qt nie da się odczytać) i sprawdza, że papier rysunku ma kolor motywu (#ffffff / #121212)
z tuszem w jego kolorze (#1f2430 / #caced8). To dowód, że dostawca obrazów `drawing` działa
w wątku GUI przeglądarki i czyta pliki z IDBFS (2026-10-28, laptop; CI go nie uruchamia).
Z `--text` skrypt potem klika rysunek (otwiera się edytor rysunku), naciska T (narzędzie tekstu),
klika w kartkę, wpisuje klawisz po klawiszu „Zażółć gęślą jaźń” i naciska Esc; plik rysunku
w IndexedDB musi potem mieć element tekstowy z tym tekstem — wpisywanie tekstu na rysunku działa
w przeglądarce (2026-10-29, laptop: oba motywy).

```sh
node tests/app/drawing/wasm-drawing-check.mjs --url http://127.0.0.1:8123/brulion/ [--shots katalog] [--text]
```

Sterowania działającą aplikacją przez agentów AI (`brulion --remote-control`, `brulion-cli app …`,
docs/CLI.md „Remote control”) w przeglądarce nie ma: stronę prowadzi się przez Chrome DevTools
Protocol — Playwrightem, obscurą albo jak ten skrypt — z jej `?selftest=…`.

Chromium: `$CHROME` albo `chromium`/`google-chrome` z `PATH`; WebGL 2 przez SwiftShader.
`BRULION_CHROME_NO_SANDBOX=1` (lub `CI=true`) wyłącza sandbox, gdy nie może wystartować
(kontenery, GitHub Actions na Ubuntu 24.04).

## Publikacja

### GitHub Pages

Robi to `.github/workflows/wasm.yml` (opis: `docs/RELEASE.md`): buduje stronę
`scripts/wasm-build.sh --setup`, uruchamia test dymny w Chrome pod ścieżką `/brulion/` (start,
trwałość po przeładowaniu i restarcie przeglądarki, pełny indeks 3000 notatek bez żadnego
wejścia, start offline z service workera) i wdraża `dist-wasm/` na GitHub Pages. Sam z siebie
nie startuje przy każdym pushu: uruchamia się go ręcznie z `main` albo pushem na `main`
ze znacznikiem `[ci]` w opisie commita (i zmianami w `src/`, `wasm/`…). Wywołany
z `release.yml` buduje `brulion-web-<wersja>.zip` do wydania, a po publikacji wydania
`release.yml` wdraża tę samą stronę na Pages — strona to więc ostatnie wydanie (wydanie
testowe jej nie zmienia), chyba że ktoś ręcznie wdroży nowszy `main`.
Jednorazowo: *Settings → Pages → Build and deployment → Source: GitHub Actions* oraz
*Settings → Environments → github-pages → Deployment branches and tags*: `main` i tag `v*`.

GitHub Pages serwuje `.wasm` jako `application/wasm`; kompresją i nagłówkami cache
(`max-age=600`) steruje GitHub — czy `.wasm` przychodzi skompresowany, widać w nagłówku
`Content-Encoding`. Wersjonowane adresy `?v=` i service worker z `updateViaCache: 'none'`
sprawiają, że nowe wydanie dociera przy następnym wczytaniu.

### Własny serwer

Na NixOS: moduł `services.brulion-web` (`docs/NIXOS.md`) — nginx z poprawnymi typami MIME,
plikami skompresowanymi z góry (gzip/brotli), nagłówkami cache i opcjonalnym proxy do
Nextcloud. Gdzie indziej wystarczy skopiować `dist-wasm/` (dowolna ścieżka). Zalecenia:

* typ MIME `application/wasm` dla `.wasm` (wymagany do kompilacji strumieniowej),
* kompresja gzip/brotli (`.wasm` kompresuje się ok. 3×, patrz rozmiary niżej),
* długi cache tylko dla adresów z `?v=<hash>` (`brulion.js`, `brulion.wasm`, `qtloader.js` —
  ich adresy zmieniają się z każdym wydaniem); `index.html`, `sw.js`, manifest i ikony bez
  długiego cache (`Cache-Control: no-cache`),
* HTTPS (service worker, powiadomienia i trwałe IndexedDB wymagają bezpiecznego kontekstu).

Przykład nginx (w bloku `http` standardowe `include mime.types;`). Uwaga: blok `types { … }`
wewnątrz `location` **zastępuje** całą mapę typów MIME — dlatego typy `.wasm` i
`.webmanifest` są tu ustawione osobnymi `location` z pustym `types {}` i `default_type`:

```nginx
map $arg_v $brulion_cache {
    ""      "no-cache";
    default "public, max-age=31536000, immutable";
}
server {
    # …
    location /brulion/ {
        alias /srv/brulion/;
        gzip on;
        gzip_types application/wasm application/javascript text/javascript application/manifest+json image/svg+xml;
        add_header Cache-Control $brulion_cache always;
        location ~ \.wasm$        { types { } default_type application/wasm;           add_header Cache-Control $brulion_cache always; }
        location ~ \.webmanifest$ { types { } default_type application/manifest+json; add_header Cache-Control "no-cache" always; }
    }
}
```

## Dane w przeglądarce

System plików Emscriptena żyje w pamięci. `WasmStorage` (`src/platform/wasm/wasmstorage.h`)
montuje na nim IDBFS (lustro w IndexedDB) w trzech katalogach:

* `/home/web_user/Brulion` — skarbiec (vault) wersji przeglądarkowej,
* katalog danych aplikacji (`QStandardPaths::AppDataLocation`,
  `/home/web_user/.local/share/Brulion/Brulion`) — historia wersji, stan synchronizacji,
  lokalne ustawienia,
* katalog pamięci podręcznej (`WasmStorage::indexCacheRoot()`, `QStandardPaths::CacheLocation`
  + `/index`, czyli `/home/web_user/.cache/Brulion/Brulion/index`) — gotowy indeks notatnika
  (`<id notatnika>/index-cache.bin`, docs/PERFORMANCE.md L2): przy kolejnej wizycie notatki,
  których tekst się nie zmienił, nie są parsowane od nowa (pliki są nadal czytane z pamięci).

Indeks w przeglądarce buduje się jednowątkowo (`wasm_singlethread`: `IndexLoader` bez puli
wątków, po kawałku w wątku GUI, jak wcześniej); przy pierwszej wizycie indeks pełnotekstowy
dobudowuje się po kawałku już po „gotowym” indeksie, przy kolejnych przychodzi z pamięci
podręcznej. Zapis pamięci podręcznej (kodowanie po 8 ms, ok. 5 s po gotowym indeksie) jest
**montażem pamięci podręcznej** (*cache mount*): osobną bazą IndexedDB zapisywaną we własnej
transakcji. IDBFS zapisuje każdy montaż jedną transakcją, a `QuotaExceededError` przerywa całą
transakcję — gdyby indeks leżał w danych aplikacji, plik, którego limit miejsca nie mieści,
zatrzymałby zapis historii wersji, stanu synchronizacji i ustawień przy każdej kolejnej próbie.
Teraz nieudany zapis montażu pamięci podręcznej:

* nie jest błędem zapisu (bez komunikatu „Saving to the browser's storage failed”, nic nie
  zostaje „niezapisane”, `beforeunload` nie pyta), tylko zwiększa `brulionStorage.cacheFailures`,
* usuwa pliki tego montażu z pamięci (następny zapis usuwa też ich stare kopie z IndexedDB,
  zwalniając miejsce) — `WasmStorage::cacheNotSaved()`,
* zatrzymuje zapisywanie indeksu do końca pracy z notatnikiem (`Workspace::stopIndexCache()`,
  w rejestrze zdarzeń jedna linia „Index cache not saved”).

Do tego indeks nie zajmie więcej niż połowę wolnego miejsca: `navigator.storage.estimate()`
z chwili wczytywania (`WasmStorage::estimate()`) wyznacza `IndexCacheKeeper::Policy::maxBytes`
(`notebook::indexCacheBudget()`: rozmiar poprzedniego pliku + połowa wolnego miejsca, najwyżej
256 MB); większy indeks nie jest zapisywany. Wczytanie, którego montaż pamięci podręcznej się
nie powiedzie, nie blokuje startu (ten montaż zostaje pusty).
`scripts/wasm-smoke.mjs --index-notes N` sprawdza, że indeks leży tylko w swojej bazie, a
`--cache-quota` (zmniejszony limit przez DevTools, `Storage.overrideQuotaForOrigin`) — że jego
nieudany zapis niczego poza nim nie zatrzymuje.

Każdy montaż to osobna baza IndexedDB nazwana ścieżką. Przebieg:

1. Start: montowanie i wczytanie z IndexedDB (`syncfs` IDBFS każdego montażu osobno — tak jak
   robi to `FS.syncfs`, ale z własnym zakończeniem, żeby montaż pamięci podręcznej mógł zawieść
   sam —, asynchronicznie). Dopiero
   po sygnale `ready()` aplikacja otwiera skarbiec (`main.cpp`); ekran ładowania pokazuje
   wtedy „Wczytywanie notatek…”.
2. Każda zmiana w tych katalogach (zamknięcie zapisywanego pliku, zmiana nazwy, usunięcie,
   mkdir, utime…) jest wychwytywana na poziomie API `FS` Emscriptena i planuje zapis do
   IndexedDB po 400 ms ciszy, najpóźniej 3 s od pierwszej niezapisanej zmiany. Rdzeń nie
   musi o niczym wiedzieć; `scheduleFlush()`/`flushNow()` są dostępne do ręcznego sterowania.
3. Przy ukryciu/zamknięciu karty (`visibilitychange`, `pagehide`) zaległe zmiany zapisują się
   od razu; `beforeunload` pyta o pozostanie na stronie, jeśli zapis jeszcze trwa.
4. Zapis nigdy nie rusza przed udanym wczytaniem — pusty stan w pamięci skasowałby notatki.
   Gdy IndexedDB jest niedostępne (niektóre tryby prywatne, zablokowane dane witryn),
   aplikacja działa tylko w pamięci, `isBackedByIndexedDb()` = false, sygnał `error()`
   pozwala UI ostrzec użytkownika.
5. Po awarii modułu WebAssembly powłoka HTML i tak zapisuje stan z pamięci (część JS żyje).
6. **Jedna karta naraz.** Każda karta ma własną kopię plików w pamięci, a zapis usuwa z
   IndexedDB to, czego karta nie ma — dwie karty skasowałyby sobie nawzajem nowe notatki.
   Dlatego strona trzyma przez cały czas życia blokadę Web Locks; druga karta czeka na
   ekranie ładowania („Brulion jest otwarty w innej karcie. Zamknij ją, aby kontynuować
   tutaj.”) i rusza sama, gdy pierwsza zostanie zamknięta. Strona przechodząca do pamięci
   wstecz/dalej (bfcache) zapisuje zmiany, oddaje blokadę i przy powrocie przeładowuje się.
   Bez Web Locks (bardzo stare przeglądarki) ochrony nie ma.

Uwagi:

* **Pliki zapisane przed `ready()` giną** (wczytany stan je zastępuje).
* **Eksmisja danych:** przeglądarka może usunąć dane witryny przy braku miejsca, a Safari
  usuwa je po 7 dniach bez wizyty, jeśli strona nie jest dodana do ekranu początkowego.
  `WasmStorage::requestPersistentStorage()` prosi o `navigator.storage.persist()` (Firefox
  pyta użytkownika — wywoływać z akcji użytkownika). Regularny eksport/synchronizacja to
  jedyna pełna kopia zapasowa.
* **Jedno źródło (origin) na GitHub Pages:** wszystkie projekty `janusz-bit.github.io/*`
  dzielą IndexedDB. Inna strona pod tym adresem mogłaby odczytać notatki — dla prywatnych
  danych lepiej hostować Brulion pod własną domeną.
* Ustawienia przez `QSettings` (format natywny) Qt na WebAssembly trzyma w `localStorage`.
* **Obrazy z internetu** (docs/SYNTAX.md §2): pamięć podręczna
  (`QStandardPaths::CacheLocation`/web-pictures) nie jest montażem IDBFS — leży w pamięci
  karty (MEMFS) i znika razem z nią; pobrane zdjęcie po ponownym otwarciu karty trzeba
  pokazać jeszcze raz (albo zapisać w notatniku, wtedy jest załącznikiem w IndexedDB). Limit
  to 50 MB zamiast 200 MB, a adresy `data:` zdjęć wyrzuconych z pamięci podręcznej znikają z
  pamięci razem z plikami.
  * **CORS**: zapytanie o zdjęcie wysyła przeglądarka (Qt for WebAssembly: `emscripten_fetch`),
    a odpowiedź dostaje strona Brulionu tylko wtedy, gdy serwer zdjęcia na to pozwala
    (`Access-Control-Allow-Origin`; np. `upload.wikimedia.org` wysyła `*`, większość stron nie).
    Przeglądarka nie mówi stronie, dlaczego zapytanie się nie udało, więc nieudane połączenie
    jest opisane jako „%1 nie pozwala innym stronom pobierać swoich obrazów (CORS) albo nie ma
    połączenia…” z radą, by zapisać zdjęcie w notatniku w aplikacji na komputerze lub telefonie
    (`Workspace::setWebPageScheme`, ustawiane z `location.protocol` — `platform::pageScheme()`).
  * **Mixed content**: Brulion otwarty przez `https://` nie pobierze niczego spod `http://`
    (przeglądarka blokuje takie zapytanie) — komunikat mówi to wprost.
  * **Proste zapytania**: w przeglądarce `PictureFetcher::setSimpleRequests` nie ustawia
    `User-Agent` ani `Accept-Language` (przeglądarka wysyła własne; inny nagłówek wymagałby
    zapytania wstępnego OPTIONS, na które serwery zdjęć zwykle nie odpowiadają), zostaje tylko
    `Accept: image/…` (nagłówek „bezpieczny” dla CORS). Qt i tak odrzuca nagłówki zabronione
    (`Cookie`, `Referer`…); ciasteczka nie są wysyłane (`withCredentials` wyłączone).
  * **Przekierowania** wykonuje sama przeglądarka: Qt for WebAssembly nie wysyła sygnału
    `redirected`, więc ręczne sprawdzanie przekierowań (`webpicture::redirectAllowed`) działa
    tylko w aplikacji. Przeglądarka i tak nie przejdzie z `https://` na `http://` (mixed
    content), a Chrome ogranicza zapytania stron z internetu do adresów sieci prywatnej
    (Private Network Access).
  * **Eksport** z „Dołącz kopie” pobiera brakujące zdjęcia tak samo — z tymi samymi
    ograniczeniami CORS; zdjęcie, którego nie da się pobrać, zostaje w eksporcie adresem. Kopii
    jest najwyżej 256 MB łącznie (pamięć karty przeglądarki; jeden plik HTML trzyma je jeszcze
    jako tekst `data:`), dalsze zostają adresami.
  * Do sprawdzenia w przeglądarce (build wasm z GitHub Actions): zdjęcie z
    `upload.wikimedia.org` (CORS dozwolony) i ze strony bez CORS, adres `http://` na stronie
    `https://` (GitHub Pages).

## Udostępnianie do Brulionu

Wersja w przeglądarce **zainstalowana jako aplikacja** jest w systemowym menu **Udostępnij**:
w Chrome, Edge albo Samsung Internet na Androidzie (menu przeglądarki → **Zainstaluj aplikację**
albo **Dodaj do ekranu głównego**) i na ChromeOS. Stronę z przeglądarki (jej tytuł i adres) albo
zaznaczony tekst z innej aplikacji wysyłasz przez **Udostępnij → Brulion**; otwiera się to samo
okno **Zapisz w Brulionie** co w aplikacji na Androida (docs/ANDROID.md „Udostępnij → Brulion”):
tekst do poprawienia, **Dzisiejszy dziennik** albo **Nowa strona**, nic nie zapisuje się przed
**Zapisz**. Daty w udostępnionym tekście nie tworzą przypomnień.

* Tylko tekst i odnośniki. Pliki (`.md`, `.txt`) udostępnisz aplikacji na Androida; wersja
  w przeglądarce ich nie przyjmuje (cel udostępniania z plikami wymaga wysłania ich do serwera
  albo do service workera, którego GitHub Pages ani prosty serwer nie zastąpią).
* Brulion jest już otwarty w innej karcie: udostępnienie trafia do tamtej karty (tam otwiera się
  okno), a nowa karta nie wczytuje aplikacji, tylko mówi „Przekazano do Brulionu otwartego
  w innej karcie.” i zamyka się, gdy przeglądarka na to pozwala. Gdy tamta karta nie odpowie
  w 4 s (zamrożona, starsza wersja Brulionu), nowa karta startuje jak zwykle i czeka, aż tamta
  zostanie zamknięta („Brulion jest otwarty w innej karcie…”), a potem pokazuje okno.
* Na Androidzie zainstalowana aplikacja zwykle otwiera udostępnienie w swoim jedynym oknie:
  otwarty Brulion wczytuje się od nowa z udostępnionym tekstem (zapisane notatki są bezpieczne;
  przy zmianie, która jeszcze się zapisuje, przeglądarka może zapytać, czy opuścić stronę).
* Bez notatnika (pierwsze uruchomienie) Brulion najpierw prosi o otwarcie albo utworzenie
  notatnika — udostępnienie czeka.
* Firefox, Safari (także na iPhonie) i przeglądarki bez instalacji jako WebAPK (np. Brave)
  celów udostępniania nie obsługują — Brulionu w ich menu **Udostępnij** nie będzie.

Technicznie: `wasm/manifest.webmanifest` ma `share_target` metodą GET z akcją `./` (sama
aplikacja: działa bez service workera — GitHub Pages i nginx podają dla niej `index.html` — i offline
z jego pamięci) i parametrami `share-title`, `share-text`, `share-url`. `index.html` zabiera je
z paska adresu (`history.replaceState`: przeładowanie nie udostępnia drugi raz; inne parametry
zostają) i przekazuje aplikacji jako `--share-query` (zakodowane jak formularz); zapytanie
czyta rdzeń (`notebook::sharedContentFromQuery`, `src/core/notebook/share.h`: `+` to spacja,
`%XX` to bajty UTF-8, błędne sekwencje → U+FFFD, nieznane klucze pomijane, tekst do 1 000 000
znaków), a `AppController::receiveShareQuery` otwiera okno. Gdy inna karta trzyma blokadę magazynu
(Web Lock `brulion-storage:…`, niżej „Dane w przeglądarce”), `index.html` wysyła
`{type: "share", id, query}` kanałem `BroadcastChannel` „`brulion-share:<ścieżka strony>`”;
odpowiada tylko karta z blokadą (`WasmShare`, `src/platform/wasm/wasmshare.h`, nasłuchuje od
startu, zanim poprosi o blokadę) — `{type: "taken", id}` — i przekazuje zapytanie do
`receiveShareQuery`. Bez `launch_handler`: Chrome z `focus-existing` gubi dane udostępnienia.
Test: `scripts/wasm-smoke.mjs --share` (wyżej); ręcznie na telefonie — TODO.md.

## Powiadomienia (przypomnienia)

`WasmNotifier` (`src/platform/wasm/wasmnotifier.h`) używa Notification API:

* `permission()`, `requestPermission()` — przeglądarki pokazują pytanie o zgodę tylko w
  odpowiedzi na działanie użytkownika (kliknięcie przycisku w UI),
* `showNow(id, tytuł, treść, ścieżka, linia, alarm, cicho)` — alarmy zostają na ekranie do
  zamknięcia (`requireInteraction`) i wibrują, gdzie to możliwe; `cicho` — opcja `silent`
  (bez dźwięku i wibracji); ten sam `id` zastępuje poprzednie powiadomienie,
* `playTone(alarm)` — sygnał w samej stronie (Web Audio): alarm z dźwiękiem „Domyślny” pika
  przez kilka sekund. Dźwięk powiadomienia wybiera przeglądarka/system, więc w przeglądarce
  *Ustawienia → Przypomnienia* oferują tylko „Domyślny” i „Cisza”. Ograniczenia: tylko przy
  otwartej karcie i dopiero po pierwszej interakcji użytkownika ze stroną (zasady autoodtwarzania
  przeglądarek) — inaczej sygnału nie słychać,
* `activated(id, ścieżka, linia)` po kliknięciu (strona dostaje fokus),
* `firesWhenClosed()` = false: przeglądarka nie powiadomi o niczym przy zamkniętej karcie —
  terminy odmierza rdzeń (`ReminderScheduler`) w otwartej aplikacji.

Chrome na Androidzie nie pozwala stronie tworzyć powiadomień (`new Notification` →
„Illegal constructor”); wtedy powiadomienie idzie przez rejestrację service workera, a
`sw.js` przekazuje kliknięcie do otwartej karty (`postMessage`) albo otwiera aplikację z
`?brulion-id=…&brulion-note=…&brulion-line=…` (`WasmNotifier::takeLaunchActivation()`).

## Nextcloud (WebDAV) a CORS

Przeglądarka pozwala stronie z `https://janusz-bit.github.io` łączyć się z
`https://chmura.example.com/remote.php/dav/…` tylko wtedy, gdy serwer odpowie nagłówkami
**CORS** (także na zapytania wstępne `OPTIONS` dla `PROPFIND`, `PUT`, `MKCOL`, `DELETE` z
nagłówkami `Authorization`, `Depth`, `If-Match`). Nextcloud domyślnie ich nie wysyła dla
WebDAV, więc synchronizacja z wersji przeglądarkowej „po prostu” nie zadziała. Możliwości:

1. **Ten sam origin (zalecane):** hostuj `dist-wasm/` na tym samym serwerze co Nextcloud,
   np. `https://chmura.example.com/brulion/` (reverse proxy / dodatkowy `location` w nginx
   jak wyżej). Zapytania są wtedy same-origin, CORS niepotrzebny, a dane w IndexedDB są
   oddzielone od innych stron.
2. **Nagłówki CORS na reverse proxy przed Nextcloud** — tylko dla konkretnego originu
   (`Access-Control-Allow-Origin: https://janusz-bit.github.io`, nigdy `*` z
   uwierzytelnianiem), `Access-Control-Allow-Methods: GET, PUT, DELETE, PROPFIND, MKCOL,
   MOVE, COPY, OPTIONS`, `Access-Control-Allow-Headers: Authorization, Content-Type, Depth,
   Destination, If-Match, If-None-Match, Overwrite`, `Access-Control-Expose-Headers: ETag,
   OC-ETag, DAV`, odpowiedź `204` na `OPTIONS` bez wymagania logowania. Uwaga: to otwiera
   WebDAV dla skryptów z tego originu (na GitHub Pages — z każdego projektu użytkownika).
   Istnieją też aplikacje Nextcloud dodające CORS dla WebDAV (np. WebAppPassword) —
   sprawdź ich aktualny stan i zakres przed użyciem.
3. **Aplikacje natywne:** wersje NixOS i Android nie podlegają CORS i synchronizują się
   z Nextcloud bez zmian po stronie serwera (a Syncthing tylko tam jest dostępny).

Używaj hasła aplikacji Nextcloud (Ustawienia → Bezpieczeństwo), nie głównego hasła.

## Czcionki: emoji i symbole

Qt for WebAssembly nie widzi czcionek systemu. Ma tylko dwie własne, DejaVu Sans i DejaVu
Sans Mono (dlatego w przeglądarce Ustawienia → Wygląd i Edytor nie mają wyboru czcionki
interfejsu, notatek ani kodu; kolor akcentu, tła i gęstość działają jak wszędzie;
wkompilowane w `brulion.wasm`; czcionki komputera dałoby się wczytać jedynie przez
Local Font Access API, tylko w Chromium i po zgodzie użytkownika, o którą Qt domyślnie nie
prosi). DejaVu nie ma żadnego emoji, którego używa Brulion: przypomnienia ⏰, alarmy 🔔,
znaczniki zadań 📅 ⏳ 🛫 ✅ ❌ 🔁 🔺 ⏫ 🔼 🔽 ⏬ ➕, a także ⧉ osadzonych bloków — wszystkie były
**pustymi kwadratami** (białymi w ciemnym motywie), w notatkach, edytorze, ustawieniach i
podpowiedziach. Ikony Lucide (`BIcon`, SVG) działały poprawnie.

Dlatego aplikacja zawiera dwie małe czcionki zapasowe z `assets/fonts/` (fragmenty Noto Emoji
i Noto Sans Math, licencja SIL OFL 1.1, robione przez `scripts/fallback-fonts.py`) i w
przeglądarce zawsze instaluje je przy starcie jako czcionki zapasowe: tekst zostaje w DejaVu, a
tylko brakujące znaki biorą się z nich. Emoji są jednokolorowe, w kolorze tekstu. Szczegóły,
zasady na innych platformach i lista znaków: docs/UI.md, „Emoji and symbol fonts”. Test:
`scripts/wasm-smoke.mjs --glyphs`.

## Rozmiar

Build `MinSizeRel` (Qt 6.11.2, emsdk 4.0.7), pełna aplikacja:

| Plik | Surowy | gzip -9 | brotli -q 11 |
|---|---|---|---|
| `brulion.wasm` | 40 259 601 B (38,39 MiB) | 13 670 465 B (13,04 MiB) | 9 403 485 B (8,97 MiB) |
| `brulion.js` | 275 745 B (0,26 MiB) | 64 196 B | |

Z tego ok. 26,2 MB to kod, 13,8 MB dane (skompilowany QML, zasoby Qt: czcionki, tabele
Unicode). Wynik pomiaru z 18.10.2026; `scripts/wasm-build.sh` wypisuje aktualne wartości po
każdym buildzie. Pierwsze wejście pobiera więc ok. 9–13 MB (zależnie od kompresji serwera),
kolejne starty idą z cache service workera (lokalnie < 1 s).

Czcionki zapasowe emoji i symboli (wyżej, „Czcionki: emoji i symbole”) dokładają
+190 272 B surowego `brulion.wasm` (+118 111 B po gzip -9, +103 429 B po brotli; pomiar z
22.10.2026, ten sam toolchain przed i po).

Build dla przeglądarki nie zawiera QML deweloperskiego (galerii, placu zabaw edytora, skryptów
i samotestów `--editor-script`; opcja `BRULION_DEV_QML`, docs/UI.md): −597 014 B surowego
`brulion.wasm` (−160 008 B po gzip -9, −104 716 B po brotli).

## Klawiatura ekranowa (telefon)

Pasek narzędzi edycji ma leżeć dokładnie na klawiaturze ekranowej. Strona prosi przeglądarkę
o `interactive-widget=resizes-content` (`wasm/index.html`): otwarta klawiatura zmniejsza całą
stronę, a z nią ekran Qt (`#screen`) i okno aplikacji, więc pasek przy dolnej krawędzi okna leży
na klawiaturze, a aplikacja niczego nie podnosi (zakrycie 0). Domyślne zachowanie przeglądarek
(`resizes-visual`: strona zostaje, zmniejsza się tylko widoczny obszar, `window.visualViewport`)
nie działa w Brave: Brave na Androidzie chowa przy klawiaturze swój dolny pasek, ale
`visualViewport.height` dalej go odejmuje
([brave-browser#60074](https://github.com/brave/brave-browser/issues/60074); na Pixelu
412×915: widoczne 433,9 px, `visualViewport.height` 385,9 px) — aplikacja podnosiła pasek o ok.
48 px za wysoko i między nim a klawiaturą zostawał pusty pas. Żadne API strony nie pokazuje
prawdziwej wartości; rozmiar strony, który przeglądarka ustala sama, jest dobry. Kosztem jest
przeliczenie układu okna przy otwarciu i zamknięciu klawiatury (na emulatorze: jedna zmiana
wysokości okna Qt w każdą stronę; w Chrome i Brave jeszcze jedna, gdy przed klawiaturą pojawia się
pasek autouzupełniania przeglądarki, niżej). `resizes-content`
obsługują Chrome ≥ 108, Brave, Edge, Samsung Internet i Firefox ≥ 132; iOS Safari i starsze
przeglądarki je pomijają.

Gdzie przeglądarka zmniejsza tylko widoczny obszar (iOS Safari, starsze przeglądarki,
`?keyboard=visual`), aplikacja podnosi treść o część ekranu Qt, której widoczny obszar nie
obejmuje: wysokość kontenera `#screen` − (`visualViewport.offsetTop` + `visualViewport.height`).
Liczy się względem kontenera Qt, nie `innerHeight` (Chromium w trakcie zmiany rozmiaru strony podaje
nieaktualne `innerHeight`: stary wzór `innerHeight − height − offsetTop` liczył wtedy klawiaturę
drugi raz). Strona powiększona gestem (`visualViewport.scale` > 1,01, np. touchpad na komputerze),
wartości błędne (NaN, wysokość 0 po obrocie) i różnice poniżej 2 px (zaokrąglenia) to nie klawiatura.

Powłoka (`watchVisualViewport` w `src/platform/wasm/wasmshell.cpp`) przy zdarzeniach
`visualViewport` (`resize`, `scroll`), `resize` okna, zmianie fokusu i zmianie rozmiaru
kontenera Qt (`ResizeObserver`) przekazuje surowe odczyty (`innerHeight`, `visualViewport`,
wysokość kontenera, ekran dotykowy, fokus w polu klawiatury Qt) co klatkę animacji, aż wartości
przez 6 klatek się nie zmienią (ostatni odczyt: „ustalony”); liczy C++ (`ViewportKeyboard`
w `src/app/ui/keyboardinsets.h`, testy `tst_keyboardinsets` z sekwencjami nagranymi
w przeglądarkach). Klawiatura liczy się jako widoczna (`shown`: dolna nawigacja chowa się od
razu — strona zmienia rozmiar w jednym kroku, więc nawigacja nie czeka pod wysuwaną klawiaturą
jak na Androidzie, `KeyboardInsets.slidesOver` jest tu fałszywe —, komunikaty idą na górę;
schowana klawiatura przywraca nawigację, a pasek edycji, gdy pisanie trwa, leży na niej) przy
zakryciu albo — gdy przeglądarka zmniejszyła stronę — na ekranie
dotykowym z fokusem w polu Qt, gdy widoczny obszar jest co najmniej 120 px niższy niż najwyższy
widziany przy tej szerokości (obrót telefonu zaczyna od nowa; pasek adresu przeglądarki, 56 px, to
nie klawiatura). Fokus przechodzący między elementami Qt (Enter: następny blok) nie chowa jej, dopóki
odczyty się nie ustalą; ustalony odczyt bez fokusu w polu Qt chowa ją (edycja skończona — na
podzielonym ekranie strona zostaje niższa bez klawiatury). Telefon obrócony przy otwartej
klawiaturze (sprawdzone na emulatorze): Chrome i Brave chowają klawiaturę, a blok zostaje
edytowany — pierwsza wysokość w poziomie jest pełna; Firefox zostawia klawiaturę, ale najpierw
podaje stronę w poziomie bez niej (301 px), potem z nią (62 px). W obu przypadkach najwyższa
wysokość przy nowej szerokości to strona bez klawiatury. `animating` trwa, dopóki odczyty się nie
ustalą: kursor przewija się w widok dopiero potem. `KeyboardInsets` (docs/UI.md „On-screen keyboard”) podnosi o zakrycie widok, pasek
narzędzi edycji, okna wysuwane od dołu, menu i komunikaty — tak samo jak na Androidzie. Bez
`visualViewport` (stare przeglądarki) nic nie jest zgłaszane, a Qt nie zna klawiatury
(`keyboardRectangle` w przeglądarce jest pusty).

Ukryte pole tekstowe, przez które Qt odbiera pisanie (`.qt-window-input-element`), Qt stawia
w lewym górnym rogu edytowanego tekstu; po stuknięciu bloku nisko na ekranie przeglądarka
przesuwała do niego widok (Chrome: górny pasek aplikacji uciekał z ekranu, iOS przewijał stronę),
a pod oknem zmniejszonym przez klawiaturę przewijała kontenery Qt. Na ekranach dotykowych powłoka
przypina je w lewym górnym rogu okna (styl w otwartym shadow root `#qt-shadow-container`), a
przewinięte kontenery Qt wraca na 0; stronę przewiniętą przez iOS przewija z powrotem na górę.
Okna kandydatów klawiatur CJK pojawiają się przez to w rogu okna.

Chrome i Brave nad klawiaturą każdego pola tekstowego stawiają własny pasek autouzupełniania
(hasła, karty, adresy; 48 px w Chrome, 64 px w Brave — pas z trzema ikonami po prawej): pasek
narzędzi Brulionu leży wtedy na nim, a ten na klawiaturze. To część przeglądarki, nie strony
(`visualViewport` i rozmiar strony go nie obejmują). Na emulatorze sprawdzone: `autocomplete="off"`,
nazwy i etykiety pola go nie chowają; chowa go tylko pole wyszukiwania (`type="search"`), ale
w nim klawiatura zaczynała od wielkiej litery pierwsze słowo wpisane w środku zdania — więc pole Qt
zostaje polem tekstowym.

Znane ograniczenia: przeglądarka, która pomija `interactive-widget`, a ma błąd Brave, dalej
zostawi pas (nie da się go wykryć). Przeglądarka, która po obrocie telefonu od razu poda stronę
z klawiaturą (żadna z trzech sprawdzonych), zapamięta ją jako najwyższą przy tej szerokości:
klawiatura liczy się wtedy jako schowana, dopóki raz nie zejdzie (pasek narzędzi i tak zostaje na
dole okna, czyli na klawiaturze). Nowa karta w Brave dostaje na chwilę widok jak przy schowanym
pasku ([brave-browser#59940](https://github.com/brave/brave-browser/issues/59940)) — to błąd
Brave. iOS Safari — tylko „best effort”, bez automatycznego testu na iOS: ścieżka
`visualViewport` z przypiętym polem, sprawdzana przez `scripts/wasm-smoke.mjs --keyboard`
i `--keyboard-phone` (wyżej).

Test w prawdziwych przeglądarkach: `scripts/android-emulator-test.sh --web-keyboard dist-wasm`
(emulator Androida 16 z KVM, Pixel 6; job `keyboard-android` w `.github/workflows/wasm.yml`).
Serwuje witrynę pod `/brulion/` (`scripts/web-test-server.py`; w emulatorze `http://10.0.2.2`,
które przeglądarki traktują jak bezpieczny kontekst: Chrome i Brave przez
`--unsafely-treat-insecure-origin-as-secure`, Firefox przez `dom.securecontext.allowlist`),
w Chrome z obrazu systemu, Brave 1.97.59 (wersja z #60074) i Firefoksie 157.0.1 (przypięte APK
z sumami SHA-256) zapisuje dziennik 40 polskich bloków, otwiera
`?selftest=keyboard&report=…` (oraz z `&keyboard=visual`), stuka blok i czeka na klawiaturę.
Aplikacja w tym teście maluje miejsce klawiatury na purpurowo, a pasek narzędzi na
turkusowo (`KeyboardInsets.marked`): na zrzucie ekranu nad górną krawędzią klawiatury (ramka
IME z `dumpsys window`) nie może być purpurowych wierszy (pasa; dopuszczalne 3 px), tylko ≥ 20
wierszy paska — co najwyżej nad paskiem autouzupełniania przeglądarki (≤ 72 dp) — a według
aplikacji pasek kończy się na dole okna Qt, które wypełnia stronę. Słowo wpisane klawiszami
klawiatury trafia do bloku, a klawisz akcji klawiatury to Enter, który dzieli blok
(`focusText` w wyniku testu). Potem telefon obraca się w poziom przy otwartej klawiaturze: gdy
klawiatura zostaje (Firefox), liczy się jako widoczna, a pasek kończy się na dole okna; gdy
przeglądarka ją chowa (Chrome, Brave), wraca dolna nawigacja i pasek leży tuż na niej; po obrocie
z powrotem klawiatura znów się liczy (Chrome i Brave: po stuknięciu bloku). Po „Wstecz” (klawiatura
schowana) nie zostaje nic purpurowego, wraca dolna nawigacja, a pasek leży tuż na niej (`toolbarBottom` =
`bottomNavTop` w wyniku testu). Z `?keyboard=visual` wynik tylko się
zapisuje (Brave pokazuje pas z #60074).
Skrypt zapisuje odczyty (`samples-<przeglądarka>-<tryb>.jsonl`), wypisuje je jako wiersze testu
`tst_keyboardinsets` i liczy, ile razy okno Qt zmieniło wysokość przy otwarciu klawiatury.

Wynik z 28.10.2026 (Android 16, Pixel 6, 1080×2400, gęstość 420): w Chrome 133, Brave 1.97.59
i Firefoksie 157.0.1 pasek narzędzi leży na klawiaturze (Firefox: 0 px; Chrome i Brave: na ich
pasku autouzupełniania, 126 i 168 px, bez ani jednego piksela strony między nimi), okno Qt
zmniejsza się raz przy otwarciu i raz przy zamknięciu; z `?keyboard=visual` Brave pokazuje pas
124 px (#60074: zgłasza zakrycie 376 px przy 328 px rzeczywistego), Chrome i Firefox nie. Obrót
(29.10.2026): Chrome i Brave chowają klawiaturę (strona w poziomie 866×308 i 865×307 px), Firefox ją
zostawia (najpierw strona 871×301 px bez niej, po chwili 871×62 px z nią); pasek leży na dole okna
w Firefoksie, a w Chrome i Brave na dolnej nawigacji, która wraca po schowaniu klawiatury (60 px
nad dołem okna). Znaki spoza ASCII wpisane klawiaturą (à, ß, ç, ñ — przytrzymanie klawisza w angielskim
układzie Gboard; polskiego układu obraz emulatora nie ma, a jego pobranie wymaga ustawień Gboard)
trafiają do bloku bez zmian we wszystkich trzech (sprawdzone ręcznie skryptem, nie w teście).

## Ograniczenia techniczne

* Wiersz poleceń (`brulion-cli`, docs/CLI.md) i odbieranie poleceń od niego (`app open`) są tylko
  na komputerze z Linuksem; notatki z przeglądarki agent czyta przez Nextcloud.
* Jednowątkowy Qt bez asyncify/JSPI: brak zagnieżdżonych pętli zdarzeń (`QDialog::exec()`,
  `QEventLoop::exec()`); dialogi i operacje muszą być asynchroniczne (sygnały, `open()`).
* Pliki od/do użytkownika: `QFileDialog::getOpenFileContent()` / `saveFileContent()`.
* Wymagany WebGL 2 (powłoka ostrzega, gdy go brak, i pozwala spróbować mimo to).
* Pamięć: start 50 MB, wzrost do 4 GB (domyślne Qt).
* Sieć tylko przez `QNetworkAccessManager` (fetch przeglądarki) i z zasadami CORS.
* **Schowek.** „Kopiuj odnośnik” (menu bloku, paleta poleceń, Ctrl+Shift+C; docs/UI.md „The
  block editor”) pisze przez `QClipboard`, który w Qt dla WebAssembly korzysta z asynchronicznego
  Clipboard API przeglądarki: wymaga bezpiecznego kontekstu (https albo `localhost`) i gestu
  użytkownika (kliknięcie pozycji menu nim jest). Odmowy przeglądarki Qt nie zgłasza — Brulion
  mówi „Skopiowano …” tylko wtedy, gdy tekst wraca ze schowka Qt, więc odmowy samej przeglądarki
  nie wykryje (nie sprawdzone jeszcze ręcznie w Firefoksie i Safari). Ctrl+Shift+C w Chromium i
  Firefoksie otwiera narzędzia deweloperskie, zanim strona dostanie klawisze — wtedy zostaje
  menu bloku albo paleta poleceń (Ctrl+K → „Kopiuj odwołanie do bloku”).
  Tak samo kopiowanie zaznaczonych bloków (docs/UI.md „Selecting and copying”: Ctrl+C / Ctrl+X,
  „Kopiuj” w pasku zaznaczenia i w menu bloku): `QClipboard` dostaje Markdown
  jako text/plain i — gdy przeglądarka przyjmie (Clipboard API z `ClipboardItem`; Firefox i Safari
  różnie) — HTML jako text/html, tworzony od razu (na Linuksie dopiero na prośbę innej aplikacji:
  przeglądarka bierze dane przy samym kopiowaniu, `clipboardReadsAtOnce()`); kopiowanie odbywa
  się w obsłudze klawisza albo kliknięcia, więc liczy się jako gest użytkownika. Odczyt kontrolny po kopiowaniu (na pulpicie i Androidzie
  chroni przed „Skopiowano” bez kopii) w przeglądarce niczego nie gwarantuje: Qt zapisuje do
  schowka przeglądarki asynchronicznie i zwraca własną kopię danych, a odmowę (brak gestu, http,
  Safari, Firefox bez `ClipboardItem` z HTML) tylko loguje — toast „Skopiowano” może wtedy być
  nieprawdziwy. Schowka „primary selection” (X11) w przeglądarce nie ma.
  Wklejanie: Ctrl+V przychodzi do strony jako zdarzenie `paste`, z którego Qt bierze tekst i HTML
  schowka (HTML z innych aplikacji Brulion zamienia na Markdown); menu edytowanego tekstu nie ma w
  przeglądarce „Wklej” — strona nie może sama odczytać schowka bez zgody. Kopiowanie zaznaczonych
  bloków (Ctrl+C) sprawdza w Chromium bez okna `scripts/wasm-smoke.mjs --copy` (2026-10-23,
  Chromium 155: Markdown jako text/plain i HTML Brulionu jako text/html w schowku przeglądarki);
  wklejanie i inne przeglądarki (Firefox, Safari) nie były sprawdzane ręcznie.
* **Żadnych powtarzanych timerów 0 ms.** Dyspozytor zdarzeń Qt 6.11.2 dla WebAssembly
  (`QEventDispatcherWasm::sendTimerEvents()`) ustawia kolejny `setTimeout` przeglądarki tylko
  wtedy, gdy `QTimerInfoList::activateTimers()` zwróci > 0, a ta funkcja nie liczy timerów o
  interwale 0. Powtarzany `QTimer` z interwałem 0 odpala więc raz i wstrzymuje **wszystkie**
  timery Qt, dopóki inny timer nie zostanie uruchomiony/zatrzymany albo nie przyjdzie wejście
  (ruch myszy, klawisz). Tak indeks skarbca w przeglądarce stawał w połowie (370–570 z 1000
  notatek niezaindeksowanych po 60 s bez dotykania karty — w Chromium i Firefoksie; dziennik
  pokazywał „Wczytywanie notatnika…”, wyszukiwanie, odnośniki, zadania i przypomnienia były
  niepełne), a dopiero ruch myszy popychał go o kilka porcji. Dlatego `Workspace` indeksuje
  w przeglądarce timerem 1 ms (przeglądarka i tak czeka między kolejnymi `setTimeout` ≥ 4 ms).
  Bezpieczne są: interwał ≥ 1 ms, timery jednorazowe (zatrzymanie timera ponownie ustawia
  `setTimeout`), `QTimer::singleShot(0, …)` i wywołania kolejkowane (idą przez zdarzenia, nie
  timery). Test: `scripts/wasm-smoke.mjs --index-notes` (wyżej).
  Minimalny przypadek: `QCoreApplication` z jednym powtarzanym `QTimer` 0 ms — licznik
  wywołań zatrzymuje się na 1 (z interwałem 1 ms: ~230 wywołań/s); dodatkowy timer 250 ms
  nie odpala wtedy ani razu (obok timera 1 ms: 4 razy na sekundę).
* Kod skompilowany: `index.html` podaje `WebAssembly.compileStreaming()` oryginalną odpowiedź
  `fetch` (postęp pobierania czyta z jej kopii, `response.clone()`), bo tylko odpowiedź z
  adresem URL może trafić do pamięci podręcznej skompilowanego kodu przeglądarki. Chrome
  zapisuje wtedy skompilowany moduł przy pierwszych wizytach (przy odpowiedzi z service workera
  obok niej w Cache Storage) i od trzeciej wizyty nie kompiluje funkcji od nowa: pierwsza
  klatka 0,85 → 0,63 s, praca głównego wątku 1,78 → 1,28 s (laptop, 1000 notatek); przy
  4× wolniejszym CPU (jak telefon) 2,47 → 1,77 s i 6,7 → 5,4 s. Gdy serwer wysyła inny typ
  niż `application/wasm`, powłoka tworzy odpowiedź z tym typem (wymaga go
  `compileStreaming`) — wtedy bez tej pamięci.

## Pliki

| Ścieżka | Zawartość |
|---|---|
| `wasm/index.html`, `wasm/sw.js`, `wasm/manifest.webmanifest` | powłoka HTML, service worker, manifest |
| `wasm/icons/` | ikony PWA; PNG wygenerowane z `assets/logo.svg` i `wasm/icons/maskable.svg` (odstęp bezpieczny dla ikon maskowalnych) oraz `badge.svg` (monochromatyczna plakietka powiadomień) |
| `src/platform/wasm/` | `WasmStorage` (IDBFS), `WasmNotifier`, `WasmShare` (udostępnienia przekazane przez inną kartę), most do ekranu ładowania i odczyty `visualViewport` (klawiatura ekranowa; `wasmshell.h`), test trwałości (`wasmselftest.h`) |
| `assets/fonts/`, `src/app/ui/fallbackfonts.h` | czcionki zapasowe emoji i symboli (bez nich: puste kwadraty), `scripts/fallback-fonts.py` je odtwarza |
| `scripts/wasm-build.sh`, `scripts/wasm-smoke.mjs` | build + złożenie strony, test w Chromium |
| `tests/app/drawing/wasm-drawing-check.mjs` | rysunek w notatce w jasnym i ciemnym motywie, w Chromium; z `--text` tekst wpisany na rysunku |

Ikony po zmianie logo:

```sh
cd wasm/icons
rsvg-convert -w 192 -h 192 ../../assets/logo.svg -o icon-192.png
rsvg-convert -w 512 -h 512 ../../assets/logo.svg -o icon-512.png
rsvg-convert -w 192 -h 192 maskable.svg -o maskable-192.png
rsvg-convert -w 512 -h 512 maskable.svg -o maskable-512.png
rsvg-convert -w 180 -h 180 maskable.svg -o apple-touch-icon.png
rsvg-convert -w 96 -h 96 badge.svg -o badge-96.png
optipng -o5 *.png
```
