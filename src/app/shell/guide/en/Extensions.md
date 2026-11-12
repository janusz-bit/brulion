---
tags: [documentation]
aliases: [Extension, Luau, Luau console, Scripting]
brulion: document
---
# Extensions

Brulion can run code that lives in your notebook, as Emacs runs your init file and TiddlyWiki keeps its own code in tiddlers. An **extension** is a page tagged `extension` (or `rozszerzenie`): the code in its `luau` blocks adds commands to the palette, views of its own and reactions to saves and changes. The language is Luau, a small, fast Lua with types; the code reads and writes only the open notebook, with time and memory limits, and never reaches files, the network or other apps.

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

Open the palette and run **Hello**: the message appears at the bottom. Change the code and the page saves itself — Brulion loads it again **at once**. Everything around the code blocks (headings, notes, examples in other languages) is documentation for you.

## When something goes wrong
An error names the **line of the page**: the page's corner shows "Error on line 14: …" with a button to that line, and so do **Settings → Extensions** and the **Luau console**. While you are still typing, the version before the error keeps running — half-written code never takes a working extension away.

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

## The gym's own code
The [[Gym]] is an extension too. **Settings → Extensions → The gym's code → Edit the code** writes its source into a page of your notebook, one section per heading, and from then on the gym runs **from that page**: change it, save, and the change works. **Restore** puts the page in the trash and the built-in gym runs again.

## Learn more
The whole `b` API, the limits and the rules of approval are in Brulion's developer documentation: `docs/EXTENSIONS.md` in the source code. The format of an extension page: `docs/SYNTAX.md` §13.
