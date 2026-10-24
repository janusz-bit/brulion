**Brulion {{VERSION}}** — notatki w Markdown, które działają offline: konspekty z blokami i dziennik (jak w Logseq) na zwykłych plikach `.md` (jak w Obsidianie), zadania, kalendarz, przypomnienia z alarmami, obliczenia i tabele. Synchronizacja przez Syncthing albo Nextcloud.

**Nowości w {{VERSION}}**

* **Foldery: przenoszenie i tworzenie** — „Przenieś do…” dla stron i folderów (menu, „⋯” na telefonie, paleta, przeciąganie myszą), a linki, tagi i filtry zapytań wskazujące na przeniesione pliki są poprawiane. „Nowy folder” także w głównym folderze notatnika; nowa strona trafia do folderu otwartej strony. Drzewo pokazuje otwartą stronę i sortuje po polsku. Nextcloud synchronizuje puste foldery i zmiany nazw folderów.
* **Tagi jako linki do stron** — `#praca` otwiera stronę „praca”, jej odnośniki pokazują bloki z tym tagiem, a zmiana nazwy strony poprawia tagi.
* **Odnośniki do punktów** — „Kopiuj odnośnik” w menu każdego bloku: `((id))`, `[[Strona#^id]]`, `[[Strona#Nagłówek]]`; `Ctrl+Shift+C`; kliknięty odnośnik przewija do celu i go podświetla.
* **Zaznaczanie i kopiowanie grup bloków** — klawiaturą, myszą i palcem, także pojedyncze punkty (Ctrl+klik, „Pojedynczo”), oraz widok **„Zaznacz tekst”** do kopiowania dowolnego fragmentu strony przez akapity.
* **Pasek narzędzi zawsze przy klawiaturze** na telefonie — w dzienniku, na stronie, w tabelach i w trybie źródła.
* **Wiersz poleceń i agenci AI** — `brulion-cli` (notatki, dziennik, zadania, przypomnienia po polsku, zapytania; JSON), serwer MCP i — po włączeniu w ustawieniach — sterowanie otwartą aplikacją.

**Uwaga:** tagi otwierają teraz strony (widok tagu jest pod „Widok tagu”), przeniesienie strony o nazwie takiej jak inna może zamienić `[[Dom]]` i `#dom` w innych notatkach na `[[Projekty/Dom]]` / `#Projekty/Dom`, a na komputerze przeciąganie myszą po tekście zaznacza bloki zamiast przewijać. Szczegóły: strona „Lista zmian” w dokumentacji.

Z wersji 0.3.7: lista wszystkich zmian w dokumentacji. Z wersji 0.3.6: wybór statusu zadania z menu i poleceniem „/”, emoji w przeglądarce zamiast pustych kwadratów oraz poprawki bezpieczeństwa i stabilności wzorów, bloków kodu, dokumentacji, Nextcloud i obrazów. Z wersji 0.3.5: wybór godziny na okrągłym zegarze. Z wersji 0.3.4: obrazy i załączniki pokazywane jako zdjęcia. Z wersji 0.3.3: wzory matematyczne rysowane od razu w notatkach. Z wersji 0.3.2: przełączanie notatników jednym kliknięciem. Z wersji 0.3.1: synchronizacja Nextcloud przypisana do notatnika. Z wersji 0.3.0: dokumentacja napisana w Brulionie i szybkie przełączanie notatników.

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
| Wiersz poleceń (Linux) | `brulion-cli` — w AppImage jako `./Brulion-{{FILE_VERSION}}-x86_64.AppImage cli …`, w Nix `nix run github:{{REPO}}/{{TAG}}#brulion-cli -- …` |
| Przeglądarka | {{PAGES_URL}} — albo `brulion-web-{{FILE_VERSION}}.zip` na własny serwer (dowolny katalog, HTTPS) |
| Kod źródłowy | `brulion-{{FILE_VERSION}}-source.tar.gz` |

**Instalacja**

* **Android:** pobierz plik `.apk` na telefonie i otwórz go (pierwszy raz system poprosi o zgodę na instalowanie aplikacji z tego źródła). Nowa wersja instaluje się na poprzednią bez utraty notatek.
* **Linux (AppImage):** `chmod +x Brulion-*.AppImage`, a potem `./Brulion-{{FILE_VERSION}}-x86_64.AppImage` (albo `…-aarch64.AppImage`). Gdy system zgłasza błąd FUSE (np. w kontenerze): `./Brulion-*.AppImage --appimage-extract-and-run`.
* **NixOS:** `nix run github:{{REPO}}/{{TAG}}` albo pakiet z flake'a w konfiguracji systemu.
* **Przeglądarka:** otwórz stronę i wybierz „Zainstaluj aplikację” — działa także bez internetu.

Sumy kontrolne SHA-256: `SHA256SUMS.txt` (`sha256sum -c SHA256SUMS.txt --ignore-missing`).
