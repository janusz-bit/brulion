---
license: {{dataset_license}}
pretty_name: Brulion assistant tasks
language:
- pl
- en
multilinguality: multilingual
size_categories:
- {{size_category}}
task_categories:
- text-generation
tags:
- tool-calling
- function-calling
- agent
- benchmark
- synthetic
- notes
- markdown
- brulion
configs:
{{configs}}
---

# Brulion assistant tasks

Tasks for the assistant of [Brulion]({{app_url}}), an offline-first Markdown notes app: made-up
notebooks — folders of Markdown notes with a daily journal, tasks, due dates, reminders, links and
tags — and requests about them whose outcome is known. `brulion-assistantbench` runs the app's
real assistant on a copy of a task's notebook and checks what came of it: the notes afterwards,
the answer's facts, the tools called. They are what
[{{model_id}}](https://huggingface.co/{{model_id}}) was trained and measured on.

Everything here is made up: no real notes, conversations or people. Names, phone numbers,
addresses and codes are invented; any likeness to real ones is chance.

## Splits

{{splits_table}}

* `hand` — written by hand (`finetune/notebooks`, `finetune/tasks` of the repository), for
  measuring only: training never saw them, nor their notebooks.
* `test` — generated, for measuring only: other seeds (other notebooks and facts) than training,
  and of each task's ways of asking the last one, which training never saw.
* `sft` — generated, the tasks supervised fine-tuning learnt from (below, "Training sequences");
  5 % of them, chosen by their id, were held out for a validation loss.
* `grpo` — generated, the tasks whose solutions' steps were GRPO's prompts.

Tasks by category:

{{categories_table}}

## Layout

```text
<split>/notebooks/<notebook>/…   a notebook's Markdown files, as the app's notebook folder holds them
<split>/tasks/<notebook>.json     the tasks on that notebook, in the bench's format
data/tasks-<split>.jsonl          the same tasks, one per line (for the viewer and `datasets`)
data/notebooks-<split>.jsonl      the notebooks' files, one per line: notebook, path, language, text
{{training_layout}}```

```python
from datasets import load_dataset
tasks = load_dataset("{{dataset_id}}", "tasks", split="test")
notebooks = load_dataset("{{dataset_id}}", "notebooks", split="test")
```

## A task

A task file holds the tasks of one notebook; `notebook`, `language` and `now` (the clock, local
wall-clock time) are shared by its tasks, each may give its own:

```json
{{task_example}}
```

| Key | Meaning |
|---|---|
| `id` | unique: `<notebook>/<name>` |
| `category` | `search`, `query`, `read`, `append`, `journal`, `update`, `write`, `multi` (several steps or a sum), `no_tool` (no tool needed), `clarify` (the assistant must ask back), `decline` (the user says no to a change) |
| `ask` / `messages` | the user's message, or a conversation (`{role, content}`, the user's last) |
| `page` | the page the conversation is about (its text goes along, as when the chat is opened from a page) |
| `answers` | the user's answers to the changes asked for, in order: `yes`, `all`, `no` |
| `expect.files` | notes that must exist afterwards, each with text checks; `path` may be a list (one of them must pass) |
| `expect.onlyListedChanges` | no other note may be created, changed or removed (default `true`) |
| `expect.answer` | text checks of the assistant's answer |
| `expect.toolsCalled`, `toolsNotCalled`, `noTools` | tools that must, must not be called; no tool at all |
| `solution` | one way of doing it, as the model's turns: `{"call": name, "input": {…}}`, `{"calls": […]}` or `{"answer": "…"}` (last), each with `thoughts` if wanted; a call with `unlessShown` is made only when no tool result before shows that text; `alternatives` names other calls as right at that step (a retry) |
| `note` | why the task is as it is |

Text checks compare words with letter case and diacritics folded ("Żółw" holds "zolw"):
`contains` (every one), `anyOf` (of each group at least one), `notContains` (none), `matches`
(every one matches) and `notMatches` (none matches) — regular expressions over the text as
written, case-insensitive, `^` and `$` at every line. Whatever the task, a note that gets the same
line twice fails, and so does an answer that claims a change no call made. A solution's
`{{ref Page.md#L6}}` is the block's ref as a search of the conversation gave it. In
`data/tasks-*.jsonl`, `expect` and `solution` are JSON texts (their shapes vary from task to
task), the rest plain columns.

## Running the tasks

```sh
git clone {{app_url}} brulion && cd brulion
hf download {{dataset_id}} --repo-type dataset --exclude "training/*" --local-dir tasks
nix develop -c scripts/dev-build.sh brulion-assistantbench
build/tests/assistantbench/brulion-assistantbench --solution \
    --tasks tasks/test/tasks --notebooks tasks/test/notebooks          # every task done by its own solution
```

The model, on a GPU through llama.cpp's server (16 384 tokens of context a slot, control tokens
written as text), without thinking (`--effort low`):

```sh
llama-server -m {{base_gguf}} --lora {{adapter_file}} -c 65536 -np 4 -ngl 99 \
    --special --no-context-shift --port 8091
build/tests/assistantbench/brulion-assistantbench --server http://127.0.0.1:8091 \
    --model {{base_gguf}} --effort low --jobs 4 \
    --tasks tasks/test/tasks --notebooks tasks/test/notebooks --out results.jsonl
```

docs/FINETUNE.md in the repository describes the bench, the task format, the runs and the
training. The generated splits come from `finetune/generate.py` (Python, the standard library
only). A notebook is named `gen-<split>-<language>-<seed>`; `--count N --seed S` makes the
notebooks of seeds S to S + N/2 − 1, a Polish and an English one each, and the same seed makes the
same notebook:

```sh
python3 -I finetune/generate.py --out test --split test --count 6 --seed 1001
```
{{#regenerate}}

The splits here, made again by the generator of the commit below:

{{seeds_note}}
{{/regenerate}}
{{#eval}}

## Results

{{eval_setup}}

{{eval_table}}

{{eval_after}}
{{/eval}}
{{#training}}

## Training sequences

`training/*.jsonl` are the bench's `--training` lines the model was trained on, in the order the
bench wrote them: per line a task's `id`, `category`, `language`, `model`, `attempt`, `seed`,
`temperature`, `sequence` (its number within the attempt) and `segments` — the sequence as the
model read and wrote it, each segment `{text, special, train, tokens}`: a control token or a text,
whether it is learnt, its tokens in LFM3's vocabulary (llama.cpp's, of the prompts the app wrote).
`model` says how a sequence was made:

* `solution` — the task's solution written as the model's turns, every step learnt;
* `guided` — the solution's calls, then the base model's own answer (LiquidAI/LFM3-1.7B, Liquid
  AI's QAD 4-bit file, without thinking), sampled; kept when the task passed;
* `corrected` — an earlier fine-tune of this project (SFT 1 + GRPO 1 in docs/FINETUNE.md) took
  the first one or two steps itself, read but not learnt, and the solution went on from where they
  led, learnt; kept when the task passed.

{{training_files}}

Supervised fine-tuning learnt the `train` segments of each task's best two attempts (no failed
tool call first, then the fewest tokens written) — not an answer whose tool call failed, nor the
tasks held out for its validation loss. GRPO learnt none of them: it took their steps as prompts
and wrote answers of its own.
{{/training}}

## License

The notebooks, the tasks and the training sequences: {{dataset_license_name}}, by {{author}}
({{year}}).{{#training}} The training sequences hold text written by models too — the base model's
answers (`guided`) and the earlier fine-tune's steps (`corrected`), both from LiquidAI/LFM3-1.7B
under the LFM Open License v1.0, which has no terms for a model's output — and token ids of its
vocabulary. Their prompts hold the app's system text and tool descriptions, which the app's
repository has under AGPL-3.0; here they are under {{dataset_license_name}} as well.{{/training}} The
generator, the bench and the app are AGPL-3.0 code in [the app's repository]({{app_url}}). The cards
describe commit [`{{commit_short}}`]({{code_url}}).
