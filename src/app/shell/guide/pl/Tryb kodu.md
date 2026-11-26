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
**Dodaj projekt…** (ikona folderu obok *Projekty* albo w palecie) i wybierz jego folder. Pod każdym projektem są jego wątki; **+** zaczyna nowy wątek, **⋯** (albo prawy przycisk) otwiera go w edytorze, pokazuje folder, **przypina** go (przypięte projekty są na górze), przesuwa (albo przeciągnij go myszą), otwiera **ustawienia projektu** i zdejmuje projekt z listy — jego folder i wątki zostają. Projekty są zapisane tylko na tym urządzeniu.

**Ustawienia projektu** — nazwa, emoji jako ikona, agent i miejsce pracy nowych wątków oraz **skrypty**: przyciski nad wątkiem, które uruchamiają polecenie w nowej karcie terminala, w folderze wątku. Każdy ma nazwę, polecenie, ikonę, skrót klawiszowy (tylko w trybie kodu; także w palecie jako *Uruchom: …*) i może **uruchamiać się po utworzeniu worktree** — z *Zaczekaj na niego, zanim agent zacznie* pierwsza wiadomość czeka, aż się skończy (instalacja, budowanie).

Projekt z plikiem **`t3.json`** (plik projektu T3 Code) wnosi swoje skrypty, ikonę (`iconPath`) i miejsce pracy nowych wątków (`defaultThreadEnvMode`): Brulion czyta go znowu, gdy się zmieni, i nigdy go nie zapisuje. Jego skrypty mają oznaczenie *z t3.json*; zmieniony staje się Twój i zastępuje skrypt pliku o tej nazwie. Polecenie uruchamia się tylko wtedy, gdy Ty je uruchomisz.

## Wątki
Nowy wątek zaczyna się od Twojej pierwszej wiadomości. Nad nią wybierasz, gdzie agent pracuje (**Ctrl+Shift+X**):
- **Lokalnie** — w samym folderze projektu;
- **Nowy worktree** — we własnym worktree git na nowej gałęzi (`brulion/<nazwa>`; prefiks jest w Ustawieniach), od wybranej gałęzi (*Od main*; zdalna jest najpierw pobierana), więc kilku agentów może pracować naraz i nie wchodzić sobie w drogę. Worktree trafiają do `~/.brulion/worktrees` (zmienisz to w Ustawieniach → Rozszerzenia → Tryb kodu);
- **istniejący worktree** — także *Nowy wątek w tym worktree* w menu wątku.

**Gałąź** w nagłówku otwiera wybór gałęzi (**Ctrl+Shift+G**): przełącz się na gałąź, wpisz nową nazwę, by ją utworzyć, albo wpisz `#123`, `123` lub adres pull requesta, by go **pobrać** — w folderze projektu albo w nowym worktree, gdzie pracuje nad nim nowy wątek (agent wie, który to pull request). Brulion nie przełącza gałęzi, gdy agent pracuje w tym folderze.

W wątku działa wszystko z [[AI|czatu z AI]]: pisanie, gdy agent odpowiada, model i wysiłek agenta, zatwierdzanie i plany, limity subskrypcji, pytania na boku, pytanie innych agentów. Panel boczny pokazuje stan każdego wątku: *Plan* albo *Zgoda?*, gdy czeka na Ciebie, kółko, gdy agent pracuje, *Błąd* na czerwono, kropkę, gdy odpowiedź jest nieprzeczytana; przypięte wątki są na górze. Wątki odfiltrujesz po tytule albo gałęzi i posortujesz po ostatniej aktywności albo dacie utworzenia. Menu wątku zmienia jego nazwę (także **F2** albo podwójne kliknięcie nazwy w nagłówku), przypina go, oznacza jako nieprzeczytany, kopiuje ścieżkę, gałąź albo link, archiwizuje go albo przenosi do kosza — gdy ma własny worktree, którego nie używa inny wątek, Brulion pyta, czy usunąć też worktree (gałąź zostaje; *Cofnij* przywraca jedno i drugie).

**Klawisze** w trybie kodu: **Ctrl+Shift+O** nowy wątek, **Ctrl+Shift+N** nowy wątek lokalny, **Ctrl+1–9** wątki z panelu bocznego, **Ctrl+Shift+[** / **]** poprzedni i następny, **Ctrl+O** otwórz w edytorze, **Ctrl+D** zmiany. Zmienisz je w Ustawieniach → Skróty klawiszowe.

Wątek jest zapisany jako strona w folderze `AI` Twojego notatnika, z nazwą projektu — nigdy w dzienniku i nigdy w czacie z AI o notatkach.

## Zmiany i terminal
Obok wątku panel **Zmiany** pokazuje zmiany projektu — które, wybierasz w jego menu: **ostatnią turę** albo każdą wcześniejszą **turę** agenta (tylko to, co zmieniła; **cofniesz** samą ją albo wrócisz do stanu sprzed niej), wszystko **niezatwierdzone**, całą **gałąź** względem jej bazy albo jeden z najnowszych **commitów**. Każdy plik ma swoje różnice (obok siebie albo jedna pod drugą, szukanie przez Ctrl+F); zaznacz wiersze i **Dodaj do czatu**, by skomentować je dla agenta. Menu pliku (prawy przycisk albo ⋯) dodaje go do indeksu, usuwa z indeksu albo odrzuca jego zmiany; przyciski fragmentu robią to samo z jego częścią. Główny przycisk robi to, co pasuje teraz — **commit** (tylko zaznaczonych plików, jeśli chcesz na nowej gałęzi; **Napisz z AI** pisze wiadomość na podstawie zmian, gdy AI jest skonfigurowane), **push**, **pobranie**, **pull request** (z zalogowanym wierszem poleceń GitHuba `gh`) albo **publikację** projektu na GitHubie — i pyta przed wypchnięciem na gałąź domyślną; menu gałęzi także **odkłada zmiany** (stash). Gdy scalanie albo rebase zatrzyma się na konfliktach, baner pozwala wybrać stronę każdego pliku, a potem kontynuować albo przerwać. Pod wątkiem **terminal** uruchamia powłoki w folderze wątku (ikona terminala w nagłówku go pokazuje i chowa). **Otwórz w** (ikona `</>`, **Ctrl+O**) otwiera folder w Twoim edytorze; strzałka obok wymienia edytory znalezione na tym komputerze — VS Code, Cursor, Zed, środowiska JetBrains… — i menedżer plików, i zapamiętuje ten, który wybierzesz (własne polecenie wpiszesz w Ustawieniach → Rozszerzenia → Tryb kodu).

## Na telefonie
Komputer, który udostępnia telefonowi swoich agentów ([[AI#Agent na innym komputerze]]), udostępnia też swoje projekty: na telefonie tryb kodu pokazuje je z nazwą komputera. Napisz w wątku — zaczętym na komputerze albo nowym, **Lokalnie** albo w **nowym worktree** — a agent pracuje na tamtym komputerze, w projekcie, jakbyś przy nim siedział. Punkt kontrolny każdej tury powstaje tam, więc z powrotem przy komputerze panel **Zmiany** pokaże, co zmieniła tura z telefonu. Na telefonie widzisz pliki zmienione w ostatniej turze i ich różnice; commit, push i terminal są na komputerze.

## Pliki i wyszukiwanie
Panel obok wątku ma trzy karty: **Zmiany**, **Pliki** i **Szukaj**.
- **Ctrl+P** (albo *Przejdź do pliku…* w palecie) znajduje plik projektu po kilku literach nazwy — `cvw` znajdzie `CodeView.qml`; Enter otwiera go w karcie **Pliki**, Ctrl+Enter w Twoim edytorze.
- **Pliki** pokazują plik tylko do odczytu, z kolorami i numerami wierszy (obrazy jako obrazy). Kliknij wiersz — Shift+klik albo przeciągnięcie po numerach zaznacza więcej — i wybierz **Skomentuj…**: wiersze z Twoim komentarzem trafiają do wiadomości wątku, żeby agent się nimi zajął.
- **Ctrl+Shift+F** szuka tekstu we wszystkich plikach projektu (bez ignorowanych); wynik otwiera plik w swoim wierszu. `Aa`, `ab` i `.*` to wielkość liter, całe słowa i wyrażenia regularne.
- Wpisz **@** w wiadomości wątku, żeby wskazać plik albo folder projektu (`@src/main.cpp`); agent dostaje też same wskazane pliki.
- Plik, do którego agent podlinkuje w odpowiedzi, otwiera się w karcie Pliki w swoim wierszu.

## Terminal
Każdy wątek ma własne terminale: przejdź do innego wątku i z powrotem, a jego powłoki, ich wyjście i działające w nich programy — serwer deweloperski, testy — wciąż tam są. **Ctrl+J** pokazuje i chowa terminal. Karta mieści do czterech terminali: **podziel w prawo** (Ctrl+Shift+D) albo **w dół** (Ctrl+Alt+Shift+D); **Ctrl+Alt+]** i **Ctrl+Alt+[** przechodzą między nimi, **Ctrl+Shift+T** otwiera nowy, **Ctrl+Shift+W** zamyka (Brulion najpierw pyta, jeśli coś w nim jeszcze działa). Kropka na karcie oznacza nowe wyjście, dzwonek — że program zadzwonił, a „exit 1” — jak program się zakończył. W Windows terminal uruchamia PowerShell 7 (a bez niego Windows PowerShell albo cmd) i potrzebuje Windows 10 w wersji 1809 lub nowszej; skrypty projektu działają w tej powłoce, tak jakby je wpisać.

**Ctrl+Shift+F** przeszukuje wyjście (Enter idzie w górę, do starszych wyników), **Ctrl+Shift+A** zaznacza całe, **Ctrl+Shift+Home** i **End** skaczą na jego początek i do najnowszej linii. Przytrzymaj **Ctrl** nad adresem strony albo ścieżką pliku, np. `src/main.cpp:12` — zostanie podkreślona — i kliknij, by ją otworzyć (pliki w Twoim edytorze). Prawy przycisk daje **Dodaj do czatu** — zaznaczenie albo to, co widać w terminalu, trafia do pola wiadomości wątku — oraz Kopiuj, Wklej, Wyczyść i resztę. Usunięcie wątku kończy jego terminale; archiwizacja kończy te, w których nic nie działa. Po ponownym uruchomieniu karty wątku wracają w swoich folderach, a polecenie jest wpisane i czeka na Enter. Klawisze zmienisz w Ustawieniach → Skróty klawiszowe.

## Dla ciekawych
- Agent wątku dostaje narzędzia czatu Brulionu — powiadomienie Cię, zapytanie innego agenta, prośbę o sekret — ale żadnych Twoich notatek ([[Wiersz poleceń i agenci AI]]).
- Poproszony „daj znać, gdy testy przejdą”, może wysłać Ci własne powiadomienie ([[AI#Powiadomienia od agenta]]).
