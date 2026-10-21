---
tags: [dokumentacja]
brulion: document
---
# Dokument i konspekt

Każda notatka ma jeden z dwóch trybów. **Zawartość pliku jest taka sama** — zmienia się tylko sposób pokazywania i edycji.

| Tryb | Wygląd | Dobry do |
|---|---|---|
| **Dokument** (jak Obsidian) | ciągły tekst: nagłówki, akapity, zwykłe listy | opisów, artykułów, notatek ze spotkań |
| **Konspekt** (jak Logseq) | punkt z kropką dla każdego bloku, wcięcia, zwijanie, przybliżanie | dziennik, listy myśli, planowanie |

Ta strona jest dokumentem; [[Zadania|Zadania]] też. Dziennik domyślnie jest konspektem.

## Wybór i przełączanie trybu
- Przy tworzeniu strony (Ctrl+N) wybierasz tryb w oknie.
- Później przełączysz go ikoną obok tytułu, w menu strony („Przełącz na dokument” / „Przełącz na konspekt”) albo poleceniem „Przełącz tryb notatki” w palecie.
- Domyślne tryby nowych stron i dziennika ustawisz w *Ustawienia → Notatnik*.
- Wybór zapisuje się w samej notatce jako właściwość `brulion: document` albo `brulion: outline`, więc na każdym urządzeniu wygląda tak samo.
- Notatki bez tego klucza zgadują tryb po zawartości: strona z samych punktów otwiera się jako konspekt, strona z nagłówków i akapitów jako dokument.

## Bloki
Blokiem jest każdy punkt listy, a poza listami — każdy akapit, nagłówek, tabela, cytat, blok kodu. Kliknij blok, żeby go edytować; poza edycją widzisz go sformatowanego.

| Klawisze | Działanie |
|---|---|
| Enter | podziel blok w miejscu kursora (nowy blok) |
| Shift+Enter | nowa linia wewnątrz bloku |
| Tab / Shift+Tab | wcięcie / wysunięcie bloku razem z dziećmi |
| Alt+↑ / Alt+↓ | przeniesienie bloku z dziećmi |
| Ctrl+. | zwinięcie / rozwinięcie dzieci (konspekt) |
| Esc | koniec edycji i zaznaczenie bloku |
| Ctrl+Enter | zmiana stanu zadania |

W konspekcie kliknij kropkę bloku, żeby się do niego **przybliżyć** — widzisz tylko ten blok i jego dzieci. W dokumencie Enter na pustym punkcie listy kończy listę, a Tab działa tylko na punktach listy.

## Menu `/`
Wpisz `/` na początku bloku, a pojawi się lista poleceń: zadanie, przypomnienie, alarm, termin, powtarzanie, tabela, obliczenia, zapytanie, nagłówek, cytat, ramka, kod, szablon, obraz, osadzenie i odwołanie do bloku. Polskie znaki w filtrze są opcjonalne.

## Podpowiedzi podczas pisania
- `[[` — strony (z możliwością utworzenia nowej),
- `((` — bloki z całego notatnika,
- `#` — tagi,
- po `📅`, `⏳`, `⏰` albo `@` — daty słowami: „jutro o 9”, „w piątek 18:00”.

## Ramki, wyróżnienia, komentarze
> [!note] Ramka
> Rodzaje: `note`, `tip`, `warning`, `danger`, `info`, `todo`, `quote`. Wpisujesz `> [!tip] Tytuł`, a w kolejnych liniach `> treść`.

==Zakreślenie== piszesz między podwójnymi znakami równości, ~~przekreślenie~~ w `~~tyldach~~`, a komentarz widoczny tylko w edycji w `%%procentach%%`.

## Tryb źródła
**Ctrl+E** pokazuje cały plik jako surowy Markdown z podświetleniem składni. Przydaje się do hurtowych zmian; kolejne naciśnięcie wraca do bloków.

## Obrazy i pliki
Wklej obraz (Ctrl+V), upuść pliki na stronę albo użyj `/Obraz…`. Pliki trafiają do folderu załączników (`assets/`) i są osadzane jako `![[plik.png]]`.

Kliknięcie osadzonego obrazu pokazuje samo zdjęcie: dopasowane do widoku, a kliknięcie na nim (albo przycisk „Pokaż naturalny rozmiar”) — naturalną wielkość z możliwością przeciągania. „Pokaż jako tekst” w pasku, „Pokaż obraz” w menu strony i Ctrl+E pokazują plik jako tekst w edytorze — to starszy widok, przydatny dla plików tekstowych (treść binarnego obrazu to tylko nieczytelne znaki).

Zobacz też: [[Linki i odnośniki zwrotne|Linki i odnośniki zwrotne]], [[Skróty klawiszowe|Skróty klawiszowe]].
