---
tags: [dokumentacja]
aliases: [Sztuczna inteligencja, Asystent AI, Claude, ChatGPT, Ollama, Model na urządzeniu]
brulion: document
---
# AI

Brulion potrafi pytać model AI o Twoje notatki — tak samo na komputerze, jak na telefonie. Robi to za Ciebie **Asystent AI**, a Twoje własne [[Rozszerzenia]] mogą to robić przez `b.ai`.

## Jaki model i ile to kosztuje
Otwórz **Ustawienia → Rozszerzenia → AI** i wybierz:

- **Claude** — klucz z platform.claude.com (**API keys**). Bez podanego modelu Brulion pyta `claude-opus-5-5`; `claude-haiku-5-5` jest szybszy i dużo tańszy.
- **Zgodne z OpenAI** — OpenAI, OpenRouter albo własny serwer: Ollama lub LM Studio na Twoim komputerze (adres, np. `http://192.168.1.10:11434/v1`, i nazwa modelu). Własny serwer nie potrzebuje klucza i nic nie kosztuje; telefon dosięgnie go w domu albo przez Tailscale. Adres `http://` nie jest szyfrowany — ustawienia o tym mówią: w domowej sieci to w porządku, poza nią użyj `https://`.
- **Na tym urządzeniu** — mały model, który działa w samym Brulionie: bez klucza, bez konta, bez internetu. Zobacz [[#Model na tym urządzeniu]] niżej.

Naciśnij **Sprawdź**: model odpowie „OK” albo zobaczysz, co jest nie tak.

> [!note] Subskrypcje
> Subskrypcja Claude Pro/Max albo ChatGPT Plus działa tylko w aplikacjach tych firm; inne programy używają klucza API, rozliczanego osobno, za użycie. Pytanie o jedną notatkę kosztuje ułamek grosza. Z subskrypcją możesz zamiast tego dać tym aplikacjom dostęp do notatek — na komputerze robi to **Ustawienia → Rozszerzenia → claude.ai i ChatGPT**: [[Wiersz poleceń i agenci AI#Twoje notatki w claude.ai i ChatGPT]] — albo pozwolić ich własnemu agentowi odpowiadać w czacie z AI, na komputerze albo, przez Twój komputer, na telefonie: [[#Własny agent AI (ACP)]].

Klucz zostaje **tylko na tym urządzeniu** (nie w notatniku, nie w synchronizacji, nie w dziennikach zdarzeń). Notatki trafiają do dostawcy tylko wtedy, gdy o to poprosisz, albo gdy zrobi to rozszerzenie zatwierdzone przez Ciebie z uprawnieniem `ai`.

## Model na tym urządzeniu
Gdy wybierzesz **Na tym urządzeniu**, model działa w Brulionie — także na telefonie — i nic, o co pytasz, nie opuszcza urządzenia. Wybierz jeden z trzech modeli — własny model Bruliona, zrobiony dla asystenta (domyślny), i dwa firmy Liquid AI: LFM3 1.7B, z którego powstał, oraz LFM3-VL 2B — i naciśnij **Pobierz** (najlepiej przez Wi-Fi; na telefonie pobiera się dalej za inną aplikacją i przy wygaszonym ekranie — powiadomienie pokazuje, ile już jest, i ma **Zatrzymaj**; przerwane pobieranie zacznie się od miejsca, w którym stanęło):

| Model | Rozmiar | W czym dobry |
|---|---|---|
| **Brulion Assistant 1.7B** („Brulion” w ustawieniach; domyślny) | 1,1 GB | tylko tekst; LFM3 1.7B wytrenowany do pracy z **asystentem AI** — najlepiej radzi sobie z pracą na notatniku razem z nim; odpowiada od razu, bez namysłu; duży plik ma wspólny z LFM3 1.7B (gdy tamten już jest, to 92 MB więcej) |
| **LFM3 1.7B** | 1,0 GB | tylko tekst; przeszukuje notatnik i zmienia notatki lepiej niż LFM3-VL 2B |
| **LFM3-VL 2B** | 1,2 GB | **widzi obrazki**: zdjęcia, skany, zrzuty ekranu i tekst na nich |

Czego się spodziewać:

- Jest **mały**. Streszczenie strony, pytanie o notatkę, tekst ze sfotografowanego paragonu wychodzą dobrze; długie zadanie w wielu krokach może zejść na manowce, a jego polszczyzna jest prostsza niż u Claude'a. Do poważnej pracy wybierz Claude'a.
- Jest **wolniejszy** niż model w chmurze, najbardziej na telefonie: szybki telefon pisze jakieś 15–18 tokenów (kawałków słów) na sekundę i pokazuje pierwsze myśli po kilku sekundach, ale pytanie, które wymaga przeszukania notatek, zajmuje mu minutę albo więcej, a wolniejszemu telefonowi dłużej. Nagrzany telefon (długie używanie, ładowanie) zwalnia, dopóki nie ostygnie. Najdłużej trwa pierwsze pytanie — model czyta wtedy instrukcje asystenta i zachowuje na urządzeniu to, co z nich przeczytał, na kolejne pytania. Dopóki odpowiada, mówi o tym powiadomienie z **Zatrzymaj**: pracuje dalej przy wygaszonym ekranie i za inną aplikacją (może tam wolniej — telefon daje najszybsze rdzenie najpierw aplikacji na ekranie); gdy Brulion jest na ekranie, ekran w tym czasie nie gaśnie. Potem model zostaje w pamięci przez dziesięć minut.
- Zajmuje około **2,5 GB pamięci**, dopóki jest wczytany, i potrzebuje procesora z ostatnich lat (telefon mniej więcej od 2018 roku, komputer z AVX2).
- Zbyt długą prośbę **przycina** — długą stronę w środku, długi wynik wyszukiwania na końcu — zamiast odmawiać.

Modele należą do Liquid AI, mają własną licencję (LFM Open License v1.0) i nie są częścią Bruliona: aplikacja pobiera wybrany model z Hugging Face. Brulion Assistant 1.7B to LFM3 1.7B wytrenowany dalej przez Bruliona, na tej samej licencji — firma Liquid AI go nie stworzyła ani nie popiera. **Usuń** kasuje jego pliki (plik potrzebny innemu pobranemu modelowi zostaje). Nie każde wydanie ma tego dostawcę: nie ma go wersja w przeglądarce ani część paczek na komputery.

### Własny model
**Dodaj plik modelu…** (pod trzema modelami) przyjmuje plik GGUF innego modelu — takiego, który pobierzesz sam, np. Qwen3, Llama 3.2, Gemma 3, Phi-4 mini albo SmolLM z Hugging Face. Brulion najpierw czyta, co plik mówi o sobie, i mówi, dlaczego nie może go użyć (to nie model, to adapter, za duży na pamięć tego urządzenia, rodzaj, którego llama.cpp nie zna). Na komputerze używa pliku tam, gdzie leży; na telefonie kopiuje go do swojego miejsca (pokazuje, ile już skopiował; plik bywa duży). Ustawienia pokazują potem, co to jest — „Qwen3 4B · Q4_K_M · 2,5 GB · kontekst 32 768 · ChatML” — i możesz dać mu inną **Nazwę**, dać mu **Obrazki…** (jego plik widzenia, `mmproj-….gguf`, dla modelu, który widzi) albo go **Usunąć** (plik skopiowany przez Bruliona znika; Twój własny zostaje, gdzie był).

Brulion pisze do niego w jego własnym formacie czatu (ChatML, Llama 3, Gemma, Phi, Mistral, DeepSeek), pozwala myśleć modelowi, który myśli (albo odpowiedzieć od razu), i trzyma jego wywołania narzędzi przy narzędziach asystenta. Własnego modelu nikt nie trenował do pracy z asystentem, więc to loteria: model od 4B w górę radzi sobie dobrze, maleńki często błądzi. Niczego w pliku się nie sprawdza — używaj plików od twórców, którym ufasz.

**Krótsze wyniki dla innych modeli.** Model na urządzeniu inny niż Brulion Assistant (który wytrenowano na narzędziach asystenta) dostaje od asystenta krótsze wyniki narzędzi — długą stronę w częściach — żeby w nich nie utonął. **Ustawienia → Rozszerzenia → AI → Kod asystenta → Ustawienia → Mniej narzędzi dla małych modeli** daje do tego małemu modelowi 8 narzędzi zamiast 12 i krótsze instrukcje: mniej czyta przy pierwszym pytaniu, ale w naszych testach LFM3 1.7B robił tak mniej zadań, więc to jest wyłączone.

## Asystent AI
Włącz go w **Ustawieniach → Rozszerzenia → AI → Asystent AI**. Rozmawiasz z nim w **czacie z AI**, jak w komunikatorze: Twoje wiadomości po prawej, odpowiedzi AI po lewej, i możesz pytać dalej — „A w przyszłym tygodniu?”, „Przesuń czynsz na piątek”. Górny pasek ma przycisk **✨** — także na telefonie — z pozycjami:

- **Nowa rozmowa** — zapytaj o cokolwiek w notatkach albo poproś, żeby coś w nich zrobiło. Strona na ekranie idzie razem z pytaniem (w dzienniku: dzisiejsza notatka); zanim wyślesz pierwszą wiadomość, możesz ją zabrać (× na jej etykiecie). Na start są podpowiedzi: „Co mam do zrobienia w tym tygodniu?”, „Zaplanuj mi jutro”.
- **Nowa rozmowa obok strony** — to samo w panelu bocznym, obok strony, którą czytasz (w szerokim oknie).
- **Rozmowy** — wszystkie Twoje rozmowy, od najnowszej.
- **Streść tę stronę** — nowa rozmowa, która prosi o streszczenie strony i jej zadań; dopytasz w niej o resztę.
- **Ustawienia AI…** — dostawca, klucz i model.

AI **samo przeszukuje i czyta cały notatnik** — strony, dziennik, zadania z terminami, tagi, przypomnienia — więc możesz zapytać „Co mam do zrobienia w tym tygodniu?”, „Co pisałem o wyjeździe?” albo poprosić „Dopisz mleko do listy zakupów”. **Odpowiedź widać na żywo**, słowo po słowie, a nad nią to, co model myśli, zwinięte do jednej szarej linijki — stuknij, żeby przeczytać całość (model na urządzeniu myśli po angielsku, Brulion Assistant 1.7B odpowiada bez namysłu, a nowsze modele Claude przysyłają streszczenie swojego namysłu). Gdy szuka albo czyta, mówi o tym: „Szukam w notatkach: „wyjazd”…”, „Czytam „Zakupy”…”. **Zatrzymaj** przerywa od razu — model też przestaje pisać, więc kolejne pytanie nie czeka. **Ogląda też obrazki** w notatkach, gdy model widzi (Claude, LFM3-VL na urządzeniu): „Ile zapłaciłem za mleko? Paragon jest w Zakupach.” Brulion Assistant 1.7B i LFM3 1.7B czytają tylko tekst: gdy pobierzesz też LFM3-VL 2B, Brulion wczyta go do obrazka na ich miejsce (kilka sekund dłużej), a potem wraca do wybranego modelu — w pamięci jest zawsze jeden model, więc nie zajmuje jej więcej. To samo jest w palecie (Ctrl+K) jako **AI: nowa rozmowa** i **Czat z AI**.

**Także rozszerzenia.** Poproś o nowe polecenie, widok albo reakcję — „polecenie, które policzy słowa dzisiejszej notatki” — a AI przeczyta, jak się pisze rozszerzenia, wypróbuje kod na Twoich notatkach, niczego nie zmieniając, i zapisze stronę w **Rozszerzenia/**. Potem czeka na Ciebie jak kod z innego urządzenia: przeczytaj stronę i naciśnij **Przeczytaj i uruchom**, gdy zgadzasz się z tym, co robi (zob. [[Rozszerzenia]]).

**Także sama aplikacja.** Z modelem w sieci (Claude, Twój serwer — nie z modelem na urządzeniu, który zostaje przy narzędziach, na których go wytrenowano) asystent na prośbę zmienia też **samego Bruliona**: „włącz ciemny motyw”, „powiększ czcionkę”, „motyw sepia z morskim akcentem”, „przycisk do kalendarza w górnym pasku”, „otwórz graf”. Sprawdza, jakiego ustawienia albo polecenia potrzebuje, i najpierw pyta, jak przy notatce: **Zgoda** od razu zmienia ustawienie; motyw staje się **stroną motywu** w **Motywy/**, wybraną dla notatnika (Ustawienia → Wygląd → Motywy ją pokazują, a inne urządzenia dostają ją razem z notatnikiem); przycisk staje się małym rozszerzeniem w **Rozszerzenia/**, które pokaże go, gdy naciśniesz **Przeczytaj i uruchom** na jego stronie. Odpowiedź wymienia potem, co zmieniło w aplikacji: `⚙️ W aplikacji: …`.

**Zmiany tylko za Twoją zgodą.** Zanim AI dopisze coś do strony albo dnia, zapisze stronę czy odhaczy zadanie, rozmowa pokazuje, co chce zapisać: **Zgoda**, **Zgoda na wszystkie zmiany tego pytania** albo **Nie**. Tego, na co się nie zgodzisz, AI nie zapisze — i dowie się o tym. Odpowiedź wymienia potem strony, które zmieniło: `✏️ Zmienione: [[…]]`.

**Każda rozmowa to strona** w folderze **AI/**, nazwana od Twojego pierwszego pytania — `AI/2026-11-15 Co mam do zrobienia w tym tygodniu` — a dziennik z tego dnia dostaje wiersz z linkiem do niej. Przeczytasz ją więc później na telefonie, wyszukasz, podlinkujesz albo poprawisz; czat ciągnie się dalej od tego, co jest na stronie. Jej zadania nie trafiają do Twoich list zadań, a przypomnienie w odpowiedzi nigdy nie zadzwoni (strona ma `zadania: ignoruj`). **Otwórz rozmowę jako stronę** (w czacie) pokazuje ją w edytorze.

**Wykresy, diagramy i wzory** w odpowiedzi są narysowane tak jak w notatkach: blok `chart` pod tabelą z odpowiedzi ([[Tabele i wykresy]]), diagram `mermaid` ([[Diagramy]]), wzór `$$…$$` ([[Wzory matematyczne]]). Dotknij go, by zobaczyć go w dużym oknie; **Kopiuj źródło** — tam albo w menu wiadomości — kopiuje blok, który wkleisz do notatki. Twój własny agent AI (niżej) wie, że może tak odpowiadać: poproś go o „diagram tego procesu” albo „wykres tych liczb”.

**Obrazki, aparat i głos.** Spinacz obok pola dodaje do wiadomości obrazki — z pliku albo, na telefonie, **Zrób zdjęcie** aparatem; obrazek można też wkleić (Ctrl+V) albo upuścić na rozmowę. Trafiają do folderu załączników notatnika, widać je w rozmowie, a AI je ogląda, gdy jego model widzi (Claude, LFM3-VL na urządzeniu). Obrazki jeszcze niewysłane zostają w szkicu rozmowy, tak jak jej tekst — także zdjęcie, gdy telefon zamknie Brulion przy otwartym aparacie: po powrocie czeka w polu. Przycisk **mikrofonu** pozwala podyktować wiadomość: na Androidzie rozpoznawaniem mowy telefonu; na komputerze Brulion słucha sam, modelem mowy notatek głosowych ([[Dźwięk, wideo i notatki głosowe]]; pierwsze naciśnięcie proponuje jego pobranie) — naciśnij, mów, potem naciśnij jeszcze raz albo **Enter**, a słowa trafią do pola w miejscu kursora (z włączonym *Tekstem w trakcie nagrywania* widać je tam już podczas mówienia, a po zakończeniu zastępuje je staranniej usłyszany tekst całości); **Esc** wyrzuca nagranie. Najwyżej pięć minut; dźwięk nie opuszcza komputera, a nagranie jest usuwane.

**Każda wiadomość ma swoją godzinę** — pod spodem, a nad pierwszą wiadomością dnia jest ten dzień. **Kilka rozmów odpowiada naraz**: zapytaj w jednej i pisz dalej w drugiej. Model na urządzeniu odpowiada po kolei: pozostałe czekają w kolejce, a rozmowa o tym mówi („Czekam na model na tym urządzeniu…”).

**Długie rozmowy.** Mały model przeczyta tylko tyle, ile się w nim zmieści: gdy rozmowa robi się dla niego za długa, AI dostaje ją od późniejszego pytania — całe pytania z odpowiedziami od początku zostają pominięte, nigdy Twoje ostatnie — a linia w rozmowie pokazuje, od którego miejsca czyta.

**Zmień nazwę, przypnij, zarchiwizuj albo usuń** rozmowę z jej menu na liście (prawy przycisk albo długie przytrzymanie na telefonie): data zostaje przed nazwą, **Przypnij** trzyma ją na górze listy, **Zarchiwizuj** odkłada ją do zwiniętego **Archiwum** na dole (dotknij go, by je zobaczyć), a **Cofnij** w komunikacie odwraca każdą z tych zmian. Przypięcie i archiwum są zapisane na stronie rozmowy (`przypięta: tak`, `zarchiwizowana: tak`), więc Twoje inne urządzenia układają ją tak samo.

**Archiwizowane same.** **Ustawienia → Rozszerzenia → AI → Archiwizuj rozmowy nieruszane przez**: **1 dzień**, **3 dni**, **Tydzień** albo **30 dni** (domyślnie **Nigdy**) odkłada każdą rozmowę, której ostatnia wiadomość jest starsza — sprawdzane co godzinę na tym urządzeniu. Przypięte zostają, tak samo jak te, które właśnie odpowiadają, o coś pytają, nie powiodły się, mają nieprzeczytaną odpowiedź, czekającą wiadomość albo szkic; komunikat mówi, ile trafiło do **Archiwum**, z przyciskiem **Cofnij**.

**Najpierw to, co czeka na Ciebie.** Rozmowa, która o coś pyta (czy może zmienić stronę, zalogować się), ma znak zapytania i treść pytania; nieudana — powód na czerwono; z nieprzeczytaną odpowiedzią — kropkę, którą otwarcie rozmowy gasi. Takie rozmowy wędrują na górę listy. **Szukaj** nad listą znajduje rozmowy po nazwach i po tym, co w nich padło: wiersz pokazuje pasującą linię, a otwarta rozmowa — tę wiadomość (Ctrl+F, ↑ ↓ i Enter, Alt+1…9 dla pierwszych dziewięciu, F2 zmienia nazwę: [[Skróty klawiszowe#Czat z AI]]).

**Powiadomienia.** Gdy rozmowa dostanie odpowiedź, nie powiedzie się albo o coś pyta, a Brulion jest w tle, powiadomienie o tym mówi; kliknięcie otwiera rozmowę. **Ustawienia → Rozszerzenia → AI → Powiadamiaj o odpowiedziach AI**: **Zawsze** (także gdy jesteś gdzie indziej w Brulionie), **W tle** (domyślnie) albo **Nigdy** — nigdy o rozmowie, którą masz na ekranie.

**Kto odpowiada w nowej rozmowie.** Asystent AI, chyba że wybierzesz kogoś innego: **Ustawienia → Rozszerzenia → AI → W nowych rozmowach odpowiada** — Twój agent AI (Claude Code, Codex …) albo rozmówca z rozszerzenia. Odpowiada też w linkach z promptem, które nikogo nie wskazują, i jest pierwszym wyborem nowej rozmowy według harmonogramu; każdą rozmowę można nadal oddać komuś innemu chipem **Odpowiada: …**. Ktoś, kogo nie ma na tym urządzeniu: do tego czasu odpowiada asystent.

**Pisz dalej, gdy odpowiada.** Wiadomość wysłana w międzyczasie czeka pod odpowiedzią i idzie sama, gdy odpowiedź zostanie zapisana — kilka idzie po kolei. **Wyślij teraz** (albo Ctrl+Enter) przerywa odpowiedź i najpierw pyta o tę wiadomość; **Edytuj** wkłada ją z powrotem do pola, **×** ją zabiera. Gdy zatrzymasz odpowiedź, czekające wiadomości zostają, z przyciskiem **Wznów**. **Strzałka w górę** w pustym polu przywraca wcześniejsze pytania. Wpisz **@** i początek nazwy strony (albo `[[`), żeby ją wskazać: stuknij ją wśród podpowiedzi (albo naciśnij Tab) — Twój własny agent AI dostaje wskazane strony razem z wiadomością.

**Menu wiadomości** (prawy przycisk albo długie przytrzymanie): **Cytuj** wstawia jej tekst do pola jako cytat; **Edytuj od tego miejsca** wyjmuje z rozmowy Twoją wiadomość i wszystko po niej, a wiadomość wraca do pola, żeby zapytać inaczej (**Cofnij** w komunikacie przywraca całość); **Rozgałęź od tego miejsca** zaczyna nową rozmowę, która toczy się dalej od tej wiadomości — spróbuj innej drogi, nie tracąc tej rozmowy (nowa prowadzi do niej: „Rozgałęzienie z …”).

**Pytanie na boku.** Wpisz **/btw** i pytanie — `/btw ile kosztuje bilet ulgowy?` — albo naciśnij przycisk z dymkiem obok nazwy rozmowy, żeby zapytać o coś obok rozmowy, nie dopisując tego do niej. Odpowiedź przychodzi w **rozmowie pobocznej**, która otwiera się obok rozmowy (na telefonie w jej miejscu): ten sam rozmówca, który zna dotychczasową rozmowę, a sama rozmowa toczy się dalej — także gdy właśnie odpowiada. Pytaj dalej we własnym polu rozmowy pobocznej. **Przenieś do rozmowy** wstawia jej odpowiedź jako cytat do pola rozmowy, **Usuń** ją wyrzuca; **Zamknij** (albo Esc, albo Wstecz) ją zamyka, a jej znacznik pod nazwą rozmowy otwiera ją znowu. Rozmowy poboczne nie pokazują się na liście ani w jej wyszukiwaniu, nie dają kropki ani powiadomienia i trafiają do kosza razem ze swoją rozmową; każda to osobna strona w `AI/` (`side:` wskazuje jej rozmowę).

**Zgłoś odpowiedź.** Odpowiedź AI, która jest obraźliwa, szkodliwa albo po prostu błędna: kliknij ją prawym przyciskiem (na telefonie przytrzymaj) i wybierz **Zgłoś odpowiedź…**. Wskaż, co jest z nią nie tak (obraźliwa lub nienawistna, treści seksualne, brutalna lub niebezpieczna, nękanie, fałszywa lub wprowadzająca w błąd, inne), dopisz komentarz, jeśli chcesz, i zobacz, co zawiera zgłoszenie: odpowiedź — możesz ją skrócić albo usunąć z niej to, co prywatne —, model oraz wersję Bruliona i urządzenie. Twoje pytanie trafia do zgłoszenia tylko wtedy, gdy zaznaczysz **Dodaj moje pytanie sprzed niej**; reszta rozmowy i Twoje notatki — nigdy. **Pokaż tekst zgłoszenia** pokazuje je dokładnie tak, jak zostanie wysłane. **Wyślij zgłoszenie** otwiera je jako nowe zgłoszenie (issue) na GitHubie w przeglądarce — są tam publiczne, każdy może je przeczytać — albo, w wersji, która ma podany adres, jako e-mail w programie pocztowym; wysyłasz je tam, Brulion sam niczego nie wysyła. Odpowiedź zwija się potem do jednej linii „Zgłoszona odpowiedź”; **Ukryj tę odpowiedź** zwija ją bez zgłaszania („Ukryta odpowiedź”). **Pokaż** rozwija zwiniętą odpowiedź, a jej menu zdejmuje oznaczenie. Oznaczenie zostaje w stronie rozmowy (`zgłoszone:` / `reported:`, `ukryte:` / `hidden:` w jej właściwościach), więc widać je też na innych urządzeniach. Odpowiedzi **AI w edytorze** też mają pod spodem **Zgłoś odpowiedź…**.

Asystent pracuje narzędziami (szukanie, czytanie, zapis): na własnym serwerze wybierz model, który je obsługuje (np. Qwen 3 albo Llama 3.1); Claude je obsługuje. Gdy model odpowiada, przycisk ✨ się kręci. Odpowiedź pisze się dalej, gdy oglądasz inne strony, i trafia do swojej rozmowy.

## AI w edytorze
Z włączonym Asystentem AI sztuczna inteligencja pomaga też **pisać**. Zaznacz tekst (albo tylko postaw kursor w bloku, albo zaznacz bloki) i wybierz:

- **Przepisz…** — wybierz, jak: **Krócej**, **Jaśniej**, **Popraw pisownię i gramatykę**, **Bardziej oficjalnie**, **Swobodniej**, **Przetłumacz na angielski** albo **na polski** — albo powiedz to własnymi słowami („jako lista”, „cieplej, do listu”).
- **Pisz dalej** — następne zdanie czy dwa za kursorem, w tonie notatki.
- **Streść** — krótkie streszczenie pod tekstem.

Znajdziesz je w menu zaznaczonego tekstu (prawy przycisk), w menu bloku, w palecie (**AI: Przepisz…** — **Ctrl+Shift+A** —, **AI: Pisz dalej**, **AI: Streść**), a na telefonie pod **✨** na pasku edycji albo na pasku zaznaczonych bloków. Działa też w trybie źródła. Odpowiedź widać na żywo, a przy przepisywaniu także **co się zmienia**, słowo po słowie; nic nie jest wysyłane, dopóki nie wybierzesz, a w notatce nic się nie zmienia, dopóki nie naciśniesz **Zastąp** albo **Wstaw** — i jedno **Cofnij** to odwraca. **Spróbuj ponownie** prosi o inną odpowiedź.

## Własny rozmówca
W czacie z AI zamiast Asystenta AI może odpowiadać rozszerzenie: polecenie z `chat = true` dostaje rozmowę (`ctx.messages`) i zwraca swoją odpowiedź. Gdy takie jest, czat pozwala wybrać, **kto odpowiada** w rozmowie (nad jej wiadomościami albo „Odpowiada: …” przed pierwszą); strona rozmowy to zapamiętuje.

```luau
b.command("pirat", function(ctx)
    if type(ctx.messages) ~= "table" or #ctx.messages == 0 then
        return "Porozmawiaj ze mną w czacie z AI."
    end
    return b.ai({
        system = "Jesteś wesołym piratem. Odpowiadaj najwyżej dwoma zdaniami.",
        messages = ctx.messages,
        maxTokens = 400,
    })
end, { label = "Pirat", chat = true })
```

## Własny agent AI (ACP)
Na komputerze w czacie z AI może też odpowiadać agent AI, którego uruchamiasz sam — **Claude Code**, **Codex**, **Gemini CLI**, **OpenCode** albo dowolny program mówiący protokołem **Agent Client Protocol** (ACP), tym samym, przez który łączą się z nimi edytory takie jak Zed. Agent loguje się sam, więc **działa Twoja subskrypcja**: Claude Code korzysta z logowania do Claude Pro/Max, Codex — do ChatGPT.

1. **Ustawienia → Rozszerzenia → Agenci AI → Dodaj**: Claude Code, Codex, Gemini CLI albo OpenCode — uruchamiane przez `npx`, więc potrzebny jest Node.js — albo **Inny agent…** z własnym poleceniem. **Sprawdź** uruchamia go w folderze notatnika i mówi, czy odpowiada i jakim modelem.
2. W czacie z AI wybierz go jako tego, **kto odpowiada** w rozmowie — albo w każdej nowej: **Ustawienia → Rozszerzenia → AI → W nowych rozmowach odpowiada**.

Agent pracuje w folderze notatnika: czyta i zmienia notatki przez Brulion — zmienioną stronę od razu widać w edytorze i w **historii** strony — i uruchamia polecenia, których wynik widać w rozmowie, razem z jego planem i każdym krokiem. Kroki są podsumowane w jednym wierszu — „Przeczytano 3 pliki · uruchomiono 2 polecenia” — który rozwija i zwija ich listę; **Ustawienia → Rozszerzenia → Agenci AI → Wywołania narzędzi w czacie** pokazuje **Wszystkie**, **Bez gotowych** (nieudane zostają) albo **Żadne** (rozmowa mówi wtedy tylko, że agent pracuje; nieudane i tak widać). Z włączonymi **Narzędziami Brulionu** (domyślnie) dostaje też `brulion-cli mcp` — szukanie, zapytania, zadania, dzienniki ([[Wiersz poleceń i agenci AI]]). Zanim zmieni plik albo uruchomi polecenie, pyta — tak, jak każą jego własne ustawienia — a pytanie pojawia się w rozmowie, zmiana pliku jako porównanie wersji, z odpowiedziami (**Allow**, **Reject** …); gdy jesteś gdzie indziej w Brulionie, mówi o tym komunikat z przyciskiem **Otwórz**. Gdy agent prosi o zalogowanie, też pyta w rozmowie; logowanie w terminalu jest nazwane w komunikacie (np. uruchom `claude` i wpisz `/login`). **Zatrzymaj** przerywa jego turę.

**Zgody.** Chip z tarczą nad rozmową mówi, jak odpowiadać na pytania agenta: **Najpierw pytaj** (każde czeka na Ciebie), **Pozwól zmieniać notatki** (zmiany notatek idą same, polecenia dalej pytają) albo **Pozwól na wszystko**. Plan, który agent prosi zatwierdzić (tryb planowania Claude Code), zawsze czeka: widać go jako czytelny plan z przyciskiem **Zapisz plan jako stronę**. Po turze, która zmieniła notatki, karta je wymienia — przy każdej **Pokaż zmianę**, a **Cofnij te zmiany** przywraca je takie, jakie były (notatka zmieniona w międzyczasie zostaje bez zmian).

Nad rozmową jego **model**, **tryb** (ile może zrobić bez pytania), **wysiłek** i reszta to chipy: stuknij, żeby wybrać — agent zapamięta wybór na kolejne rozmowy, także przed ich pierwszą wiadomością. Obok widać, ile jego pamięci zajmuje rozmowa i ile kosztowała — a przy Claude Code i Codeksie na tym komputerze także, ile **limitów subskrypcji** już zużyto: „5 h: 12 % · tydzień: 68 %”, na pomarańczowo powyżej 80 %, na czerwono przy 100 %. Po najechaniu widać każde okno i kiedy się odnawia (także tygodniowy limit samego modelu, np. Opus); kliknięcie otwiera je z **Odśwież**. Brulion odczytuje je z logowania na tym komputerze (`~/.claude`, `~/.codex`) najwyżej co pięć minut i tylko u samego Anthropic albo OpenAI; gdy logowanie wygasło, mówi, jak je odnowić. **Ustawienia → Rozszerzenia → Agenci AI** pokazują je też w wierszu agenta. Wiersz mówi też, ile agent zużył — „Dziś 1,2 mln tokenów · ≈ 4,10 USD · 30 dni ≈ 61 USD” — a **Zużycie…** pokazuje to dzień po dniu za 7, 30 albo 90 dni, dla każdego modelu, w tym notatniku albo we wszystkich projektach. Brulion liczy to z własnych logów sesji agenta na tym komputerze; koszt to szacunek, ile te same tokeny kosztowałyby w API dostawcy — subskrypcja nie płaci za nie nic więcej. Liczą się też logi, które Codex skompresował, a jego automatyczne przeglądy zgód są wyceniane jak model, którego Codex używa do nich z kluczem API (GPT-5.6-Luna). Wpisz `/` w polu, żeby zobaczyć polecenia agenta (`/review`, `/compact` …). Rozmowa toczy się dalej w tej samej sesji agenta także po ponownym uruchomieniu Brulionu; rozmowa zmieniona w międzyczasie (pisana na telefonie, z odpowiedzią asystenta) zaczyna nową sesję, która dostaje całą jej treść.

> [!warning] Agent to Twój program
> Działa z Twoimi uprawnieniami na tym komputerze, a to, co może zrobić bez pytania, jest jego własnym ustawieniem (jego trybem). Aplikacja na komputer uruchamia agentów sama; na telefonie i w przeglądarce odpowiada zamiast tego agent na innym Twoim komputerze (niżej).

### Agent na innym komputerze
Telefon nie uruchamia programów, ale może to zrobić za niego Twój komputer — laptop, stacjonarny, domowy serwer — w notatniku, który ma otwarty, a Brulion na telefonie z nim rozmawia. Subskrypcja działa tak samo: agent loguje się na tamtym komputerze.

1. **Na komputerze**: **Ustawienia → Rozszerzenia → Agenci AI → Udostępnij agentów telefonowi**. Brulion uruchamia swoich agentów dla Twoich innych urządzeń, dopóki działa; wiersz pod przełącznikiem mówi gdzie (np. `http://laptop.tail1ea20e.ts.net:8790`).
2. **Na telefonie**, z tym samym notatnikiem (synchronizowanym): **Ustawienia → Rozszerzenia → Agenci AI** pokazuje agentów komputera („Claude Code — Na laptop”); **Dodaj** jednego. W czacie z AI jest wtedy wśród tych, **kto odpowiada**.

To wszystko, gdy oba są w Twoim **tailnecie** (Tailscale albo headscale): komputer sam znajduje tam swoją nazwę, zapisuje ją w ustawieniach notatnika, żeby telefon go znalazł, i pozwala **Twoim własnym urządzeniom** sparować się bez kodu — Tailscale mówi mu, kto pyta. Poza tailnetem komputer używa swojego adresu w Twojej sieci (bez szyfrowania po drodze: w domu w porządku), a telefon paruje się raz kodem: **Sparuj urządzenie → Pokaż kod** na komputerze pokazuje kod QR — zeskanuj go aparatem telefonu, Brulion otworzy wypełnione parowanie, stuknij **Sparuj** (albo wpisz adres i kod w **Dodaj → Agent na innym komputerze…**). **Odłącz wszystkie** kończy każde parowanie.

Wszystko opisane wyżej działa tak samo — plan i kroki, porównania zmian, pytania, chipy — ale pliki, które czyta i zmienia, polecenia, które uruchamia, i narzędzia Brulionu są **tamtego komputera**: notatnika, który ma otwarty, synchronizowanego z telefonem (Syncthing albo Nextcloud: na serwerze `acp serve --nextcloud …` synchronizuje go przed każdą turą i po niej). Telefon może zasnąć, a Ty możesz przejść do innej aplikacji, gdy agent pracuje: ciche powiadomienie („Claude Code odpowiada…”, z przyciskiem **Zatrzymaj**) sprawia, że Brulion dalej pobiera to, co agent mówi, więc odpowiedź czeka w rozmowie, gdy wrócisz (aby je widzieć, zezwól na powiadomienia w **Ustawienia → Rozszerzenia → Agenci AI**). Nawet gdy telefon w tym czasie zamknie Brulion, agent kończy pracę tam, a komputer przechowuje jego odpowiedź: przy następnym uruchomieniu Brulionu rozmowa pokazuje „… nadal pracuje na …”, a potem odpowiedź. Na Ciebie czeka tam też **pytanie** agenta (czy może zmienić plik, uruchomić polecenie): Brulion uruchomiony ponownie pokazuje je w rozmowie, a Twoja odpowiedź trafia na komputer — odrzucone zostaje tam tylko pytanie, na które nikt nie odpowiedział przez godzinę. `brulion-cli acp revoke` od razu odłącza telefon ([[Wiersz poleceń i agenci AI#Twój agent dla telefonu]]).

**Może też zmieniać aplikację na telefonie.** Z włączonym **Pozwól mu zmieniać działającą aplikację** w **Edytuj…** agenta (domyślnie wyłączone) jego narzędzia na komputerze mogą też zmieniać Brulion na tym urządzeniu, gdy odpowiada — motyw, ustawienia, rozszerzenia na tę sesję, elementy okna — i oglądać go na zrzutach ekranu: poproś na telefonie „zrób zielony akcent”, a zrobi to tam. Chip **Na żywo** na górnym pasku pokazuje każdą zmianę („Claude Code na laptop”) i ją cofa. Nigdy nie zamyka aplikacji i działa, dopóki Brulion działa (na telefonie powiadomienie opisane wyżej utrzymuje go, gdy agent odpowiada).

> [!tip] Serwer bez ekranu
> Na domowym serwerze bez aplikacji to samo robi `brulion-cli acp serve --agent claude --tailscale` (na NixOS usługa `services.brulion-acp` z `tailscale = true`): [[Wiersz poleceń i agenci AI#Twój agent dla telefonu]].

### Agenci pytają innych agentów
Agent, który odpowiada w czacie AI na komputerze, może dać zadanie innemu rozmówcy czatu i dostać od niego odpowiedź: Claude Code pyta Codexa o drugą opinię, zleca innemu agentowi poszukiwania, prosi asystenta o notatkę. Zadanie staje się **podrozmową** — zwykłą rozmową w `AI/`, która wskazuje tę, z której wyszła (`parent:`) — widoczną pod swoją rozmową jako znacznik z tym, kto odpowiada, i jak mu idzie (dotknij, by ją otworzyć; w podrozmowie **Z: …** prowadzi z powrotem). Jej pytania (czy może zmienić plik?) pokazują się w niej jak w każdej rozmowie, a gdy jesteś gdzie indziej — komunikat z **Otwórz**.

Nie trzeba nic ustawiać: każdy agent z **narzędziami Brulionu** — także ten na innym komputerze — dostaje narzędzia czatu dla własnej rozmowy. Może zaczynać tylko podrozmowy tej rozmowy, najwyżej trzy poziomy w głąb i cztery naraz, a podrozmowa nigdy nie dostaje narzędzi zmieniających aplikację. Skrypty robią to samo przez `brulion-cli app chat …` ([[Wiersz poleceń i agenci AI#Rozmowy w działającej aplikacji]]).

Zamiast czekać na odpowiedź, agent może dać się podrozmowie **obudzić**: kończy swoją turę, a gdy skończy się tura podrozmowy — odpowiedź, błąd, zatrzymanie albo pytanie do Ciebie — do rozmowy agenta przychodzi wiadomość („↩ Podrozmowa … odpowiedziała:” i cytat odpowiedzi, z napisem **Z podrozmowy** nad nią), na którą agent odpowiada jak na każdą; jego trwająca tura idzie pierwsza. W tym czasie znacznik podrozmowy pokazuje ↩. Rozmowa budzi się najwyżej dziesięć razy na godzinę — dwaj agenci nie mogą się budzić bez końca —, a zarchiwizowana nie budzi się wcale: wiadomość czeka w niej z przyciskiem **Wznów**.

### Sekret, o który prosi agent
Czasem agent potrzebuje czegoś, co możesz dać tylko Ty — tokenu, hasła, PIN-u, klucza API. Nigdy nie prosi o to w czacie (rozmowa to notatka: synchronizowana, z historią wersji, wysyłana do modelu). Prosi w **prywatnej karcie** rozmowy: kto prosi i po co, przycisk do strony, na której się logujesz albo tworzysz token, gdy ją podał, i zamaskowane pole z **Pokaż**, **Wklej**, **Wyślij** i **Anuluj**. Gdy jesteś gdzie indziej, komunikat z **Otwórz** mówi, że czeka.

To, co wyślesz, trafia do agenta jako **plik, który możesz przeczytać tylko Ty**, usuwany, gdy kończy się jego odpowiedź (najwyżej po 15 minutach) — nigdy do rozmowy, rejestru zdarzeń ani powiadomienia. Agent, który nie umie użyć pliku, może poprosić o samą wartość; karta ostrzega wtedy, że trafi ona do jego dostawcy AI. **Anuluj** — albo Zatrzymaj — i agent dowie się, że jej nie dostał.

### Powiadomienia od agenta
Asystent AI z modelem w sieci też to potrafi. Poproś agenta „daj znać, gdy skończysz”, a powie Ci to własnym powiadomieniem, z jego nazwą w tytule („Claude Code: Budowanie”) — także na telefonie, gdy agent działa na innym komputerze. Kliknięcie otwiera jego rozmowę albo stronę, którą wskazał; gdy jesteś w Brulionie, pokazuje się jako komunikat z **Otwórz**. Najwyżej 10 na godzinę z jednej rozmowy; każde trafia na [[Przypomnienia i alarmy#Lista powiadomień|listę powiadomień]]. **Ustawienia → Rozszerzenia → AI → Pozwól agentom AI wysyłać powiadomienia** je wyłącza: agent mówi to wtedy w odpowiedzi.

## Rozmowy według harmonogramu
**Ustawienia → Rozszerzenia → Rozmowy według harmonogramu** wysyła wiadomość asystentowi albo agentowi wtedy, kiedy powiesz — „Zaplanuj mi dzień” w dni robocze o 7:30, „Co się zmieniło w notatkach w tym tygodniu?” w piątki. **Dodaj…**, wpisz wiadomość i **kiedy** słowami — `codziennie o 7:30`, `w poniedziałki o 9:00`, `co 3 godziny`, `jutro o 8` (albo po angielsku: `every day at 7:30`) —, wybierz, kto odpowiada, i czy za każdym razem to **nowa rozmowa**, czy **ta sama rozmowa** ciągnięta dalej. Reguła wraca słowami, z pierwszym uruchomieniem, już przy pisaniu; każdy wiersz mówi, kiedy będzie następne, z **Uruchom teraz**, **Edytuj…** i **Usuń**.

**Tylko gdy to ważne.** Wpisz warunek w wiadomość — „codziennie o 7: jeśli jakieś zadanie jest zaległe, powiadom mnie” — a asystent (z modelem w sieci) albo agent wyśle Ci [[#Powiadomienia od agenta|powiadomienie]] tylko wtedy, z jego nazwą w tytule; w inne dni odpowiedź po prostu czeka w swojej rozmowie.

Są przechowywane **na tym urządzeniu**, dla tego notatnika — nigdy w notatniku, więc zsynchronizowany plik nigdy nie uruchomi agenta. Działają, gdy Brulion działa (na komputerze utrzymuje go ikona w zasobniku; na telefonie gdy aplikacja jest otwarta); pominięta, gdy Brulion był zamknięty, uruchamia się raz przy następnym starcie, a nie tyle razy, ile ją pominął. Agenci i skrypty też nimi zarządzają: `brulion-cli app chat schedule add "Zaplanuj mi dzień" --when "w dni robocze o 7:30"`.

### Webhooki
Rozmowa według harmonogramu może też zacząć się, gdy coś dzieje się gdzie indziej — push na GitHubie, dzwonek w Home Assistant, zadanie cron na innym komputerze, skrót iOS. W jej **Edytuj…** włącz **Uruchamiaj ją też webhookiem**: dostaje własny adres (pokazany skrócony; **Kopiuj** kopiuje go całego, **Nowy adres** go wymienia). Zostaw **Kiedy** puste, a uruchomi ją tylko webhook.

Włącz **Webhooki** pod rozmowami według harmonogramu (na komputerze): Brulion odpowiada wtedy pod `http://127.0.0.1:8791`, dopóki działa — tylko na tym komputerze albo w Twoim **tailnecie**, gdy działa tu Tailscale. Twoje własne odwrotne proxy albo `tailscale funnel --bg 8791` udostępnia go w internecie; ten adres wpisz w **Adres publiczny**. `POST` na adres webhooka zaczyna rozmowę, a to, co żądanie przysyła (JSON, tekst, formularz), idzie z jej wiadomością — jako dane, o których AI wie, że nie wolno ich słuchać:

```sh
curl -X POST https://dom.example.com/brulion/hooks/Xk3d… -d '{"event": "dzwonek"}'
```

Najwyżej raz na minutę i nigdy, gdy jej jedna rozmowa jeszcze odpowiada. Adres to jedyny klucz: trzymaj go w tajemnicy. Jest przechowywany na tym urządzeniu razem z harmonogramem, a agenci zarządzający harmonogramami nigdy go nie widzą ([[Wiersz poleceń i agenci AI#Rozmowy w działającej aplikacji]]).

### W drugą stronę: asystent w Twoim edytorze
Zed i środowiska JetBrains rozmawiają z agentami tak samo — a asystent AI Brulionu może być jednym z nich, z Twoimi notatkami i modelem wybranym wyżej: `brulion-cli acp agent` ([[Wiersz poleceń i agenci AI#Asystent Brulionu w edytorze]]).

## We własnych rozszerzeniach
Strona rozszerzenia z `ai` wśród uprawnień może pytać model:

````markdown
---
tags: [rozszerzenie]
uprawnienia: [zapis, ai]
---
```luau
b.command("haiku", function(ctx)
    local wiersz = b.ai({ prompt = "Napisz haiku o dniu " .. b.today(), effort = "low" })
    b.appendToJournal(b.today(), "- " .. wiersz:gsub("\n", " / "))
    return wiersz
end, { label = "Haiku na dziś" })
```
````

`b.ai("pytanie")` zwraca tekst; tabela dodaje `system`, `model`, `maxTokens`, `effort`, `thoughts = true` (to, co model myślał, w polu `thoughts` odpowiedzi), `images` (obrazki z notatnika, nazwane tak, jak osadzają je notatki: `images = { "paragon.jpg" }`) albo `messages` dla rozmowy. Gdy model pisze, Brulion działa dalej. W **konsoli Luau** spróbuj `b.ai("Ile to 2 + 2?")`. Gdy modelu nie da się zapytać — brak klucza, brak połączenia — polecenie zatrzymuje się z podaniem powodu; `pcall(b.ai, "…")` to łapie.

## Narzędzia: model, który sam sprawdza
Dzięki `tools` model może wywoływać funkcje Twojego kodu — przeszukać notatki, przeczytać stronę — zanim odpowie; tak działa Asystent AI. Narzędzie ma nazwę, opis mówiący, kiedy je wywołać, i kształt danych wejściowych. Odpowiedź, która wywołuje narzędzia, ma `toolCalls`: Twój kod je wykonuje i pyta ponownie, z turą modelu (`raw`, bez zmian) i wszystkimi wynikami w jednej wiadomości, aż przyjdzie odpowiedź bez wywołań.

```luau
local narzedzia = {
    {
        name = "szukaj_w_notatkach",
        description = "Wyszukiwanie pełnotekstowe w notatkach użytkownika. "
            .. "Wywołaj je, zanim odpowiesz na pytanie o notatki.",
        input = {
            type = "object",
            properties = { query = { type = "string", description = "Słowa do wyszukania" } },
            required = { "query" },
        },
    },
}

b.command("zapytaj-notatki", function(ctx)
    local pytanie = b.ask("Zapytaj swoje notatki")
    if pytanie == "" then
        return nil
    end
    local wiadomosci = { { role = "user", content = pytanie } }
    for tura = 1, 6 do
        local tekst, odpowiedz = b.ai({ messages = wiadomosci, tools = narzedzia })
        if not odpowiedz.toolCalls then
            return tekst
        end
        table.insert(wiadomosci, { role = "assistant", raw = odpowiedz.raw })
        local wyniki = {}
        for _, wywolanie in odpowiedz.toolCalls do
            local znalezione = {}
            for _, trafienie in b.search(tostring(wywolanie.input.query), 10) do
                table.insert(znalezione, `[[{trafienie.page}]]: {trafienie.excerpt}`)
            end
            local tresc = #znalezione > 0 and table.concat(znalezione, "\n") or "Nic nie znaleziono."
            table.insert(wyniki, { id = wywolanie.id, content = tresc })
        end
        table.insert(wiadomosci, { role = "user", results = wyniki })
    end
    return "Brak odpowiedzi po sześciu turach."
end, { label = "Zapytaj moje notatki (AI)" })
```

Strona potrzebuje `uprawnienia: [ai]`; na własnym serwerze wybierz model, który obsługuje narzędzia. [[Przepisy na rozszerzenia]] mają podsumowanie tygodnia pisane przez model, a [[API rozszerzeń]] wymienia resztę tego, co Twój kod może wywołać.
