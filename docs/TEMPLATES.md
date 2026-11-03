# Szablony

Szablon to zwykła strona Markdown w folderze szablonów notatnika (domyślnie `templates/`). Nie ma
osobnego edytora ani formatu: piszesz go jak każdą inną notatkę, a synchronizuje się razem z
resztą plików. Składnia w skrócie: [SYNTAX.md](SYNTAX.md#12-templates).

## Nowy szablon

1. Na pasku bocznym otwórz menu folderu `templates` i wybierz **Nowa strona tutaj**. Jeśli folderu
   jeszcze nie ma, utwórz go.
2. Nazwij stronę tak, jak ma się nazywać szablon, np. `Spotkanie`.
3. Wpisz treść, opcjonalnie ze [zmiennymi](#zmienne):

   ```markdown
   # {{title}}

   Data: {{date}} ({{weekday}}), {{time}}

   ## Uczestnicy
   -

   ## Ustalenia
   -
   ```

Szablony można układać w podfolderach (`templates/praca/Klient.md`) — też będą na liście.

## Wstawianie szablonu

W edytorze wpisz `/szablon` (po angielsku `/template`), wybierz **Szablon…**, a potem szablon z
listy. Lista ma wyszukiwarkę i pokazuje wszystkie strony z folderu szablonów, także szablon
dziennika.

Gdzie trafia tekst:

* z bloku pustego — zastępuje ten blok;
* z bloku z treścią — pod nim, za całym blokiem najwyższego poziomu z jego podpunktami.

Kursor staje na pierwszym wstawionym bloku. Wstawienie cofniesz jak każdą inną zmianę.

Na razie nie ma tworzenia nowej strony z wybranego szablonu. Utwórz stronę i wstaw szablon do
pustego bloku.

## Zmienne

| Zmienna | Co wstawia | Przykład |
|---|---|---|
| `{{date}}` | data | `2026-10-14` |
| `{{date:FORMAT}}` | data i czas we własnym [formacie](#format-daty-i-godziny) | `{{date:dd.MM.yyyy}}` → `14.10.2026` |
| `{{time}}` | godzina | `09:05` |
| `{{time:FORMAT}}` | to samo co `{{date:FORMAT}}` | `{{time:HH:mm:ss}}` → `09:05:00` |
| `{{title}}` | tytuł strony, do której wstawiasz | `Remont łazienki` |
| `{{weekday}}` | dzień tygodnia | `środa` |
| `{{month}}` | nazwa miesiąca w mianowniku | `październik` |
| `{{week}}` | numer tygodnia ISO | `42` |
| `{{end}}` | ostatni dzień okresu w [notatce tygodniowej lub miesięcznej](#szablony-notatek-tygodniowych-i-miesięcznych), gdzie indziej to samo co `{{date}}` | `2026-10-18` |
| `{{end:FORMAT}}` | to samo we własnym formacie | `{{end:d MMMM yyyy}}` → `18 października 2026` |
| `{{cursor}}` | nic — zobacz niżej | |

* Wielkość liter nie ma znaczenia, a w klamrach mogą być spacje: `{{ Title }}` działa jak
  `{{title}}`.
* Nieznana zmienna zostaje w tekście tak, jak ją napisano (`{{projekt}}`).
* Nazwy dni i miesięcy są w języku aplikacji. W `brulion-cli` wybiera go opcja `--lang`.
* `{{date}}` i `{{time}}` to chwila wstawienia, także gdy wstawiasz szablon do wczorajszej notatki
  dziennej. Tylko [szablon dziennika](#szablon-dziennika) wypełnia się datą swojego dnia, a
  [szablony tygodnia i miesiąca](#szablony-notatek-tygodniowych-i-miesięcznych) — swojego okresu.
* `{{month}}` to nazwa do nagłówka („październik 2026”). `{{date:MMMM}}` daje dopełniacz
  („14 października”), dobry w zdaniu z dniem.

**`{{cursor}}` na razie nie ustawia kursora.** Zmienna jest usuwana z tekstu, a kursor staje tam,
gdzie opisano przy [wstawianiu](#wstawianie-szablonu) i przy
[szablonie dziennika](#szablon-dziennika).

### Format daty i godziny

Format to [wzorzec daty Qt](https://doc.qt.io/qt-6/qdatetime.html#toString):

| Wzorzec | Znaczenie | Przykład |
|---|---|---|
| `yyyy`, `yy` | rok | `2026`, `26` |
| `MM`, `M` | numer miesiąca | `03`, `3` |
| `MMMM`, `MMM` | nazwa miesiąca | `października`, `paź` |
| `dd`, `d` | dzień miesiąca | `04`, `4` |
| `dddd`, `ddd` | dzień tygodnia | `środa`, `śr.` |
| `HH`, `mm`, `ss` | godzina, minuta, sekunda | `09`, `05`, `00` |
| `'tekst'` | tekst dosłownie | `{{date:yyyy-MM-dd'T'HH:mm}}` → `2026-10-14T09:05` |

`{{date:dddd, d MMMM yyyy}}` daje `środa, 14 października 2026`. Format nie może zawierać znaku `}`.

## Szablon dziennika

Nowe notatki dzienne zaczynają się od pliku `templates/journal.md`. Wystarczy go utworzyć:

```markdown
## {{weekday}}, {{date:dd.MM.yyyy}}

### Na dziś
- [ ] Przejrzeć zadania

### Notatki
- 
```

* **Plik powstaje przy pierwszej zmianie.** Dzień bez notatki tylko pokazuje szablon jako tekst
  startowy, więc samo oglądanie dnia niczego nie tworzy.
* **Data to dzień notatki.** `{{date}}` we wczorajszej notatce to wczoraj, a `{{time}}` to godzina
  jej utworzenia. `{{title}}` to nazwa pliku, np. `2026-10-14`.
* **Kursor** staje na pierwszym pustym punkcie listy. Gdy takiego nie ma, na końcu powstaje nowy
  pusty blok.
* **Pusty punkt na końcu.** Zakończ szablon pustym `- `, jak wyżej. Szybkie dodawanie, udostępnianie,
  widżety Androida i `brulion-cli journal append` wypełnią ten punkt, zamiast dopisywać nowy pod nim.
* **Tryb notatki.** Szablon może go ustawić w nagłówku (`brulion: document` albo `outline`,
  [SYNTAX.md](SYNTAX.md)).
* **Brak pliku.** Gdy pliku szablonu nie ma albo pole w ustawieniach jest puste, nowe dni zaczynają
  się od pustej strony.

## Szablony notatek tygodniowych i miesięcznych

Notatki tygodnia (`journals/2026-W45.md`) i miesiąca (`journals/2026-11.md`) zaczynają się od
plików `templates/weekly.md` i `templates/monthly.md`. Działają jak szablon dziennika: notatka
powstaje przy pierwszej zmianie, a oglądanie tygodnia czy miesiąca niczego nie tworzy.

* **`{{date}}` to pierwszy dzień okresu** — poniedziałek tygodnia albo 1. dzień miesiąca, a
  **`{{end}}`** ostatni: niedziela albo ostatni dzień miesiąca. `{{title}}` to nazwa pliku
  (`2026-W45`), `{{week}}` numer tygodnia, `{{month}}` nazwa miesiąca.
* **Tydzień liczy się według ISO**, od poniedziałku, także gdy w ustawieniach tydzień zaczyna się
  w niedzielę. Na przełomie roku numer należy do roku tygodnia: 1 stycznia 2027 to tydzień 53 roku
  2026.

Przykład tygodnia:

```markdown
# Tydzień {{week}}: {{date:d MMMM}} – {{end:d MMMM yyyy}}

## Cele
- 

## Podsumowanie
- 
```

i miesiąca:

```markdown
# {{month}} {{date:yyyy}}

## Najważniejsze
- 
```

Otworzysz je skrótami **Ctrl+Alt+W** i **Ctrl+Alt+M**, z palety poleceń albo z kalendarza (numer
tygodnia, nazwa miesiąca w nagłówku). Folder, nazwy plików i szablony zmienisz w
**Ustawienia → Notatnik** (`weeklyFolder`, `weeklyFileFormat`, `weeklyTemplate`, `monthlyFolder`,
`monthlyFileFormat`, `monthlyTemplate` w `.brulion/config.json`), szczegóły formatów:
[SYNTAX.md](SYNTAX.md#weekly-and-monthly-notes--notatki-tygodniowe-i-miesięczne).

## Na co uważać

* **Zadania i przypomnienia z szablonu są widoczne jak każde inne.** Treść szablonu jest indeksowana
  jak zwykła notatka: `- [ ] Przejrzeć zadania` pojawi się na liście zadań, a
  `⏰ 2026-11-05 09:00` w przypomnieniach. Zapis `⏰ {{date}} 09:00` nie jest w szablonie
  przypomnieniem, staje się nim dopiero po wstawieniu. Natomiast `- [ ] Coś 📅 {{date}}` w szablonie
  jest już zadaniem, tyle że bez terminu.
* **Wyszukiwanie obejmuje szablony.** Ich tekst, ze zmiennymi, pojawia się w wynikach.
* **Nagłówek YAML działa tylko na górze strony.** Szablon z `---` na początku wstawiony w środek
  notatki nie ustawi jej właściwości. Działa na pustej stronie i w szablonie dziennika.
* **Graf i Sprzątanie.** Graf domyślnie ukrywa szablony (przełącznik **Szablony**), a Sprzątanie
  nigdy ich nie proponuje do usunięcia — ani notatek tygodniowych i miesięcznych.

## Inny folder lub plik

W **Ustawienia → Notatnik** są pola **Folder szablonów**, **Szablon dziennika**, **Szablon
notatki tygodniowej** i **Szablon notatki miesięcznej**. Zapisują się w `.brulion/config.json`
(`templatesFolder`, `journalTemplate`, `weeklyTemplate`, `monthlyTemplate`), więc działają na
wszystkich urządzeniach. Gdy przeniesiesz albo przemianujesz folder szablonów lub plik szablonu w
Brulionie, ustawienia pójdą za nim.

Skarbiec Obsidiana bez `.brulion/config.json` korzysta z ustawień Obsidiana:
[IMPORT.md](IMPORT.md#folder-obsidiana-bez-importu).
