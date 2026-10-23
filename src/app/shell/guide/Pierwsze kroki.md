---
tags: [dokumentacja]
brulion: document
---
# Pierwsze kroki

## Notatniki i foldery
Brulion przechowuje notatki jako zwykłe pliki `.md` w wybranym folderze — ten folder to Twój **notatnik**. Nie ma konta ani bazy danych; kopię zapasową zrobisz, kopiując folder, a ten sam folder otworzysz w Obsidianie czy Logsequ.

Przy pierwszym uruchomieniu wybierasz folder, tworzysz nowy notatnik albo otwierasz „Przykładowy brulion” z notatkami do wypróbowania. Między notatnikami przełączasz się przyciskiem „Przełącz notatnik” (dwie strzałki) obok nazwy notatnika na górze paska bocznego. Jego menu ma listę ostatnich notatników oraz pozycje „Dokumentacja Brulionu” i „Otwórz inny notatnik…”. Ctrl+Shift+B przenosi do dokumentacji i z powrotem.

## Pierwszy wpis
1. Otwórz **Dziennik** (Ctrl+J) — to strona na dziś.
2. Zacznij pisać. Każda linia, która zaczyna się od myślnika, to osobny blok.
3. Zmiany zapisują się same; nie ma przycisku „Zapisz”.

Strona dnia powstaje dopiero przy pierwszej zmianie — samo zaglądanie niczego nie tworzy.

## Pisanie na telefonie
Gdy piszesz, pasek narzędzi leży tuż nad klawiaturą — w dzienniku i na każdej stronie, także gdy przewijasz. Ma wcięcia, przenoszenie bloków, zadanie, link `[[`, tag `#`, polecenia `/`, termin, przypomnienie, obraz, cofanie i ponawianie oraz **Więcej** (⋯) — menu bloku, np. z kopiowaniem odnośnika do niego; gdy przycisków jest więcej, niż mieści się na ekranie, przesuń pasek palcem w bok. Dolna nawigacja i przycisk szybkiej notatki chowają się na czas pisania.

- **Ukryj klawiaturę** (ostatni przycisk paska) kończy pisanie: chowa klawiaturę i pasek, a dolna nawigacja wraca.
- Gdy klawiaturę schowasz przyciskiem Wstecz albo klawiszem na samej klawiaturze, pasek zostaje na dole ekranu z wszystkimi narzędziami — dotknij tekstu, a klawiatura wróci.
- W komórce tabeli pasek ma Cofnij, Ponów, **Następna komórka** i Ukryj klawiaturę; w źródle Markdown (Więcej → Źródło Markdown) — `[[`, `#`, `/`, Cofnij, Ponów i Ukryj klawiaturę.
- Okna wyświetlane od dołu (szybka notatka, przypomnienie, zmiana nazwy), podpowiedzi przy pisaniu i pola formularzy (np. w ustawieniach) też zawsze leżą nad klawiaturą.

## Nowa strona
- **Ctrl+N**, przycisk z ołówkiem obok nazwy notatnika albo „+” przy sekcji **Strony** w pasku bocznym (→ „Nowa strona”). Gdy jesteś na stronie w folderze, nowa strona trafi do tego samego folderu — widać to w oknie jako „W folderze: Projekty”; krzyżyk przy nim albo „Zmień…” wybiera inne miejsce. Nazwa może też zawierać `/`, wtedy strona trafi do folderu: `Projekty/Dom`.
- W oknie wybierasz tryb: dokument albo konspekt (zobacz [[Dokument i konspekt|Dokument i konspekt]]).
- Skrót: wpisz w dowolnej notatce `[[Nazwa nowej strony]]` i kliknij link — strona powstanie.

## Szybka notatka
**Ctrl+Shift+N** (na telefonie przycisk „+” nad dolnym paskiem) otwiera jedno pole tekstowe. Wpisany tekst trafia na koniec dzisiejszego dziennika (z dokumentacji Brulion najpierw wraca do Twojego notatnika). Daty w nim stają się terminami i przypomnieniami: „zadzwonić do Ani jutro o 9”.

## Szukanie i przechodzenie
- **Ctrl+O** — szybkie przejście do strony po kilku literach nazwy.
- **Ctrl+Shift+F** — wyszukiwanie w treści wszystkich notatek; polskie znaki można pomijać („zolw” znajdzie „żółw”).
- **Ctrl+K** — paleta poleceń.
- **Alt+←** / **Alt+→** — wstecz i dalej, jak w przeglądarce.

## Pasek boczny
Dziennik, Kalendarz, Zadania, Wszystkie strony, Tagi, Graf i Szukaj. Niżej ulubione strony, ostatnio otwarte oraz drzewo folderów. Na telefonie pasek boczny otwiera przycisk ☰ lub „Menu” w dolnej nawigacji.

## Foldery
Foldery porządkują strony jak katalogi na dysku: strona `Projekty/Dom` to plik `Dom.md` w folderze `Projekty`.
- **Nowy folder**: „+” przy sekcji **Strony** → „Nowy folder” (w głównym folderze notatnika) albo menu folderu → „Nowy folder…” (w nim). Nazwy z niedozwolonymi znakami są poprawiane, a ukryte (z kropką na początku, np. `.prywatne`) odrzucane.
- **Menu** strony lub folderu w drzewie otwiera prawy przycisk myszy albo przytrzymanie palcem; na telefonie także przycisk „⋯” przy każdym wierszu. Folder ma tam m.in. „Pokaż strony folderu”, „Szukaj w folderze” i „Eksportuj folder…”, strona — „Kopiuj link”.
- **Przenoszenie**: „Przenieś do…” (w menu drzewa, w menu **Więcej** strony, na liście **Wszystkie strony** i w palecie poleceń) pokazuje foldery z wyszukiwaniem, a ostatni wiersz „Nowy folder „…”” od razu zakłada folder o wpisanej nazwie. Przed przeniesieniem widać, ile linków się zmieni. Na komputerze stronę lub folder możesz też przeciągnąć myszą na inny folder (zamknięty otworzy się po chwili) albo na nagłówek **Strony** — do głównego folderu; Esc przerywa przeciąganie. Ukośnik w oknie zmiany nazwy (F2) też przenosi: „Archiwum/Dom” (w tytule edytowanym w nagłówku strony ukośnik jest zwykłym znakiem tytułu).
- Linki i tagi zawsze prowadzą dalej do tej samej strony (zobacz [[Linki i odnośniki zwrotne#Zmiana nazwy i przenoszenie|Zmiana nazwy i przenoszenie]]). Przeniesienie folderu dziennika, szablonów albo załączników poprawia też ustawienia notatnika.
- Drzewo samo pokazuje i podświetla otwartą stronę (wyłączysz to w menu „+” → „Pokazuj otwartą stronę”), a „Zwiń wszystkie” zamyka wszystkie foldery. Folder dziennika jest na górze, dni od najnowszego.
- **Wszystkie strony** w folderze („Pokaż strony folderu” albo okruszki nad stroną) pokazują jego podfoldery nad stronami — tak przeglądasz notatnik folder po folderze także na telefonie. „Z podfolderami” decyduje, czy lista obejmuje też strony podfolderów.

Widoki bez własnego przycisku — centrum synchronizacji, import i eksport, kosz, ustawienia — znajdziesz w menu **Więcej** (⋮) oraz w palecie poleceń. Rejestr zdarzeń jest w *Ustawienia → Aplikacja* i w palecie.

## Co dalej
- [[Dokument i konspekt|Dokument i konspekt]]
- [[Linki i odnośniki zwrotne|Linki i odnośniki zwrotne]]
- [[Spis treści|Spis treści]]
