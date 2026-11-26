---
tags: [dokumentacja]
aliases: [Kod, Projekty, Wątki, T3 Code]
brulion: document
---
# Tryb kodu

Brulion ma drugi tryb — do programowania z agentami AI, tak jak T3 Code: **projekty** — foldery na tym komputerze, zwykle repozytoria git — i **wątki**, każdy to jedno zadanie dla agenta, takiego jak Claude Code, Codex czy OpenCode. Agent pracuje w projekcie z Twoją subskrypcją; widzisz, co zmienia, uruchamiasz skrypty projektu w terminalu, robisz commity i pull requesty — a Brulion daje Ci znać [[Przypomnienia i alarmy#Lista powiadomień|powiadomieniem]], gdy agent odpowie, coś się nie uda albo Cię potrzebuje.

Przełączasz **górnym przyciskiem paska ikon** (ikona `</>`; w trybie kodu ikona notatnika wraca do notatek), poleceniami *Tryb kodu* / *Tryb notatek* w palecie albo w **Ustawieniach → Rozszerzenia → Tryb kodu**. Tryb kodu nie pokazuje nic z Twoich notatek; notatnik zostaje otwarty pod spodem — przypomnienia, synchronizacja i powiadomienia działają dalej.

> [!info] Czego potrzebujesz
> Agenta AI w **Ustawieniach → Rozszerzenia → Agenci AI** ([[AI#Własny agent AI (ACP)]]) — na komputerze: agenci działają w aplikacji na komputer.

## Projekty
**Dodaj projekt…** (ikona folderu obok *Projekty* albo w palecie) i wybierz jego folder. Pod każdym projektem są jego wątki; **+** zaczyna nowy wątek, **⋯** (albo prawy przycisk) otwiera go w edytorze, pokazuje folder, ustawia **nazwę i skrypty** i zdejmuje projekt z listy — jego folder i wątki zostają. Projekty są zapisane tylko na tym urządzeniu.

**Skrypty** to przyciski projektu nad wątkiem — po jednym wierszu, `nazwa: polecenie`:

```
dev: npm run dev
test: npm test
```

Kliknięcie uruchamia polecenie w folderze wątku, w nowej karcie terminala.

## Wątki
Nowy wątek zaczyna się od Twojej pierwszej wiadomości. Nad nią wybierasz, gdzie agent pracuje:
- **Lokalnie** — w samym folderze projektu;
- **Nowy worktree** — we własnym worktree git na nowej gałęzi (`brulion/<nazwa>`), więc kilku agentów może pracować naraz i nie wchodzić sobie w drogę. Worktree trafiają do `~/.brulion/worktrees` (zmienisz to w Ustawieniach → Rozszerzenia → Tryb kodu).

W wątku działa wszystko z [[AI|czatu z AI]]: pisanie, gdy agent odpowiada, model i wysiłek agenta, zatwierdzanie i plany, limity subskrypcji, pytania na boku, pytanie innych agentów. Panel boczny pokazuje stan każdego wątku: kółko, gdy agent pracuje, znak zapytania, gdy o coś pyta, czerwony znak po błędzie, kropkę, gdy odpowiedź jest nieprzeczytana. Prawy przycisk archiwizuje wątek albo przenosi go do kosza.

Wątek jest zapisany jako strona w folderze `AI` Twojego notatnika, z nazwą projektu — nigdy w dzienniku i nigdy w czacie z AI o notatkach.

## Zmiany i terminal
Obok wątku panel **Zmiany** pokazuje zmiany projektu względem gita — każdy plik z jego różnicami; po turze agenta zobaczysz tylko to, co zmieniła ta tura, i możesz ją **cofnąć**. Stamtąd robisz **commit**, **push** i **pull request** (z zalogowanym wierszem poleceń GitHuba `gh`). Pod wątkiem **terminal** uruchamia powłoki w folderze wątku (ikona terminala w nagłówku go pokazuje i chowa). **Otwórz w edytorze** (ikona `</>`) otwiera folder w Twoim edytorze: jego polecenie — `code`, `zed`, `cursor` — ustawisz w Ustawieniach → Rozszerzenia → Tryb kodu.

## Na telefonie
Komputer, który udostępnia telefonowi swoich agentów ([[AI#Agent na innym komputerze]]), udostępnia też swoje projekty: na telefonie tryb kodu pokazuje je z nazwą komputera. Napisz w wątku — zaczętym na komputerze albo nowym, **Lokalnie** albo w **nowym worktree** — a agent pracuje na tamtym komputerze, w projekcie, jakbyś przy nim siedział. Punkt kontrolny każdej tury powstaje tam, więc z powrotem przy komputerze panel **Zmiany** pokaże, co zmieniła tura z telefonu. Na telefonie widzisz pliki zmienione w ostatniej turze i ich różnice; commit, push i terminal są na komputerze.

## Dla ciekawych
- Agent wątku dostaje narzędzia czatu Brulionu — powiadomienie Cię, zapytanie innego agenta, prośbę o sekret — ale żadnych Twoich notatek ([[Wiersz poleceń i agenci AI]]).
- Poproszony „daj znać, gdy testy przejdą”, może wysłać Ci własne powiadomienie ([[AI#Powiadomienia od agenta]]).
