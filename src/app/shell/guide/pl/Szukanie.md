---
tags: [documentation]
brulion: document
---
# Szukanie

**Ctrl+Shift+F** (albo **Szukaj** na pasku ikon, na telefonie w dolnej nawigacji) przeszukuje cały notatnik: tytuły i treść notatek, a także **tekst plików PDF i obrazów**, które w nim są. Polskie znaki można pomijać — „zolw” znajdzie „żółw” — a ostatnie słowo może być początkiem wyrazu: „wyzn” znajdzie „wyznacznik”. Drobna literówka też nie przeszkadza, ale to, co wpisano dokładnie, jest zawsze na górze.

## Wyniki
- Notatka otwiera się w wierszu ze słowami.
- **PDF** pokazuje swoją stronę — „Strona 3 z 12” — i ile jeszcze stron pasuje. Otwiera się w przeglądarce PDF Brulionu **na tej stronie, ze słowami podświetlonymi**; strzałki wyszukiwania przeglądarki prowadzą do kolejnych miejsc.
- **Obraz** (zdjęcie tablicy, zrzut ekranu) otwiera się z zaznaczonymi miejscami słów. **Rozpoznany tekst** na jego pasku pokazuje wszystko, co w nim odczytano — można to zaznaczyć i skopiować.
- „Rozpoznany tekst” pod wynikiem znaczy, że słowa odczytano z obrazu albo skanu (rozpoznawanie może się mylić, więc gdy jedno słowo nie wystarcza, szukaj kilku).
- PDF lub obraz znajdziesz też po nazwie pliku: „wyklad” znajdzie „Wykład 3.pdf”.

## Zawężanie
- **Wszystko / Strony / Dziennik / Załączniki** pod polem.
- Słowa w samym zapytaniu: `w:pdf`, `w:obrazy`, `w:załączniki`, `w:notatki`, `w:strony`, `w:dziennik` — np. `w:pdf całka`. Działają też angielskie: `in:pdf`, `in:pictures`, `in:attachments`, `in:notes` (i `typ:`, `type:`). Kilka się sumuje: `w:pdf w:obrazy`.
- Przycisk **Folder** (albo „Szukaj w folderze” w menu folderu) — tylko jeden folder i jego podfoldery.

## Jak czytane są PDF-y i obrazy
Brulion czyta załączniki **w tle, na tym urządzeniu, bez internetu** — nic nie jest nigdzie wysyłane. Tekst PDF-a jest gotowy chwilę po otwarciu notatnika; długi plik jest czytany strona po stronie, więc nawet 100-megabajtowy skan wykładu nie zapycha pamięci. W trakcie pod filtrami wyszukiwania widać linijkę „Czytanie załączników… 3 z 40” z przyciskiem **Wstrzymaj**. Trwająca synchronizacja i pisanie mają pierwszeństwo: czytanie czeka.

To, co przeczytano, zostaje na tym urządzeniu (nie w notatniku, więc nie idzie przez synchronizację): niezmieniony plik nie jest czytany drugi raz, a przemianowany lub przeniesiony zachowuje swój tekst. Zmieniony plik jest czytany od nowa.

## Rozpoznawanie tekstu (OCR)
Zeskanowana strona PDF-a albo obraz nie ma własnego tekstu — **rozpoznawanie tekstu** odczytuje go z obrazu. Jest **domyślnie wyłączone**, bo zajmuje około sekundy jednego rdzenia procesora na stronę (z najniższym priorytetem, więc komputer nie zwalnia). Włączysz je w **Ustawienia → Szukaj → Rozpoznawaj tekst na obrazach i skanach (OCR)**. Wtedy:
- zdjęcia notatek i tablic, zrzuty ekranu i skany dają się wyszukać,
- zeskanowaną stronę da się też przeszukać w przeglądarce PDF (Ctrl+F), z podświetleniem słów.

**Języki rozpoznawania** są automatyczne — język Brulionu i angielski — albo wybrane ręcznie. Zmiana języków odczytuje rozpoznane strony od nowa.

## Ustawienia → Szukaj
- **Szukaj w załącznikach** — włącza i wyłącza czytanie PDF-ów i obrazów.
- **Rozpoznawaj tekst na obrazach i skanach (OCR)** i jego **języki**.
- **W tym notatniku** — ile plików i stron przeczytano, co jest czytane teraz, **Wstrzymaj** / **Wznów**.
- **Tekst przechowywany na tym urządzeniu** — jego rozmiar; **Wyczyść** czyta wszystkie pliki od nowa.

## Na różnych urządzeniach
- **Komputer (Linux, Windows)** — PDF-y są czytane wszędzie. Rozpoznawanie tekstu korzysta z programu Tesseract: pakiet Nix ma go wbudowanego z polskim i angielskim; gdzie indziej trzeba go zainstalować — na Linuksie pakiety `tesseract-ocr`, `tesseract-ocr-pol` i `tesseract-ocr-eng`, na Windows instalator Tesseracta (UB Mannheim) z językiem polskim — i uruchomić Brulion ponownie. Ustawienia → Szukaj mówią, czego brakuje. Kolejne języki: wrzuć ich pliki `*.traineddata` do folderu, który podają Ustawienia.
- **Android** — tekst PDF-ów od Androida 15; bez rozpoznawania tekstu w telefonie (aplikacja byłaby dużo większa).
- **Przeglądarka** — wersja webowa nie czyta PDF-ów ani obrazów; otwiera je w nowej karcie, gdzie przeszukuje je przeglądarka.
