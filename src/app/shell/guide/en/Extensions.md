---
tags: [documentation]
aliases: [Extension, Luau, Luau console, Scripting]
brulion: document
---
# Extensions

Brulion can run code that lives in your notebook, as Emacs runs your init file and TiddlyWiki keeps its own code in tiddlers. An **extension** is a page tagged `extension` (or `rozszerzenie`): the code in its `luau` blocks adds commands to the palette, views of its own and reactions to saves and changes. The language is Luau, a small, fast Lua with types; the code reads and writes only the open notebook, with time and memory limits, and never reaches files, the network or other apps.

This page shows the way in. [[Extension API]] lists everything the code can call, and [[Extension recipes]] has ready pieces to copy.

## Your first extension
**Settings → Extensions → New extension** (or **New extension** in the palette, Ctrl+K) writes a page into the `Extensions` folder with an example and opens it:

````markdown
---
tags: [extension]
description: My first extension.
---
```luau
b.command("hello", function(ctx)
    return "Hello! Today is " .. b.today()
end, { label = "Hello" })
```
````

Open the palette and run **Hello**: the text the function returns appears as a message at the bottom. Change the code and the page saves itself — Brulion loads it again **at once**. Everything around the code blocks (headings, notes, examples in other languages) is documentation for you.

## How it works
The page's code runs once when it is loaded, from top to bottom, and says what the extension has: `b.command` a command, `b.view` a view, `b.on` a reaction. Afterwards Brulion calls those functions — a command when you choose it, a view when it is shown. Everything the code can do with the notebook is in one table, `b`: `b.today()`, `b.pages()`, `b.read("Home")`, `b.query("type:tasks status:open")` and the rest of [[Extension API]].

Variables live only until the page is saved again or Brulion is closed. What should stay belongs in your notes.

## A command about the page on screen
A command gets `ctx`: `ctx.page` is the name of the page on screen (an empty text when there is none). What the function returns is shown as a message.

```luau
b.command("count-words", function(ctx)
    if ctx.page == "" then
        return "Open a page first."
    end
    local words = 0
    for _ in (b.read(ctx.page) or ""):gmatch("%S+") do
        words += 1
    end
    return `{ctx.page}: {words} words`
end, { label = "Count the words of this page" })
```

With `block = true` after the label, the command works on the block under the cursor: `ctx.block` has its text, its task status and the `ref` that lets the code change it, and the palette offers the command only while you are in a note.

## Asking and changing
`b.ask("…")` asks you for a line of text, `b.choose("…", { "A", "B" })` shows a list to pick from, `b.form` asks for numbers with − and +. To change notes, the page needs the permission `write`:

````markdown
---
tags: [extension]
permissions: [write]
---
```luau
b.command("note", function(ctx)
    local text = b.ask("What to note?")
    if text == "" then
        return nil
    end
    b.appendToJournal(b.today(), "- " .. os.date("%H:%M") .. " " .. text)
    return "Noted in today's journal"
end, { label = "Note with the time" })
```
````

`b.appendToJournal` adds to a day's note, `b.write` makes or replaces a page, `b.replaceBlock` changes one line — and refuses when the line has changed since the code read it, so a sync that arrives in the middle is never overwritten.

## When something goes wrong
An error names the **line of the page**: the page's corner shows "Error on line 14: …" with a button to that line, and so do **Settings → Extensions** and the **Luau console**. While you are still typing, the version before the error keeps running — half-written code never takes a working extension away. `print(…)` writes to the Luau console: the quickest way to see what your code has in hand. The messages you may meet are explained in [[Extension API#Errors]].

## Code from other devices
Pages arrive from everywhere: a sync, an import, a shared folder. So a page's code runs on a device only after you **approve** it there: the page's corner says "new or changed elsewhere — read it, then run it" with **Run**, and so does **Settings → Extensions**. What you write on this device in Brulion stays approved; a change that comes any other way waits for you again. Words around the code never need a new approval.

What an extension may do beyond reading is written on its page:

```markdown
---
tags: [extension]
permissions: [write]
---
```

`write` (`zapis`) lets it change notes, `ai` lets it ask an AI model (see [[AI]]). Approving a page approves its permissions; asking for more later needs a new approval.

## The Luau console
**Luau console** in the palette runs one line of Luau at a time with the notebook's API: `b.today()`, `#b.pages({})` (how many pages), `b.blocks({ tag = "project" })`. A value shows below; variables stay for the next line; ↑ and ↓ bring back what you typed. Whatever extensions `print()` shows up there too, with their errors. **Run Luau block** (the palette, the cursor in a `luau` block of any page) runs that block and shows the result as a message.

That works in this documentation too: put the cursor in the code of "Count the words of this page" above and choose **Run Luau block** — the command is in the palette until the extensions are loaded again. The examples of [[Extension API]] and [[Extension recipes]] can be tried the same way.

## Views and reactions
A view is a function that returns what to show, made of Brulion's own pieces (`b.ui.heading`, `text`, `link`, `button`, `check`, `stepper`, `table`, `chart`); with a `label` it is in the palette:

```luau
b.view("projects", function(params)
    local links = {}
    for _, name in b.pages({ tag = "project" }) do
        table.insert(links, b.ui.link(name))
    end
    return b.ui.column({ b.ui.heading("Projects"), b.ui.column(links) })
end, nil, { label = "Projects" })
```

A reaction runs when something happens: `b.on("save", function(e) … end)` after this device saved a page (`e.page`), `b.on("change", function(e) … end)` after notes changed anywhere (`e.pages`). One that keeps failing is switched off until the extension loads again.

## Written by AI
Ask the [[AI|AI assistant]] for one — "AI: ask or ask to do…", then "a command that counts the words of today's note". It reads how extensions are written, tries its code on your notes without changing anything, and with your yes writes the page into **Extensions/**. An agent such as Claude Code does the same through [[Command line and AI agents|the command line]]: it reads the API (`brulion-cli extension docs`), tries its code (`extension eval`, `extension run` — what the code would write comes back as a diff) and writes the page (`extension write`). Either way the code waits for you like code from another device: read the page, and press **Run** when you agree with what it does. Every later change by the AI waits again.

## The gym's own code
The [[Gym]] is an extension too. **Settings → Extensions → The gym's code → Edit the code** writes its source into a page of your notebook, one section per heading, and from then on the gym runs **from that page**: change it, save, and the change works. **Restore** puts the page in the trash and the built-in gym runs again.

## Learn more
- [[Extension API]] — every function of `b`, what a block looks like, the limits and the error messages.
- [[Extension recipes]] — commands, views and reactions to copy.
- [[AI]] — `b.ai`: asking a model from your own code.
- For those who build Brulion: `docs/EXTENSIONS.md` and `docs/SYNTAX.md` §13 in the source code.
