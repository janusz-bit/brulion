---
tags: [dokumentacja]
brulion: document
---
# Sprawdzanie pisowni

Brulion podkreśla na czerwono słowa, których nie zna słownik — po polsku i po angielsku, także oba języki naraz. Sprawdza blok, który właśnie edytujesz, i źródło Markdown (Ctrl+E); na komputerze także resztę strony. Na telefonie i tablecie reszta strony zostaje czysta, dopóki nie włączysz **Podkreślaj na całej stronie** (zob. Ustawienia niżej).

## Poprawianie
- **Na komputerze** kliknij podkreślone słowo prawym przyciskiem myszy. Na górze menu są podpowiedzi — kliknięcie wstawia poprawione słowo (Ctrl+Z je cofa). Pod nimi:
  - **Dodaj „…” do słownika** — słowo trafia do słownika użytkownika i nie będzie już podkreślane,
  - **Ignoruj** — słowo przestaje być podkreślane do zamknięcia Brulionu.
- **Na telefonie i tablecie** dotknij podkreślonego słowa — pojawią się te same podpowiedzi.
- **W bloku, którego nie edytujesz**, kliknij podkreślone słowo prawym przyciskiem: te same podpowiedzi, **Dodaj do słownika** i **Ignoruj**. Podpowiedź poprawia blok bez otwierania go (Ctrl+Z ją cofa). Tekst linku `[opis](adres)` jest tam podkreślany dopiero podczas edycji bloku. Na telefonie i tablecie dotknij bloku, żeby go edytować, a potem dotknij słowa.
- **W źródle Markdown** (Ctrl+E) kliknij podkreślone słowo prawym przyciskiem — na telefonie i tablecie dotknij go. Ctrl+Z cofa poprawkę także tam.

Słowo, które właśnie piszesz, nie jest podkreślane, dopóki go nie skończysz.

## Co jest sprawdzane
Tylko zwykły tekst. Brulion pomija:
- `kod w tekście`, bloki kodu, obliczenia (`calc`) i zapytania,
- w źródle Markdown także właściwości na początku pliku (nagłówek między liniami `---`) i wzory między liniami `$$`,
- linki `[[Strona]]`, odwołania do bloków, adresy stron i e-maili (tekst linku `[opis](adres)` jest sprawdzany),
- `#tagi`, daty, przypomnienia, powtarzanie i priorytety zadań (📅 ⏰ 🔁 ⏫…),
- wzory `$…$`, nazwy właściwości `klucz::`, słowa `TODO`/`DONE`, komentarze `%%…%%`,
- liczby i słowa z cyframi (`mp3`, `COVID-19`), skróty wielkimi literami (`PDF`, `NASA`), nazwy w rodzaju `iPhone`, nazwy plików i ścieżki.

Słowa z łącznikiem (`biało-czerwony`) i z apostrofem (`don't`) są sprawdzane w całości albo po częściach. Skrót z kropką (`np.`, `e.g.`) jest poprawny, jeśli słownik zna go z kropką.

## Ustawienia
**Ustawienia → Edytor**:
- **Sprawdzaj pisownię** — włącza i wyłącza podkreślanie.
- **Podkreślaj na całej stronie** — podkreśla także w blokach, których nie edytujesz. Domyślnie włączone na komputerze, wyłączone na telefonie i tablecie (tam każde słowo z ekranu trafia do systemowego sprawdzania pisowni).
- **Języki pisowni** — *Automatycznie* (język aplikacji, język systemu i angielski: przy polskim interfejsie polski i angielski), *Polski*, *Angielski* albo *Oba*. Słowo jest poprawne, jeśli zna je którykolwiek z wybranych języków.
- **Słownik użytkownika** — lista dodanych słów: tu je dopiszesz albo usuniesz.

Słownik użytkownika należy do notatnika: jest zapisany w notatniku (plik `.brulion/dictionary.txt`, jedno słowo w wierszu) i przechodzi z nim przez synchronizację — przez Nextcloud i przez Syncthing —, więc słowo dodane w telefonie zna też komputer. Słowa dodane na dwóch urządzeniach w tym samym czasie zostają oba, a słowo usunięte na jednym urządzeniu znika także na drugim. Notatnik otwarty pierwszy raz w tej wersji zaczyna od słów, które to urządzenie miało wcześniej (były ustawieniem urządzenia, wspólnym dla wszystkich notatników).

Słowo dodane jako `Kraków` jest poprawne także jako `KRAKÓW`, ale nie jako `kraków`. Słowo dodane małymi literami (`brulion`) jest poprawne także na początku zdania (`Brulion`).

## Na różnych urządzeniach
- **Komputer (Linux, Windows)** — Brulion używa Hunspella i słowników polskiego i angielskiego dołączonych do aplikacji (AppImage, Windows, pakiet Nix). Inna wersja Brulionu sięga po słowniki systemu (np. pakiety `hunspell-pl` i `hunspell-en-us`). Bez żadnego słownika ustawienia sprawdzania pisowni są ukryte.
- **Android** — Brulion pyta systemowe sprawdzanie pisowni (to samo, którego używa klawiatura). Jeśli jest wyłączone w ustawieniach Androida (Ustawienia → System → Język → Sprawdzanie pisowni), Brulion niczego nie podkreśla, a jego ustawienia pisowni są ukryte. Przy włączonym **Podkreślaj na całej stronie** trafiają do niego także słowa z całego ekranu.
- **Przeglądarka** — wersja webowa nie sprawdza pisowni.
