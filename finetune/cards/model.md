---
license: other
license_name: lfm1.0
license_link: LICENSE
base_model: LiquidAI/LFM3-1.7B
base_model_relation: {{base_model_relation}}
library_name: {{library_name}}
pipeline_tag: text-generation
language:
- pl
- en
tags:
- brulion
- lfm3
- lora
- gguf
- llama.cpp
- tool-calling
- function-calling
- agent
- notes
- on-device
datasets:
- {{dataset_id}}
---

# {{title}}

{{title}} is a LoRA adapter for [LiquidAI/LFM3-1.7B](https://huggingface.co/LiquidAI/LFM3-1.7B),
trained as the on-device assistant of [Brulion]({{app_url}}), an offline-first Markdown notes app:
it searches, queries, reads and changes the user's notebook through the app's tools, in Polish and
English, and answers at once, without thinking. The app runs it over Liquid AI's own 4-bit file,
`{{base_gguf}}`: llama.cpp adds the adapter as it computes, and the 4-bit weights stay as they are.

> **A modified model.** This repository is a Derivative Work of LFM3-1.7B by Liquid AI, Inc.,
> distributed under the LFM Open License v1.0 (`LICENSE`). The weights were changed by fine-tuning
> (`NOTICE`, "Training" below). It is not made, endorsed or supported by Liquid AI; "LFM" and
> "Liquid AI" appear here only to describe where the base model comes from. Commercial use is
> licensed only below US $10 million of annual revenue (below, "License").

## Files

{{files_table}}

`SHA256SUMS` lists every file's SHA-256 (`sha256sum -c SHA256SUMS`).

## Use with llama.cpp

In the app the model is two files: Liquid AI's `{{base_gguf}}` of
[LiquidAI/LFM3-1.7B-GGUF](https://huggingface.co/LiquidAI/LFM3-1.7B-GGUF/tree/{{base_gguf_revision}})
({{base_gguf_bytes}} bytes, SHA-256 `{{base_gguf_sha256}}`) and `{{adapter_file}}`
({{adapter_bytes}} bytes, SHA-256 `{{adapter_sha256}}`). Every result below was measured so. The
QAD file's weights were trained for 4 bits and lie on the 4-bit grid: merged in and quantized
again, the adapter would mostly round back to them, so it is added as the model computes. It is in
{{adapter_type}}: llama.cpp has no fast path for bf16 on ARM.

```sh
hf download LiquidAI/LFM3-1.7B-GGUF {{base_gguf}} --revision {{base_gguf_revision}} --local-dir .
hf download {{repo_id}} {{adapter_file}} --local-dir .
llama-server -m {{base_gguf}} --lora {{adapter_file}} -c 16384 --reasoning off \
    --temp 0.1 --top-k 50 --repeat-penalty 1.1 --repeat-last-n 64
```

`--reasoning off` makes the chat template open every answer with an empty `<think></think>`, as
the model was trained. Send the app's system text and tools (below) with `/v1/chat/completions`;
the tool calls come back as OpenAI `tool_calls`.

**Speed.** An adapter costs time where it runs. On a Pixel 9 Pro (Tensor G4; llama.cpp v0.9.0
built with the NDK, its `android_armv8.6_1` CPU plugin, 4 threads on the big cores; 965 tokens
read and 128 written, the median of 3 rounds):

| | Reading (tokens/s) | Writing (tokens/s) |
|---|---|---|
| `{{base_gguf}}` alone | 166.3 | 24.7 |
| with this adapter (rank 48, f16) | 110.6 (−33 %) | 21.2 (−14 %) |

On x86, counted in processor cycles per token, an earlier rank-48 adapter of this project added
9 % to reading and 15–18 % to writing.
{{#merged_gguf}}

### Merged GGUF files

For programs that take a single GGUF file ({{merged_gguf_list}}): the adapter merged into the
bf16 weights and converted by llama.cpp's scripts; the 4-bit files are quantized from the BF16 one
by `llama-quantize`, the usual way, not trained for 4 bits as Liquid AI's QAD file is. The app does
not use them, and they were not measured with the bench: measure them before relying on them. For
an earlier adapter of this project (one trained with thinking, measured with an older bench) the
bench gave 184 of the 245 test tasks over the QAD file, 176 merged into Q4_0 and 191 merged into
Q4_K_M.

```sh
llama-server -m {{merged_gguf_example}} -c 16384 --reasoning off \
    --temp 0.1 --top-k 50 --repeat-penalty 1.1 --repeat-last-n 64
```
{{/merged_gguf}}
{{#peft}}

## Use with transformers and PEFT

`adapter_config.json` and `adapter_model.safetensors` are the same adapter in PEFT's format, for
further training or for transformers:

```python
import torch
from peft import PeftModel
from transformers import AutoModelForCausalLM, AutoTokenizer

base = AutoModelForCausalLM.from_pretrained("LiquidAI/LFM3-1.7B", revision="{{base_revision}}",
                                            dtype=torch.bfloat16, trust_remote_code=True)
model = PeftModel.from_pretrained(base, "{{repo_id}}")
tokenizer = AutoTokenizer.from_pretrained("LiquidAI/LFM3-1.7B", revision="{{base_revision}}")
messages = [{"role": "system", "content": system_text},  # the app's (below, "Prompt format")
            {"role": "user", "content": "Today is Wednesday, 2026-11-18.\n\nWhat's on my shopping list?"}]
prompt = tokenizer.apply_chat_template(messages, tools=tools,  # the app's twelve tools, as JSON schemas
                                       add_generation_prompt=True, enable_thinking=False, tokenize=False)
```

The bf16 model with the adapter was not measured; the results below are over the 4-bit file.
{{/peft}}
{{#merged_weights}}

`merged/` holds the adapter merged into the bf16 weights (safetensors), with the base model's
config, tokenizer, chat template and model code (`trust_remote_code=True`); not measured either.
{{/merged_weights}}

## Prompt format

LFM3's chat template with tools, thinking off. The app writes the prompt itself
(`local::prompt()` in [src/core/ai/local.cpp]({{code_url}}/src/core/ai/local.cpp), after the model's
`chat_template.jinja`; a test compares the two), so llama.cpp's chat template with
`--reasoning off` gives the same tokens. A conversation of the training data — one question, one
search and the answer:

```text
<|startoftext|><|im_start|>system
You are the assistant in the notes app Brulion: you search, read and change the user's notebook with your tools.

Notes are Markdown: items "- ", tasks "- [ ]" (done "- [x]", in progress "- [/]"), …

Rules:
- …

<tools>
{"name": "search_notes", "description": "Full-text search over every note of the notebook: …", "parameters": {"properties": {"limit": {"description": "At most this many matches (default 15, at most 50)", "type": "integer"}, "query": {"description": "Words to look for", "type": "string"}}, "required": ["query"], "type": "object"}}
… one line per tool …
</tools><|im_end|>
<|im_start|>user
Today is Tuesday, 2027-02-09.

Which film did Sophie recommend?<|im_end|>
<|im_start|>assistant
<think></think><tool_call>
<function=search_notes>
<parameter=query>
Sophie
</parameter>
</function>
</tool_call><|im_end|>
<|im_start|>tool
<tool_response>
- [[2027-02-08]], line 1: Sophie recommended the film "Up". (ref journals/2027-02-08.md#L1@eb9a97eb)
</tool_response><|im_end|>
<|im_start|>assistant
<think></think>"Up".<|im_end|>
```

* **System text and tools.** The model was trained with the assistant's own system text — English
  or Polish, the language of the app and the notebook — and its twelve tools: `search_notes`,
  `query_notes`, `list_pages`, `read_page`, `append_to_journal`, `append_to_page`, `write_page`,
  `update_block`, `look_at_picture`, `extension_reference`, `check_extension`,
  `write_extension`. Both are in
  [src/core/extensions/builtin/asystent.luau]({{code_url}}/src/core/extensions/builtin/asystent.luau)
  (`WRITTEN`, `TOOLS`); another prompt or other tools are untested.
* **The first message** starts with the day (`Today is Wednesday, 2026-11-18.`, `Dziś jest
  środa, 2026-11-18.`) and, when the chat is opened from a page, that page's text
  (`The conversation is about the page [[Title]]: … ---`).
* **Tool results** come one `tool` turn each, in the order of the calls; a failed one starts with
  `Error: `. Every turn of the assistant starts with an empty `<think></think>`.
* **The loop.** At most 16 turns per question; a reading call repeated with the same input is not
  run again, nor a change already made (the result says so); from the eighth turn on, the last
  result says which turn of 16 it is. Every change to a note is shown to the user, who approves
  or declines it.
* **Sampling**, as the app does: temperature 0.1, top-k 50, repetition penalty 1.1 over the last
  64 tokens, a fixed seed; 16 384 tokens of context, at most 2 048 tokens per answer.

## Intended use

* The Brulion app's assistant on the device (Android, Linux) over the user's own notebook, every
  change approved by the user.
* A small on-device model for research on tool use, or a starting point for training another
  app's assistant on tasks of its own.

Out of scope: a general chat assistant; facts from the model's memory (1.7 B parameters); other
prompts, tools or languages than the ones above without measuring them first; thinking mode (not
trained); unattended changes to notes; anything where a wrong answer harms someone (health,
money, safety).

## Limitations

* Measured on made-up notebooks of 8 to 18 pages each; real notebooks are larger and less tidy.
* {{weakest}}
* Of the 23 tasks it failed in all four runs (11 of the 245 generated, 12 of the 88 hand-written),
  10 need arithmetic or counting over what it read (a week's spending, hours at a rate, a recipe
  for twelve, a budget's rest), 5 a day worked out from words, all of them Polish ("pojutrze", "w
  przyszły wtorek", "wczoraj", "na piątek", "w przyszłym tygodniu"), 4 a fact asked for in other
  words than the note's ("kosze" for the bin collection, "numer indeksu" for the note's "numer
  albumu"), 4 something else (a query for tomorrow's reminders written wrong, a note to compose
  written into the journal, the wrong one of a recipe's ingredients, an invoice's total given for
  a rate).
* It is a 1.7 B model: it can still state a wrong fact. Its answers link the pages they draw on
  (`[[Page]]`) — check them.
* Trained on notebooks of a generator (home, car, contacts, shopping, recipes, a reading list,
  projects, journal days): other kinds of notes are less well covered.

## Training

Two stages, as Liquid AI fine-tunes LFM3 for an app's tools — supervised fine-tuning on the
solutions' steps without thinking, then GRPO with a reward for the right call or answer — and the
two adapters combined into one ([finetune/]({{code_url}}/finetune),
[docs/FINETUNE.md]({{code_url}}/docs/FINETUNE.md)):

{{training}}

The data are the dataset's `sft` and `grpo` splits — made-up notebooks and tasks written by
`finetune/generate.py`, each task with a solution — and the training sequences the bench made of
them (the dataset's `training/` files), in the tokens of the prompts the app writes: the solutions'
steps written as the model's turns, the solutions' calls with the base model's own answers, and an
earlier fine-tune's first steps (read, not learnt) with the solution going on from there. The
hand-written tasks (`hand`) and the generated `test` split were never trained on.

## Evaluation

`brulion-assistantbench` ([tests/assistantbench]({{code_url}}/tests/assistantbench)) runs the
app's real assistant — its Luau extension, system text, tools and the notebook index — on a copy
of each task's notebook, the model answering every request as the app's runner does, and checks
the outcome: the lines that must be in the notes afterwards, no change nobody asked for, the facts
of the answer, the tools called. A task passes only when every check holds.

{{eval_setup}}

{{eval_table}}

{{eval_after}}

By category, the generated test set (a run's mean):

{{category_table}}

## License

[LFM Open License v1.0](LICENSE), the base model's license; this repository adds no terms of its
own. In short — the license itself is what counts:

* You may use, change and redistribute the model. Commercial use is licensed only as long as you,
  together with the entities that control you, that you control or that are under common control
  with you, have less than US $10 million of annual revenue; above that it is not licensed. The
  limit does not apply to a qualified non-profit's non-commercial or research use.
* Redistributing it or a model made from it: include `LICENSE`, keep the notices (`NOTICE`), and
  mark the files you change.
* No trademark rights: do not name your model or product after LFM or Liquid AI beyond describing
  where it comes from.
* No warranty; the license ends for anyone who breaks its terms.

The app, the bench and the training code are AGPL-3.0 ([the Brulion repository]({{app_url}})); the
tasks are the dataset [{{dataset_id}}](https://huggingface.co/datasets/{{dataset_id}})
({{dataset_license_name}}). The model card describes commit [`{{commit_short}}`]({{code_url}}).
