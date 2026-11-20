---
tags: [dokumentacja]
aliases: [Dźwięk, Wideo, Notatki głosowe, Nagrania, Transkrypcja]
brulion: document
---
# Dźwięk, wideo i notatki głosowe

Nagranie albo film w notatce odtwarza się tam, gdzie jest, a notatkę głosową nagraną w Brulionie można zamienić na tekst — na urządzeniu, bez wysyłania dźwięku gdziekolwiek.

## Odtwarzacze w notatkach
Dźwięk albo film osadzasz jak obraz — wklejasz, upuszczasz na stronę albo wybierasz poleceniem `/Obraz…` / `/Plik…`: `![[Wykład.mp3]]`, `![[Wycieczka.mp4]]`. Brulion odtwarza nagrania MP3, M4A, WAV, Ogg, Opus, FLAC i 3GP oraz filmy MP4, WebM, OGV, MOV i MKV — te same formaty co Obsidian, więc te same notatki działają w obu.

Odtwarzacz ma:
- **Odtwórz / Wstrzymaj** i czas — gdzie jest i jak długie jest nagranie (wiadomo to, zanim cokolwiek naciśniesz);
- **pasek**, żeby przeskoczyć w dowolne miejsce: przeciągnij go albo stuknij tam, gdzie chcesz być;
- **prędkość** — stuknij **1×**, żeby przejść do 1,25×, 1,5×… aż do 2× i z powrotem do 0,5×;
- **dźwięk** — wyciszenie, a na komputerze obok głośność;
- przy filmie jego pierwszą klatkę, zanim ruszy, i **Pełny ekran** (`Esc` albo `F` wychodzi; `Spacja` wstrzymuje, `←` / `→` przeskakują o 5 sekund, `M` wycisza);
- **Więcej**: *Otwórz w odtwarzaczu*, *Spisz tekst*, *Kopiuj link do 1:23* (do chwili, w której jest), *Otwórz w innej aplikacji*.

**Gra jedno naraz**: uruchom drugie, a pierwsze się wstrzyma — i zapamięta, gdzie było, więc następne **Odtwórz** zacznie od tego miejsca. Nagranie **gra dalej, gdy przewiniesz dalej**, otworzysz inną stronę albo przejdziesz do kalendarza: mały pasek na dole pokazuje, co gra, z wstrzymaniem i **×** — stuknięcie w nazwę otwiera nagranie. Film gra tylko tam, gdzie go widać: przewinięty poza ekran, wstrzymuje się.

Kliknięcie pliku w drzewie, w linku `[[Wykład.mp3]]` albo *Otwórz w odtwarzaczu* otwiera **widok odtwarzacza**: film w dużym oknie, pasek z ±10 sekund, a pod nim notatki, które nagranie osadzają albo do niego linkują.

## Link do chwili
Dopisz do nazwy `#t=` i czas: `![[Wykład.mp3#t=1:23]]` uruchamia odtwarzacz od 1:23, `[[Wykład.mp3#t=83]]` otwiera widok odtwarzacza w 83. sekundzie, a `![[Wykład.mp3#t=10,20]]` gra od 0:10 i wstrzymuje się na 0:20. Działają sekundy (`#t=83`), minuty i sekundy (`#t=1:23`), godziny (`#t=1:02:03`) i `#t=1m23s` — ten sam zapis co w przeglądarkach i we wtyczkach Obsidiana do multimediów. *Więcej → Kopiuj link do 1:23* kopiuje taki link do chwili, w której jest nagranie — gotowy do wklejenia w notatki.

## Nagrywanie notatki głosowej
Wpisz w bloku **/Notatka głosowa** (albo `/nagranie`, `/dyktuj`), stuknij **mikrofon** na pasku nad klawiaturą w telefonie, użyj *Nagraj notatkę głosową* w palecie poleceń (`Ctrl+K`) albo **Notatka głosowa** w szybkiej notatce. Za pierwszym razem Brulion prosi o mikrofon (w telefonie pyta system). Nagrywanie zaczyna się od razu:
- nagrany czas i wskaźnik, jak głośno mówisz (w Androidzie nagrywa sam system, więc zamiast wskaźnika podczas nagrywania „oddycha” mikrofon);
- **Wstrzymaj** i **Wznów**;
- **Zapisz** wstawia nagranie do notatki — tam, gdzie był kursor, albo na koniec strony — a z szybkiej notatki albo z palety (poza notatką) do **dzisiejszego dziennika**;
- **Anuluj** (albo `Esc`, gest wstecz) wyrzuca je.

Nagranie to zwykły plik w folderze załączników, nazwany godziną nagrania: `Nagranie 2026-11-20 18.30.m4a`. Brulion nagrywa AAC w M4A, jeśli urządzenie to potrafi — gra wszędzie, także w Obsidianie na iPhonie — a w przeciwnym razie Opus w Ogg albo WAV. Nagranie zatrzymuje się samo po trzech godzinach.

## Zamiana mowy na tekst
Brulion potrafi spisać, co zostało powiedziane — po polsku, angielsku i w wielu innych językach — **na urządzeniu**: żaden dźwięk go nie opuszcza, a po pobraniu modelu nie trzeba konta ani internetu. Korzysta z modelu Whisper od OpenAI (licencja MIT).

1. Otwórz *Ustawienia → Rozszerzenia → Mowa* i wybierz model:
   - **Small** (252 MB) — polski rozpoznany na tyle dobrze, że wystarczy poprawić; minuta mowy w kilka sekund na komputerze; domyślny;
   - **Base** (78 MB) — szybki na każdym urządzeniu, ale po polsku niedokładny;
   - **Large Turbo** (547 MB) — najdokładniejszy, także z nazwami i liczbami; wolniejszy, w telefonie minuta mowy zajmuje chwilę.
2. Stuknij **Pobierz** (najlepiej przez Wi-Fi; możesz wyjść ze strony, a przerwane pobieranie wznowi się od miejsca, w którym stanęło).
3. Wybierz **język**: *Automatycznie* rozpoznaje go z pierwszych 30 sekund; *Polski* albo *Angielski* lepiej sprawdza się przy krótkich notatkach.

Gdy **Spisuj notatki głosowe** jest włączone (domyślnie), nagrana notatka głosowa spisuje się sama. Każde nagranie w notatce — także takie, które masz skądinąd — spiszesz z jego menu: **Więcej → Spisz tekst**. W trakcie odtwarzacz pokazuje postęp i **Zatrzymaj**; gdy skończy, tekst pojawia się **tuż pod nagraniem**, jako zwykły blok, który możesz poprawić, przenieść albo usunąć. Kilka nagrań spisuje się po kolei.

> [!tip] Gdzie to działa
> Odtwarzacze działają na komputerze (Linux, Windows) i w Androidzie; w przeglądarce nagranie albo film otwiera się w nowej karcie, gdzie odtwarza je przeglądarka. Zamiana mowy na tekst działa tam, gdzie model na urządzeniu: w pakiecie dla NixOS i w Androidzie (jeszcze nie w AppImage, w Windows ani w przeglądarce).

## W pliku
```markdown
- Wykład ![[Wykład.mp3]]
- Od ważnego fragmentu: ![[Wykład.mp3#t=12:40]]
- ![[Nagranie 2026-11-20 18.30.m4a]]
	- Jutro rano kupić mleko i chleb, potem zadzwonić do dentysty.
```
