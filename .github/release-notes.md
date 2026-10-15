**Brulion {{VERSION}}** — notatki w Markdown, które działają offline: konspekty z blokami i dziennik (jak w Logseq) na zwykłych plikach `.md` (jak w Obsidianie), zadania, kalendarz, przypomnienia z alarmami, obliczenia i tabele. Synchronizacja przez Syncthing albo Nextcloud.

**Najważniejsze**

* Dwa tryby notatki: **konspekt** (bloki z wcięciami, zwijanie, przybliżanie — jak w Logseq) i **dokument** (ciągły tekst z podglądem na żywo — jak w Obsidianie). Tryb wybierasz przy tworzeniu strony i możesz go zmienić w każdej chwili; zapisuje się w nagłówku notatki (`brulion: document`).
* Dziennik na każdy dzień, linki `[[…]]`, tagi, odwołania do bloków `((…))`, odnośniki zwrotne, graf, wyszukiwarka (także bez polskich znaków), historia wersji i kosz.
* Zadania z terminami, kalendarz (miesiąc, tydzień, agenda), przypomnienia ⏰ i alarmy ⏰! — na Androidzie działają przy zamkniętej aplikacji i po restarcie telefonu. Daty można wpisywać po polsku („jutro o 9”, „w piątek 18:00”).
* Obliczenia w blokach `calc` (jednostki, waluty, procenty) i tabele z formułami.
* Synchronizacja: Syncthing (zwykły folder, scalanie kopii konfliktów) albo Nextcloud/WebDAV (w tle, scalanie trójstronne, ochrona przed masowym usuwaniem).
* Import z Obsidiana, Logseq, Notion, Evernote, Google Keep, Joplin, Simplenote, Roam, Bear, Markdown, HTML i iCalendar; eksport do 9 formatów.
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
