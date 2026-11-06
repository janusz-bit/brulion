---
tags: [dokumentacja]
brulion: document
---
# Sprawdzanie pisowni

Brulion podkreśla na czerwono słowa, których nie zna słownik — po polsku i po angielsku, także oba języki naraz. Sprawdza tekst bloku, który właśnie edytujesz; reszta strony zostaje czysta, więc czytanie notatek nic nie rozprasza.

## Poprawianie
- **Na komputerze** kliknij podkreślone słowo prawym przyciskiem myszy. Na górze menu są podpowiedzi — kliknięcie wstawia poprawione słowo (Ctrl+Z je cofa). Pod nimi:
  - **Dodaj „…” do słownika** — słowo trafia do słownika użytkownika i nie będzie już podkreślane,
  - **Ignoruj** — słowo przestaje być podkreślane do zamknięcia Brulionu.
- **Na telefonie i tablecie** dotknij podkreślonego słowa — pojawią się te same podpowiedzi.

Słowo, które właśnie piszesz, nie jest podkreślane, dopóki go nie skończysz.

## Co jest sprawdzane
Tylko zwykły tekst. Brulion pomija:
- `kod w tekście`, bloki kodu, obliczenia (`calc`) i zapytania,
- linki `[[Strona]]`, odwołania do bloków, adresy stron i e-maili (tekst linku `[opis](adres)` jest sprawdzany),
- `#tagi`, daty, przypomnienia, powtarzanie i priorytety zadań (📅 ⏰ 🔁 ⏫…),
- wzory `$…$`, nazwy właściwości `klucz::`, słowa `TODO`/`DONE`, komentarze `%%…%%`,
- liczby i słowa z cyframi (`mp3`, `COVID-19`), skróty wielkimi literami (`PDF`, `NASA`), nazwy w rodzaju `iPhone`, nazwy plików i ścieżki.

Słowa z łącznikiem (`biało-czerwony`) i z apostrofem (`don't`) są sprawdzane w całości albo po częściach. Skrót z kropką (`np.`, `e.g.`) jest poprawny, jeśli słownik zna go z kropką.

## Ustawienia
**Ustawienia → Edytor**:
- **Sprawdzaj pisownię** — włącza i wyłącza podkreślanie.
- **Języki pisowni** — *Automatycznie* (język aplikacji, język systemu i angielski: przy polskim interfejsie polski i angielski), *Polski*, *Angielski* albo *Oba*. Słowo jest poprawne, jeśli zna je którykolwiek z wybranych języków.
- **Słownik użytkownika** — lista dodanych słów: tu je dopiszesz albo usuniesz.

Słownik użytkownika jest ustawieniem tego urządzenia i obejmuje wszystkie notatniki. Nie jest zapisywany w notatniku, więc nie przechodzi przez synchronizację — na drugim urządzeniu dodajesz słowa osobno.

Słowo dodane jako `Kraków` jest poprawne także jako `KRAKÓW`, ale nie jako `kraków`. Słowo dodane małymi literami (`brulion`) jest poprawne także na początku zdania (`Brulion`).

## Na różnych urządzeniach
- **Komputer (Linux, Windows)** — Brulion używa Hunspella i słowników polskiego i angielskiego dołączonych do aplikacji (AppImage, Windows, pakiet Nix). Inna wersja Brulionu sięga po słowniki systemu (np. pakiety `hunspell-pl` i `hunspell-en-us`). Bez żadnego słownika ustawienia sprawdzania pisowni są ukryte.
- **Android** — Brulion pyta systemowe sprawdzanie pisowni (to samo, którego używa klawiatura). Jeśli jest wyłączone w ustawieniach Androida (Ustawienia → System → Język → Sprawdzanie pisowni), Brulion niczego nie podkreśla, a jego ustawienia pisowni są ukryte.
- **Przeglądarka** — wersja webowa nie sprawdza pisowni.
