---
tags: [documentation]
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

`b.ai("pytanie")` zwraca tekst; tabela dodaje `system`, `model`, `maxTokens`, `effort` albo `messages` dla rozmowy. Gdy model pisze, Brulion działa dalej. W **konsoli Luau** spróbuj `b.ai("Ile to 2 + 2?")`.
