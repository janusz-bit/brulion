**Brulion {{VERSION}}** — notatki w Markdown, które działają offline: konspekty z blokami i dziennik (jak w Logseq) na zwykłych plikach `.md` (jak w Obsidianie), zadania, kalendarz, przypomnienia z alarmami, obliczenia i tabele. Synchronizacja przez Syncthing albo Nextcloud.

**Nowości w {{VERSION}}**

* **Rysunki w notatkach** — `/Rysunek` (na telefonie także przycisk **Rysunek** nad klawiaturą) otwiera kartkę do rysowania: pióro z naciskiem rysika, zakreślacz, gumka (także drugim końcem rysika), kolory, grubości, papier gładki, w linie albo w kratkę. Gdy rysujesz rysikiem, palce tylko przesuwają i powiększają kartkę. Rysunek to plik SVG w załącznikach (`![[rysunek.svg]]`): Obsidian pokazuje go jako obraz, excalidraw.com otwiera do edycji, a zmiany z dwóch urządzeń łączą się linia po linii.
* **Widżety na Androidzie** — **Zadania — Mój dzień** (zaległe i dzisiejsze), **Kalendarz** (miesiąc z kropkami i agenda) i **Notatki** (dziennik, szybka notatka, ulubione). **+** dodaje zadanie do dzisiejszego dziennika od razu, także przy zamkniętym Brulionie.
* **Udostępnij → Brulion** na Androidzie — stronę z przeglądarki, tekst albo pliki `.md`/`.txt` zapiszesz w dzisiejszym dzienniku albo jako nową stronę.
* **„Zrobione” w powiadomieniu przypomnienia** — przycisk obok **Drzemka** i **OK** oznacza zadanie jako zrobione (`[x]` z datą `✅`) bez otwierania notatki: Linux, Windows, Android (także na ekranie alarmu) i baner przegapionych przypomnień.
* **Windows: natywne powiadomienia, autostart i kalendarz z zasobnika** — przypomnienia i alarmy jako powiadomienia Windows z przyciskami, uruchamianie po zalogowaniu (przełącznik i zadanie w instalatorze), a kliknięcie ikony obok zegara otwiera mały kalendarz z zadaniami dnia (na Linuksie: **Kalendarz** w menu ikony).
* **Wygląd na twoją miarę** — **Ustawienia → Wygląd**: kolor akcentu (gotowy albo własny, zawsze czytelny), białe albo czarne tło (OLED), czcionki interfejsu, notatek i kodu, odstęp między wierszami, zwarta gęstość na komputerze oraz wybór, co pokazuje pasek boczny i dolna nawigacja na telefonie.
* **Menu na znacznikach i linkach** — prawy przycisk albo przytrzymanie na 🔁, priorytecie, `#tagu`, `[[linku]]` i `[[2026-10-20]]` (otwórz, otwórz w panelu bocznym, kopiuj, zmień regułę, przesuń datę). Menu dat działa też w wynikach zapytań, wstawionych blokach, tabelach, widoku **Zadania** i kalendarzu; każdą zmianę cofniesz.
* **Import z Microsoft To Do** — logowanie kontem Microsoft (kod na microsoft.com/devicelogin, wymaga własnej bezpłatnej rejestracji aplikacji — przewodnik krok po kroku) albo plik JSON z Graph Explorera. Każda lista staje się stroną z zadaniami, terminami, przypomnieniami, powtarzaniem, krokami i plikami.
* **Nextcloud: zmiana nazwy dociera jako zmiana nazwy** — folder albo strona przemianowane na innym urządzeniu albo w Nextcloudzie nie są pobierane od nowa ani nie trafiają do kosza; historia wersji, ulubione i zwinięcia idą za nimi.
* **Folder Obsidiana jako notatnik** sam przejmuje ustawienia dziennika, szablonów i załączników z `.obsidian/`.
* **Zaznaczanie bloków z kilku dni dziennika** naraz i ich kopiowanie (każdy dzień po swoim linku); szybsze kopiowanie dużych stron na komputerze.
* **Zdjęcia z internetu:** powiększenie na całe okno, zapis wklejonego adresu obrazu w notatniku, w eksporcie HTML i Pandoc wybór: adresy albo kopie zdjęć.
* Poprawki: Enter na początku tekstu zadania wstawia puste zadanie nad nim, ponowienie nie gubi pustego akapitu, pasek przewijania w długich listach nie skacze, zdjęcia `![[https://…]]` w eksporcie, czytelne komunikaty CORS w przeglądarce.

Z wersji 0.4.5: zdjęcia z internetu w notatkach i pierwsza (testowa) wersja na Windows. Z wersji 0.4.4: eksport dla programu Pandoc (jeden dokument `.md` z notatnika, folderu albo strony — do Worda, LibreOffice, PDF i EPUB). Z wersji 0.4.2: zmiana daty z menu (prawy przycisk albo przytrzymanie znacznika daty). Z wersji 0.4.1: usunięty widok „Zaznacz tekst” (kopiuj, zaznaczając całe bloki). Z wersji 0.4.0: przenoszenie i tworzenie folderów (z poprawianiem linków i synchronizacją folderów w Nextcloud), tagi jako linki do stron, kopiowanie odnośników do punktów i nagłówków, zaznaczanie i kopiowanie grup bloków, pasek narzędzi przy klawiaturze na telefonie oraz wiersz poleceń `brulion-cli` i serwer MCP dla agentów AI. Z wersji 0.3.7: lista wszystkich zmian w dokumentacji. Z wersji 0.3.6: wybór statusu zadania z menu i poleceniem „/”, emoji w przeglądarce zamiast pustych kwadratów oraz poprawki bezpieczeństwa i stabilności wzorów, bloków kodu, dokumentacji, Nextcloud i obrazów. Z wersji 0.3.5: wybór godziny na okrągłym zegarze. Z wersji 0.3.4: obrazy i załączniki pokazywane jako zdjęcia. Z wersji 0.3.3: wzory matematyczne rysowane od razu w notatkach. Z wersji 0.3.2: przełączanie notatników jednym kliknięciem. Z wersji 0.3.1: synchronizacja Nextcloud przypisana do notatnika. Z wersji 0.3.0: dokumentacja napisana w Brulionie i szybkie przełączanie notatników.

**Najważniejsze**

* Dwa tryby notatki: **konspekt** (bloki z wcięciami, zwijanie, przybliżanie — jak w Logseq) i **dokument** (ciągły tekst z podglądem na żywo — jak w Obsidianie). Tryb wybierasz przy tworzeniu strony i możesz go zmienić w każdej chwili; zapisuje się w nagłówku notatki (`brulion: document`).
* Dziennik na każdy dzień, linki `[[…]]`, tagi, odwołania do bloków `((…))`, odnośniki zwrotne, graf, wyszukiwarka (także bez polskich znaków), historia wersji i kosz.
* Zadania z terminami, kalendarz (miesiąc, tydzień, agenda), przypomnienia ⏰ i alarmy ⏰! — na Androidzie działają przy zamkniętej aplikacji i po restarcie telefonu. Daty można wpisywać po polsku („jutro o 9”, „w piątek 18:00”).
* Obliczenia w blokach `calc` (jednostki, waluty, procenty) i tabele z formułami.
* Synchronizacja: Syncthing (zwykły folder, scalanie kopii konfliktów) albo Nextcloud/WebDAV (w tle, scalanie trójstronne, ochrona przed masowym usuwaniem).
* Import z Obsidiana, Logseq, Notion, Evernote, Google Keep, Microsoft To Do, Joplin, Simplenote, Roam, Bear, Trilium, Markdown, HTML i iCalendar; eksport do 10 formatów (m.in. dokument dla programu Pandoc: Word, LibreOffice, PDF).
* Interfejs po polsku (angielski do wyboru), jasny i ciemny motyw, własny kolor akcentu i czcionki.
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
