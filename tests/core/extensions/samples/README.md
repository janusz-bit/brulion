# Sample extensions

Ten extensions written the way a user (or an AI agent) writes them — pages of a notebook with
prose and ```` ```luau ```` blocks (docs/EXTENSIONS.md) —, the page of code seven of them share,
and the notebook they are tested on. `tst_extensions_samples` loads all eleven pages into one
engine and plays the user; `tst_shell_extensions` (tests/app/shell) runs them in the app's own
class, where the shared page waits for approval as any code (its users wait with it).

| Page (`extensions/`) | Language | Needs | What it shows |
|---|---|---|---|
| Licznik słów | Polish | — | read-only commands, a command on the block under the caret, a table of every page |
| Przegląd zadań | Polish | write | a view of overdue and coming tasks: checkboxes, buttons, a stepper; a stale line refused; a tick the project index hears |
| Nastrój | Polish | write | `b.form` into the journal, block properties, a chart with gaps, weekday averages |
| Wydatki | Polish and English | write | amounts with a decimal comma, months through a view's params, the notebook's currency in the page's own properties and a device's own in its settings (`b.settings`) |
| Indeks projektów | Polish | write | `change` and `save` hooks that keep a page written, only when it differs — also after another extension's write; two projects of one name by their paths |
| Pomodoro | English | write | a timer of its own (`b.timer` with its words, a `timer` hook), its length a setting, the journal, a bar chart, a node that is not always there |
| Quick entries | English | write | an extension that writes extension pages: `b.checkExtension`, then `b.write` |
| Fiszki proste | Polish | write | a review that moves through a view's params; the schedule kept in the note's own line |
| Link check | English | — | every page read in one call — by its path: two pages of one name are both read — with a time budget of its own, `b.search` |
| Action items | English | write, ai | `b.ai` with a tool, a question for every proposal, `pcall` around the model |
| Wspólne funkcje | Polish | — | the code seven of the others share (`b.require`): a page that returns a table of functions |

To try one in the app, copy its page into a notebook (the folder does not matter) and press **Run**
on the page's state line: code that arrives from elsewhere waits for approval on each device. With
the command line: `brulion-cli --vault notebook extension check "Licznik słów" --stdin < "extensions/Licznik słów.md"`.

`notebook/` is the test's notebook (today is 2026-11-14 there): projects with tasks, flashcards,
daily notes with moods, expenses and focus sessions, links that lead nowhere.
