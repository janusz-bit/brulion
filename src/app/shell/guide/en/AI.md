---
tags: [documentation]
aliases: [Artificial intelligence, AI assistant, Claude, ChatGPT, Ollama, Model on the device]
brulion: document
---
# AI

Brulion can ask an AI model about your notes — on the computer and on the phone alike. The **AI assistant** does it for you, and your own [[Extensions]] can do it with `b.ai`.

## Which model, and what it costs
Open **Settings → Extensions → AI** and choose:

- **Claude** — a key from platform.claude.com (**API keys**). Without a model named, Brulion asks `claude-opus-5-5`; `claude-haiku-5-5` is faster and much cheaper.
- **OpenAI-compatible** — OpenAI, OpenRouter, or a server of your own: Ollama or LM Studio on your computer (the address, e.g. `http://192.168.1.10:11434/v1`, and the model's name). Your own server needs no key and costs nothing; the phone reaches it at home or through Tailscale. An `http://` address is not encrypted — the settings say so: fine on your home network, anywhere else use `https://`.
- **On this device** — a small model that runs inside Brulion itself: no key, no account, no internet. See [[#A model on this device]] below.

Press **Check**: the model answers "OK", or you see what is wrong.

> [!note] Subscriptions
> A Claude Pro/Max or ChatGPT Plus subscription works only in those companies' own apps; other apps use an API key, billed separately by use. A question about one note costs a fraction of a cent. With a subscription, let those apps reach your notes instead: [[Command line and AI agents#Your notes in claude.ai and ChatGPT]].

The key stays **only on this device** (not in the notebook, not in sync, not in the logs). Notes go to the provider only when you ask, or when an extension you approved with the `ai` permission does.

## A model on this device
With **On this device** chosen, the model runs in Brulion — on the phone too — and nothing you ask leaves the device. Choose one of three models — Brulion's own, made for the assistant (the default), and two by Liquid AI: LFM3 1.7B, which it was made from, and LFM3-VL 2B — and press **Download** (best over Wi-Fi; on a phone keep Brulion on the screen until it is done — the screen stays on by itself; a download that stops goes on from where it was):

| Model | Size | Good at |
|---|---|---|
| **Brulion Assistant 1.7B** ("Brulion" in the settings; the default) | 1.1 GB | text only; LFM3 1.7B trained for the **AI assistant** — the best one at working through your notebook with it; answers at once, without thinking; shares its big file with LFM3 1.7B (with that one here, 92 MB more) |
| **LFM3 1.7B** | 1.0 GB | text only; searches the notebook and changes notes better than LFM3-VL 2B |
| **LFM3-VL 2B** | 1.2 GB | **sees pictures**: photos, scans, screenshots, and the text on them |

What to expect:

- It is **small**. A summary of a page, a question about a note, the text of a photographed receipt go well; a long task with many steps may go astray, and its Polish is plainer than Claude's. For serious work choose Claude.
- It is **slower** than a model in the cloud, most of all on a phone: a fast phone writes some 15–18 tokens (pieces of words) a second and starts showing its thoughts within seconds, but a question that needs a search of the notes takes it a minute or more, a slower phone longer. A phone that got hot (long use, charging) slows down until it cools. The very first question is the slowest — the model reads the assistant's instructions then, and keeps what it read of them on the device for the questions after. The phone's screen stays on while it answers; keep Brulion on the screen — behind another app the model works many times slower and after a while stops until you come back. The model then stays in memory for ten minutes.
- It takes about **2.5 GB of memory** while it is loaded, and needs a processor from the last years (a phone from about 2018 on, a computer with AVX2).
- A request too long for it is **cut** — a long page in its middle, a long search result at its end — rather than refused.

The models are Liquid AI's, under their own licence (LFM Open License v1.0), and are not part of Brulion: the app downloads the one you choose from Hugging Face. Brulion Assistant 1.7B is LFM3 1.7B trained further by Brulion, under the same licence — Liquid AI neither made nor endorses it. **Remove** deletes its files (a file another downloaded model needs stays). Not every build has this provider: the browser version and some desktop packages do not.

## The AI assistant
Switch it on in **Settings → Extensions → AI → AI assistant**. You talk to it in the **AI chat**, as in a messenger: your messages on the right, the AI's answers on the left, and you can go on asking — "And next week?", "Move the rent to Friday". The top bar has a **✨** button — on the phone too — with:

- **New conversation** — ask anything about your notes, or ask it to do something in them. The page on screen goes along (in the journal: today's note); before you send the first message you can take it away (× on its chip). Suggestions get you started: "What do I have to do this week?", "Plan my tomorrow".
- **New conversation beside the page** — the same in the side panel, next to the page you are reading (on a wide window).
- **Conversations** — every conversation you had, the newest first.
- **Summarise this page** — a new conversation that asks for a summary of the page and its tasks; ask more about it there.
- **AI settings…** — the provider, the key and the model.

The AI **searches and reads the whole notebook** by itself — pages, the journal, tasks with their dates, tags, reminders — so you can ask "What do I have to do this week?", "What did I write about the trip?" or "Add milk to my shopping list". You **see the answer as it is written**, word by word, and before it what the model is thinking (in small grey letters — the model on the device thinks in English; Brulion Assistant 1.7B answers without thinking). While it searches or reads it says so: "Searching the notes: “trip”…", "Reading “Shopping”…". **Stop** ends it at once — the model stops writing too, so your next question does not wait. It also **looks at pictures** in your notes when the model can see (Claude, LFM3-VL on the device): "What did I pay for milk? The receipt is in Shopping." Brulion Assistant 1.7B and LFM3 1.7B read text only: with LFM3-VL 2B downloaded too, Brulion loads it for a picture in their place (a few seconds more) and goes on with the chosen model afterwards — one model in memory at a time, so no more of it. The same is in the palette (Ctrl+K) as **AI: new conversation** and **AI chat**.

**Extensions, too.** Ask for a new command, view or reaction — "a command that counts the words of today's note" — and the AI reads how extensions are written, tries its code on your notes without changing anything, and writes the page into **Extensions/**. It then waits for you like code from another device: read the page and press **Read and run** when you agree with what it does (see [[Extensions]]).

**Changes only with your yes.** Before the AI adds to a page or a day, writes a page or ticks off a task, the conversation shows what it would write: **Allow**, **Allow every change of this question** or **No**. What you decline is not written, and the AI is told so. Its answer then names the pages it changed: `✏️ Changed: [[…]]`.

**Every conversation is a page** in the folder **AI/**, named after your first question — `AI/2026-11-15 What do I have to do this week` — and the day's journal gets a line that links to it. So you can read it later on the phone, search it, link it or edit it; the chat goes on from what the page says. Its tasks stay out of your task lists and a reminder in an answer never rings (the page has `tasks: ignore`). **Open the conversation as a page** (in the chat) shows it in the editor.

The assistant works with tools (searching, reading, writing): on a server of your own pick a model that supports them (e.g. Qwen 3 or Llama 3.1); Claude does. While the model answers, the ✨ button spins. One answer is written at a time; it goes on while you look at other pages and lands in its conversation.

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

`b.ai("question")` returns the text; a table adds `system`, `model`, `maxTokens`, `effort`, `images` (pictures of the notebook, named as notes embed them: `images = { "receipt.jpg" }`) or `messages` for a conversation. While the model writes, Brulion keeps working. In the **Luau console** try `b.ai("What is 2 + 2?")`. When the model cannot be asked — no key, no connection — the command stops with the reason; `pcall(b.ai, "…")` catches it.

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
