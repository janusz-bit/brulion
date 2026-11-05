**Brulion {{VERSION}}** — notatki w Markdown, które działają offline: konspekty z blokami i dziennik (jak w Logseq) na zwykłych plikach `.md` (jak w Obsidianie), zadania, kalendarz, przypomnienia z alarmami, obliczenia i tabele. Synchronizacja przez Syncthing albo Nextcloud.

**Nowości w {{VERSION}}**

Wersja 0.9.0 nie została opublikowana, więc jej nowości trafiają do Ciebie dopiero w tym wydaniu.

* **Karty i osobne okna** — kilka stron otwartych naraz, każda z własną historią: **Ctrl+T**, **Ctrl+W**, **Ctrl+Tab**, **Ctrl+1…9**, środkowy przycisk myszy albo **Ctrl+kliknięcie** otwiera stronę w nowej karcie. Widok można podzielić na dwie strony obok siebie, a na komputerze otworzyć stronę w osobnym oknie. Karty wracają po ponownym uruchomieniu; na telefonie przełącza się je z górnego paska.
* **Sprawdzanie pisowni** — błędy po polsku i angielsku są podkreślone w edytowanym bloku, a prawy przycisk (na telefonie dotknięcie słowa) podpowiada poprawki, **Dodaj do słownika** i **Ignoruj**. Kod, linki, tagi i daty są pomijane. Na komputerze Hunspell, na Androidzie sprawdzanie pisowni klawiatury; **Ustawienia → Edytor**.
* **Podgląd linku** — najechanie myszą na `[[stronę]]` albo `((blok))` pokazuje jego treść w dymku, bez opuszczania notatki.
* **Notatki tygodniowe i miesięczne** — `journals/2026-W45.md` i `journals/2026-11.md` z własnymi szablonami, otwierane numerem tygodnia albo tytułem miesiąca w kalendarzu, **Ctrl+Alt+W** / **Ctrl+Alt+M** i `brulion-cli week` / `month`.
* **Szukanie w dzienniku przez wiele dni** — **Ctrl+F** w strumieniu dziennika liczy i podświetla trafienia we wszystkich dniach i przechodzi między nimi; „Zamień wszystko” obejmuje je wszystkie i cofa się jednym **Cofnij**.
* **Nawyki** — punkty z `habit:: codziennie` (albo `pn, śr, pt`, `3 razy w tygodniu`) odhaczane w dzienniku: widok **Nawyki** pokazuje serię, skuteczność i siatkę miesiąca.
* **Zależności i czas zadań** — `🆔` / `⛔` z Obsidian Tasks działają: zadanie zablokowane pokazuje, na co czeka, a zapytania znają `is:blocked`. Przy zadaniu z podzadaniami jest licznik „3/5”. Licznik czasu (wpisy `:LOGBOOK:` jak w Logseq) z podsumowaniem **Czas pracy** w Zadaniach i opcjonalną historią zmian statusu.
* **Tabele i wykresy** — sortowanie i filtrowanie tabeli z menu kolumny, eksport i import CSV (`/Tabela z CSV…`) oraz bloki ```` ```chart ```` (słupki, linie, koło) z tabeli, bloku `calc` albo zapytania; w eksporcie HTML jako SVG.
* **Druk, PDF i Word bez Pandoca** — **Drukuj…** (**Ctrl+P**) i **Eksportuj stronę** w menu strony: Markdown (także z załącznikami w ZIP), HTML, PDF i Word (DOCX) zrobione przez sam Brulion, z polskimi znakami, tabelami, obrazami i diagramami — bez Pandoca i LaTeX-a, także na telefonie. PDF i DOCX są też w eksporcie całego notatnika.
* **Przeglądarka PDF** — zaznaczanie i kopiowanie tekstu, spis treści, działające linki, PDF-y z hasłem, dwie strony obok siebie i widoczne adnotacje. **Otwórz w innej aplikacji** działa też dla dokumentów Office, OpenDocument, EPUB, audio i wideo.
* **Wygląd stron** — ikona strony (emoji albo ikona) przed tytułem, w drzewku i przy linkach, okładka u góry strony i kolorowe zakreślenia (**Zakreśl…**, zapis jak we wtyczce Highlightr do Obsidiana).
* **Statystyki, tryb skupienia i własne skróty** — liczba słów, znaków i czas czytania strony albo zaznaczenia; **F8** chowa wszystko poza tekstem, a przewijanie „jak w maszynie do pisania” trzyma wiersz na środku. Skróty poleceń zmienisz w **Ustawienia → Skróty klawiszowe**.
* **Rysunki** — wyrównywanie i równe rozkładanie zaznaczenia, napisy w trójkątach, gwiazdach i innych kształtach, kopiowanie i wklejanie z excalidraw.com, poprzednia / następna strona PDF w edytorze rysunku oraz obrazy z rysunków Obsidiana (także w eksporcie).
* Zaokrąglone tagi i daty także w odnośnikach zwrotnych, Zadaniach, osadzeniach, tabelach i wynikach zapytań; długie wiersze tych list są teraz przycinane z „…”.

Z wersji 0.8.1: diagramy Mermaid, automatyczne kopie zapasowe, znajdź i zamień (w notatce i w całym notatniku), klikalne części tagu zagnieżdżonego i usuwanie z cofaniem. Z wersji 0.7.0: więcej kształtów, uchwyty do zmiany rozmiaru i obracania oraz panel **Styl** w rysunkach, wstawianie i przenoszenie wierszy i kolumn tabel, **Sprzątanie** (notatki i załączniki, do których nic nie prowadzi) i komunikat o nowej wersji na Androidzie i Windows. Z wersji 0.6.3: własny kolor tła rysunku. Z wersji 0.6.2: dowolny plik jako link (`/Plik…`). Z wersji 0.6.1: podgląd i przeglądarka PDF w aplikacji, klikalne części linku do strony w folderze. Z wersji 0.6.0: odświeżony wygląd, edycja właściwości strony, ciemne tło, kształty, tekst i lasso w rysunkach, rysowanie na slajdach i PDF-ach, rysunki z Obsidiana (`*.excalidraw.md`), scalanie konfliktów rysunków, „Otwórz w Brulionie” na Windows i Linuksie, szybszy i lżejszy indeks, szybsze cofanie, płynny graf, mniejsza aplikacja. Z wersji 0.5.0: rysunki w notatkach, widżety na Androidzie, „Udostępnij → Brulion”, „Zrobione” w powiadomieniu przypomnienia, natywne powiadomienia, autostart i kalendarz z zasobnika na Windows, ustawienia wyglądu, menu na znacznikach i linkach, import z Microsoft To Do. Z wersji 0.4.5: zdjęcia z internetu w notatkach i pierwsza (testowa) wersja na Windows. Z wersji 0.4.4: eksport dla programu Pandoc (jeden dokument `.md` z notatnika, folderu albo strony — do Worda, LibreOffice, PDF i EPUB). Z wersji 0.4.2: zmiana daty z menu (prawy przycisk albo przytrzymanie znacznika daty). Z wersji 0.4.1: usunięty widok „Zaznacz tekst” (kopiuj, zaznaczając całe bloki). Z wersji 0.4.0: przenoszenie i tworzenie folderów (z poprawianiem linków i synchronizacją folderów w Nextcloud), tagi jako linki do stron, kopiowanie odnośników do punktów i nagłówków, zaznaczanie i kopiowanie grup bloków, pasek narzędzi przy klawiaturze na telefonie oraz wiersz poleceń `brulion-cli` i serwer MCP dla agentów AI. Z wersji 0.3.7: lista wszystkich zmian w dokumentacji. Z wersji 0.3.6: wybór statusu zadania z menu i poleceniem „/”, emoji w przeglądarce zamiast pustych kwadratów oraz poprawki bezpieczeństwa i stabilności wzorów, bloków kodu, dokumentacji, Nextcloud i obrazów. Z wersji 0.3.5: wybór godziny na okrągłym zegarze. Z wersji 0.3.4: obrazy i załączniki pokazywane jako zdjęcia. Z wersji 0.3.3: wzory matematyczne rysowane od razu w notatkach. Z wersji 0.3.2: przełączanie notatników jednym kliknięciem. Z wersji 0.3.1: synchronizacja Nextcloud przypisana do notatnika. Z wersji 0.3.0: dokumentacja napisana w Brulionie i szybkie przełączanie notatników.

**Najważniejsze**

* Dwa tryby notatki: **konspekt** (bloki z wcięciami, zwijanie, przybliżanie — jak w Logseq) i **dokument** (ciągły tekst z podglądem na żywo — jak w Obsidianie). Tryb wybierasz przy tworzeniu strony i możesz go zmienić w każdej chwili; zapisuje się w nagłówku notatki (`brulion: document`).
* Dziennik na każdy dzień, linki `[[…]]`, tagi, odwołania do bloków `((…))`, odnośniki zwrotne, graf, wyszukiwarka (także bez polskich znaków) z zamianą, historia wersji, kosz i automatyczne kopie zapasowe.
* Zadania z terminami, kalendarz (miesiąc, tydzień, agenda), przypomnienia ⏰ i alarmy ⏰! — na Androidzie działają przy zamkniętej aplikacji i po restarcie telefonu. Daty można wpisywać po polsku („jutro o 9”, „w piątek 18:00”).
* Obliczenia w blokach `calc` (jednostki, waluty, procenty) i tabele z formułami.
* Synchronizacja: Syncthing (zwykły folder, scalanie kopii konfliktów) albo Nextcloud/WebDAV (w tle, scalanie trójstronne, ochrona przed masowym usuwaniem).
* Import z Obsidiana, Logseq, Notion, Evernote, Google Keep, Microsoft To Do, Joplin, Simplenote, Roam, Bear, Trilium, Markdown, HTML i iCalendar; eksport do 12 formatów (m.in. PDF, Word i dokument dla programu Pandoc) oraz druk.
* Interfejs po polsku (angielski do wyboru), jasny i ciemny motyw, własny kolor akcentu i czcionki.
* Rysunki w notatkach: pióro z naciskiem rysika, kształty, tekst, ciemne tło, rysowanie na slajdach i PDF-ach; pliki SVG, które Obsidian pokazuje, a excalidraw.com otwiera do edycji.
* Android: widżety na ekranie głównym i „Udostępnij → Brulion”.

| Platforma | Do pobrania |
|---|---|
| Android 9 lub nowszy (arm64) | `brulion-{{FILE_VERSION}}-arm64-v8a.apk` |
| Linux x86_64 | `Brulion-{{FILE_VERSION}}-x86_64.AppImage` — Ubuntu 22.04+, Debian 12+, Fedora 36+ (glibc ≥ 2.35) |
| Linux aarch64 (np. Raspberry Pi 4/5 z 64-bitowym systemem) | `Brulion-{{FILE_VERSION}}-aarch64.AppImage` — Ubuntu 24.04+, Debian 13+, Fedora 40+ (glibc ≥ 2.39) |
| Windows 10/11 (x86_64) — wersja testowa | `Brulion-{{FILE_VERSION}}-windows-x86_64-setup.exe` (instalator) albo `brulion-{{FILE_VERSION}}-windows-x86_64.zip` (wersja przenośna) |
| NixOS | flake `github:{{REPO}}/{{TAG}}`, pakiet `brulion` |
| Wiersz poleceń (Linux, Windows) | `brulion-cli` — w AppImage jako `./Brulion-{{FILE_VERSION}}-x86_64.AppImage cli …`, w Nix `nix run github:{{REPO}}/{{TAG}}#brulion-cli -- …`, w Windows `brulion-cli.exe` obok `brulion.exe` |
| Przeglądarka | {{PAGES_URL}} — albo `brulion-web-{{FILE_VERSION}}.zip` na własny serwer (dowolny katalog, HTTPS) |
| Kod źródłowy | `brulion-{{FILE_VERSION}}-source.tar.gz` |

**Instalacja**

* **Android:** pobierz plik `.apk` na telefonie i otwórz go (pierwszy raz system poprosi o zgodę na instalowanie aplikacji z tego źródła). Nowa wersja instaluje się na poprzednią bez utraty notatek.
* **Linux (AppImage):** `chmod +x Brulion-*.AppImage`, a potem `./Brulion-{{FILE_VERSION}}-x86_64.AppImage` (albo `…-aarch64.AppImage`). Gdy system zgłasza błąd FUSE (np. w kontenerze): `./Brulion-*.AppImage --appimage-extract-and-run`.
* **Windows (wersja testowa):** uruchom `Brulion-{{FILE_VERSION}}-windows-x86_64-setup.exe` — instaluje się tylko dla bieżącego użytkownika, bez uprawnień administratora (odinstalowanie zostawia notatki i ustawienia). Aplikacja nie jest podpisana, więc SmartScreen ostrzega: „Więcej informacji” → „Uruchom mimo to”. Wersja przenośna: rozpakuj `brulion-{{FILE_VERSION}}-windows-x86_64.zip` i uruchom `brulion.exe` z folderu `Brulion`. Przypomnienia i alarmy są powiadomieniami Windows; w ustawieniach włączysz uruchamianie po zalogowaniu.
* **NixOS:** `nix run github:{{REPO}}/{{TAG}}` albo pakiet z flake'a w konfiguracji systemu.
* **Przeglądarka:** otwórz stronę i wybierz „Zainstaluj aplikację” — działa także bez internetu.

Sumy kontrolne SHA-256: `SHA256SUMS.txt` (`sha256sum -c SHA256SUMS.txt --ignore-missing`).
