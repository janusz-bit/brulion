---
tags: [documentation]
brulion: document
---
# Math formulas

You write formulas in a note as in LaTeX, and Brulion shows them right away as a readable formula — without the internet and without running any code. The file keeps only the LaTeX source, so the note stays plain Markdown.

## Inline
Surround a formula with `$` signs: `$E = mc^2$` looks like this: $E = mc^2$.

A formula can stand in the middle of a sentence: the area of a circle is $\pi r^2$, and the square root of two is $\sqrt{2}$.

So that amounts and variables don't turn into formulas, the opening `$` must be followed right away by a character (not a space), and it must not come right after a letter or a digit. Right before the closing `$` there must not be a space, and right after it no letter or digit. That is why `cost $5 and $10`, `Netflix 15$, Spotify 10$`, `10$/h` or `$PATH:$HOME` stay ordinary text. A formula may continue on the next line, but not across an empty line. If you want to write a dollar sign on its own, put a backslash before it: `\$100`.

Whatever starts inside a formula belongs to the formula — as in code: a `#tag`, a `[[link]]`, a task's due date or a reminder written between dollar signs doesn't get into tags, backlinks or the calendar.

A backslash before a bracket means an ordinary bracket, as in Obsidian: `\(…\)` and `\[…\]` aren't formulas (that is how imports from other apps write brackets). Write formulas between `$…$` or `$$…$$`.

## On a line of their own
A longer formula can go between double dollar signs:

$$
\frac{a + b}{2} = \frac{a}{2} + \frac{b}{2}
$$

or in a `math` block:

```math
\sum_{i=1}^{n} i = \frac{n(n+1)}{2}
```

A `math` block (also `latex` and `tex`) is edited in place: click it to see and correct the LaTeX source; Enter adds another line of the formula (handy for matrices and systems of equations). You insert it with the `/Math formula` command.

When each `$$` stands on its own line, all lines between them belong to the formula, including those starting with `+`, `-`, `>` or `1.` — they don't turn into a list or a quote, not even in an outline item. An empty line ends such a formula (LaTeX doesn't allow one either).

## What Brulion understands
- Greek letters and symbols: `\alpha`, `\pi`, `\infty`, `\leq`, `\to`, `\in`;
- subscripts and superscripts: `x_i`, `x^{n+1}`, `a_1^2`;
- fractions, roots and functions: `\frac{a}{b}`, `\sqrt{x}`, `\sqrt[3]{x}`, `\sin`, `\log`;
- large operators: `\sum_{i=1}^{n}`, `\int_0^1`, `\bigcup_i` (also with `\limits`);
- brackets sized to the formula: `\left( … \right)`, `\left\{ … \right\}`, `\big[ … \big]`;
- text in a formula: `\text{if } x > 0`;
- matrices, systems of equations and alignment: `pmatrix`, `cases`, `aligned`.

An unknown command isn't lost — it shows up as you wrote it. Formulas are read-only: the file always keeps the LaTeX, and HTML export shows the finished formula — also a `math` block in an outline or journal item.

See also [[Calculations|Calculations]] — there Brulion computes numbers instead of typesetting formulas.
