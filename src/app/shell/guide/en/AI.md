---
tags: [documentation]
aliases: [Artificial intelligence, AI assistant, Claude, ChatGPT, Ollama]
brulion: document
---
# AI

Brulion can ask an AI model about your notes — on the computer and on the phone alike. The **AI assistant** does it for you, and your own [[Extensions]] can do it with `b.ai`.

## Which model, and what it costs
Open **Settings → Extensions → AI** and choose:

- **Claude** — a key from platform.claude.com (**API keys**). Without a model named, Brulion asks `claude-opus-5-5`; `claude-haiku-5-5` is faster and much cheaper.
- **OpenAI-compatible** — OpenAI, OpenRouter, or a server of your own: Ollama or LM Studio on your computer (the address, e.g. `http://192.168.1.10:11434/v1`, and the model's name). Your own server needs no key and costs nothing; the phone reaches it at home or through Tailscale.

Press **Check**: the model answers "OK", or you see what is wrong.

> [!note] Subscriptions
> A Claude Pro/Max or ChatGPT Plus subscription works only in those companies' own apps; other apps use an API key, billed separately by use. A question about one note costs a fraction of a cent.

The key stays **only on this device** (not in the notebook, not in sync, not in the logs). Notes go to the provider only when you ask, or when an extension you approved with the `ai` permission does.

## The AI assistant
Switch it on in **Settings → Extensions → AI → AI assistant**. Then, with a page open, the palette (Ctrl+K) has:

- **AI: ask about this page** — type a question; the answer goes into **today's note** under `🤖 your question ([[Page]])`, its points as items. Without a page open it is a plain question.
- **AI: summarise this page** — a few sentences and the page's tasks, also into today's note.

The answer stays with your notes, so you can read it later on the phone, link it or edit it.

## In your own extensions
An extension page with `ai` among its permissions can ask a model:

````markdown
---
tags: [extension]
permissions: [write, ai]
---
```luau
b.command("haiku", function(ctx)
    local poem = b.ai({ prompt = "Write a haiku about " .. b.today(), effort = "low" })
    b.appendToJournal(b.today(), "- " .. poem:gsub("\n", " / "))
    return poem
end, { label = "A haiku for today" })
```
````

`b.ai("question")` returns the text; a table adds `system`, `model`, `maxTokens`, `effort`, or `messages` for a conversation. While the model writes, Brulion keeps working. In the **Luau console** try `b.ai("What is 2 + 2?")`.
