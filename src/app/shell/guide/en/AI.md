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
> A Claude Pro/Max or ChatGPT Plus subscription works only in those companies' own apps; other apps use an API key, billed separately by use. A question about one note costs a fraction of a cent. With a subscription, let those apps reach your notes instead — on the computer **Settings → Extensions → claude.ai and ChatGPT** does it: [[Command line and AI agents#Your notes in claude.ai and ChatGPT]] — or let their own agent answer in the AI chat, on the computer or, through a computer of yours, on the phone: [[#An AI agent of your own (ACP)]].

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

The AI **searches and reads the whole notebook** by itself — pages, the journal, tasks with their dates, tags, reminders — so you can ask "What do I have to do this week?", "What did I write about the trip?" or "Add milk to my shopping list". You **see the answer as it is written**, word by word, and above it what the model is thinking, folded to one grey line — tap it to read all of it (the model on the device thinks in English; Brulion Assistant 1.7B answers without thinking; Claude's newer models send a summary of their thinking). While it searches or reads it says so: "Searching the notes: “trip”…", "Reading “Shopping”…". **Stop** ends it at once — the model stops writing too, so your next question does not wait. It also **looks at pictures** in your notes when the model can see (Claude, LFM3-VL on the device): "What did I pay for milk? The receipt is in Shopping." Brulion Assistant 1.7B and LFM3 1.7B read text only: with LFM3-VL 2B downloaded too, Brulion loads it for a picture in their place (a few seconds more) and goes on with the chosen model afterwards — one model in memory at a time, so no more of it. The same is in the palette (Ctrl+K) as **AI: new conversation** and **AI chat**.

**Extensions, too.** Ask for a new command, view or reaction — "a command that counts the words of today's note" — and the AI reads how extensions are written, tries its code on your notes without changing anything, and writes the page into **Extensions/**. It then waits for you like code from another device: read the page and press **Read and run** when you agree with what it does (see [[Extensions]]).

**Changes only with your yes.** Before the AI adds to a page or a day, writes a page or ticks off a task, the conversation shows what it would write: **Allow**, **Allow every change of this question** or **No**. What you decline is not written, and the AI is told so. Its answer then names the pages it changed: `✏️ Changed: [[…]]`.

**Every conversation is a page** in the folder **AI/**, named after your first question — `AI/2026-11-15 What do I have to do this week` — and the day's journal gets a line that links to it. So you can read it later on the phone, search it, link it or edit it; the chat goes on from what the page says. Its tasks stay out of your task lists and a reminder in an answer never rings (the page has `tasks: ignore`). **Open the conversation as a page** (in the chat) shows it in the editor.

**Charts, diagrams and formulas** in an answer are drawn as in your notes: a `chart` block under a table of the answer ([[Tables and charts]]), a `mermaid` diagram ([[Diagrams]]), a `$$…$$` formula ([[Math formulas]]). Tap one to see it large; **Copy source** — there, or in the message's menu — copies the block to paste into a note. An AI agent of your own (below) knows it may answer so: ask it for "a diagram of the process" or "a chart of these numbers".

**Pictures, the camera and your voice.** The paperclip next to the field adds pictures to your message — from a file, or on the phone **Take a photo** with the camera; you can also paste a picture (Ctrl+V) or drop it on the conversation. They are kept in the notebook's attachments folder and shown in the conversation, and the AI looks at them when its model can see (Claude, LFM3-VL on the device). The **microphone** button lets you dictate the message: on Android with the phone's own speech recognition; on the computer Brulion hears you itself, with the speech model of voice notes ([[Audio, video and voice notes]]; the first press offers its download) — press it, speak, then press it again or **Enter**, and the words go into the field where the cursor is; **Esc** throws the recording away. At most five minutes; no sound leaves the computer and the recording is deleted.

**Every message has its time** — under it, and the day above the first message of a day. **Several conversations answer at once**: ask in one and go on in another. The model on the device answers one at a time: the others wait in line, and the conversation says so ("Waiting for the model on this device…").

**Long conversations.** A small model reads only so much: when a conversation gets too long for it, the AI gets it from a later question on — whole questions and answers from the beginning are left out, never your last one — and a line in the conversation shows where it starts reading.

**Rename, pin, archive or delete** a conversation from its menu in the list (right-click, or a long press on the phone): the day stays in front of its name, **Pin** keeps it on top of the list, **Archive** puts it away into the folded **Archived** at the bottom (tap it to see them), and **Undo** in the message takes each of these back. Pinned and archived are written in the conversation's page (`pinned: true`, `archived: true`), so your other devices sort it the same way.

**What needs you comes first.** A conversation that asks you something (may it change a page, sign in) shows a question mark and what it asks; one that failed, its reason in red; one with an answer you have not seen yet, a dot — opening it reads it. They come to the top of the list. **Search** above the list finds conversations by their names and by what was said in them: the row shows the matching line, and opening it shows that message (Ctrl+F, ↑ ↓ and Enter, Alt+1…9 for the first nine, F2 to rename: [[Keyboard shortcuts#AI chat]]).

**Notifications.** When a conversation is answered, fails or asks something while Brulion is in the background, a notification says so; a click opens the conversation. **Settings → Extensions → AI → Notify when the AI answers**: **Always** (also when you are elsewhere in Brulion), **In the background** (the default) or **Never** — never for the conversation on your screen.

**Write on while it answers.** A message you send meanwhile waits under the answer and goes by itself when the answer is written — several go one after another. **Send now** (or Ctrl+Enter) stops the answer and asks that one first; **Edit** puts it back in the field, **×** takes it away. When you stop an answer the waiting messages stay, with **Resume**. **Up** in an empty field brings back your earlier questions. Type **@** and the start of a page's name (or `[[`) to name a page: tap it among the suggestions (or press Tab) — an AI agent of your own gets the pages you name with your message.

**A message's menu** (right-click, or a long press): **Quote** puts its text as a quote into the field; **Edit from here** takes your message and everything after it out of the conversation and puts the message back in the field, to ask it differently (**Undo** in the message brings it all back); **Fork from here** starts a new conversation that continues from that message — try another way without losing this one (it links back: "Forked from …").

**Report an answer.** An answer of the AI that is offensive, harmful or simply wrong: right-click it (a long press on the phone) and choose **Report response…**. Pick what is wrong with it (offensive or hateful, sexual content, violent or dangerous, harassment, false or misleading, other), add a comment if you like, and look at what the report holds: the answer — you can shorten it or take out what is private —, the model and Brulion's version and device. Your question goes along only when you tick **Add my question before it**; the rest of the conversation and your notes never do. **Show the report's text** shows it exactly as it will go. **Send report** opens it as a new issue on GitHub in your browser — issues there are public, anyone can read them — or, in a build that names an address, as an email in your email app; you send it there, Brulion sends nothing by itself. The answer is then folded to a line, "Reported response"; **Hide this response** folds it without reporting anything ("Hidden response"). **Show** opens a folded answer again, and its menu takes the mark away. The mark is kept in the conversation's page (`zgłoszone:` / `reported:`, `ukryte:` / `hidden:` in its properties), so it stays on your other devices too. The answers of **AI in the editor** have **Report response…** under them as well.

The assistant works with tools (searching, reading, writing): on a server of your own pick a model that supports them (e.g. Qwen 3 or Llama 3.1); Claude does. While the model answers, the ✨ button spins. An answer goes on while you look at other pages and lands in its conversation.

## AI in the editor
With the AI assistant on, the AI also helps you **write**. Select text (or just put the cursor in a block, or select blocks) and choose:

- **Rewrite…** — pick how: **Shorter**, **Clearer**, **Fix spelling and grammar**, **More formal**, **More casual**, **Translate to English** or **to Polish** — or say it in your own words ("as a list", "friendlier, for a letter").
- **Continue writing** — the next sentence or two after the cursor, in the note's tone.
- **Summarize** — a short summary under the text.

You find them in the menu of the selected text (right-click), the block's menu, the palette (**AI: Rewrite…** — **Ctrl+Shift+A** —, **AI: Continue writing**, **AI: Summarize**), and on the phone behind the **✨** on the editing toolbar or on the toolbar of selected blocks. Source mode works too. You see the answer as it is written and, for a rewrite, **what changes** word by word; nothing is sent until you choose, and nothing in the note changes until you press **Replace** or **Insert** — and one **Undo** takes it back. **Try again** asks for another answer.

## Your own chat partner
An extension can answer in the AI chat instead of the AI assistant: a command with `chat = true` gets the conversation (`ctx.messages`) and returns its answer. With one installed, the chat lets you choose **who answers** a conversation (above its messages, or "Answers: …" before the first one); the conversation's page remembers it.

```luau
b.command("pirate", function(ctx)
    if type(ctx.messages) ~= "table" or #ctx.messages == 0 then
        return "Talk to me in the AI chat."
    end
    return b.ai({
        system = "You are a cheerful pirate. Answer in two sentences at most.",
        messages = ctx.messages,
        maxTokens = 400,
    })
end, { label = "Pirate", chat = true })
```

## An AI agent of your own (ACP)
On the computer, the AI chat can also be answered by an AI agent you run yourself — **Claude Code**, **Codex**, **Gemini CLI**, **OpenCode** or any program that speaks the **Agent Client Protocol** (ACP), the protocol editors such as Zed use for them. The agent signs in by itself, so **your subscription works**: Claude Code uses your Claude Pro/Max login, Codex your ChatGPT login.

1. **Settings → Extensions → AI agents → Add**: Claude Code, Codex, Gemini CLI or OpenCode — started with `npx`, so Node.js must be installed — or **Another agent…** with a command of your own. **Check** starts it in the notebook's folder and tells you whether it answers, and with which model.
2. In the AI chat choose it as **who answers** a conversation.

The agent works in the notebook's folder: it reads and changes your notes through Brulion — a page it changes is in the editor at once and in the page's **history** — and runs commands whose output you see in the conversation, with its plan and each step it takes. The steps are summed up in one line — "Read 3 files · ran 2 commands" — that opens and closes their list; **Settings → Extensions → AI agents → Tool calls in chat**: **Show all**, **Hide finished** (a failed one stays) or **Hide all** (the conversation then only says the agent is working; failed ones still show). With **Brulion's tools** on (the default) it also gets `brulion-cli mcp` — search, queries, tasks, journals ([[Command line and AI agents]]). Before it changes a file or runs a command it asks — as its own settings say — and the question shows in the conversation, a file's change as a diff, with its answers (**Allow**, **Reject** …); when you are elsewhere in Brulion, a message says so, with **Open**. When it wants you to sign in, the conversation asks too; a sign-in in a terminal is named in the message (e.g. run `claude` and type `/login`). **Stop** cancels its turn.

**Approvals.** The shield chip above the conversation says how its questions are answered: **Ask first** (each one waits for you), **Allow note edits** (changes of notes go by themselves, commands still ask) or **Allow everything**. A plan it asks you to approve (Claude Code's plan mode) always waits: it shows as a readable plan with **Save the plan as a page**. After a turn that changed notes, a card lists them — **Show the change** for each, and **Undo these changes** puts them back as they were (a note you changed since stays as you left it).

Above the conversation, its **model**, **mode** (how much it may do without asking), **effort** and the rest are chips: tap one to choose — the agent remembers it for its next conversations, also before their first message. Next to them, how much of its memory the conversation uses and what it cost — and for Claude Code and Codex on this computer, how much of your **subscription's limits** is used: "5 h: 12 % · week: 68 %", amber above 80 %, red at 100 %. Hover for each window with when it resets (and a model's own weekly limit, such as Opus); a click opens them with **Refresh**. Brulion reads them from your sign-in on this computer (`~/.claude`, `~/.codex`) at most every five minutes, and only from Anthropic or OpenAI themselves; when the sign-in has expired it says how to renew it. **Settings → Extensions → AI agents** shows them on the agent's row too. Type `/` in the field for the agent's own commands (`/review`, `/compact` …). A conversation goes on in the same session of the agent even after Brulion is started again; one that changed meanwhile (written on the phone, answered by the assistant) starts a new session that gets all of it.

> [!warning] An agent is a program of yours
> It runs with your rights on this computer, and what it may do without asking is its own setting (its mode). The desktop app starts agents itself; on the phone and in the browser an agent on another computer of yours answers instead (below).

### An agent on another computer
A phone starts no programs, but a computer of yours — a laptop, a desktop, a home server — can run the agent for it, in the notebook it has open, and Brulion on the phone talks to it. Your subscription works the same way: the agent signs in on that computer.

1. **On the computer**: **Settings → Extensions → AI agents → Share agents with the phone**. Brulion runs its agents for your other devices while it runs; the line under the switch says where (e.g. `http://laptop.tail1ea20e.ts.net:8790`).
2. **On the phone**, with the same notebook (synced): **Settings → Extensions → AI agents** lists the computer's agents ("Claude Code — On laptop"); **Add** one. In the AI chat it is then among **who answers**.

That is all when both are in your **tailnet** (Tailscale, or headscale): the computer finds its name there by itself, writes it into the notebook's settings so the phone finds it, and lets **your own devices** pair without a code — Tailscale tells it who is asking. Outside a tailnet the computer uses its address in your network (not encrypted on the way: fine at home), and a phone pairs once with a code: **Pair a device → Show code** on the computer shows a QR code — scan it with the phone's camera, Brulion opens the pairing filled in, tap **Pair** (or type the address and the code under **Add → An agent on another computer…**). **Unpair all** ends every pairing.

Everything above works the same — its plan and steps, the diffs, its questions, the chips — but the files it reads and changes, the commands it runs and Brulion's tools are that **computer's**: the notebook it has open, kept in sync with the phone (Syncthing, or Nextcloud: on a server `acp serve --nextcloud …` syncs it before and after every turn). The phone may sleep and you may switch to another app while the agent works: a quiet notification ("Claude Code is answering…", with **Stop**) keeps Brulion fetching what it says, so the answer is in the conversation when you come back (allow notifications in **Settings → Extensions → AI agents** to see it). Even when the phone ends Brulion meanwhile, the agent finishes there and the computer keeps its answer: the next time Brulion starts, the conversation shows "… is still working on …" and then the answer. A **question** of the agent (may it change a file, run a command) waits there for you as well: Brulion started again shows it in the conversation, and your answer goes to the computer — only one nobody answered for an hour is declined there. `brulion-cli acp revoke` unpairs the phone at once ([[Command line and AI agents#Your agent for the phone]]).

> [!tip] A server without a screen
> On a home server without the app, `brulion-cli acp serve --agent claude --tailscale` does the same (on NixOS the service `services.brulion-acp` with `tailscale = true`): [[Command line and AI agents#Your agent for the phone]].

### Agents ask other agents
An agent answering in the AI chat on the computer can hand a task to another partner of the chat and get the answer back: Claude Code asks Codex for a second opinion, gives another agent a research subtask, has the assistant write a note. The task becomes a **sub-conversation** — an ordinary conversation in `AI/` that names the one it came from (`parent:`) — shown under its conversation as a chip with who answers and how far it got (tap it to open it; in the sub-conversation **From: …** leads back). Its questions (may it change a file?) show in it as any conversation's, with a message and **Open** when you are elsewhere.

There is nothing to set up: every agent with **Brulion's tools** gets the chat's tools for its own conversation. It can start sub-conversations of that conversation only, three levels deep at most and four running at once, and a sub-conversation never gets the tools that change the app. Scripts do the same with `brulion-cli app chat …` ([[Command line and AI agents#Conversations in the running app]]).

## Scheduled conversations
**Settings → Extensions → Scheduled conversations** sends a message to the assistant or an agent when you say — "Plan my day" every weekday at 7:30, "What changed in my notes this week?" on Fridays. **Add…**, write the message and **when** in words — `every day at 7:30`, `on mondays at 9:00`, `every 3 hours`, `tomorrow at 8` (or in Polish: `codziennie o 7:30`, `w poniedziałki o 9:00`, `co 3 godziny`, `jutro o 8`) —, choose who answers and whether each time is **a new conversation** or **the same conversation** going on. The rule is shown back in words with its first run while you type; each row says when it runs next, with **Run now**, **Edit…** and **Delete**.

They are kept **on this device**, for this notebook — never in the notebook, so a synced file can never start an agent. They run while Brulion runs (the tray keeps it running on the computer; on the phone while the app is open); one missed while Brulion was closed runs once when it starts again, not once for every time it missed. Agents and scripts manage them too: `brulion-cli app chat schedule add "Plan my day" --when "weekdays at 7:30"`.

### The other way: the assistant in your editor
Zed and the JetBrains IDEs talk to agents the same way — and Brulion's AI assistant can be one of them, with your notes and the model chosen above: `brulion-cli acp agent` ([[Command line and AI agents#Brulion's assistant in your editor]]).

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

`b.ai("question")` returns the text; a table adds `system`, `model`, `maxTokens`, `effort`, `thoughts = true` (what the model thought, in the reply's `thoughts`), `images` (pictures of the notebook, named as notes embed them: `images = { "receipt.jpg" }`) or `messages` for a conversation. While the model writes, Brulion keeps working. In the **Luau console** try `b.ai("What is 2 + 2?")`. When the model cannot be asked — no key, no connection — the command stops with the reason; `pcall(b.ai, "…")` catches it.

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
