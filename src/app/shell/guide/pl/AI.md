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
> Subskrypcja Claude Pro/Max albo ChatGPT Plus działa tylko w aplikacjach tych firm; inne programy używają klucza API, rozliczanego osobno, za użycie. Pytanie o jedną notatkę kosztuje ułamek grosza.

Klucz zostaje **tylko na tym urządzeniu** (nie w notatniku, nie w synchronizacji, nie w dziennikach zdarzeń). Notatki trafiają do dostawcy tylko wtedy, gdy o to poprosisz, albo gdy zrobi to rozszerzenie zatwierdzone przez Ciebie z uprawnieniem `ai`.

## Model na tym urządzeniu
Gdy wybierzesz **Na tym urządzeniu**, model działa w Brulionie — także na telefonie — i nic, o co pytasz, nie opuszcza urządzenia. Wybierz jeden z dwóch modeli firmy Liquid AI i naciśnij **Pobierz** (najlepiej przez Wi-Fi; na telefonie zostaw Bruliona na ekranie, aż skończy — ekran sam nie zgaśnie; przerwane pobieranie zacznie się od miejsca, w którym stanęło):

| Model | Rozmiar | W czym dobry |
|---|---|---|
| **LFM3-VL 2B** | 1,2 GB | **widzi obrazki**: zdjęcia, skany, zrzuty ekranu i tekst na nich |
| **LFM3 1.7B** | 1,0 GB | tylko tekst; lepiej przeszukuje notatnik i zmienia notatki |

Czego się spodziewać:

- Jest **mały**. Streszczenie strony, pytanie o notatkę, tekst ze sfotografowanego paragonu wychodzą dobrze; długie zadanie w wielu krokach może zejść na manowce, a jego polszczyzna jest prostsza niż u Claude'a. Do poważnej pracy wybierz Claude'a.
- Jest **wolniejszy** niż model w chmurze, najbardziej na telefonie: szybki telefon odpowiada na pytanie w mniej więcej pół minuty, wolniejszy potrzebuje więcej. Najdłużej trwa pierwsze pytanie — model czyta wtedy instrukcje asystenta i zachowuje na urządzeniu to, co z nich przeczytał, na kolejne pytania. Ekran telefonu nie gaśnie, dopóki model odpowiada; zostaw Bruliona na ekranie — za inną aplikacją model pracuje wielokrotnie wolniej, a po chwili staje, dopóki nie wrócisz. Potem model zostaje w pamięci przez dziesięć minut.
- Zajmuje około **2,5 GB pamięci**, dopóki jest wczytany, i potrzebuje procesora z ostatnich lat (telefon mniej więcej od 2018 roku, komputer z AVX2).
- Zbyt długą prośbę **przycina** — długą stronę w środku, długi wynik wyszukiwania na końcu — zamiast odmawiać.

Modele należą do Liquid AI, mają własną licencję (LFM Open License v1.0) i nie są częścią Bruliona: aplikacja pobiera wybrany model z Hugging Face. **Usuń** kasuje jego pliki. Nie każde wydanie ma tego dostawcę: nie ma go wersja w przeglądarce ani część paczek na komputery.

## Asystent AI
Włącz go w **Ustawieniach → Rozszerzenia → AI → Asystent AI**. Wtedy górny pasek ma przycisk **✨** — także na telefonie — z pozycjami:

- **Zapytaj AI…** — zapytaj o cokolwiek w notatkach albo poproś, żeby coś w nich zrobiło. AI **samo przeszukuje i czyta cały notatnik** — strony, dziennik, zadania z terminami, tagi, przypomnienia — więc możesz zapytać „Co mam do zrobienia w tym tygodniu?”, „Co pisałem o wyjeździe?” albo poprosić „Dopisz mleko do listy zakupów”. **Ogląda też obrazki** w notatkach, gdy model widzi (Claude, LFM3-VL na urządzeniu): „Ile zapłaciłem za mleko? Paragon jest w Zakupach.” Strona na ekranie idzie razem z pytaniem (w dzienniku: dzisiejsza notatka).
- **Streść tę stronę** — kilka zdań i zadania ze strony.
- **Ustawienia AI…** — dostawca, klucz i model.

**Także rozszerzenia.** Poproś o nowe polecenie, widok albo reakcję — „polecenie, które policzy słowa dzisiejszej notatki” — a AI przeczyta, jak się pisze rozszerzenia, wypróbuje kod na Twoich notatkach, niczego nie zmieniając, i zapisze stronę w **Rozszerzenia/**. Potem czeka na Ciebie jak kod z innego urządzenia: przeczytaj stronę i naciśnij **Uruchom**, gdy zgadzasz się z tym, co robi (zob. [[Rozszerzenia]]).

**Zmiany tylko za Twoją zgodą.** Zanim AI dopisze coś do strony albo dnia, zapisze stronę czy odhaczy zadanie, okno pokazuje, co chce zapisać: **Zgoda**, **Zgoda na wszystkie zmiany tego pytania** albo **Nie**. Tego, na co się nie zgodzisz, AI nie zapisze — i dowie się o tym.

Odpowiedź trafia do **dzisiejszej notatki** pod `🤖 Twoje pytanie`, jej punkty jako elementy listy, z `✏️ Zmienione: [[…]]` — stronami, które AI zmieniło. Asystent pracuje narzędziami (szukanie, czytanie, zapis): na własnym serwerze wybierz model, który je obsługuje (np. Qwen 3 albo Llama 3.1); Claude je obsługuje. Gdy model odpowiada, przycisk się kręci. Te same polecenia są w palecie (Ctrl+K) jako **AI: zapytaj albo poproś…** i **AI: streść tę stronę**.

Odpowiedź zostaje w notatkach, więc przeczytasz ją później na telefonie, podlinkujesz albo poprawisz.

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

`b.ai("pytanie")` zwraca tekst; tabela dodaje `system`, `model`, `maxTokens`, `effort`, `images` (obrazki z notatnika, nazwane tak, jak osadzają je notatki: `images = { "paragon.jpg" }`) albo `messages` dla rozmowy. Gdy model pisze, Brulion działa dalej. W **konsoli Luau** spróbuj `b.ai("Ile to 2 + 2?")`. Gdy modelu nie da się zapytać — brak klucza, brak połączenia — polecenie zatrzymuje się z podaniem powodu; `pcall(b.ai, "…")` to łapie.

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
