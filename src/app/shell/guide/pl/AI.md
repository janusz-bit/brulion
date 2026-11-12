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
Włącz go w **Ustawieniach → Rozszerzenia → AI → Asystent AI**. Wtedy przy otwartej stronie paleta (Ctrl+K) ma:

- **AI: zapytaj o tę stronę** — wpisz pytanie; odpowiedź trafi do **dzisiejszej notatki** pod `🤖 Twoje pytanie ([[Strona]])`, jej punkty jako elementy listy. Bez otwartej strony to zwykłe pytanie.
- **AI: streść tę stronę** — kilka zdań i zadania ze strony, także do dzisiejszej notatki.

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
