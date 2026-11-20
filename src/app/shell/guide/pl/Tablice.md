---
tags: [documentation]
aliases: [Tablica, Kanwa, Canvas, Obsidian Canvas, JSON Canvas, Mapa myśli, Tablica korkowa]
brulion: document
---
# Tablice

**Tablica** (canvas) to biała tablica: karty rozłożone swobodnie na nieskończonej powierzchni i połączone liniami. Kartą może być krótki tekst (Markdown), jedna z twoich notatek (cała strona albo tylko jedna sekcja), obraz, rysunek, PDF, inna tablica, dowolny inny plik albo adres strony internetowej. Grupy obejmują kilka kart ramką z etykietą. Przydaje się do planowania projektu, map myśli, tablic inspiracji, mapy powiązań między notatkami albo ściany karteczek przed egzaminem.

Tablica to plik `.canvas` w otwartym formacie **JSON Canvas** — tym samym, którego używa Obsidian Canvas. Tablica zrobiona w Obsidianie otwiera się i daje edytować w Brulionie, i odwrotnie; Brulion zmienia tylko to, co zmienisz, a resztę pliku zostawia bajt w bajt (także ustawienia wtyczek Obsidiana, których nie zna).

## Nowa tablica
- Paleta poleceń (`Ctrl+K`) → **Nowa tablica**, albo menu stron na pasku bocznym (**…** obok stron) → **Nowa tablica**, albo menu folderu → **Nowa tablica tutaj**. Brulion tworzy `Tablica bez tytułu.canvas` i ją otwiera; nazwę zmienisz na pasku bocznym jak każdego pliku.
- Tablica otwiera się z paska bocznego, z linku `[[Plan.canvas]]`, z wyszukiwania i z szybkiego przełącznika (`Ctrl+O`).

## Dodawanie kart
- **Kliknij dwukrotnie** (stuknij dwa razy) puste miejsce — pojawi się tam karta tekstowa i od razu możesz pisać. Tekst to Markdown: nagłówki, listy, `**pogrubienie**`, `[[linki]]`, `#tagi`, zadania.
- Pasek narzędzi: **Dodaj kartę**, **Dodaj notatkę…** (wybierz stronę albo wpisz nową nazwę, żeby ją utworzyć), **Dodaj plik…** (obraz, PDF albo dowolny plik — trafia do załączników notatnika), **Dodaj adres strony…**, **Grupa** (wokół zaznaczonych kart). Na telefonie są pod **+**.
- **Przeciągnij stronę** z paska bocznego na tablicę — stanie się kartą notatki tam, gdzie ją upuścisz. Pliki przeciągnięte z innej aplikacji też się dodają.
- **Wklej** (`Ctrl+V`): karty skopiowane z tablicy (także z Obsidiana), obraz, skopiowane pliki, adres strony (karta linku) albo dowolny tekst (karta tekstowa).
- **Przeciągnij z boku karty**: gdy wskaźnik jest nad kartą, na jej bokach widać cztery kółka. Przeciągnij jedno na inną kartę, żeby je połączyć; upuść na pustym miejscu, a pojawi się tam nowa, połączona karta.

## Co pokazują karty
- **Karta tekstowa** pokazuje sformatowany Markdown; kliknij ją dwukrotnie, żeby edytować (`Esc` albo kliknięcie obok kończy edycję, karta rośnie, żeby tekst się zmieścił).
- **Karta notatki** pokazuje stronę tak jak w edytorze albo tylko sekcję lub blok, na który wskazuje (`#Nagłówek`, `#^blok`). Długą notatkę przewiniesz kółkiem myszy, gdy karta jest zaznaczona. Kliknij dwukrotnie, żeby ją otworzyć.
- **Obrazy**, **rysunki** (w motywie aplikacji), **PDF-y** (pierwsza strona) i **tablice** (ich obraz) pokazują obraz; inne pliki swoją nazwę. Linki do stron internetowych pokazują adres — Brulion nigdy sam nie wczytuje strony.
- Gdy mocno oddalisz widok, karty pokazują tylko tytuł dużymi literami, więc i na dużej tablicy łatwo się odnaleźć.

## Poruszanie się
- **Przesuwanie tablicy**: kółko myszy albo dwa palce na touchpadzie, środkowy przycisk myszy, albo przytrzymaj `Spację` i przeciągnij. Na ekranie dotykowym przeciągnij jednym palcem.
- **Powiększanie**: `Ctrl` + kółko, rozsunięcie palców na touchpadzie lub ekranie, `Ctrl+=` / `Ctrl+-`, `Ctrl+0` — 100%. **Pokaż całą tablicę**: `Shift+1`; zaznaczenie: `Shift+2`.

## Zaznaczanie i edycja
- Kliknij kartę albo linię, żeby ją zaznaczyć; `Shift`+kliknięcie dodaje do zaznaczenia. Przeciągnij po pustym miejscu, żeby zaznaczyć kilka kart prostokątem. Na telefonie włącz **Zaznacz kilka** na pasku narzędzi.
- Przeciągnij zaznaczone karty, żeby je przesunąć — grupa przesuwa się razem z kartami w środku. Przeciągnij róg albo bok zaznaczonej karty, żeby zmienić jej rozmiar.
- Pasek nad zaznaczeniem: **kolor** (sześć kolorów jak w Obsidianie), **edytuj**, **otwórz**, **groty** linii (bez, w jedną stronę, w obie, odwróć), **powiel** (`Ctrl+D`), **grupuj** / **rozgrupuj** (`Ctrl+G` / `Ctrl+Shift+G`), **wyrównaj** i **rozłóż równo**, **kolejność** (na wierzch, pod spód), **usuń** (`Del`).
- Kliknij dwukrotnie **linię**, żeby dodać jej etykietę, a **grupę**, żeby zmienić jej nazwę.
- Prawy przycisk (przytrzymanie) otwiera menu z tym samym i jeszcze: kopiuj, wytnij, wklej, otwórz w nowej karcie.
- **Cofnij** i **Ponów** (`Ctrl+Z`, `Ctrl+Shift+Z`) cofają każdą zmianę.
- Strzałki przesuwają zaznaczone karty (z `Shift` o jeden punkt), `Enter` edytuje zaznaczoną kartę, `Esc` czyści zaznaczenie.

## Linki, wyszukiwanie i zmiana nazw
- Notatki na tablicy i `[[linki]]` w jej kartach tekstowych liczą się jako linki: **odnośniki** notatki pokazują tablicę (kliknięcie otwiera ją na tej karcie), graf też.
- **Wyszukiwanie** znajduje tablice po tekście ich kart; wynik otwiera tablicę powiększoną na karcie.
- Gdy **zmienisz nazwę albo przeniesiesz** stronę lub plik, każda tablica, która go pokazuje albo do niego linkuje, nadąża: Brulion zmienia ścieżkę w pliku tablicy.
- Zadania i przypomnienia napisane na karcie tablicy są tam tylko tekstem: nie pojawiają się w **Zadaniach** i nigdy nie dzwonią.

## Zapis, synchronizacja i eksport
- Tablica zapisuje się sama chwilę po każdej zmianie. Gdy inne urządzenie albo Obsidian zmieni ją, kiedy jest otwarta, obie wersje łączą się karta po karcie.
- Gdy ta sama tablica zmieniła się na dwóch urządzeniach, synchronizacja scala ją karta po karcie (karta przesunięta na jednym i poprawiona na drugim zachowuje obie zmiany). Gdy to niemożliwe, **centrum synchronizacji** pokazuje obie wersje jako obrazy i przycisk **Scal tablice**.
- **Więcej** → **Eksportuj jako obraz PNG** albo **Eksportuj jako obraz SVG** zapisuje obraz całej tablicy obok niej. Eksport notatnika do HTML pokazuje tablice jako obrazy.

> [!example] Spróbuj
> Przykładowy brulion (*Ustawienia → O aplikacji → Przykładowy brulion*) ma tablicę *Plan remontu.canvas* — otwórz ją z paska bocznego i poprzesuwaj karty.
