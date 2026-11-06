# Brulion translations

Interface texts are in English in the code (`qsTr()` in QML, `tr()` / `QCoreApplication::translate()`
in C++). English is the app's default language (also in the browser and on a Polish system);
`brulion_pl.ts` is the Polish translation, chosen in the first step of the introduction or in the
settings. `brulion_en.ts` holds only the English plural forms of texts with `%n`. The same files
also translate the `brulion-cli` command line (context `brulion::cli`, see
[docs/CLI.md](../docs/CLI.md#language)): the names of commands, options and values (`task add`,
`--due`, `todo`, `today`) stay unchanged in the translation.

## Refreshing

```sh
scripts/remote-build.sh -n i18n update_translations   # lupdate: new texts go into both .ts files
rsync -a laptop:work/brulion-i18n/src/i18n/ i18n/      # the .ts files come back from the laptop
```

New entries have `type="unfinished"` — translate them (Qt Linguist or a text editor). The `.qm`
files are compiled by the normal build (`lrelease` in the `brulion` target, a second time for
`brulion-cli`); its output shows the number of untranslated texts.

## Rules

* Plurals always through `%n`: `qsTr("%n note(s)", "", n)`. Polish has three forms:
  1 (`notatka`), 2–4, 22–24 … (`notatki`), 0, 5–21, 25–31 … (`notatek`); English has two.
* Whole sentences in one text — do not glue sentences together from pieces (word order and
  inflection in Polish depend on the whole). A text passed as `%1` should not need inflecting;
  when it does, use separate texts for the variants or a colon („Wersja: %1”).
* The same English text with two meanings in one context gets a disambiguating comment:
  `qsTr("Normal", "text width")` (normal width ≠ normal size).
* Buttons: a verb in the imperative mood („Zapisz”, „Anuluj”, „Usuń”, „Przywróć”).
  Questions in confirmation dialogs: the infinitive („Usunąć na zawsze?”).
* Typography: quotation marks „…”, ellipsis …, en dash – or em dash — as in the source,
  non-breaking spaces (`&#xa0;`) kept.
* No grammatical gender where the subject is unknown (a page title, a folder):
  „Element „%1” trafi do kosza”, „Przywrócono „%1””.

## Glossary

| English | Polish |
|---|---|
| notebook, vault (ours) | notatnik |
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
| reminder, alarm, snooze | przypomnienie, alarm, drzemka (button „Drzemka 10 min”) |
| query | zapytanie |
| sync, sync center | synchronizacja, centrum synchronizacji |
| event log, log, detailed log | rejestr zdarzeń (not „dziennik” — that is the notes' journal), logi, szczegółowy rejestr |
| sync run, history of runs | synchronizacja (#42), historia synchronizacji |
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
