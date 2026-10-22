// The documentation notebook as an older version wrote it, for the tests of updateDocsVault()
// (src/app/shell/demovault.h): pages recognised by the hashes of 0.3.0–0.3.5.
#pragma once

#include <QString>

namespace olddocs {

/// "Spis treści.md" exactly as 0.3.0–0.3.2 wrote it.
inline const QString kTableOfContents030 = QString::fromUtf16(uR"md(---
tags: [dokumentacja]
aliases: [Dokumentacja, Pomoc]
brulion: document
---
# Dokumentacja Brulionu

Ta dokumentacja jest zwykłymi notatkami Brulionu — osobnym brulionem „Dokumentacja Brulionu”. Możesz ją czytać, klikać linki, a nawet edytować: to Twoja kopia. Przykłady w niej są żywe, więc najlepiej uczyć się, zmieniając je na próbę. Do własnych notatek wrócisz przyciskiem przełączania brulionów obok nazwy brulionu w pasku bocznym.

## Od czego zacząć
- [[Pierwsze kroki|Pierwsze kroki]] — pierwszy wpis, nowa strona, wyszukiwanie.
- [[Dokument i konspekt|Dokument i konspekt]] — dwa tryby pisania i praca na blokach.

## Pisanie i porządkowanie
- [[Linki i odnośniki zwrotne|Linki i odnośniki zwrotne]] — `[[strony]]`, odwołania do bloków, osadzenia.
- [[Tagi i właściwości|Tagi i właściwości]] — `#tagi`, nagłówek YAML, `klucz:: wartość`.
- [[Dziennik, kalendarz i szablony|Dziennik, kalendarz i szablony]] — codzienne strony i szablony.

## Planowanie
- [[Zadania|Zadania]] — terminy, priorytety, powtarzanie.
- [[Przypomnienia i alarmy|Przypomnienia i alarmy]] — powiadomienia i budziki.
- [[Zapytania|Zapytania]] — listy zadań i bloków zbierane z całego brulionu.

## Liczenie
- [[Obliczenia|Obliczenia]] — bloki `calc`, tabele z formułami, rachunki w tekście.

## Pliki i urządzenia
- [[Synchronizacja|Synchronizacja]] — Syncthing, Nextcloud, konflikty, historia wersji.
- [[Import i eksport|Import i eksport]] — przenoszenie notatek z i do innych programów.
- [[Format plików|Format plików]] — jak notatki wyglądają na dysku.

## Pod ręką
- [[Skróty klawiszowe|Skróty klawiszowe]]

> [!tip] Paleta poleceń
> Nie pamiętasz, gdzie coś jest? Naciśnij Ctrl+K i wpisz kilka liter — paleta znajdzie polecenie, widok albo ustawienie.
)md");
/// The line 0.3.3 added to it (before "- [[Dziennik…").
inline const QString kFormulasLine033 = QString::fromUtf16(
        u"- [[Wzory matematyczne|Wzory matematyczne]] — LaTeX w tekście i blokach: ułamki, pierwiastki, macierze.\n");

/// "Spis treści.md" as 0.3.3–0.3.5 wrote it.
inline QString tableOfContents033()
{
    QString text = kTableOfContents030;
    text.insert(text.indexOf(QStringLiteral("- [[Dziennik")), kFormulasLine033);
    return text;
}

} // namespace olddocs
