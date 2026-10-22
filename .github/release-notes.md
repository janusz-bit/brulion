**Brulion {{VERSION}}** — notatki w Markdown, które działają offline: konspekty z blokami i dziennik (jak w Logseq) na zwykłych plikach `.md` (jak w Obsidianie), zadania, kalendarz, przypomnienia z alarmami, obliczenia i tabele. Synchronizacja przez Syncthing albo Nextcloud.

**Nowości w {{VERSION}}**

* **Status zadania z menu i poleceniem „/”:** prawy przycisk myszy albo przytrzymanie palcem na polu wyboru zadania otwiera menu statusu — do zrobienia, w toku, zrobione, oczekujące, anulowane albo bez statusu (zadanie staje się zwykłym punktem). Działa wszędzie, gdzie widać zadania: w notatce i dzienniku, w wynikach zapytań, we wstawionych blokach, w odnośnikach zwrotnych, na stronach tagów, w kalendarzu i w widoku Zadania. W edytorze ten sam wybór dają polecenia „/” („Zadanie: do zrobienia”, „Zadanie: w toku”… — wystarczy wpisać np. `/zrobione`, `/czeka` albo `/bez statusu`) i Ctrl+Shift+Enter; Ctrl+Enter nadal przełącza status po kolei, a na telefonie menu otwiera przytrzymanie przycisku Zadanie nad klawiaturą.
* **Emoji w przeglądarce:** dzwonki ⏰ 🔔, znaczniki zadań 📅 ⏳ ✅ 🔁 ⏫ i inne emoji nie są już pustymi kwadratami — Brulion ma własne czcionki zapasowe (Noto Emoji i Noto Sans Math, SIL OFL 1.1). Pomagają też na Linuksie bez czcionki z emoji.
* **Bezpieczeństwo i stabilność wzorów:** zsynchronizowana lub zaimportowana notatka nie może już przez wzór z `\left<` … `\right>` wstawić własnego HTML (zdalny obrazek bez kliknięcia, skrypt w eksporcie HTML), a bardzo głęboko zagnieżdżony wzór nie wywraca aplikacji. Interfejs w ogóle nie pobiera niczego z sieci.
* **Bloki kodu (błąd z 0.3.5):** zmiana w bloku kodu, pod którym jest identyfikator `^id` (dopisywany przez „Kopiuj odwołanie do bloku” i `((`), właściwość `id::` albo opis w tym samym punkcie, nie zamienia już po zapisie reszty strony w kod. Linia ``` wpisana w akapicie, wklejony kod bez zamknięcia i sama linia otwierająca dostają linię zamykającą, a niezamknięty blok znów zamkniesz, wpisując ```.
* **Dolary to nie wzory:** `Netflix 15$, Spotify 10$`, `10$/h` czy `$PATH:$HOME` zostają zwykłym tekstem, także po imporcie z HTML, Evernote, Keep, Notion, Trilium i kalendarzy. **Uwaga:** `\(` i `\[` to znów zwykłe nawiasy (jak w Obsidianie) — wzory zapisane od 0.3.3 jako `\(…\)` lub `\[…\]` zamień na `$…$` i `$$…$$`. Poprawnie rysują się też `\left\{…\right\}`, `\bigcup` i podobne, a wzór między liniami `$$` zostaje jednym blokiem, także w konspekcie.
* **Dokumentacja Brulionu:** czytanie jej nie wyłącza już przypomnień i alarmów Twojego notatnika, po restarcie otwiera się Twój notatnik, a szybka notatka trafia do niego. Dokumentacja aktualizuje się razem z aplikacją (Twoje zmiany i usunięcia zostają) i znika z listy ostatnich notatników.
* **Nextcloud:** każdy notatnik ma własny folder na serwerze — Brulion nie zapisze folderu, z którym synchronizuje się już inny notatnik, a dwa notatniki połączone z jednym folderem przez wersje 0.3.1–0.3.5 wstrzymują synchronizację, dopóki jednemu nie wybierzesz innego folderu. Ustawienia z 0.3.0 przejmuje notatnik, który był wtedy synchronizowany; „Wyloguj” wyłącza synchronizację we wszystkich notatnikach.
* **Obrazy:** widok „Pokaż jako tekst” jest tylko do odczytu (przypadkowy znak nie nadpisze zdjęcia), czyta tylko pokazywany początek pliku i Ctrl+E przełącza w obie strony; pod obrazem widać notatki, które go osadzają, małe obrazy nie są rozciągane, a formaty, których urządzenie nie odczyta (WebP, AVIF na telefonie i w przeglądarce), są nazwane — także w notatce.
* **Zegar i edytor:** przeciąganie wskazówki działa także od cyfry, tarcza mieści się na telefonie trzymanym poziomo, a w trybie źródła na telefonie tekst przewija się palcem.
* **Inne:** zadanie zaczynające się od kodu albo wzoru pokazuje się w widoku Zadania w całości; obliczenia z przecinkiem dziesiętnym w funkcjach (`sqrt(2,25)` = 1,5; niejednoznaczne zapisy pokazują błąd zamiast złego wyniku); powrót do notatnika, którego folder zniknął, nie tworzy pustego notatnika; AppImage można aktualizować przez AppImageUpdate (pliki `.zsync`), a wersja w przeglądarce i `nix run` pokazuje właściwy numer wersji.

Z wersji 0.3.5: wybór godziny na okrągłym zegarze. Z wersji 0.3.4: obrazy i załączniki pokazywane jako zdjęcia. Z wersji 0.3.3: wzory matematyczne rysowane od razu w notatkach. Z wersji 0.3.2: przełączanie notatników jednym kliknięciem. Z wersji 0.3.1: synchronizacja Nextcloud przypisana do notatnika. Z wersji 0.3.0: dokumentacja napisana w Brulionie i szybkie przełączanie notatników.

**Najważniejsze**

* Dwa tryby notatki: **konspekt** (bloki z wcięciami, zwijanie, przybliżanie — jak w Logseq) i **dokument** (ciągły tekst z podglądem na żywo — jak w Obsidianie). Tryb wybierasz przy tworzeniu strony i możesz go zmienić w każdej chwili; zapisuje się w nagłówku notatki (`brulion: document`).
* Dziennik na każdy dzień, linki `[[…]]`, tagi, odwołania do bloków `((…))`, odnośniki zwrotne, graf, wyszukiwarka (także bez polskich znaków), historia wersji i kosz.
* Zadania z terminami, kalendarz (miesiąc, tydzień, agenda), przypomnienia ⏰ i alarmy ⏰! — na Androidzie działają przy zamkniętej aplikacji i po restarcie telefonu. Daty można wpisywać po polsku („jutro o 9”, „w piątek 18:00”).
* Obliczenia w blokach `calc` (jednostki, waluty, procenty) i tabele z formułami.
* Synchronizacja: Syncthing (zwykły folder, scalanie kopii konfliktów) albo Nextcloud/WebDAV (w tle, scalanie trójstronne, ochrona przed masowym usuwaniem).
* Import z Obsidiana, Logseq, Notion, Evernote, Google Keep, Joplin, Simplenote, Roam, Bear, Trilium, Markdown, HTML i iCalendar; eksport do 9 formatów.
* Interfejs po polsku (angielski do wyboru), jasny i ciemny motyw.

| Platforma | Do pobrania |
|---|---|
| Android 9 lub nowszy (arm64) | `brulion-{{FILE_VERSION}}-arm64-v8a.apk` |
| Linux x86_64 | `Brulion-{{FILE_VERSION}}-x86_64.AppImage` — Ubuntu 22.04+, Debian 12+, Fedora 36+ (glibc ≥ 2.35) |
| Linux aarch64 (np. Raspberry Pi 4/5 z 64-bitowym systemem) | `Brulion-{{FILE_VERSION}}-aarch64.AppImage` — Ubuntu 24.04+, Debian 13+, Fedora 40+ (glibc ≥ 2.39) |
| NixOS | flake `github:{{REPO}}/{{TAG}}`, pakiet `brulion` |
| Przeglądarka | {{PAGES_URL}} — albo `brulion-web-{{FILE_VERSION}}.zip` na własny serwer (dowolny katalog, HTTPS) |
| Kod źródłowy | `brulion-{{FILE_VERSION}}-source.tar.gz` |

**Instalacja**

* **Android:** pobierz plik `.apk` na telefonie i otwórz go (pierwszy raz system poprosi o zgodę na instalowanie aplikacji z tego źródła). Nowa wersja instaluje się na poprzednią bez utraty notatek.
* **Linux (AppImage):** `chmod +x Brulion-*.AppImage`, a potem `./Brulion-{{FILE_VERSION}}-x86_64.AppImage` (albo `…-aarch64.AppImage`). Gdy system zgłasza błąd FUSE (np. w kontenerze): `./Brulion-*.AppImage --appimage-extract-and-run`.
* **NixOS:** `nix run github:{{REPO}}/{{TAG}}` albo pakiet z flake'a w konfiguracji systemu.
* **Przeglądarka:** otwórz stronę i wybierz „Zainstaluj aplikację” — działa także bez internetu.

Sumy kontrolne SHA-256: `SHA256SUMS.txt` (`sha256sum -c SHA256SUMS.txt --ignore-missing`).
