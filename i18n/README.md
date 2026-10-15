# Tłumaczenia Brulionu

Teksty interfejsu są po angielsku w kodzie (`qsTr()` w QML, `tr()` / `QCoreApplication::translate()`
w C++). `brulion_pl.ts` to tłumaczenie polskie (domyślny język aplikacji), `brulion_en.ts` zawiera
tylko angielskie formy liczby mnogiej tekstów z `%n`.

## Odświeżanie

```sh
scripts/remote-build.sh -n i18n update_translations   # lupdate: nowe teksty trafiają do obu plików .ts
rsync -a laptop:work/brulion-i18n/src/i18n/ i18n/      # pliki .ts wracają z laptopa
```

Nowe wpisy mają `type="unfinished"` — przetłumacz je (Qt Linguist albo edytor tekstu). Kompilację
`.qm` robi zwykłe budowanie (`lrelease` w celu `brulion`); jego wynik pokazuje liczbę
nieprzetłumaczonych tekstów.

## Zasady

* Liczba mnoga zawsze przez `%n`: `qsTr("%n note(s)", "", n)`. Polski ma trzy formy:
  1 (`notatka`), 2–4, 22–24 … (`notatki`), 0, 5–21, 25–31 … (`notatek`); angielski dwie.
* Całe zdania w jednym tekście — nie skleja się zdań z kawałków (szyk i odmiana w polskim
  zależą od całości). Tekst przekazywany jako `%1` nie powinien wymagać odmiany; gdy wymaga,
  użyj osobnych tekstów dla wariantów albo dwukropka („Wersja: %1”).
* Ten sam angielski tekst w jednym kontekście o dwóch znaczeniach dostaje komentarz
  rozróżniający: `qsTr("Normal", "text width")` (normalna szerokość ≠ normalny rozmiar).
* Przyciski: czasownik w trybie rozkazującym („Zapisz”, „Anuluj”, „Usuń”, „Przywróć”).
  Pytania w oknach potwierdzenia: bezokolicznik („Usunąć na zawsze?”).
* Typografia: cudzysłowy „…”, wielokropek …, półpauza – lub pauza — jak w źródle,
  twarde spacje (`&#xa0;`) zachowane.
* Bez rodzaju gramatycznego tam, gdzie nie znamy podmiotu (tytuł strony, folder):
  „Element „%1” trafi do kosza”, „Przywrócono „%1””.

## Słownik

| angielski | polski |
|---|---|
| notebook, vault (nasz) | notatnik |
| vault (Obsidian) | skarbiec |
| note / page | notatka / strona |
| journal, daily note | dziennik, notatka dzienna |
| block, outline, outliner | blok, konspekt |
| backlinks, linked references | odnośniki |
| unlinked mentions | wzmianki bez linku |
| link | link |
| block reference, embed | odwołanie do bloku, osadzenie |
| tag, property | tag, właściwość |
| task: to do / in progress / waiting / done / cancelled | zadanie: do zrobienia / w toku / oczekujące / zrobione / anulowane |
| due / scheduled / start | termin / zaplanowane / początek |
| overdue | zaległe |
| priority highest … lowest | priorytet najwyższy, wysoki, średni, niski, najniższy |
| reminder, alarm, snooze | przypomnienie, alarm, drzemka (przycisk „Drzemka 10 min”) |
| query | zapytanie |
| sync, sync center | synchronizacja, centrum synchronizacji |
| conflict, conflict copy, merge | konflikt, kopia konfliktu, scal |
| keep mine / the other version | zachowaj moją / druga wersja |
| app password | hasło aplikacji |
| trash, delete for good | kosz, usuń na zawsze |
| version history | historia wersji |
| side panel, sidebar | panel boczny, pasek boczny |
| quick capture | szybka notatka |
| command palette | paleta poleceń |
| graph, node | graf, węzeł |
| attachment, template | załącznik, szablon |
| system tray | zasobnik systemowy |
| offline | offline |
