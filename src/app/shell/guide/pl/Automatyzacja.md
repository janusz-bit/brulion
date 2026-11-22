---
tags: [dokumentacja]
aliases: [linki brulion, Tasker, Skróty, URI]
brulion: document
---
# Automatyzacja

Brulion rozumie **linki brulion://**. Taki link otwiera stronę, dzisiejszy dziennik albo kalendarz, albo dodaje notatkę, zadanie z przypomnieniem czy nową stronę — z zakładki w przeglądarce, z launchera, ze skrótu klawiszowego pulpitu, ze skryptu albo z aplikacji do automatyzacji na telefonie (Tasker, Automate, MacroDroid). Telefon ma też **kafelki w Szybkich ustawieniach** i **skróty na ikonie aplikacji**.

## Wypróbuj
Otwórz **Ustawienia → Automatyzacja**. W części „Utwórz link” wybierz, co link ma zrobić, wypełnij pola i naciśnij **Wypróbuj** — albo **Kopiuj link** i wklej go tam, gdzie można wstawić link.

```text
brulion://daily                                  dzisiejszy dziennik
brulion://open?page=Zakupy&heading=Dzi%C5%9B       strona, przy jej nagłówku
brulion://capture?text=Pomys%C5%82%20na%20prezent  szybka notatka do dzisiejszego dziennika
brulion://task?text=Zadzwoni%C4%87%20do%20mamy&remind=jutro%20o%2018
brulion://append?page=Zakupy&heading=Dzi%C5%9B&text=Mleko
brulion://new?title=Spotkanie%20z%20Ann%C4%85&template=Spotkanie
brulion://search?query=urlop
brulion://view?name=calendar
```

Spacje i polskie litery zapisuje się jako `%20`, `%C5%BC` itd. — każda aplikacja, która tworzy linki, zrobi to za Ciebie. Znak `+` w linku to też spacja; prawdziwy plus to `%2B`.

## Co potrafi link
- **open** — strona (`page=`: jej nazwa, ścieżka, alias albo data), przy nagłówku (`heading=`), bloku (`block=`) albo wierszu (`line=`). Działa też `page=Zakupy#Dziś`, jak link w notatce.
- **daily** — dziennik danego dnia: `date=wczoraj`, `date=w przyszły piątek`, `date=2026-12-24`; bez tego dzisiejszy.
- **capture** — szybka notatka do dzisiejszego dziennika; data w tekście robi z niej zadanie z przypomnieniem, jak w szybkiej notatce aplikacji. Bez `text=` tylko otwiera okno szybkiej notatki.
- **append**, **prepend** — tekst na końcu albo na początku strony (`page=`), sekcji (`heading=`), pod blokiem (`block=`) albo w dzienniku danego dnia (`date=`; bez `page` i `date` — dzisiejszym).
- **task** — zadanie: `text=`, a do tego `due=`, `remind=`, `alarm=`, `every=` (np. `co tydzień`), `priority=` — słowami, po polsku albo po angielsku. Bez nich brana jest data z tekstu, jak w szybkiej notatce.
- **new** — nowa strona: `title=`, `folder=`, `template=` (strona z Twojego folderu szablonów), `text=`. Strona o tej nazwie nigdy nie zostanie nadpisana — po prostu się otworzy.
- **search**, **view** — szukanie albo widok: `journals`, `calendar`, `tasks`, `habits`, `graph`, `pages`, `tags`, `settings`.
- **run** — polecenie rozszerzenia, które zatwierdzono na tym urządzeniu (`extension=` i `command=`).
- **chat** — nowa rozmowa czatu AI z promptem w polu (`prompt=`, najwyżej 8000 znaków), o stronie (`page=`), z odpowiedzią agenta albo rozszerzenia ustawionego na tym urządzeniu (`partner=acp:claude`). **Nigdy nie wysyła się sama**: czytasz ją i naciskasz Wyślij. Znaki, których nie widać (o zerowej szerokości, znaczniki kierunku, znaczniki Unicode), są najpierw usuwane, a rozmowa ostrzega, że mogły ukrywać polecenia. Taki link robi **Kopiuj link z promptem** w menu wiadomości (prawy przycisk albo długie przytrzymanie) i **AI: Kopiuj link z promptem** w palecie poleceń.

Przy kilku notatnikach `vault=` wybiera jeden po nazwie albo folderze. Pełna lista parametrów jest w dokumentacji dla programistów (docs/UI.md, „Automation”).

## Bezpieczeństwo: klucz automatyzacji
Link może przyjść skądkolwiek — ze strony internetowej, z wiadomości, z notatki zsynchronizowanej od kogoś innego. Dlatego:
- Link, który **otwiera**, działa od razu: niczego nie zmienia.
- Link, który **zapisuje**, najpierw pokazuje **„Zapisać z linku?”**: gdzie i co dokładnie zapisze. Nic nie zostaje zapisane przed naciśnięciem **Zapisz**.
- Twoje własne automatyzacje pomijają to pytanie dzięki **kluczowi automatyzacji** notatnika: **Ustawienia → Automatyzacja → Utwórz klucz**, potem dopisz do linku `&key=…` (albo zaznacz „Z kluczem” w kreatorze linków). Trzymaj klucz w tajemnicy jak hasło. Zostaje na tym urządzeniu i nigdy się nie synchronizuje; **Nowy klucz** unieważnia stary.
- **Linki bez klucza** w tych samych ustawieniach: **Pytaj** (domyślnie), **Tylko otwieranie** (zapis tylko z kluczem) albo **Wyłączone**.
- Link nigdy niczego nie usuwa ani nie nadpisuje strony: tylko dodaje blok albo tworzy stronę o wolnej nazwie. Polecenie rozszerzenia działa tylko wtedy, gdy zatwierdzono to rozszerzenie. Link z promptem tylko wypełnia pole czatu.

## Na komputerze
- **Linux**: wpis Brulionu w menu przyjmuje linki brulion://, więc działa `xdg-open 'brulion://daily'`, a także skrót klawiszowy GNOME albo KDE z tym poleceniem. Uruchomiony Brulion wychodzi na wierzch i robi swoje.
- **Windows**: instalator rejestruje linki brulion://: `start brulion://daily`, okno Uruchamianie (Win+R), skrót na pulpicie z adresem brulion:// jako celem.
- **Wiersz poleceń**: `brulion-cli uri run 'brulion://append?…'` zapisuje w ogóle bez aplikacji, `brulion-cli uri open '…'` przekazuje link do uruchomionej aplikacji, `brulion-cli uri build …` napisze link za Ciebie — zobacz [[Wiersz poleceń i agenci AI]].

## Na telefonie (Android)
- **Kafelki Szybkich ustawień**: otwórz panel Szybkich ustawień, edytuj kafelki (ołówek) i przeciągnij między nie **Szybką notatkę** i **Nowe zadanie**. Stuknięcie otwiera małe okno na tym, co jest na ekranie; notatka albo zadanie trafia od razu do dzisiejszego dziennika, bez otwierania Brulionu.
- **Skróty na ikonie**: przytrzymaj ikonę Brulionu: **Szybka notatka**, **Nowe zadanie**, **Dzisiejszy dziennik**, **Szukaj** — przeciągnij któryś na ekran główny, żeby go zatrzymać.
- **Tasker, Automate, MacroDroid**: wyślij intencję z akcją `io.github.januszbit.brulion.action.RUN` do pakietu `io.github.januszbit.brulion`. Link wstaw w pole *Data* intencji, a tekst i klucz w dodatki (extras):

```text
Akcja:    io.github.januszbit.brulion.action.RUN
Data:     brulion://append?page=Zakupy&heading=Dzi%C5%9B
Extra:    text:%CLIP
Extra:    key:%BRULION_KEY
Cel:      Broadcast Receiver   (klasa io.github.januszbit.brulion.android.AutomationReceiver)
```

Z kluczem notatka zapisuje się w tle, nawet gdy Brulion jest zamknięty, a zadanie z godziną od razu dostaje przypomnienie. Brulion odpowiada rozgłoszeniem `io.github.januszbit.brulion.action.RESULT` (`ok`, `error`, a z kluczem także `path` i `line`) — zdarzenie „Intent Received” w Taskerze. Polecenie, które coś otwiera albo nie ma klucza, wymaga celu *Activity* (`io.github.januszbit.brulion.android.AutomationActivity`): Brulion wychodzi na wierzch i najpierw pyta.

## W przeglądarce
W **Ustawienia → Automatyzacja → Linki w tej przeglądarce** naciśnij **Zezwól**: linki zaczynające się od `web+brulion:` będą wtedy otwierać ten Brulion (`web+brulion://daily`). Reszta działa tak samo.
