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
Obok wątku panel **Zmiany** pokazuje zmiany projektu — które, wybierasz w jego menu: **ostatnią turę** albo każdą wcześniejszą **turę** agenta (tylko to, co zmieniła; **cofniesz** samą ją albo wrócisz do stanu sprzed niej), wszystko **niezatwierdzone**, całą **gałąź** względem jej bazy albo jeden z najnowszych **commitów**. Każdy plik ma swoje różnice (obok siebie albo jedna pod drugą, szukanie przez Ctrl+F); zaznacz wiersze i **Dodaj do czatu**, by skomentować je dla agenta. Menu pliku (prawy przycisk albo ⋯) dodaje go do indeksu, usuwa z indeksu albo odrzuca jego zmiany; przyciski fragmentu robią to samo z jego częścią. Główny przycisk robi to, co pasuje teraz — **commit** (tylko zaznaczonych plików, jeśli chcesz na nowej gałęzi), **push**, **pobranie**, **pull request** (z zalogowanym wierszem poleceń GitHuba `gh`) albo **publikację** projektu na GitHubie — i pyta przed wypchnięciem na gałąź domyślną; menu gałęzi także **odkłada zmiany** (stash). Gdy scalanie albo rebase zatrzyma się na konfliktach, baner pozwala wybrać stronę każdego pliku, a potem kontynuować albo przerwać. Pod wątkiem **terminal** uruchamia powłoki w folderze wątku (ikona terminala w nagłówku go pokazuje i chowa). **Otwórz w edytorze** (ikona `</>`) otwiera folder w Twoim edytorze: jego polecenie — `code`, `zed`, `cursor` — ustawisz w Ustawieniach → Rozszerzenia → Tryb kodu.

## Dla ciekawych
- Agent wątku dostaje narzędzia czatu Brulionu — powiadomienie Cię, zapytanie innego agenta, prośbę o sekret — ale żadnych Twoich notatek ([[Wiersz poleceń i agenci AI]]).
- Poproszony „daj znać, gdy testy przejdą”, może wysłać Ci własne powiadomienie ([[AI#Powiadomienia od agenta]]).
