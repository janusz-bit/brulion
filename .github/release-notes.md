**Brulion {{VERSION}}** — notatki w Markdown, które działają offline: konspekty z blokami i dziennik (jak w Logseq) na zwykłych plikach `.md` (jak w Obsidianie), zadania, kalendarz, przypomnienia z alarmami, obliczenia i tabele. Synchronizacja przez Syncthing albo Nextcloud.

**Nowości w {{VERSION}}**

* **Odświeżony wygląd** — spokojniejszy pasek boczny, `#tagi` i daty w tekście jako zaokrąglone etykiety, linie wcięć w listach i konspekcie, czytelniejsze ustawienia, kalendarz i przełączniki, wygodniejsza szerokość tekstu (680 px; szerszą wybierzesz w **Ustawienia → Edytor**), a na telefonie **+ Notatka** w dolnym pasku zamiast przycisku nad treścią i większe pola dotyku.
* **Edycja właściwości strony** — panel **Właściwości** ma pola według typu (tekst, liczba, data, data i godzina, pole wyboru, lista, tagi, aliasy) z podpowiedziami kluczy z notatnika; **Ctrl+;** dodaje właściwość. Brulion zmienia tylko edytowany wiersz frontmattera albo `klucz:: wartość`, a każdą zmianę cofniesz.
* **Rysunki: ciemne tło, kształty, tekst i lasso** — tło rysunku **Jak motyw**, **Jasne** albo **Ciemne** (zmienia się od razu, także w notatkach), prostokąt, elipsa, romb, linia, strzałka, tekst i zaznaczanie lassem (przesuwanie, kolor, kopiowanie, usuwanie). Rysunki z excalidraw.com z kształtami i tekstem da się teraz edytować.
* **Notatki na slajdach i PDF-ach** — `/Rysunek na obrazie lub PDF` (także `/slajd`, `/pdf`) kładzie rysunek na obrazie albo na wybranych stronach PDF-a; podkład jest zablokowany, więc gumka i lasso go nie ruszą.
* **Rysunki z Obsidiana** — pliki `*.excalidraw.md` wtyczki Excalidraw otwierają się, widać je w notatkach i zapisują się w swoim formacie; notatka *canvas* z Trilium importuje się jako jeden rysunek.
* **Scal rysunki** — kopię konfliktową rysunku od Syncthinga centrum synchronizacji pokazuje obok oryginału, a **Scal rysunki** łączy elementy obu wersji; Nextcloud scala rysunki zmienione na dwóch urządzeniach tak samo.
* **Otwórz w Brulionie poza Androidem** — pliki `.md` i `.txt` otworzysz w Brulionie z menedżera plików na Windows (instalator) i Linuksie (Nix, AppImage), a wersja w przeglądarce zainstalowana jako aplikacja przyjmuje udostępnianie — wszystko przez to samo okno co „Udostępnij → Brulion” na Androidzie.
* **Szybciej i lżej** — indeks notatek powstaje na kilku rdzeniach i zostaje zapisany na urządzeniu (kolejne otwarcie analizuje tylko zmienione notatki), zajmuje ok. 60 % mniej pamięci; po zapisie Zadania, Kalendarz i zapytania odświeżają się w ok. 6 ms zamiast ok. 112 ms (10 000 notatek); szybsze cofanie na długich stronach; płynny graf liczony w osobnym wątku; systemowe okna wyboru plików, a dzięki nim APK mniejszy o ok. 15 MB i wersja w przeglądarce o ok. 2,3 MB do pobrania.
* **Telefon** — dolna nawigacja wraca, gdy schowasz klawiaturę (pasek narzędzi leży wtedy tuż nad nią), szuflada bez pustych pasów i z otwartą stroną zawsze widoczną.
* Poprawki: pasek narzędzi przylega do klawiatury w wersji w przeglądarce (Brave na Androidzie), Enter w przybliżonym nagłówku nie gubi kursora, rysik rysuje z naciskiem, a drugi koniec rysika ściera (Android, przeglądarka), rysunek w Firefoksie zapisuje się po ostatniej linii.

Z wersji 0.5.0: rysunki w notatkach, widżety na Androidzie, „Udostępnij → Brulion”, „Zrobione” w powiadomieniu przypomnienia, natywne powiadomienia, autostart i kalendarz z zasobnika na Windows, ustawienia wyglądu, menu na znacznikach i linkach, import z Microsoft To Do. Z wersji 0.4.5: zdjęcia z internetu w notatkach i pierwsza (testowa) wersja na Windows. Z wersji 0.4.4: eksport dla programu Pandoc (jeden dokument `.md` z notatnika, folderu albo strony — do Worda, LibreOffice, PDF i EPUB). Z wersji 0.4.2: zmiana daty z menu (prawy przycisk albo przytrzymanie znacznika daty). Z wersji 0.4.1: usunięty widok „Zaznacz tekst” (kopiuj, zaznaczając całe bloki). Z wersji 0.4.0: przenoszenie i tworzenie folderów (z poprawianiem linków i synchronizacją folderów w Nextcloud), tagi jako linki do stron, kopiowanie odnośników do punktów i nagłówków, zaznaczanie i kopiowanie grup bloków, pasek narzędzi przy klawiaturze na telefonie oraz wiersz poleceń `brulion-cli` i serwer MCP dla agentów AI. Z wersji 0.3.7: lista wszystkich zmian w dokumentacji. Z wersji 0.3.6: wybór statusu zadania z menu i poleceniem „/”, emoji w przeglądarce zamiast pustych kwadratów oraz poprawki bezpieczeństwa i stabilności wzorów, bloków kodu, dokumentacji, Nextcloud i obrazów. Z wersji 0.3.5: wybór godziny na okrągłym zegarze. Z wersji 0.3.4: obrazy i załączniki pokazywane jako zdjęcia. Z wersji 0.3.3: wzory matematyczne rysowane od razu w notatkach. Z wersji 0.3.2: przełączanie notatników jednym kliknięciem. Z wersji 0.3.1: synchronizacja Nextcloud przypisana do notatnika. Z wersji 0.3.0: dokumentacja napisana w Brulionie i szybkie przełączanie notatników.

**Najważniejsze**

* Dwa tryby notatki: **konspekt** (bloki z wcięciami, zwijanie, przybliżanie — jak w Logseq) i **dokument** (ciągły tekst z podglądem na żywo — jak w Obsidianie). Tryb wybierasz przy tworzeniu strony i możesz go zmienić w każdej chwili; zapisuje się w nagłówku notatki (`brulion: document`).
* Dziennik na każdy dzień, linki `[[…]]`, tagi, odwołania do bloków `((…))`, odnośniki zwrotne, graf, wyszukiwarka (także bez polskich znaków), historia wersji i kosz.
* Zadania z terminami, kalendarz (miesiąc, tydzień, agenda), przypomnienia ⏰ i alarmy ⏰! — na Androidzie działają przy zamkniętej aplikacji i po restarcie telefonu. Daty można wpisywać po polsku („jutro o 9”, „w piątek 18:00”).
* Obliczenia w blokach `calc` (jednostki, waluty, procenty) i tabele z formułami.
* Synchronizacja: Syncthing (zwykły folder, scalanie kopii konfliktów) albo Nextcloud/WebDAV (w tle, scalanie trójstronne, ochrona przed masowym usuwaniem).
* Import z Obsidiana, Logseq, Notion, Evernote, Google Keep, Microsoft To Do, Joplin, Simplenote, Roam, Bear, Trilium, Markdown, HTML i iCalendar; eksport do 10 formatów (m.in. dokument dla programu Pandoc: Word, LibreOffice, PDF).
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
