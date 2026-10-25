---
tags: [dokumentacja]
brulion: document
---
# Linki i odnośniki zwrotne

Linki łączą notatki w sieć. Każda strona pokazuje w panelu bocznym (zakładka **Linki**) i pod treścią **odnośniki zwrotne**: strony i bloki, które do niej prowadzą — linkiem albo [[Tagi i właściwości#Tag to link do strony|tagiem]] — także te, które tylko wspominają jej nazwę bez linku.

## Rodzaje linków
| Zapis | Znaczenie |
|---|---|
| `[[Strona]]` | link do strony |
| `[[Strona\|podpis]]` | link z własnym tekstem |
| `[[Strona#Nagłówek]]` | link do nagłówka |
| `[[Strona#Rozdział#Nagłówek]]` | link do nagłówka „Nagłówek” w rozdziale „Rozdział” (gdy nazwa się powtarza) |
| `[[#Nagłówek]]` | link do nagłówka na tej samej stronie |
| `[[Strona#^id]]` | link do bloku o identyfikatorze `id` |
| `((id))` | odwołanie do bloku z dowolnej strony — pokazuje jego tekst |
| `[tekst](https://adres)` | zwykły link w Markdownie |
| `#strona`, `#[[Dwa słowa]]` | tag — prowadzi do strony o tej nazwie |

Przykłady: [[Spis treści|Spis treści]], [[Zadania#Powtarzanie]], oraz strona, która jeszcze nie istnieje: [[Notatka do utworzenia]]. Kliknięcie takiego linku tworzy stronę.

## Jak Brulion znajduje stronę
Nazwa z linku jest dopasowywana do plików po kolei: pełna ścieżka w notatniku, ścieżka względem bieżącego folderu, sama nazwa pliku (bez względu na wielkość liter), a na końcu aliasy z nagłówka strony (`aliases: [Skrót]`). Przy kilku pasujących wygrywa strona o najkrótszej ścieżce. Gdy dwie strony mają tę samą nazwę w różnych folderach, użyj ścieżki: `[[Projekty/Dom]]`.

## Osadzanie
Wykrzyknik przed linkiem wstawia treść zamiast odnośnika:
- `![[Strona]]` — cała strona, `![[Strona#Nagłówek]]` — sekcja,
- `!((id))` — pojedynczy blok,
- `![[zdjęcie.png]]`, `![[dokument.pdf]]` — załączniki.

Osadzony fragment pokazuje bieżący tekst źródła; zmieniasz go u źródła.

## Zdjęcia z internetu
Zdjęcie spod adresu internetowego wstawisz jak w każdym Markdownie: `![opis](https://example.com/kot.jpg)` (działa też `![[https://example.com/kot.jpg]]`). Najprościej zrobić to poleceniem `/` → **Obraz z internetu…**: wklej adres, a opcja **Zapisz w notatniku** od razu pobierze kopię.

- **Najpierw pytanie.** Zwykle w miejscu zdjęcia widać nazwę strony, z której pochodzi, i przycisk **Pokaż**. Brulion niczego nie pobiera, dopóki go nie klikniesz, więc samo otwarcie notatki z importu albo z synchronizacji nie zdradza żadnej stronie, że ją czytasz. W **Ustawieniach → Edytor** włączysz **Pokazuj obrazy z internetu bez pytania**. Nawet wtedy zdjęcia z twojej sieci domowej (np. z serwera NAS pod `192.168.…` albo `nas.local`, z routera `fritz.box`, z adresu Tailscale `100.…` albo z `localhost`) czekają na **Pokaż**, a wyłączenie tej opcji przerywa pobieranie, które rozpoczęła.
- **Pamięć podręczna.** Raz pobrane zdjęcie zostaje na tym urządzeniu (do 200 MB, najdawniej oglądane znikają pierwsze) i pokazuje się także bez sieci. Do pliku notatki nic się nie dopisuje. W wersji w przeglądarce pamięć podręczna mieści 50 MB i znika po zamknięciu karty — zdjęcie, które ma zostać, zapisz w notatniku.
- **Zapisz w notatniku** — przycisk przy zdjęciu, albo pozycja w menu, które otworzysz kliknięciem pokazanego zdjęcia (na górze menu widać, z jakiej strony pochodzi). Kopia trafia do folderu załączników, a link zmienia się na nią: `![opis](assets/kot.jpg)`. Taki obraz działa offline, synchronizuje się z notatkami i nie zniknie, gdy strona go usunie. Zmianę cofniesz `Ctrl+Z`. W palecie poleceń (Ctrl+K) jest **Zapisz obrazy z internetu w notatniku**, czyli wszystkie zdjęcia z bieżącej strony naraz — w dzienniku z dnia, który właśnie edytujesz (zwykle dzisiejszego).
- Menu zdjęcia ma też **Otwórz w przeglądarce** i **Kopiuj adres obrazu**.
- Brulion pobiera tylko adresy `http://` i `https://`, najwyżej 20 MB, i sprawdza, czy to naprawdę obraz (strona logowania albo błąd się nie pokaże). Nie wysyła ciasteczek, nazwy aplikacji ani języka urządzenia, a przekierowanie z internetu do twojej sieci domowej odrzuca. Gdy się nie uda, w miejscu zdjęcia widać powód i przycisk **Spróbuj ponownie**.
- W wersji w przeglądarce wiele stron nie pozwala innym witrynom pobierać swoich obrazów (CORS) — wtedy pobierz zdjęcie na komputerze albo telefonie, a do notatnika trafi kopia.

## Odwołania do bloków
Żeby odwołać się do bloku, wpisz `((` i zacznij pisać jego treść — Brulion doda blokowi identyfikator, czyli końcówkę ` ^abc123`. Odwołanie `((abc123))` działa w całym notatniku, więc przeniesienie bloku na inną stronę niczego nie psuje. Link `[[Strona#^abc123]]` pokazuje nazwę strony i początek tekstu bloku.

## Kopiowanie odnośnika do bloku lub nagłówka
Menu bloku ma pozycję **Kopiuj odnośnik**, która kładzie odnośnik w schowku — wklejasz go potem w dowolnej notatce:
- **Odwołanie do bloku** — `((abc123))`: działa w całym notatniku i pokazuje tekst bloku,
- **Link do bloku** — `[[Strona#^abc123]]`: rozumie go też Obsidian,
- **Link do nagłówka** (tylko na nagłówkach) — `[[Strona#Nagłówek]]`: nie zmienia pliku,
- **Osadzenie** — `!((abc123))`, a dla nagłówka `![[Strona#Nagłówek]]`, czyli cała sekcja.

Menu bloku otwierasz prawym przyciskiem myszy na dowolnym bloku (tekst, nagłówek, kod, tabela, cytat), przytrzymaniem kropki albo numeru punktu, a na telefonie przyciskiem **Więcej** (⋯) na pasku nad klawiaturą, gdy edytujesz blok albo komórkę tabeli. Z klawiatury **Ctrl+Shift+C** kopiuje odwołanie do edytowanego albo zaznaczonego bloku. W palecie poleceń (Ctrl+K) są „Kopiuj link do strony”, „Kopiuj odwołanie do bloku”, „Kopiuj link do bloku” i „Kopiuj link do nagłówka” — tego, pod którym stoi kursor. W panelu bocznym, w zakładce **Konspekt**, prawy przycisk albo przytrzymanie nagłówka kopiuje link do niego lub jego osadzenie. Po każdym kopiowaniu Brulion pokazuje, co skopiował.

- Odwołanie, link do bloku i osadzenie bloku dopisują mu identyfikator (` ^abc123`), jeśli go nie miał — to jedna zmiana, którą cofa Ctrl+Z. Identyfikator Logseqa (`id::`) zostaje użyty bez zmian.
- Link do nagłówka niczego nie dopisuje, ale przestaje prowadzić do celu, gdy zmienisz treść nagłówka. Odwołanie do bloku przetrwa i zmianę nagłówka, i przeniesienie bloku.
- W linku do nagłówka znikają formatowanie, krzyżyki tagów i znaki, które psują linki (`#`, `|`, `^`, `:`, nawiasy kwadratowe): z „Plan: **wrzesień** #praca” powstaje `[[Strona#Plan wrzesień praca]]`. Wielkość liter i polskie znaki nie mają znaczenia przy wyszukiwaniu nagłówka.
- Gdy dwa nagłówki na stronie mają tę samą treść, Brulion kopiuje ścieżkę jak Obsidian: `[[Strona#Rozdział#Nagłówek]]`; jeśli i ona się powtarza — link do bloku.
- Nazwa strony w odnośniku to jej tytuł, a gdy kilka stron ma ten sam tytuł — najkrótsza ścieżka, która je rozróżnia (`[[Projekty/Dom#^abc123]]`). Tak samo nazywają stronę „Kopiuj link” w menu strony i w drzewie folderów oraz `brulion-cli block link`. Dzień dziennika ma nazwę pliku: `[[2026-10-22#^abc123]]`.
- W tej dokumentacji odnośniki do bloków nie są tworzone, bo zmieniłyby jej strony; linki do nagłówków i stron działają.

Kliknięcie odnośnika do bloku lub nagłówka przewija stronę do celu, zaznacza go i na chwilę podświetla — także gdy cel jest na tej samej stronie albo w tym samym dniu dziennika. Gdy celu nie ma, Brulion o tym mówi. Link do bloku, który przeniesiono na inną stronę, otwiera stronę, na której blok jest teraz.

## Zmiana nazwy i przenoszenie
Zmiana nazwy strony (F2) aktualizuje linki do niej i tagi, które ją nazywają, w pozostałych notatkach. Tak samo przeniesienie strony, załącznika albo całego folderu („Przenieś do…”, przeciągnięcie w drzewie, ukośnik w nowej nazwie w oknie zmiany nazwy): każdy link i tag dalej prowadzi do tej samej strony, a okno przenoszenia (i podpowiedź przy przeciąganiu) mówi wcześniej, ile ich się zmieni.
- Link po samej nazwie zostaje, dopóki wskazuje tę stronę: `[[Dom]]` po przeniesieniu jedynej strony „Dom” się nie zmienia.
- Gdy po przeniesieniu nazwa wskazywałaby inną stronę (np. w notatniku jest drugi „Dom” o krótszej ścieżce), link dostaje najkrótszą jednoznaczną ścieżkę: `[[Projekty/Dom]]`. Tak samo tagi: `#dom` → `#Projekty/Dom`.
- Linki ze ścieżką idą za stroną: `[[Projekty/Dom]]` → `[[Archiwum/Dom]]`; także obrazy i pliki z przenoszonego folderu: `![[Projekty/plan.png]]`.
- [[Zapytania|Zapytania]] też idą za stroną i folderem: `ref:Dom`, `[[Dom]]`, `page:Dom`, `tag:dom` i `path:Projekty/` dostają nową nazwę albo ścieżkę (`path:Archiwum/Projekty/`) i dalej znajdują to samo. Gdy nowa nazwa należy też do innej strony, `page:` dostaje zapis, który wskazuje tylko jedną z nich: po zmianie nazwy „Dom” na „Mieszkanie”, gdy jest już strona `Archiwum/Mieszkanie`, `page:Dom` zmienia się w `page:Mieszkanie.md`, a `page:Mieszkanie` w `page:Archiwum/Mieszkanie`. `page:` z gwiazdką, `page:ta` i nazwa kilku stron naraz (dwie strony „Dom”) zostają bez zmian. Okno zmiany nazwy liczy je razem z linkami i tagami.
- Nagłówek, blok i alias zostają: `[[Dom#Kuchnia|kuchnia]]` → `[[Projekty/Dom#Kuchnia|kuchnia]]`.

Podpowiedź `[[` też wstawia ścieżkę, gdy wybrana strona ma imienniczkę w innym folderze. Historia strony i kosz pozwalają cofnąć pomyłki, a komunikat po przeniesieniu ma „Cofnij”.

## Graf
Widok **Graf** rysuje strony i dni dziennika jako sieć połączeń: cały notatnik albo okolice jednej strony (głębokość 1–2). Tag, który nazywa stronę, łączy się z nią jak link. Przełączniki ukrywają tagi bez strony, brakujące strony, dzienniki i szablony.

Dalej: [[Tagi i właściwości|Tagi i właściwości]].
