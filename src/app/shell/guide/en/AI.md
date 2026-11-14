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
Switch it on in **Settings → Extensions → AI → AI assistant**. Then the top bar has a **✨** button — on the phone too — with:

- **Ask the AI…** — ask anything about your notes, or ask it to do something in them. The AI **searches and reads the whole notebook** by itself — pages, the journal, tasks with their dates, tags, reminders — so you can ask "What do I have to do this week?", "What did I write about the trip?" or "Add milk to my shopping list". The page on screen goes along (in the journal: today's note).
- **Summarise this page** — a few sentences and the page's tasks.
- **AI settings…** — the provider, the key and the model.

**Extensions, too.** Ask for a new command, view or reaction — "a command that counts the words of today's note" — and the AI reads how extensions are written, tries its code on your notes without changing anything, and writes the page into **Extensions/**. It then waits for you like code from another device: read the page and press **Run** when you agree with what it does (see [[Extensions]]).

**Changes only with your yes.** Before the AI adds to a page or a day, writes a page or ticks off a task, a window shows what it would write: **Allow**, **Allow every change of this question** or **No**. What you decline is not written, and the AI is told so.

The answer goes into **today's note** under `🤖 your question`, its points as items, with `✏️ Changed: [[…]]` naming the pages it changed. The assistant works with tools (searching, reading, writing): on a server of your own pick a model that supports them (e.g. Qwen 3 or Llama 3.1); Claude does. While the model answers, the button spins. The same commands are in the palette (Ctrl+K) as **AI: ask or ask to do…** and **AI: summarise this page**.

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

`b.ai("question")` returns the text; a table adds `system`, `model`, `maxTokens`, `effort`, or `messages` for a conversation. While the model writes, Brulion keeps working. In the **Luau console** try `b.ai("What is 2 + 2?")`. When the model cannot be asked — no key, no connection — the command stops with the reason; `pcall(b.ai, "…")` catches it.

## Tools: a model that looks things up
With `tools`, the model can call functions of your code — search the notes, read a page — before it answers; this is how the AI assistant works. A tool has a name, a description that says when to call it, and the shape of its input. A reply that calls tools has `toolCalls`: your code runs them and asks again, with the model's turn (`raw`, unchanged) and every result in one message, until a reply comes without calls.

```luau
local tools = {
    {
        name = "search_notes",
        description = "Full-text search over the user's notes. "
            .. "Call it before answering a question about them.",
        input = {
            type = "object",
            properties = { query = { type = "string", description = "Words to look for" } },
            required = { "query" },
        },
    },
}

b.command("ask-notes", function(ctx)
    local question = b.ask("Ask your notes")
    if question == "" then
        return nil
    end
    local messages = { { role = "user", content = question } }
    for turn = 1, 6 do
        local text, reply = b.ai({ messages = messages, tools = tools })
        if not reply.toolCalls then
            return text
        end
        table.insert(messages, { role = "assistant", raw = reply.raw })
        local results = {}
        for _, call in reply.toolCalls do
            local found = {}
            for _, hit in b.search(tostring(call.input.query), 10) do
                table.insert(found, `[[{hit.page}]]: {hit.excerpt}`)
            end
            local content = #found > 0 and table.concat(found, "\n") or "Nothing found."
            table.insert(results, { id = call.id, content = content })
        end
        table.insert(messages, { role = "user", results = results })
    end
    return "No answer after six turns."
end, { label = "Ask my notes (AI)" })
```

The page needs `permissions: [ai]`; on a server of your own, pick a model that supports tools. [[Extension recipes]] has a week's review written by a model, and [[Extension API]] lists the rest of what your code can call.
