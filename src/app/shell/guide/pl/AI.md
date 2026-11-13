---
tags: [dokumentacja]
aliases: [Sztuczna inteligencja, Asystent AI, Claude, ChatGPT, Ollama]
brulion: document
---
# AI

Brulion potrafi pytać model AI o Twoje notatki — tak samo na komputerze, jak na telefonie. Robi to za Ciebie **Asystent AI**, a Twoje własne [[Rozszerzenia]] mogą to robić przez `b.ai`.

## Jaki model i ile to kosztuje
Otwórz **Ustawienia → Rozszerzenia → AI** i wybierz:

- **Claude** — klucz z platform.claude.com (**API keys**). Bez podanego modelu Brulion pyta `claude-opus-5-5`; `claude-haiku-5-5` jest szybszy i dużo tańszy.
- **Zgodne z OpenAI** — OpenAI, OpenRouter albo własny serwer: Ollama lub LM Studio na Twoim komputerze (adres, np. `http://192.168.1.10:11434/v1`, i nazwa modelu). Własny serwer nie potrzebuje klucza i nic nie kosztuje; telefon dosięgnie go w domu albo przez Tailscale.

Naciśnij **Sprawdź**: model odpowie „OK” albo zobaczysz, co jest nie tak.

> [!note] Subskrypcje
> Subskrypcja Claude Pro/Max albo ChatGPT Plus działa tylko w aplikacjach tych firm; inne programy używają klucza API, rozliczanego osobno, za użycie. Pytanie o jedną notatkę kosztuje ułamek grosza.

Klucz zostaje **tylko na tym urządzeniu** (nie w notatniku, nie w synchronizacji, nie w dziennikach zdarzeń). Notatki trafiają do dostawcy tylko wtedy, gdy o to poprosisz, albo gdy zrobi to rozszerzenie zatwierdzone przez Ciebie z uprawnieniem `ai`.

## Asystent AI
Włącz go w **Ustawieniach → Rozszerzenia → AI → Asystent AI**. Wtedy górny pasek ma przycisk **✨** — także na telefonie — z pozycjami:

- **Zapytaj AI…** — zapytaj o cokolwiek w notatkach albo poproś, żeby coś w nich zrobiło. AI **samo przeszukuje i czyta cały notatnik** — strony, dziennik, zadania z terminami, tagi, przypomnienia — więc możesz zapytać „Co mam do zrobienia w tym tygodniu?”, „Co pisałem o wyjeździe?” albo poprosić „Dopisz mleko do listy zakupów”. Strona na ekranie idzie razem z pytaniem (w dzienniku: dzisiejsza notatka).
- **Streść tę stronę** — kilka zdań i zadania ze strony.
- **Ustawienia AI…** — dostawca, klucz i model.

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

`b.ai("pytanie")` zwraca tekst; tabela dodaje `system`, `model`, `maxTokens`, `effort` albo `messages` dla rozmowy. Gdy model pisze, Brulion działa dalej. W **konsoli Luau** spróbuj `b.ai("Ile to 2 + 2?")`. Gdy modelu nie da się zapytać — brak klucza, brak połączenia — polecenie zatrzymuje się z podaniem powodu; `pcall(b.ai, "…")` to łapie.

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
