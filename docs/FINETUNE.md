# The assistant's bench and a model of its own

The built-in AI assistant (`src/core/extensions/builtin/asystent.luau`, docs/EXTENSIONS.md "AI")
works well with Claude and poorly with the small models on the device (LFM3 1.7B, LFM3-VL 2B): they
pick the wrong tool, go round in circles, lose a task's date. The aim is a version of LFM3 1.7B
trained on the assistant's own work — published on Hugging Face (a LoRA adapter the app adds to
LFM3-1.7B-QAD-Q4_0.gguf, the tasks) and offered among the app's models. The first step is a way to
measure it: the **bench**, tasks whose outcome is known, run by the real assistant on notebooks of
their own.

| Where | What |
|---|---|
| `finetune/notebooks/<name>/` | the notebooks the tasks run on: Markdown files, as a notebook folder holds them |
| `finetune/tasks/<name>.json` | the tasks of a notebook: the user's messages, what must come of them, one way of doing it |
| `finetune/generate.py` | made-up notebooks and their tasks, as many as wanted (below, "Generated tasks") |
| `finetune/sft.py` | the model trained (LoRA) on the passed attempts (below, "Training") |
| `finetune/grpo.py` | GRPO on the solutions' steps without thinking: a reward for the right call or answer (below, "Training") |
| `finetune/export.py` | a trained adapter as the files to publish: the merged weights, GGUF files, the adapter for llama.cpp |
| `finetune/combine.py` | adapters trained one over another (SFT, then GRPO) as one adapter, the very same sum |
| `finetune/publish.py`, `finetune/cards/` | the Hugging Face repositories of the model and the tasks as local folders, their cards (below, "Publishing") |
| `tests/assistantbench/bench.h` | reading tasks, running one, checking its outcome (library `brulion_assistantbench`) |
| `tests/assistantbench/server.h` | the model's arithmetic on a llama-server (a GPU, many tasks at once) |
| `tests/assistantbench/training.h` | the training data of a passed run; a solution's turns as the model would write them |
| `tests/assistantbench/main.cpp` | `brulion-assistantbench`: the tasks run with the model, on the device's runner or a llama-server |
| `tests/assistantbench/tst_assistantbench.cpp` | every task done by its own solution with a grounded answer, the checks themselves, the vocabulary, the training sequences, a solution written as the model writes |
| `tests/assistantbench/tst_generated.sh` | the generated tasks done by their solutions with grounded answers, the same for the same seed |

Only made-up notebooks go here — never real notes or conversations: the tasks are meant to be
published with the model. The 88 tasks written by hand are for measuring only: training never sees
them, nor their notebooks. The first 42 are on `dom-pl` (22, a household, in Polish) and `work-en`
(20, a freelancer's projects); on 42 tasks two models a task or two apart differ by chance, so 46
more came on 2026-11-18, on two notebooks of their own: `studia-pl` (23, a student's timetable,
deadlines, group project and budget, in Polish) and `home-en` (23, a translator's clients and
invoices, a trip, a garden). The results below that say "42 hand-written" are of the first two;
`--filter '^(dom-pl|work-en)/'` measures those again.

## How a task runs

`assistantbench::run()` copies the task's notebook into an empty folder, opens it as the app does
(a `Vault`, its `VaultIndex` built from every note, the `NotebookHost` with the task's clock and
language, the index updated after each write) and loads the built-in assistant in the Luau engine.
It then runs the assistant's `chat` command with the task's conversation, as the AI chat would:

* every `b.ai` request goes, through `ai::requestFrom()`, to a **responder**: the model on the device
  (`brulion-assistantbench`, `ai::answerLocally()` — the request cut to fit, the model's own
  sampling, exactly as the app answers) or the task's own solution (`SolutionResponder`);
* every change the assistant asks to make (`b.choose`: yes, yes to all, no) gets the task's answer
  (`answers`, "yes" when the list runs out — so a change tried again after a "no" is made, and
  fails the task);
* any other question fails the run.

What comes out (`Outcome`): the assistant's answer (the model's last words, without the "✏️ Changed"
line the app adds; when the assistant ended the loop itself — the model only repeating a change it
had made, docs/EXTENSIONS.md "AI" — its message, "Gotowe." / "Done."), every note before and after, the tools called, the tool results that were
errors (a declined change is not one; a tool that does not exist is counted apart), and per request
the tokens read, kept from before and written, with their times. `check()` compares that with the
task.

## A task

A task file is a JSON object; `notebook`, `language` and `now` are shared by its tasks (each may
give its own):

```json
{
  "notebook": "dom-pl",
  "language": "pl",
  "now": "2026-11-16T09:30",
  "tasks": [
    {
      "id": "dom-pl/opony-zrobione",
      "category": "update",
      "ask": "Opony już wymienione, odhacz to.",
      "expect": {
        "files": [{ "path": "Samochód.md", "matches": ["- \\[x\\] Wymienić opony na zimowe"], "contains": ["#auto", "📅 2026-11-22"] }]
      },
      "solution": [
        { "call": "search_notes", "input": { "query": "opony" } },
        { "call": "update_block", "input": { "ref": "{{ref Samochód.md#L6}}", "text": "Wymienić opony na zimowe 📅 2026-11-22 #auto", "status": "done" } },
        { "answer": "Odhaczyłam wymianę opon na zimowe w [[Samochód]]." }
      ]
    }
  ]
}
```

| Key | Meaning |
|---|---|
| `id` | unique among all tasks: `<notebook>/<name>` |
| `category` | `search`, `query`, `read`, `append`, `journal`, `update`, `write`, `multi` (several steps or a sum), `no_tool` (no tool needed), `clarify` (the assistant must ask), `decline` (the user says no) |
| `notebook`, `language`, `now` | the notebook's folder; `pl` or `en` (the notebook's and the app's language: the assistant's instructions follow it); the clock, local wall-clock time |
| `ask` / `messages` | the user's message, or a conversation (`{role, content}`, the user's last) |
| `page` | the page the conversation is about (its text goes along, as when the chat is opened from a page) |
| `answers` | the answers to the changes asked for, in order: `yes`, `all`, `no` |
| `expect.files` | notes that must exist afterwards, each with text checks; `path` may be a list — one of them must pass (a reminder for tomorrow may go to today's journal or tomorrow's) |
| `expect.onlyListedChanges` | no other note may be created, changed or removed (default `true`): a question changes nothing |
| `expect.answer` | text checks of the assistant's answer |
| `expect.toolsCalled`, `toolsNotCalled`, `noTools` | tools that must, must not be called; no tool at all |
| `solution` | the model's turns: `{"call": name, "input": {…}}`, `{"calls": [{name, input}, …]}` or `{"answer": "…"}` (last), each with `thoughts` if wanted; a call with `unlessShown` (a text) is made only when no tool result before shows that text — a page read for a phone number the search may have shown already; `alternatives` (calls in the form of a step's: `[{"call": "list_pages", "input": {}}]`) names other calls as right at that step — a retry after a search that found nothing —, never played, for a reward that takes either |
| `note` | why the task is as it is (not read) |

Whatever the task, a note that gets the same line twice fails ("Zakupy.md: “- mleko” added twice"):
the change was made again. So does an answer untrue to what the run changed: a change claimed when no
note changed (the first person of a verb of changing — "Zapisałem w dzienniku…" for a draft asked
for, "Odhaczyłam…" with no call, "I've added" —, not "nie dodałam"); the run's own change said to
have been there already when the answer never says it is made ("was already marked done — nothing
left to check off", "Nie trzeba nic zmieniać"; "jest teraz odhaczone", "Done", "Marked …" say it is);
and a fact only the system text holds — its example, the journal "named by its date (2026-11-12)" —
given as the notebook's.

Text checks compare words folded — letter case and diacritics ignored, "Żółw" holds "zolw":
`contains` (every one), `anyOf` (of each group at least one: `[["2026-12-31", "31 grudnia"]]`),
`notContains` (none), and `matches` (every one matches) and `notMatches` (none matches) — regular
expressions over the text as written, case-insensitive, `^` and `$` at every line. `notMatches`
is for a word allowed in one sense only: the unpaid invoices may name 2026-031 as the paid one,
so the check fails on a sentence (or a table's line) naming it without saying it is paid ("is
marked as paid", "has been paid", "(paid)", the row's "| yes"; "not yet paid", "to be paid",
"none of them paid" say no such thing); an unpaid one may be named by its amount instead of its
number. A reminder's check names its words and its ⏰ on one line (`^(?=.*mam).*⏰ 2026-11-17 18:00`):
a bare "⏰ …" line rings with no text.

**Grounded answers.** A solution's answer states only what the run showed the model: every fact of
it — a number of three digits or more (a phone number, an amount, a code, a year), a date, a time
("09:00", "4 pm") — must be in the conversation's messages (the open page), a tool's result or the
model's own calls before (a reminder's day it worked out) — not in the system text, the same in every
conversation, whose date (2026-11-12) is an example —, however written
there ("4 500" for "4500", "+49 30 5557 1203" for "5557 1203", "16:00" for "4 pm", every kind of
space and hyphen). Of the day line the app writes first — the first message's first paragraph,
"Today is Wednesday, 2026-11-11." — only today's date counts. The app sends no calendar of the days
to come (it did for some hours of 2026-11-18: below, "The assistant of 2026-11-18"); were one there,
its dates would ground nothing — it names every day, the right one too, so a reminder's day said
wrong would pass. A full hour is also the bare hour of a note ("14:00", "2 pm" for "między 12 a 14",
"between 12 and 2 pm", "po 16", "at 10": after między/between/od/from, the second of "X a/and/do/to/i
Y", after po/przed/o/około/after/before/at/around/until/till/by — as that hour or twelve later). A
number right after "=" is the answer's own when the arithmetic just before it gives it ("8 × 60 =
480 minut, plus 30 = 510"; numbers joined by + − × x * · / ÷ or plus, minus, razy, times, short
words and , £ € $ % between them; to the cent) — its parts are facts like any other, so "2 × 1000
= 2000" needs the 1000 read. A task that works its answer out (`multi`, `no_tool`) may also state
the values its answer checks ask for — the sum of the week, the amount for twelve people. Where the
bench plays solutions (`tst_assistantbench`, `--solution`, `tst_assistantbench_generated`), and for
the model's own answers of `--guided`, an ungrounded fact fails the attempt — it is no training
data: "answer: “07700 900666” is in nothing the model read". The rule came from training: the
generator's solutions for a contact with a page of its own answered with the phone after a search
that showed only the line with the role, and every model trained on them learnt to make numbers up
(66 of 1 967 solutions of the training notebooks of 2026-11-17). A model's answers when measuring
are checked too but not counted — shown as "(not counted) answer: …", `ungrounded` in `--out`, a
count in the summary: most such facts of the passes of 2026-11-18 were right, only worked out or
written otherwise (the next bin day, "the coming week through 2027-01-27", "12–14", "16.11.2026");
some were made up (a recipe's amounts never read, "(2026-09-08?)").

A solution argument `{{ref Samochód.md#L6}}` becomes the block's ref as a tool result of the
conversation gave it (`Samochód.md#L6@1a2b3c4d`): `b.replaceBlock` takes only refs with the line's
hash, which a solution — like the model — learns from a search. A solution that never searched
keeps the placeholder, and the tool refuses it.

Writing tasks:

* check the outcome, not the way: the note and its line, the facts in the answer; name a tool only
  when using another would be wrong (`write_page` over a list that should be added to);
* leave room for right answers worded otherwise — `anyOf` for dates and numbers ("2026-12-31",
  "31 grudnia"), `matches` with `(^|[^0-9])5([^0-9]|$)` for a number that "50" must not satisfy;
* about one task in seven needs no tool or must ask: a model trained only on tool calls calls one
  for everything;
* every task gets a solution, and `tst_assistantbench` plays them all: a task whose own solution
  fails is wrong, not the model — or answers with a fact its calls never showed: read the page
  (`unlessShown` the fact, when a search may show it already).

## Generated tasks

`finetune/generate.py` (Python 3, the standard library only) makes notebooks of made-up facts —
contacts, a home page with its codes and chores, a car, a project's tasks, a shopping list,
recipes, a reading list, journal days with shopping, runs and visits — and some forty tasks on
each, every category of the table above. A task knows its outcome because the generator wrote
the facts: the phone number the answer must give, the task line that must end up `- [x]` with its
date and tag, the sum of the week's shopping. Its solution is played by the bench like a
hand-written one, so a generated task that its own solution cannot do, or whose answer is not
grounded, fails `tst_assistantbench_generated` (bash and Python: the Nix check has both). A
contact with a page of its own is searched, then its page read unless the search showed the phone
or the hours already (`unlessShown`).

```sh
python3 -I finetune/generate.py --out ~/.cache/brulion-finetune/data/train --split train --count 24 --seed 1
python3 -I finetune/generate.py --out ~/.cache/brulion-finetune/data/test --split test --count 6 --seed 1001
```

Half the notebooks are Polish, half English; the same seed makes the same notebook. The split
`test` is for measuring the trained model: other seeds (other notebooks, other facts), and of a
task's ways of asking — three or more each — the last one is kept for it, so the test asks in
words training never saw. About one task in eight needs no tool or must ask back.

The split `train` has some seven tasks more per notebook, drawn by a random generator of their own
after the others, so a seed's notebook and its other tasks stay the same and the test split never
has them:

* **other words** (`inne-slowa`, `other-words`, three per notebook): a fact asked for in words no
  note has — "the keypad combination", "the nanny", "śmieciarka", "stomatolog" (never the test's
  own "flat", "kosze", "drzwi na dole"). The solution searches the user's words and finds nothing
  (the generator checks it as the search matches: exactly, by prefix, with a typo or two), then
  searches the note's word, or lists the pages and reads the page the fact is on (the home page,
  the car's, the contacts), then answers — the retry step names the other retry in `alternatives`.
  No other solution ever retries after a search that found nothing;
* a thing bought and a task started in other words ("Picked up the ham — cross it off", "Biorę się
  za…"), a book onto the reading list called otherwise ("moja lista lektur", "my to-read list"),
  the book being read asked so that the tasks in progress answer it, a work task among them.

The train split's reminders, moves and tasks onto a page also check the answer's day (the test
keeps its checks): it names the day the solution works out ("pojutrze", "o tydzień", "on Friday")
and no other date written as one, but today's and the day a task moves from.

## Running it

```sh
nix develop -c bash -c 'BRULION_BUILD_LOCKED=1 JOBS=16 scripts/dev-build.sh -t tst_assistantbench tst_assistantbench brulion-assistantbench'
build/tests/assistantbench/brulion-assistantbench --solution
build/tests/assistantbench/brulion-assistantbench --model ~/.cache/brulion-models/LFM3-1.7B-QAD-Q4_0.gguf \
    --out results.jsonl --traces traces.jsonl
```

It runs from the checkout (`--tasks`, `--notebooks` name other folders), opens the model with the
plugin this build made (`--plugin-dir`), as the model `--model-id` of the app (its chat format and
sampling; default `lfm3-1.7b`), holding `--context` tokens (default 16 384, what the app holds on every device). `--filter <regex>` picks tasks by id, `--verbose` shows every answer.
`--assistant <file.luau>` runs another version of the assistant than the built-in one — the one
training data was made with (`git show <commit>:src/core/extensions/builtin/asystent.luau`), to
measure a change of the app on the same model or solutions.

**On a GPU**, many tasks at once: llama.cpp's own server, of the release the app pins, with a
context of `--context` tokens per slot and control tokens written as text (`--special`, so that a
turn ends where the app's runner ends it):

```sh
llama-server -m ~/.cache/brulion-models/LFM3-1.7B-QAD-Q4_0.gguf -c 262144 -np 16 -ngl 99 -fa on \
    --special --no-context-shift --host 127.0.0.1 --port 8091
build/tests/assistantbench/brulion-assistantbench --server http://127.0.0.1:8091 \
    --model ~/.cache/brulion-models/LFM3-1.7B-QAD-Q4_0.gguf --jobs 16 --out results.jsonl
```

Only the arithmetic moves there (`ServerRunner`, an `ai::LocalRunner`): `ai::answerLocally` still
writes the prompt, cuts a request that is too long and reads the answer, as in the app. The runner
turns the prompt's pieces into tokens as the app's does — `--model` is the same file, read for its
vocabulary: a control piece is its one token, a text is cut after the first letter of every place
spelling a control token (llama.cpp reads the tool tags even inside plain text) — and asks for the
app's sampling only (repetition penalty over 64 tokens, top-k, temperature, the seed), computed on
the GPU. The first requests of the 42 tasks have the same number of tokens as on the device. On the
RTX 5060 Laptop (8 GB) the 42 tasks take 2½ minutes instead of 28; the answers differ from the
device's as two processors' arithmetic does (28 and 29 of 42 passed).

**Attempts**: `--attempts N` runs each task N times, the seeds `--seed`, `--seed`+1… (default the
app's), at `--temperature` (default the app's 0.1); the summary adds the tasks done at least once.
`--training <file>` writes the passed attempts as training data (below) — not one the assistant
stepped in (`assisted`: it ended the model's loop, or asked again for an answer that said a change
was made when none was), whose turns hold what it cut short.
Each task prints PASS or FAIL with what it missed, then a summary by language and category: the
share of tasks done, requests per task, failed tool calls, the context reached at the end, tokens
written and the time. The exit code is 0 only when every task passed.

`--out` writes a line of JSON per task: `id`, `category`, `language`, `model`, `passed`, `failures`,
`ungrounded` (a model's facts the run never showed, not counted), `said`, `tools`, `toolErrors`, `unknownTools`, `toolFailures`, `confirmations`, `changed` (path →
text after, `null` for a removed note), `rounds` (per request: calls, stop reason, error, tokens
read, kept, written, times) and `ms`. `--traces` writes the conversation of each task — its last
request as `b.ai` had it (system text, tools, messages with the model's earlier turns as `raw`) and
the reply to it — the material for training data.

## Training data

Training data come from generated sets only (above, "Generated tasks"): the hand-written tasks of
`finetune/tasks` measure the assistant, and their folder's file `MEASURING-ONLY` makes `--training`
refuse them.

`--training` (with `--server`) writes a line of JSON per training sequence of every passed attempt:
`id`, `category`, `language`, `model`, `attempt`, `seed`, `temperature`, `sequence` and `segments`
— the sequence as the model read and wrote it, in order, each segment `{text, special, train,
tokens}`: a control token or a text, whether it is learnt, and its tokens. The tokens are
llama.cpp's own, of the prompt the app wrote and of the answer the model sampled — nothing is
tokenized again on the way to training, so a note's `<|im_end|>` stays letters and a Polish
letter split across two tokens stays split (`input_ids` are the segments' tokens, the labels those
of the `train` segments). Learnt are the model's own answers, each with the end of its turn
(`<|im_end|>`); an answer cut short is only read.

A run's requests make one sequence for as long as each prompt goes on from the one before — that
prompt, the answer to it and the end of the turn, token for token: the model on the device keeps
them in its memory so and reads only the rest. Where it does not, another sequence starts: the
app's prompt writes an earlier answer in other tokens than the model sampled (an emoji or a letter
taken in pieces), or a request was cut to fit. Of the passed attempts at the 42 tasks, two thirds
are one sequence. Two attempts that wrote the same sequence give it once.

**Taught**: a task the model never does by itself — asking back what "it" is, a query it never
writes right — has its solution played as the model's own turns: `--solution` with `--server`,
`--model` and `--training` writes each step as the model would have written it
(`writtenAnswer()`: the assistant's turn as `local::prompt()` writes it back into the conversation,
read back by `local::reply()` as the same answer), in the tokens of the prompts the app writes. The
generator gives every step of its solutions a short thought, in English as the model thinks ("The
user says "Tick it off.", but nothing says which task or note they mean. I should not guess or
change anything: I'll ask which one."). The server only tokenizes then: it may run on the processor.

```sh
llama-server -m ~/.cache/brulion-models/LFM3-1.7B-QAD-Q4_0.gguf -ngl 0 -c 8192 --special --port 8093
build/tests/assistantbench/brulion-assistantbench --solution --server http://127.0.0.1:8093 \
    --model ~/.cache/brulion-models/LFM3-1.7B-QAD-Q4_0.gguf --filter "$(cat never-solved.regex)" \
    --tasks train/tasks --notebooks train/notebooks --training taught.jsonl
```

**Corrected**: the states the model gets itself into — a search in other words than the note's
that finds nothing, a page read for nothing — are none of the solutions'. `--correct N` (with
`--solution --server --training`) lets the model take the first N steps itself and the solution go
on from where they led (a step of the solution the model already made, the same calls, is passed):
the model's steps are read (`Turn::learnt` false), the solution's learnt — the teacher's
corrections in the model's own states, as DAgger and "on-policy expert corrections" make them for
agents, kept only when the task passes.

## Training

`finetune/sft.py` trains a LoRA adapter on the training data, as it is: `input_ids` are the
segments' tokens, the loss is the `train` segments'. `finetune/export.py` turns an adapter into
the files to publish and to measure. Both are Python scripts with their dependencies written in
them (PEP 723: torch 2.14.1 with CUDA 13.2, transformers 5.20.2, peft 0.21.2), run by `uv`; the
base model is `LiquidAI/LFM3-1.7B` (bf16, its own model code: `trust_remote_code`).

```sh
hf download LiquidAI/LFM3-1.7B --revision 1da06bec6a819da10b54c67e5c90972b46d81871 \
    --local-dir ~/.cache/brulion-finetune/base/LFM3-1.7B
uv run finetune/sft.py --model ~/.cache/brulion-finetune/base/LFM3-1.7B --data runs/*-training.jsonl --check
LD_LIBRARY_PATH=/run/opengl-driver/lib TRITON_LIBCUDA_PATH=/run/opengl-driver/lib \
PYTORCH_CUDA_ALLOC_CONF=expandable_segments:True \
    uv run finetune/sft.py --model ~/.cache/brulion-finetune/base/LFM3-1.7B --data runs/*-training.jsonl \
    --out ~/.cache/brulion-finetune/sft-1 --chunk 256
uv run finetune/export.py --model ~/.cache/brulion-finetune/base/LFM3-1.7B \
    --adapter ~/.cache/brulion-finetune/sft-1/adapter-step282 --out ~/.cache/brulion-finetune/sft-1/export \
    --llama-cpp <the sources of llama.cpp v0.9.0>
```

(On NixOS the CUDA driver is in `/run/opengl-driver/lib`, and Triton — the model's own kernel for
its convolutions, used where no gradient is — finds it only by `TRITON_LIBCUDA_PATH`.)

* **What it learns.** `--check` first: every segment's tokens must decode to its text with the
  model's tokenizer — llama.cpp's ids and Hugging Face's are the same vocabulary. Of a task's passed
  attempts the best `--per-task` (2) are kept: with no failed tool call first, then the fewest
  tokens written — so a task solved every time weighs as much as one solved once, and the shorter
  way is learnt. An answer whose tool call failed ("No page named …", a query that does not parse)
  is read, not learnt; a change the user declined is no failure. 5 % of the tasks (by their id)
  are held out for a validation loss.
* **How.** LoRA r 16, alpha 32, dropout 0.05 on `q_proj`, `k_proj`, `v_proj`, `o_proj`, `in_proj`,
  `out_proj`, `w1`, `w2`, `w3` (16 M parameters, 0.9 %); AdamW, 2e-4, warm-up 3 %, cosine; 16
  sequences a step, one at a time, the loss averaged over their learnt tokens (`--weighting
  token`, the default; since the training without thinking `--weighting sequence`: over the
  sequences, each its tokens' mean, so a short answer weighs as much as a long one); 2 epochs. Gradient checkpointing, and the logits
  made 256 positions at a time, of the learnt ones only — 128 000 words' logits of a whole sequence
  would not fit. On the RTX 5060 Laptop (8 GB): 6.5 GB, 40 s a step, 3 h 25 min for 2 353
  sequences twice.
* **A stopped run.** Every `--save-every` steps (20) the run's state goes to `checkpoint/` in the
  output folder: the adapter, the optimizer, the schedule, the random states and the sequences
  of the epoch not yet trained on. The same command run again goes on from there as the run would
  have (the loss of the first step after it the same to the fourth digit; a checkpoint of other
  settings is refused), and the finished run removes it.
* **The files.** `export.py` merges the adapter into the weights (`hf/`: safetensors with the
  base model's tokenizer, chat template, model code and LICENSE), writes them as GGUF with the
  scripts of the llama.cpp release the app pins (BF16, then Q4_0 and Q4_K_M by `llama-quantize`)
  and the adapter alone as GGUF in f16 (`convert_lora_to_gguf.py --outtype f16`: llama.cpp has no
  fast path for bf16 on ARM, and on x86 f16 ran as fast and came closer to f32) — for
  `llama-server --lora` over LFM3-1.7B-QAD-Q4_0.gguf, the file the app has, whose 4 bits were
  trained for (below, "Training with thinking": the variants measured with the bench). Those
  weights lie on the 4-bit grid, so the adapter merged in and quantized again would mostly round
  back to them: the app adds the adapter as it computes (`LocalModel::adapter`, docs/EXTENSIONS.md
  "AI").
* **One adapter.** GRPO (below) trains a second adapter over the first one merged; the model is
  both. `combine.py` makes them one: a LoRA layer adds `alpha / r · B · A · x`, so two of the same
  scale (16 / 32 and 32 / 64: 2) are one of rank 48 and alpha 96 whose A is theirs stacked and
  whose B theirs side by side. On 24 steps of the training notebooks, greedy, the one file wrote
  what the two did, to the letter, 22 times; twice a sentence put otherwise — the sum's
  rounding in bf16, taken in another order. `export.py --adapter <the combined one>` makes its GGUF
  as any adapter's, in f16.

## Results

Measured on the 16-core x86_64 development machine (6 threads), 8 192 tokens of context, the
models' files as the app downloads them (QAD-Q4_0), 2026-11-16. The models sample as in the app
(temperature 0.1, one seed): a second run gave the same answers token for token.

| Model | Done | Polish | English | Requests per task | Failed tool calls | Context at the end | Written | Time |
|---|---|---|---|---|---|---|---|---|
| LFM3 1.7B | 28 of 42 (67 %) | 16 of 22 | 12 of 20 | 4.6 | 9 | 3 757 tokens | 53 750 tokens | 28 min |
| LFM3-VL 2B | 27 of 42 (64 %) | 13 of 22 | 14 of 20 | 4.5 | 1 | 3 558 tokens | 43 424 tokens | 24 min |

| Category | LFM3 1.7B | LFM3-VL 2B |
|---|---|---|
| search | 7 of 9 | 8 of 9 |
| query | 2 of 7 | 3 of 7 |
| read | 2 of 2 | 2 of 2 |
| multi | 3 of 3 | 2 of 3 |
| append | 4 of 4 | 2 of 4 |
| journal | 4 of 4 | 2 of 4 |
| update | 0 of 4 | 2 of 4 |
| write | 1 of 2 | 2 of 2 |
| no_tool | 2 of 3 | 2 of 3 |
| clarify | 1 of 2 | 1 of 2 |
| decline | 2 of 2 | 1 of 2 |

What goes wrong — what training has to change (eight tasks failed with both models):

* **Searching on until the context is full** (4 and 5 tasks): 7–16 rounds of searches, queries and
  pages read again, until the request no longer fits (at most 6 144 tokens of prompt in 8 192) and
  the turn ends with the error "the request is too long for the model on this device" — the user
  gets nothing.
* **Changing a task's line instead of its status**: `update_block` given a new text — "Done: Send
  the moodboard to Julia", "📅 2026-12-02: Set up the staging server", the tags and the date gone.
  Half of it was the app's: a status alone wiped the line's text, a date before a tag was lost —
  both fixed (`update_block` keeps the text, `b.replaceBlock` the dates), and `b.search` now gives
  the line with most of the words asked for.
* **The query language and the task markers**: `type:` twice in one query (refused); the query
  `{"query": "overdue"}` — JSON inside the query — and so "Is anything overdue?" answered "No" with
  the login crash a day late; tasks in progress never asked for with `status:doing`, and `- [/]`
  not read as one even with the page open.
* **Reminders as prose**: "Reminder: Call Tom on Friday, 2026-11-20 at 9 AM." instead of
  `⏰ 2026-11-20 09:00` — no alarm rings.
* **Changes nobody asked for**: an email draft appended to the journal, a task moved for "Move it to
  next week", a list rewritten with `write_page` to add two items, a declined change made anyway
  with `write_page`.
* **Made up**: "the electrician's number (the door code) is 4721"; the finished book named as the
  one being read.
* **Thinking without end**: 2 048 tokens of thoughts and no answer.

### On the GPU, after the app's fixes

The same tasks on the llama-server (above, "On a GPU"), 2026-11-17, with the app's sampling, and
the 245 generated test tasks (`--split test`, six notebooks training never sees):

| Model | 42 hand-written | 245 generated (test) | Polish | English |
|---|---|---|---|---|
| LFM3 1.7B QAD-Q4_0 (the app's) | 31 (74 %) | 189 (77 %) | 88 of 124 | 101 of 121 |
| LFM3 1.7B Q4_0 (plain, of the BF16 weights) | 31 (74 %) | 178 (73 %) | | |

Of the test's categories the weakest are asking back (1 of 6), queries (9 of 24) and questions
needing no tool (11 of 18). Of the 56 tasks failed: 22 searched until the request no longer fit,
10 thought until the 2 048 tokens of an answer ran out, 14 answered without the fact asked for,
7 changed notes they should not have.

### The assistant's words, before any training

What LFM3 is trained for (its model card and Liquid AI's fine-tuning notebooks): tools in the
system prompt as the chat template writes them — the app's prompt is that already —, the sampling
of tool use (temperature 0.1, top-k 50, repetition penalty 1.1), thinking on or off and nothing in
between (no effort levels: `enable_thinking`; the app's `effort = "low"` is off), and for an app's
own tools — a phone assistant with 108 of them — training without thinking, a call at a time. The
bench's `--effort low` measures the model without thinking; the assistant's own changes, measured
one at a time (the test set, 2026-11-17):

| The assistant | Thinking, 8 192 tokens | Thinking, 16 384 | No thinking, 8 192 |
|---|---|---|---|
| as it was | 189 (77 %) | 194 (79 %) | 117 (48 %) |
| system text as rules | 196 | | 141 |
| + statuses in words, the query in the result | 200 | | 147 |
| + the queries of the usual questions, a change is no search | 202 | 209 | 152 |
| + a reading call not run twice, the step said from the eighth | 202 (82 %) | 214 (87 %) | |

* **Rules.** The system text became a list of rules, as Liquid AI writes them: search before
  answering about the notes, answer once a result does; answer only from what the tools gave;
  ask back when the request does not say what it is about (asking back: 1 of 6 → 5–6 of 6);
  general questions without tools; which tool for which change; never say a change is made before
  its result.
* **Results a small model reads.** A query's result said a task's status as its checkbox,
  `[[Książki]], [/]: …`: the model took the tasks in progress for "unrelated" and read page after
  page until the request no longer fit. Now `task in progress`, `open task`, and the first line
  repeats the query (`2 matches of "type:tasks status:doing":`).
* **Queries.** `query_notes` begins with the queries of the usual questions (overdue, due this
  week, in progress, done this week, a tag, a page): the model wrote `due:>=… and due:<=…` and did
  not know `due:overdue`. Queries: 9 of 24 → 15 (8 192) and 20 (16 384).
* **Circles.** The same search three times, a page read again: a reading call repeated with the
  same input is not run again, its result says so; from the eighth turn on the last result says
  which turn of sixteen it is.
* **Thinking.** Without it the model writes 7 times fewer tokens (98 a task instead of 699, the
  median) — on a phone several times faster —, but it takes `append_to_journal` for a list, writes
  refs and queries wrong: 152 instead of 202.

What still fails (16 384 tokens, 31 tasks): thoughts to the 2 048 tokens of an answer (8), the
sixteen turns spent searching for a fact asked in other words than the note's — "the code of the
door downstairs", the note's "intercom code" found and judged "not the door" (8), a wrong fact or
change (15).

### Training with thinking

The model's own attempts at the 994 training tasks: 4 each at temperature 0.8 (2 850 of 3 976
passed, 828 tasks), then 8 each at 1.0 at the 166 never done (87 passed, 42 more tasks); the 124
still never done taught from their solutions — 2 493 sequences, 1.43 M tokens learnt, 2 epochs
(296 steps, 3 h 25 min). The validation loss went from 0.695 to 0.604 after the first epoch and
0.601 after the second.

| Variant | File | 42 hand-written | 245 generated (test) |
|---|---|---|---|
| epoch 1, adapter over QAD-Q4_0 | 1.05 GB + 32 MB | 33 | 188 (77 %) |
| epoch 1, merged, Q4_0 | 1.05 GB | 30 | 178 (73 %) |
| epoch 2, adapter over QAD-Q4_0 | 1.05 GB + 32 MB | 32 | 184 (75 %) |
| epoch 2, merged, Q4_0 | 1.05 GB | 30 | 176 (72 %) |
| epoch 2, merged, Q4_K_M | 1.10 GB | 33 | 191 (78 %) |

No gain beyond the noise of a run (13 tasks newly done, 14 no longer, in the first row). The
quantization trained for 4 bits is worth 11 tasks (the base model's Q4_0 does 178), so a trained
model goes over LFM3-1.7B-QAD-Q4_0.gguf as an adapter rather than quantized anew. What did not
change: the searches without end — the first query already gives the tasks asked for, and the
model asks five more ways — and the thoughts without end. What got worse: `append_to_journal` for
what is no note ("Translate into Spanish: …", a book for the reading list). Learning the model's
own passed attempts teaches mostly its thoughts (most of the tokens learnt), little of the one
choice that matters: to answer once the result is there. A second run that also learnt the
solutions of most training tasks, with the thoughts the generator writes for them to a template,
did worse — 165 and 163 of the 245 after its two epochs (29 and 34 of the 42) —: the model learnt
to state facts it had never read.

### Training without thinking, as Liquid AI does

Liquid AI's own recipe for an app's tools (its GRPO notebook: a phone assistant with 108 tools):
the model without thinking, a step at a time — the call it is to make next, or its answer —, then
reinforcement learning with a reward for the right call. The data: the assistant's prompt of
2026-11-17, 16 384 tokens, `--effort low` — the solutions of the 994 training tasks written as the
model's turns (`--solution --server --training`; its thoughts dropped) and, of the same tasks, the
solutions' calls with the model's own answers (`--guided`, 4 attempts at 0.8: 2 654 of 3 976
passed). SFT 1: 1 864 sequences, 135 000 tokens learnt (the calls and answers only), 2 epochs, 222
steps in all (2 h 10 min), validation loss 0.759 → 0.342 → 0.329; measured over LFM3-1.7B-QAD-Q4_0.gguf
as an adapter, without thinking, 16 384 tokens:

| Model | 42 hand-written | 245 generated (test) | Requests per task | Failed calls | Tokens written per task |
|---|---|---|---|---|---|
| LFM3 1.7B, thinking (the assistant of 2026-11-17) | 32 | 214 (87 %) | 4.7 | 27 | 1 300 |
| LFM3 1.7B, no thinking | 26 | 154 (63 %) | 5.2 | 88 | 184 |
| + SFT 1, epoch 1 | 34 | 198 (81 %) | 2.1 | 0 | 73 |
| + SFT 1, epoch 2 | 36 (86 %) | 197 (80 %) | 2.2 | 0 | 75 |

Asking back 6 of 6, questions without tools 17 of 18, queries 19 of 24 — and some 17 times fewer
tokens written than with thinking. What it does worse: a phone number written from memory
("07700 900 123", the number of the note right above it), a search that found nothing given up at
once ("I couldn't find…") where another word or a page would have found it, a count off by one.

`finetune/grpo.py` is the second step: the steps of the solutions as prompts (each answer of a
`--training` sequence without thinking, with everything before it); for each, 8 answers at
temperature 1.0, at most 256 tokens; the reward 1.0 for the solution's calls (the tool and each
argument compared by its kind: a page's name, a query's filters in any order, a ref exactly, the
text a change writes with some tolerance), 0.2 × the F1 of the tools' names otherwise, and for an
answer 1.0 when it calls nothing and passes the task's checks; the advantage against the group's
mean, a KL penalty (0.04) towards the model it starts from (the SFT adapter merged into the
weights: the run is a second adapter over it), LoRA r 32, lr 1e-5. On the training tasks
themselves the SFT model scores 0.99 — nothing to learn —, so GRPO runs on 48 notebooks more
(seeds 25–72, 1 985 tasks) that SFT never saw; a step draws prompts until four groups tell their
answers apart. 2 minutes a step on the RTX 5060 Laptop, 6 GB. A stopped run goes on as SFT's
does (above, "A stopped run"): every `--checkpoint-every` steps (10) GRPO's adapter, the optimizer,
the schedule and the random states the steps are drawn and the answers written with go to
`checkpoint/`.

GRPO 1 learnt little: its mean reward stayed at 0.92–0.96, about two of the twelve groups a step
told answers apart — and the reward took for the solution's call what was not: a reminder a day
off, `📅` (due) for `⏰` (reminder), even the reminder written as prose, since a change's text was
compared only as a text; and an answer with a phone number written from memory. The reward is
stricter since: in a change's text (`update_block`'s, `append_*`'s, `write_page`'s) the dates with
their markers (📅 ⏰ ⏰! ⏳ 🛫 ✅ ❌, the app's 📆 🗓 ⌛ as theirs), the other markers and the task
boxes must be the solution's, as many; its times (9:00 is 09:00, "4 pm" 16:00), other dates and
numbers each at least once, others only saying one again ("o 16" before `⏰ … 16:00`); the words
around them, wherever the date stands, keep their tolerance (`exact()`, `same_facts()`). An answer
scores 0 with a fact the model never read — the rule of the bench's `ungroundedFacts()` for the
solutions (above, "A task"): its dates, times and numbers of 3 digits or more must be in its prompt
(of the first message's day line only today's date, as in the bench; of the system turn, the app's
text and its tools, no digit: its 2026-11-12 is an example), a time on either clock or a bare hour ("między 12 a 14"), or be worked out with the working shown ("8 × 60
= 480, plus 30 = 510"), or, in a `multi` or `no_tool` task, be a value its check asks for
(`unfounded()`). It scores 0 too when it is untrue to what the calls before it changed, by the
bench's patterns (above, "A task"; `misstated()`): a change claimed when no call before it changed a
note — none was made, or each one's result was an error: declined, "Not changed: …" —
("Zapisałem w dzienniku…", "I've added", not "nie dodałam"), or the run's own change said to have
been so already without saying it is made now. A step whose solution names other calls as right (a retry by other words or by the
list of pages: "alternatives") takes those too. A solution the reward does not take is left out:
of the 3 933 steps of `taught3` of 2026-11-17 (made before the generator's solutions were
grounded) the 64 whose answer gives a phone or the hours to call that the search's line never
showed (`phone` 24, `telefon` 20, `hours` 12, `godziny` 8). GRPO 3 is the first run with this
reward. With `--rates` (a `--rate` file, made with this reward) only the steps rated 0 < p < 1 are
drawn, `--weights learnability` (the default) in proportion to p(1 − p): a step rated 0.875 — 624
of the 1 203 of `rates3b.json` — tells answers apart in a group of 8 at most two times in three,
one rated 0.5 nearly always, so they are 37 in 100 of the draws instead of half. `--self-test`
scores answers whose reward is known, `--check` (no model: `python3 -I finetune/grpo.py --check
--data … --tasks …`) those and every solution.

**SFT 2 and GRPO 2.** SFT 2, from the base model again, on more: the solutions of the training
tasks and of the 48 notebooks of GRPO 1 (seeds 25–72) as the model's turns, the guided answers,
and corrections (`--correct 1` and `2`, 2 attempts at 0.8 on seeds 25–72: SFT 1 + GRPO 1 step 50
took the first one or two steps itself) — 4 182 sequences, 259 000 tokens learnt —, stopped after
its first epoch (262 steps, 2 h 34 min; validation loss 0.918 → 0.231). In 143 sequences of
`--correct 1` the solution added again what the model had added already, and SFT 2 learnt it: in
Polish a reminder appended sixteen times. Since then a note that gets the same line twice fails
the task (above, "A task"), and the assistant does not make a change twice (docs/EXTENSIONS.md
"AI").

GRPO 2 started from SFT 2's epoch 1, on 48 notebooks more again (seeds 73–120: 1 965 tasks, the
3 933 steps of `taught3`), drawing only the steps SFT 2 does sometimes. Its first rating (`rates3.json`,
4 answers a step: always 1 687, never 93, sometimes 2 153) was biased: every step got the seeds
0..3, and seed 1 spoiled about 23 steps of 24 alike — 1 893 of the 2 153 "sometimes" were exactly
0.75. Each step's seeds are now a hash of the step; rated again with 8 answers (`rates3b.json`):
always 2 649, never 81, sometimes 1 203. GRPO 2 then took 345 s a step instead of 2 minutes — the
generation is bound by launching the GPU's work, and other builds loaded the processor — and was
stopped at step 25.

### The assistant of 2026-11-18

The assistant changed again on 2026-11-18 (docs/EXTENSIONS.md "AI"): a search shows a short page
whole under its hit and more of a long one, `query_notes` reads a query as a small model meant it,
an append to a page that does not exist is refused, a change made is not made again by the same
call, and the day line has its weekday ("Today is Wednesday, 2026-11-11."). Measured as before: on
the llama-server, 16 384 tokens, the app's sampling, over LFM3-1.7B-QAD-Q4_0.gguf (an adapter by
`--lora`), the 245 generated test tasks and the 42 hand-written. **best1** is SFT 1 + GRPO 1 (step
50) as one f16 adapter (above, "One adapter"). The rows of 2026-11-18, and the two of the assistant
of 2026-11-17 "with that day's bench" (`--assistant`), have the bench and the test set of
2026-11-18: the test made again by that day's generator, of the same seeds. The hand-written
`unpaid-invoices` got its precise check that day (above, "A task": `notMatches`), so a hand-written
result of 2026-11-18 may be one more than an older one for the same answers.

| Assistant | Model | Thinking | 245 generated (test) | 42 hand-written | Requests per task | Tokens written per task |
|---|---|---|---|---|---|---|
| 2026-11-17 | LFM3 1.7B | yes | 214 (87 %) | 32 | 4.7 | 1 300 |
| | LFM3 1.7B | no | 154 (63 %) | 26 | 5.2 | 184 |
| | SFT 1 + GRPO 1 (step 50), two adapters | no | 207 (84 %) | 35 | 2.1 | 75 |
| | SFT 2, epoch 1 | no | 201 (82 %) | 37 | 2.6 | 76 |
| | SFT 2 + GRPO 2 (step 25) | no | 200 (82 %) | — | 2.0 | 60 |
| | SFT 1 + GRPO 1 (step 50), two adapters | yes | 222 (91 %) | — | 3.5 | 728 |
| 2026-11-17, with that day's bench | best1 | yes | 220 (90 %) | 39 | 3.5 | 735 |
| | SFT 2 + GRPO 2 (step 25) | no | 200 (82 %) | 33 | 2.0 | 62 |
| 2026-11-18, before its review's fixes, with a calendar | LFM3 1.7B | yes | 214 (87 %) | 33 | 4.9 | 1 413 |
| | LFM3 1.7B | no | 149 (61 %) | 24 | 5.7 | 229 |
| | best1 | no | 198 (81 %) | 35 | 2.3 | 103 |
| 2026-11-18 | LFM3 1.7B | yes | 221 (90 %) | 35 | 4.4 | 1 296 |
| | LFM3 1.7B | no | 164 (67 %) | 27 | 5.1 | 183 |
| | best1 | no | 211 (86 %) | 38 | 2.1 | 78 |
| | SFT 2, epoch 1 | no | 205 (84 %) | 39 | 2.7 | 85 |
| | SFT 2 + GRPO 2 (step 25) | no | 213 (87 %) | 38 | 2.0 | 59 |
| | SFT 3 | no | 226 (92 %) | 39 | 2.4 | 69 |

* **The calendar.** The day's first version also wrote a calendar of the 14 days after today into
  the first message. The small model took its dates for the user's words: best1 asked back in none
  of the 6 `clarify` tasks (all 6 the day before), and "Put it where it usually goes." went into
  the journal as the calendar's first line, "- Today is Wednesday…". With the calendar every model
  did worse than without it — 214, 149, 198 against 221, 164, 211 (the review's fixes came with
  its removal, so not all of that is the calendar's) —, so the first message starts with the day
  line alone.
* **By category.** best1's searches went from 48 to 56 of 64 (more of the page under a hit), its
  changes of a task (`update`) from 27 to 25 of 34: the prompt moved under the adapter — the base
  model copies the query description's "(#name)" into `tag:#…`, 45 times where it did 15. Since
  these runs `query_notes` reads `tag:#x` as `tag:x` and a filter word it does not know as words,
  and the prompt stays as the adapters learnt it.
* **SFT 3.** From the base model, on the training data made with the day's assistant and the
  grounded generator: taught (train, train2), guided (train) and corrected (train2, by best1) —
  5 056 sequences once (one epoch, 316 steps, `--weighting sequence`), validation loss 0.144; 3 h 51
  min, the machine busy with other builds. The first model without thinking to do more than the
  base model with it: 226 of the 245 against 221. Its hand-written result is of the afternoon's
  checks (above, "A task"), 39 of the 42 and 37 of the 46 new ones; what it missed of those
  is mostly arithmetic (a sum of hours, a budget's rest), a relative day ("next week", "yesterday")
  and a word the note says otherwise ("numer indeksu" for "numer albumu").
* **On the processor.** An adapter costs time where the app runs: on x86, counted in processor
  cycles per token, a rank-48 adapter (two in one, as best1) adds 9 % to reading and 15–18 % to
  writing, one of rank 16 6.7 % and 12.5 %. It is shipped in f16: llama.cpp 0.9.0 has no fast ARM
  path for bf16. On the phone an adapter costs more (below, "GRPO 3 and the candidates to ship").

### GRPO 3 and the candidates to ship

GRPO 3 trained over SFT 3 with the strict reward on the 1 803 steps of train3 SFT 3 does only
sometimes (rated with 8 answers each: 3 021 always, 36 never): 100 steps, 8 answers a prompt, the
mean reward of ten steps from 0.83 (steps 1–10) to 0.92 (51–60) and 0.95 (91–100); its KL to SFT
3, a mean of ten steps, at most 0.0074 up to step 80, then 0.015 and 0.062 (step 96 alone 0.40);
4 h 11 min. Of its adapters (25, 50, 75, 100), measured with that morning's bench, the 75th did the
most of the two sets together — 231 of the 245 (113 of the 124 Polish ones against SFT 3's 108) and
74 of the 88 hand-written; step 100 did 231 and 73, step 50 227 and 75, step 25 226 and 74 —, and
with SFT 3 is one adapter of rank 48 (`combine.py`, 96.7 MB in f16; SFT 3 alone: rank 16, 32.2 MB).

Then every candidate again with the afternoon's bench (the assistant's new guards, the fairer
checks: above, "A task") on the test set made again with those checks (`test-a3`: the same tasks)
and all 88 hand-written tasks, one run each:

| Model | Thinking | 245 generated (test) | 88 hand-written | Requests per task | Tokens written per task |
|---|---|---|---|---|---|
| LFM3 1.7B | yes | 218 (89 %) | 74 | 4.3 | 1 310 |
| LFM3 1.7B | no | 174 (71 %) | 54 | 4.9 | 187 |
| best1 (SFT 1 + GRPO 1) | no | 214 (87 %) | 69 | 2.1 | 75 |
| SFT 2 | no | 214 (87 %) | 75 | 2.4 | 67 |
| SFT 2 + GRPO 2 (step 25) | no | 217 (89 %) | 69 | 2.1 | 63 |
| SFT 3 | no | 225 (92 %) | 76 | 2.3 | 67 |
| SFT 3 + GRPO 3 (step 75), one adapter | no | 231 (94 %) | 72 | 2.1 | 58 |
| the same, the morning's assistant | no | 229 (93 %) | 74 | 2.1 | 58 |

A run of the same model differs from another by a few hand-written tasks (the last two rows, 72
and 74: the new guards stepped in once in the 333 tasks — the llama-server's batches do not add up
the same twice; below, four runs of one model did 72 to 76), so the two candidates were run four
times each (`--attempts 4`: the app's seed and the next three, its temperature):

| Model | 245 generated (test), mean of 4 | its runs | Polish / English | 88 hand-written, mean of 4 |
|---|---|---|---|---|
| SFT 3 | 228.5 (93.3 %) | 227, 228, 229, 230 | 110.75 / 117.75 | 74.5 (84.7 %) |
| SFT 3 + GRPO 3 (step 75) | 232.25 (94.8 %) | 232, 233, 231, 233 | 113.75 / 118.5 | 74.25 (84.4 %) |

Run for run (the same seeds), GRPO 3 does 5, 5, 2 and 3 more of the generated tasks (3.75 on
average; 12 of the 15 are Polish ones) and none more of the hand-written (0, −1, +1, −1): what it
learnt is of the generator's kind. Its adapter is three times SFT 3's in size, and a rank-48
adapter costs more time than one of rank 16 (above, "On the processor"). Cut to a lower rank (the delta's singular directions, per layer), the combined adapter keeps 99.7 % of its
squared norm at rank 16 — but that is SFT 3's: what GRPO added is nearly all in the rest (the
error's squared norm, over all layers, is 97 % of GRPO's own delta's at rank 16, 37 % at 24, 18 %
at 32; in norm 99 %, 61 % and 43 %), so a smaller file of the same model there is not.

On the phone (Pixel 9 Pro, Tensor G4; `~/.cache/brulion-speed/phone/phone-speed.sh`: llama.cpp
0.9.0 built with the NDK, the CPU plugin the app takes, `android_armv8.6_1`, 4 threads on the big
cores, 965 tokens read and 128 written, 3 rounds, the order rotated, the X4 core never throttled;
the medians of the rounds, a cost the median of each round's ratio to no adapter):

| Adapter | Reading (tokens/s) | Its cost | Writing (tokens/s) | Its cost |
|---|---|---|---|---|
| none | 166.3 | | 24.7 | |
| SFT 3 (rank 16, f16) | 118.0 | 29 % | 22.6 | 8 % |
| SFT 3 + GRPO 3 (rank 48, f16) | 110.6 | 33 % | 21.2 | 14 % |

An adapter costs the phone far more of its reading than x86 (9 %) — likely because the model's
4-bit weights go through the plugin's repacked int8 kernels and the adapter's f16 ones through
plain NEON (not measured apart). SFT 3 alone reads 6.7 % and writes 6.6 % faster than the two in
one.

## What comes next

The adapter to ship is SFT 3 + GRPO 3 (step 75) as one: it is published (below, "Publishing")
and the app offers it among its models as Brulion Assistant 1.7B (`brulion-assistant-1.7b` in
`knownModels()`, src/core/ai/local.cpp: the adapter of that revision over the text model's
LFM3-1.7B-QAD-Q4_0.gguf, answering at once; docs/EXTENSIONS.md "AI"); what is still missing for
a model on the device is in TODO.md "The model on the device".

## Publishing

Published on 2026-11-18, public: the model
[janusz-bit/brulion-assistant-1.7b](https://huggingface.co/janusz-bit/brulion-assistant-1.7b)
(revision `4764e842038721cc7cc52cda67e9feff6474baa6`, the app's adapter
`brulion-assistant-1.7b-lora.gguf` in it) and the dataset
[janusz-bit/brulion-assistant-tasks](https://huggingface.co/datasets/janusz-bit/brulion-assistant-tasks)
(revision `da89019a1d3a25b54cc6a3463d800c110450a08c`), made from commit 226302bd of this
repository and uploaded with `hf upload` (huggingface_hub 2.4).

`finetune/publish.py` (the standard library only) makes the two Hugging Face repositories as local
folders, ready for `hf upload` — it uploads nothing. The model to ship is SFT 3 + GRPO 3 (step 75)
as one adapter (`combined-3`), measured four times (above, "GRPO 3 and the candidates to ship"):

```sh
F=~/.cache/brulion-finetune D=$F/data-2026-11-18 R=$F/data-2026-11-18/runs
python3 -I finetune/publish.py --owner <owner> --adapter $F/combined-3 \
    --export $F/combined-3/export --with-gguf BF16 Q4_0 Q4_K_M --with-weights \
    --eval $R/a4-final="Brulion Assistant 1.7B" --eval $R/a4-sft3="SFT stage alone (rank 16)" \
    --eval $R/a3-base-think="LFM3-1.7B, thinking" --eval $R/a3-base-nothink="LFM3-1.7B, no thinking" \
    --eval-note "…" --test $D/test-a3 --sft $D/train $D/train2 --grpo $D/train3 \
    --training $R/{taught,guided,taught2,corrected1,corrected2,taught3}-training.jsonl \
    --out ~/.cache/brulion-publish
```

* **`model/`** (`<owner>/brulion-assistant-1.7b`): the adapter as GGUF for `--lora` over
  LFM3-1.7B-QAD-Q4_0.gguf and in PEFT's format (its `base_model_name_or_path` the Hugging Face id,
  not the folder it was trained from), the base model's `LICENSE`, a `NOTICE` of what was changed
  (the LFM Open License's section 4) and `SHA256SUMS`; with `--export` (what export.py wrote) and
  `--with-gguf BF16 Q4_0 Q4_K_M`, `--with-weights` the merged files too, for other programs (the
  app runs the adapter). The GGUF files get this model's name, base model and license as metadata
  in place of the converters' (a folder's name, a local path), and the safetensors files (the PEFT
  adapter, `merged/model.safetensors`) the same in their `__metadata__`, with the notice that the
  weights were changed (the license's section 4(b)); their tensors are copied byte for byte.
* **`dataset/`** (`<owner>/brulion-assistant-tasks`): the splits `hand` (`finetune/`), `test`,
  `sft`, `grpo` as the bench reads them (`<split>/notebooks`, `<split>/tasks`), the same as JSON
  lines for the viewer (`data/`), with `--training` the training sequences. `--test`, `--sft` and
  `--grpo` each take one or more generated folders (SFT 3's data came from `train` and `train2`);
  a notebook, a task file or a task id twice is an error, and so is a training file with tasks of
  the `test` or `hand` split.
* **The cards** are `finetune/cards/model.md` and `dataset.md` (`{{name}}` filled in,
  `{{#flag}}…{{/flag}}` kept when it applies), the results tables made from the bench's
  `<prefix>-test.jsonl` and `-hand.jsonl` of each `--eval` (the first is the model; a model run
  with `--attempts N` shows a run's mean and each run's count), the training from the adapters'
  `settings.json` and `log.jsonl` (`combined-from.json` names them), the training files' table
  from their lines (kind, the split and seeds of their tasks, the stage whose settings name them).
  Each generated folder is made again with `generate.py` (`--split`, `--count`, `--seed` read from
  its notebooks' names) and compared, so the dataset card says what the generator reproduces. A
  warning when the results are of other tasks than the ones published, when a training file is
  not published or not this model's, or when the code the cards link is not committed; an error
  when a file to publish holds a local path, the git user's e-mail address or the machine's name.
  The limitations, the phone's speed and the merged files' caveats in `model.md` are this
  release's: written by hand, from the results.

The name: the license grants no use of Liquid AI's names beyond saying where the model comes from
(its section 7), so the model is "Brulion Assistant 1.7B", never "LFM-…". Commercial use is
licensed only below US $10 million of annual revenue (section 5), which the card repeats. Upload
private, check both pages, then make them public; the adapter's size and SHA-256 that publish.py
prints, with the upload's commit, are its `knownModels()` entry.
