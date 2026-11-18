#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12,<3.14"
# dependencies = [
#     "torch==2.14.1",
#     "transformers==5.20.2",
#     "peft==0.21.2",
# ]
#
# [tool.uv.sources]
# torch = { index = "pytorch-cu132" }
#
# [[tool.uv.index]]
# name = "pytorch-cu132"
# url = "https://download.pytorch.org/whl/cu132"
# explicit = true
# ///
"""GRPO for the assistant's steps, as Liquid AI trains LFM3 for an app's tools (docs/FINETUNE.md "Training").

    LD_LIBRARY_PATH=/run/opengl-driver/lib TRITON_LIBCUDA_PATH=/run/opengl-driver/lib uv run finetune/grpo.py \
        --model ~/.cache/brulion-finetune/base/LFM3-1.7B --adapter <an adapter of finetune/sft.py, or none> \
        --data runs/taught-training.jsonl --tasks train/tasks --out ~/.cache/brulion-finetune/grpo-1

The prompts are the steps of the tasks' solutions as the bench wrote them without thinking
(`--solution --effort low --training`): each answer of the model in such a sequence is a step, its
prompt everything before it — the app's prompt at that point of the conversation, in llama.cpp's
tokens. For each prompt the model writes a group of answers (`--generations`, temperature 1.0);
an answer's reward compares it with the solution's:

* a step that calls tools: 1.0 when it makes the same calls — the tool and every argument of the
  solution's, compared by kind (a page's name without case or brackets, a query's filters in any
  order, a ref or a status exactly, the words a change writes with some tolerance but its dates with
  their markers — 📅 due, ⏰ reminder… —, times, task boxes and numbers as the solution's: `exact()`,
  `same_facts()`) — or the calls the task names as right there too (its step's "alternatives": a
  retry by other words or by the list of pages), else 0.2 × the F1 of the tools' names;
* a step that answers: 1.0 when it calls no tool, its words pass the task's checks of the answer
  (the bench's: words, any of a group, none of them, expressions, none of these), state no fact it
  read nowhere (a phone, a code, an amount, a date, a time not in its prompt — the system text's
  digits are an example, not read: `unfounded()`, the bench's rule for the solutions) and say
  nothing untrue of the changes (a change claimed when the calls before it changed no note, the
  run's own change said to have been so already: `misstated()`, the bench's checkSaid()), else 0.

Answers above their group's mean are reinforced, the others discouraged, with a KL penalty towards
the model without GRPO's adapter (beta): the base model, or the one of `--adapter` (an SFT
adapter, merged into the weights first — to run the result, load both adapters). A group whose answers all score the same teaches nothing and
is skipped: each step draws prompts (at most `--draws`) until `--prompts` groups tell answers
apart. With `--rates`, only the steps the model does sometimes are drawn, the more often the more
their reward varies (`--weights`). Steps that need no tool (questions answered directly, asking
back) are drawn twice as often, or reinforcement learning teaches to call a tool always. A step
whose own solution scores below 1.0 (an answer with a number its prompt never showed) is left out.

Every `--checkpoint-every` steps the run's state goes to `checkpoint/` in `--out` (GRPO's adapter,
the optimizer, the schedule, the random states the steps are drawn and the answers written with):
the same command run again after a stop goes on from there, exactly as the run would have.

    python3 -I finetune/grpo.py --self-test
    python3 -I finetune/grpo.py --check --data runs/taught-training.jsonl --tasks train/tasks [--rates rates.json]

check the reward without a model, with the standard library only: answers whose score is known,
then the solutions — each must score 1.0 —, and with `--rates` how the steps would be drawn.
"""

import argparse
import collections
import glob
import hashlib
import itertools
import json
import math
import random
import re
import shutil
import sys
import time
import unicodedata
from difflib import SequenceMatcher
from pathlib import Path

END, START = "<|im_end|>", "<|im_start|>"


# ---- The steps ---------------------------------------------------------------------------------

def steps_of(row):
    """A training line's steps: (prompt tokens, prompt text, the solution's answer as text)."""
    segments = row["segments"]
    steps, ids, text, i = [], [], [], 0
    while i < len(segments):
        if segments[i]["train"]:
            j = i
            while j < len(segments) and segments[j]["train"]:
                j += 1
            steps.append((list(ids), "".join(text), "".join(s["text"] for s in segments[i:j])))
            for s in segments[i:j]:
                ids += s["tokens"]
                text.append(s["text"])
            i = j
        else:
            ids += segments[i]["tokens"]
            text.append(segments[i]["text"])
            i += 1
    return steps


def read_steps(files, tasks, max_prompt):
    steps, seen_keys = [], set()
    for file in files:
        for line in open(Path(file).expanduser(), encoding="utf-8"):
            row = json.loads(line)
            task = tasks.get(row["id"], {})
            for k, (prompt, read, answer) in enumerate(steps_of(row)):
                key = (row["id"], k)
                if key in seen_keys or len(prompt) > max_prompt:
                    continue
                seen_keys.add(key)
                calls, words = parse(answer)
                steps.append(make_step(row["id"], row["category"], k, prompt, read, calls, words,
                                       task.get("check", {}), task.get("alternatives", [])))
    return steps


def make_step(task, category, k, prompt, read, calls, words, check, alternatives=()):
    step = {"id": task, "category": category, "step": k, "prompt": prompt, "calls": calls, "words": words,
            "check": check}
    if not calls:  # an answer: the numbers and times its prompt shows, whether a call before changed a note
        step["grounds"], step["times"] = grounds(read)
        step["changed"] = changed_before(read)
    for made, others in alternatives:  # the solution's step of these calls: the other calls as right there
        if same_calls(made, calls):
            step["alternatives"] = others
            break
    return step


def read_tasks(folder):
    """Of every task (by id) its answer's checks (expect.answer) and, of its solution's steps that
    name other calls as right ("alternatives": a retry by another word or by the list of pages), the
    step's calls with those: (calls, [calls…])."""
    def calls_of(o):
        found = list(o.get("calls", []))
        if "call" in o:
            found.append({"name": o["call"], "input": o.get("input", {})})
        return [{"name": c["name"], "args": {k: v if isinstance(v, str) else json.dumps(v, ensure_ascii=False)
                                             for k, v in (c.get("input") or {}).items()}} for c in found]

    tasks = {}
    for file in glob.glob(str(Path(folder).expanduser() / "*.json")):
        for task in json.load(open(file, encoding="utf-8"))["tasks"]:
            tasks[task["id"]] = {"check": task.get("expect", {}).get("answer", {}),
                                 "alternatives": [(calls_of(o), [calls_of(a) for a in o["alternatives"]])
                                                  for o in task.get("solution", []) if o.get("alternatives")]}
    return tasks


def same_calls(a, b):
    """The same calls, to the letter (a solution's step found again in a training line)."""
    def key(calls):
        return sorted((c["name"], sorted((k, str(v).strip()) for k, v in c["args"].items())) for c in calls)
    return key(a) == key(b)


# ---- Reading an answer -------------------------------------------------------------------------

CALL = re.compile(r"<tool_call>\s*<function=([^>\n]+)>\n?(.*?)</function>\s*</tool_call>", re.S)
PARAMETER = re.compile(r"<parameter=([^>\n]+)>\n?(.*?)\n?</parameter>", re.S)


def parse(text):
    """An answer's tool calls ({name, args}) and its words, as local::reply() reads them."""
    text = text.split(END)[0].split(START)[0]
    calls = [{"name": m.group(1).strip(), "args": {p.group(1).strip(): p.group(2) for p in PARAMETER.finditer(m.group(2))}}
             for m in CALL.finditer(text)]
    words = re.sub(r"<tool_call>.*?(</tool_call>|$)", "", text, flags=re.S)
    return calls, words.replace("<think>", "").replace("</think>", "").strip()


# ---- Comparing -------------------------------------------------------------------------------

def plain(s):
    """What a reader sees: spaces, hyphens and apostrophes of every kind as the plain ones."""
    out = []
    for c in str(s):
        if unicodedata.category(c) == "Zs" or c == "\t":
            out.append(" ")
        elif "‐" <= c <= "―" or c == "−":
            out.append("-")
        elif c in "‘’ʼ":
            out.append("'")
        else:
            out.append(c)
    return "".join(out)


def fold(s):
    s = unicodedata.normalize("NFKD", plain(s).casefold().replace("ł", "l"))
    return "".join(c for c in s if not unicodedata.combining(c))


def norm(s):
    return " ".join(re.sub(r"[^\w#:<>=.+-]", " ", fold(s)).split())


def similar(a, b):
    a, b = norm(a), norm(b)
    if a == b:
        return True
    short, long_ = sorted((a, b), key=len)
    if short and short in long_ and len(short) / len(long_) >= 0.5:
        return True
    return bool(a) and bool(b) and SequenceMatcher(None, a, b).ratio() >= 0.6


def page_name(s):
    s = fold(s).strip()
    s = re.sub(r"^\[\[|\]\]$", "", s)
    return re.sub(r"\.md$", "", s).strip()


def filters(query):
    """A query's filters, in any order: words in quotes kept whole; sort: and limit: left out."""
    found = re.findall(r'(\w+:"[^"]*"|"[^"]*"|\S+)', fold(query))
    return {f.replace("#", "tag:") if f.startswith("#") else f for f in found
            if not f.startswith(("sort:", "limit:"))}


# What a change writes that must be as the solution's: the task markers and their dates
# (docs/SYNTAX.md "Tasks", "Reminders and alarms"; src/core/markdown/tokens.cpp reads 📆 and 🗓 as
# 📅, ⌛ as ⏳, a variation selector after any of them as nothing, and "9:00" as "09:00").
SAME_MARK = {"📆": "📅", "🗓": "📅", "⌛": "⏳"}
DATED = re.compile(r"(⏰!?|📅|📆|🗓|⏳|⌛|🛫|➕|✅|❌)[ \t]*(\d{4}-\d{2}-\d{2})"
                   r"(?:(?:[ \t]+|T)(\d{1,2}:\d{2})(?![\d:]))?(?![\d:])")
BARE_DATE = re.compile(r"(?<!\d)(\d{4}-\d{2}-\d{2})(?:(?:[ \t]+|T)(\d{1,2}:\d{2})(?![\d:]))?(?![\d:])")
TWELVE = re.compile(r"(?<![0-9])([0-9]{1,2})(?::([0-9]{2}))? ?([ap])\.?m\b\.?", re.I)
CLOCK = re.compile(r"(?<![\d:])\d{1,2}:\d{2}(?![\d:])")
MARKS = re.compile("⏰!?|[📅📆🗓⏳⌛🛫➕✅❌🔁🔺⏫🔼🔽⏬🆔⛔🏁]")
BOX = re.compile(r"^[ \t]*(?:(?:[-*+]|\d+[.)])[ \t]+)?\[([^\[\]])\](?=[ \t]|$)", re.M)
THOUSANDS = re.compile(r"(?<![\d.,])\d{1,3}(?: \d{3})+(?!\d)")
NUMBER = re.compile(r"\d+(?:[.,]\d+)?")


def hhmm(time):
    return f"{int(time.split(':')[0]):02d}:{time.split(':')[1]}" if time else ""


def twelve_hours(m):
    """A time on the twelve-hour clock as HH:MM ("4 pm" → "16:00"), None for what is none ("13 pm")."""
    hour, minute = int(m[1]), int(m[2] or 0)
    if not 1 <= hour <= 12 or minute > 59:
        return None
    return f"{hour % 12 + (12 if m[3].lower() == 'p' else 0):02d}:{minute:02d}"


def exact(text):
    """What a change writes that must be the solution's, and the rest of its words: each date with
    its marker (📅 due, ⏰ reminder, ⏰! alarm, ⏳, 🛫, ➕, ✅, ❌) and time, every other date, time ("4
    pm" is 16:00), marker (🔁, a priority) and number, its lines' task boxes ([ ], [x], [/], [-]) —
    counted —; and the text without them. A date as the app reads it only: "2026‑11‑20" with
    non-breaking hyphens is no date."""
    s = re.sub("[\ufe0e\ufe0f]", "", text)
    found = []

    def take(pattern, token):
        def one(m):
            t = token(m)
            if t is None:
                return m[0]
            found.append(t)
            return " "

        nonlocal s
        s = pattern.sub(one, s)

    take(DATED, lambda m: ("date", SAME_MARK.get(m[1], m[1]), m[2], hhmm(m[3])))
    take(BARE_DATE, lambda m: ("date", "", m[1], hhmm(m[2])))
    take(TWELVE, lambda m: ("time", twelve_hours(m)) if twelve_hours(m) else None)
    take(CLOCK, lambda m: ("time", hhmm(m[0])))
    take(MARKS, lambda m: ("mark", SAME_MARK.get(m[0], m[0])))
    take(BOX, lambda m: ("box", m[1].lower()))
    s = THOUSANDS.sub(lambda m: m[0].replace(" ", ""), plain(s))  # "1 550 zł" is 1550
    take(NUMBER, lambda m: ("number", m[0].replace(",", ".")))
    return collections.Counter(found), s


def same_facts(gold, pred):
    """A change's facts are the solution's: its task boxes, markers and marked dates as many as the
    solution's; its times, other dates and numbers each of the solution's at least, and others only
    when they say one of them again — "at 4 pm", "o 16", "on the 21st" with ⏰ 2027-04-21 16:00."""
    def counted(t):
        return t[0] in ("box", "mark") or (t[0] == "date" and t[1])

    if any(counted(t) and gold[t] != pred[t] for t in gold | pred) or any(pred[t] < n for t, n in gold.items()):
        return False
    times = {t[1] for t in gold if t[0] == "time"} | {t[3] for t in gold if t[0] == "date" and t[3]}
    dates = {t[2] for t in gold if t[0] == "date"}
    said = ({t[1] for t in gold if t[0] == "number"} | {str(int(x[:2])) for x in times}
            | {str(int(x[:2]) - 12) for x in times if int(x[:2]) > 12}
            | {str(int(d[8:])) for d in dates} | {d[:4] for d in dates})
    return all(t in gold or (t[0] == "time" and t[1] in times) or (t[0] == "number" and t[1] in said)
               or (t[0] == "date" and not t[1] and t[2] in dates and (not t[3] or t[3] in times))
               for t in pred)


# ---- What an answer states ---------------------------------------------------------------------
# The rule of tests/assistantbench/bench.cpp ungroundedFacts() and seenBefore(), which keeps the
# solutions' answers grounded: an answer's dates, times and other numbers of three digits or more
# must be in what the model read — spaces, hyphens and leading zeros aside, a number's groups in
# part ("601 234 567" of "+48 601 234 567"), a time on either clock or with a dot, an hour a note
# names bare ("między 12 a 14", "between 12 and 2 pm") —, not in the calendar of the next days the
# first message gives (today counts), not in the system text (its date is an example); or be worked
# out with the working shown ("8 × 60 = 480, plus 30 = 510"); or, in a task that works its answer out
# (multi, no_tool), be one its check asks for.

COLON_TIME = re.compile(r"(?<![0-9])([0-9]{1,2}):([0-9]{2})(?![0-9])")
DOTTED_TIME = re.compile(r"(?<![0-9])([0-9]{1,2})[:.]([0-9]{2})(?![0-9])")
ISO_DATE = re.compile(r"(?<![0-9])[0-9]{4}-[0-9]{2}-[0-9]{2}(?![0-9])")
SEEN_NUMBER = re.compile(r"[0-9]+(?:[ .,:/-][0-9]+)*")
FACT_NUMBER = re.compile(r"[0-9]+(?:[ .,-][0-9]+)*")
GROUP = re.compile(r"[0-9]+")
HOUR_RANGE = re.compile(r"\b(?:między|miedzy|between|od|from)\s+([0-9]{1,2})\s+(?:a|and|do|to|i)\s+([0-9]{1,2})(?![0-9.,:])",
                        re.I)
HOUR = re.compile(r"\b(?:po|przed|o|około|okolo|ok\.|godz\.|after|before|at|around|until|till|by)\s+([0-9]{1,2})(?![0-9.,:])",
                  re.I)
ARITHMETIC = re.compile(r"(?P<n>[0-9]+(?:[ .,][0-9]+)*)|(?P<op>[-+×*·/÷]|\b(?:x|plus|minus|razy|times)\b)|(?P<eq>=)"
                        r"|(?P<w>[^\W\d_]+)|(?P<o>\S)", re.I)
WORKED_OUT = ("multi", "no_tool")  # tasks whose answer works its values out (bench.cpp checkGrounding)


def unzeroed(digits):
    return digits.lstrip("0") or "0"


def times_of(text, dotted):
    """A text's times, on either clock — "16:00", "4 pm", "4:30 p.m.", with `dotted` "16.00" too, as a
    note may write one (not an answer: there it is as likely an amount): (start, end, minutes)."""
    out = []
    for m in TWELVE.finditer(text):
        if time := twelve_hours(m):
            out.append((m.start(), m.end(), int(time[:2]) * 60 + int(time[3:])))
    twelves = list(out)
    for m in (DOTTED_TIME if dotted else COLON_TIME).finditer(text):
        if int(m[1]) <= 23 and int(m[2]) <= 59 and not any(s <= m.start() < e for s, e, _ in twelves):
            out.append((m.start(), m.end(), int(m[1]) * 60 + int(m[2])))
    return out


def bare_hours(text):
    """The full hours a text gives bare, each as that hour and twelve hours later, in minutes."""
    out = set()
    for hour in [h for m in HOUR_RANGE.finditer(text) for h in (m[1], m[2])] + [m[1] for m in HOUR.finditer(text)]:
        if (hour := int(hour)) <= 23:
            out.add(hour * 60)
            if hour < 12:
                out.add((hour + 12) * 60)
            elif hour == 12:
                out.add(0)
    return out


def facts_of(answer):
    """An answer's facts: (as written, digits or "" for a time, minutes or None, where it starts)."""
    rest, facts = plain(answer), []

    def take_out(start, end):
        nonlocal rest
        rest = rest[:start] + "|" * (end - start) + rest[end:]

    for m in list(ISO_DATE.finditer(rest)):
        facts.append((m[0], m[0].replace("-", ""), None, m.start()))
        take_out(m.start(), m.end())
    for start, end, minutes in times_of(rest, False):
        facts.append((rest[start:end].strip(), "", minutes, start))
        take_out(start, end)
    for m in FACT_NUMBER.finditer(rest):
        if len(digits := "".join(GROUP.findall(m[0]))) >= 3:
            facts.append((m[0], digits, None, m.start()))
    return facts


def seen(prompt):
    """What the model read before a step: its prompt, of the first message's first paragraph — the
    app's day and the calendar of the next days — only today's date: the calendar names every date
    of two weeks, the right one too; of the system turn (the app's system text and its tools, the
    same in every conversation) the words, not the digits: what it names is an example, the journal
    "named by its date (2026-11-12)", never a fact of the notebook."""
    system = prompt.find(START + "system\n")
    if system >= 0 and system == prompt.find(START):
        end = prompt.find(END, system)
        end = len(prompt) if end < 0 else end
        prompt = prompt[:system] + re.sub("[0-9]", "#", prompt[system:end]) + prompt[end:]
    begin = prompt.find(START + "user\n")
    if begin < 0:
        return prompt
    begin += len(START + "user\n")
    end = prompt.find(END, begin)
    end = len(prompt) if end < 0 else end
    cut = prompt.find("\n\n", begin, end)
    if cut < 0:
        return prompt
    today = ISO_DATE.search(prompt, begin, cut)
    return prompt[:begin] + (today[0] if today else "") + prompt[cut:]


def grounds(prompt):
    """What an answer may take from its prompt: the digits of every run of a number's groups
    ("601234567" of "+48 601 234 567", "20261120" of "2026-11-20 09:00"), and the times in minutes."""
    text = plain(seen(prompt))
    digits = set()
    for m in SEEN_NUMBER.finditer(text):
        groups = GROUP.findall(m[0])
        for i in range(len(groups)):
            joined = ""
            for group in groups[i:]:
                joined += group
                digits.add(unzeroed(joined))
                if len(joined) > 24:
                    break
    return digits, {t for _, _, t in times_of(text, True)} | bare_hours(text)


def asked_for(check, fact):
    """Does the check ask for this fact: a word of it the same text or number, or an expression of it
    matching it (bench.cpp askedFor)?"""
    text, digits = fact[0], fact[1]
    for word in check.get("contains", []) + [w for group in check.get("anyOf", []) for w in group]:
        word = plain(word).strip()
        found = "".join(GROUP.findall(word))
        if word == text or (digits and found and unzeroed(found) == unzeroed(digits)):
            return True
    return any(re.search(p, text, re.I) for p in check.get("matches", []))


def value_of(number):
    """A number as written: "4 500", "245,67", "6,750", "1.5" (a last dot or comma before one or two
    digits is the decimal point, before three a thousands' separator)."""
    number = number.replace(" ", "")
    point = max(number.rfind("."), number.rfind(","))
    whole, fraction = number, ""
    if point >= 0 and len(number) - point - 1 <= 2:
        whole, fraction = number[:point], number[point + 1:]
    return float(whole.replace(".", "").replace(",", "") + "." + (fraction or "0"))


def worked_out(text):
    """Where an answer works a number out itself (bench.cpp workedOutIn): the numbers right after an
    "=" that the arithmetic just before it gives — numbers joined by + - × x * · / ÷ (or plus, minus,
    razy, times), with short words (units, "to") and , £ € $ % between them: "8 × 60 = 480 minut, plus
    30 = 510", "£245.67 + £207.46 = £453.13". By where each starts."""
    tokens = []
    for m in ARITHMETIC.finditer(text):
        kind = m.lastgroup
        if kind == "w":
            kind = "skip" if len(m[0]) <= 6 else "stop"
        elif kind == "o":
            kind = "skip" if m[0] in ",£€$%" else "stop"
        tokens.append((kind, m[0].strip(), m.start()))
    out = set()
    for e, (kind, _, _) in enumerate(tokens):
        if kind != "eq":
            continue
        r = e + 1
        while r < len(tokens) and tokens[r][0] == "skip" and tokens[r][1] != ",":
            r += 1  # ("= £453.13")
        if r >= len(tokens) or tokens[r][0] != "n":
            continue
        numbers, operators, want_number = [], [], True  # the expression before it, from its end
        for kind_, token, _ in reversed(tokens[:e]):
            if kind_ == "skip":
                continue
            if want_number and kind_ == "n":
                numbers.insert(0, value_of(token))
            elif not want_number and kind_ == "op":
                operators.insert(0, token.lower())
            else:
                break
            want_number = not want_number
        if len(numbers) < 2:
            continue
        if len(operators) == len(numbers):  # (it began with an operator: no number to it)
            operators.pop(0)
        terms, signs = [numbers[0]], [1]  # × and / first, then + and -
        for op, number in zip(operators, numbers[1:]):
            if op in ("+", "plus", "-", "minus"):
                terms.append(number)
                signs.append(1 if op in ("+", "plus") else -1)
            elif op in ("/", "÷"):
                terms[-1] = math.nan if number == 0 else terms[-1] / number
            else:
                terms[-1] *= number
        result = value_of(tokens[r][1])
        if abs(sum(s * t for s, t in zip(signs, terms)) - result) < 0.005 + 1e-9 * abs(result):
            out.add(tokens[r][2])
    return out


def unfounded(step, words):
    """What an answer states that it read nowhere (bench.cpp ungroundedFacts()): its facts not in its
    prompt, not worked out, not asked for by the check of a task that works its answer out."""
    worked = worked_out(plain(words))
    check = step["check"] if step["category"] in WORKED_OUT else {}
    return [f[0] for f in facts_of(words)
            if not (f[2] in step["times"] if f[2] is not None else unzeroed(f[1]) in step["grounds"] or f[3] in worked)
            and not asked_for(check, f)]


# ---- What an answer says of the changes --------------------------------------------------------
# The rule of bench.cpp checkSaid(), whatever the task: a change claimed when no note changed (the
# user believes it is there: "Zapisałem w dzienniku…" for a draft asked for), and the run's own change
# said to have been there already, never said to be made (the user believes nothing was done: "was
# already marked done — nothing left to check off"). The patterns are the bench's claimOfChange(),
# soAlready() and soNow(), to the letter; what changed is what the calls before the answer did.

# A change the answer says it made: the first person of a verb of changing ("Dodałam", "zapisałem",
# "przesunęłam", "I've added", "I marked") or a line opening with one ("Added … to [[Shopping]]",
# "Moved …") — not "nie dodałam", "zapisałam się", "I've added nothing".
CLAIM_OF_CHANGE = re.compile(
    r"(?<!\bnie\s)\b(?:dodał|dopisał|zapisał|wpisał|zanotował|odhaczył|oznaczył|zaznaczył|ustawił|zmienił"
    r"|utworzył|stworzył|założył|przełożył|przeniosł|anulował)(?:em|am)\b(?!\s+si[ęe]\b)"
    r"|(?<!\bnie\s)\b(?:przesun|usun)(?:ąłem|ęłam)\b|\bI(?:'ve|\s+have)?\s+(?:just\s+|now\s+"
    r"|also\s+)?(?:added|saved|noted|written down|logged|marked|ticked|checked off|moved|set|created"
    r"|updated|put|changed|recorded|scheduled|appended|rescheduled|cancell?ed|removed"
    r"|deleted)\b(?!\s+nothing)|(?:^|(?<=[.!?]\s)|(?<=\n))\s*(?:[-*]\s+)?(?:\*\*)?(?:added|saved|noted in"
    r"|noted down|logged|ticked off|moved|created|reminder set|appended)\b", re.I | re.M)
# That what was asked for was so before ("was already marked done", "the line already reads", "no
# change needed", "Nie trzeba nic zmieniać", "było już").
SO_ALREADY = re.compile(
    r"\b(?:was|were|had)\s+already\b|\balready\s+(?:reads|read|had)\b|\b(?:it's|it\s+is|is"
    r"|are)\s+already\s+(?:been\s+)?(?:marked|ticked|checked|done|completed|recorded|set|due|on)\b"
    r"|\bno\s+(?:further\s+)?changes?\s+(?:is\s+|was\s+|were\s+)?(?:needed|necessary|required)"
    r"|\bnothing\s+(?:left\s+|more\s+)?to\s+(?:change|check off|tick off|tick|update|move|mark)"
    r"|\b(?:był[aoy]?|były)\s+już\b|\bjuż\s+(?:był[aoy]?|były)\b|\bnie\s+trzeba\s+(?:było\s+)?(?:nic\s+"
    r"|niczego\s+)?(?:zmienia|zmieni|odhacza|robi|przesuwa)|\bnic\s+nie\s+trzeba\b|\bnie\s+(?:ma"
    r"|było)\s+potrzeby\b", re.I | re.M)
# That the change is there now, in other words than a claim: "now", "teraz", "Done", "Marked …",
# "zaktualizowałem", "dodano", "została zmieniona", "has been moved".
SO_NOW = re.compile(
    r"\b(?:now|teraz|obecnie)\b|\bha(?:s|ve)\s+been\s+(?:marked|moved|added|ticked|set|updated|changed"
    r"|created|saved|checked)|(?:^|(?<=[.!?]\s)|(?<=\n))\s*(?:[-*]\s+)?(?:\*\*)?(?:marked|cancell?ed|done"
    r"|gotowe|zapisane|dodane|dopisane)\b|zaktualizował|zastosował|przygotował|zrobił(?:em|am)\s+to"
    r"|\b(?:dodano|dopisano|zapisano|wpisano|odhaczono|oznaczono|zaznaczono|zmieniono|ustawiono"
    r"|przesunięto|utworzono|zaktualizowano|zanotowano)\b|\bzostał[aoy]?\s+(?:zmienion|dodan|zapisan"
    r"|odhaczon|oznaczon|zaznaczon|przesunięt|zaktualizowan|ustawion|dopisan|utworzon|wpisan)", re.I | re.M)
# The tools that change notes (asystent.luau's CHANGING).
CHANGING = {"append_to_journal", "append_to_page", "write_page", "update_block", "write_extension"}
RESPONSE = re.compile(r"<tool_response>\n?(.*?)\n?</tool_response>", re.S)


def changed_before(prompt):
    """Did a call before this step change a note: a call of a changing tool whose result is no error?
    The app writes an error "Error: …": a change declined, refused, or one that would change nothing
    (the line says it already) among them; a change made again is told it was made by the call before."""
    calls = []
    for turn in prompt.split(START)[1:]:
        role, _, body = turn.partition("\n")
        if role == "assistant":
            calls = [m.group(1).strip() for m in CALL.finditer(body)]
        elif role == "tool":
            for result in RESPONSE.findall(body):
                if calls and calls.pop(0) in CHANGING and not result.startswith("Error: "):
                    return True
    return False


def misstated(step, words):
    """What an answer says of the changes that is untrue (bench.cpp checkSaid()), in a few words; "" none."""
    said = plain(words)
    claim = CLAIM_OF_CHANGE.search(said)
    if not step["changed"] and claim:
        return f"claims a change (“{claim[0].strip()}”), and no note changed"
    already = SO_ALREADY.search(said)
    if step["changed"] and already and not claim and not SO_NOW.search(said):
        return f"says it was so already (“{already[0].strip()}”), and the run changed a note"
    return ""


def same_argument(tool, key, gold, pred):
    if key in ("ref", "status", "date"):
        return pred.strip() == gold.strip()
    if key == "name":
        return page_name(pred) == page_name(gold)
    if tool == "query_notes" and key == "query":
        return filters(pred) == filters(gold)
    if key in ("text", "markdown", "content"):  # what a change writes: its words with some tolerance, not its facts
        (gold_facts, gold_words), (pred_facts, pred_words) = exact(gold), exact(pred)
        return same_facts(gold_facts, pred_facts) and similar(gold_words, pred_words)
    return similar(gold, pred)


def same_call(gold, pred):
    if gold["name"] != pred["name"]:
        return False
    if gold["name"] == "search_notes":  # what to look for: in other words too, a word of them at least
        asked, searched = gold["args"].get("query", ""), pred["args"].get("query", "")
        return similar(asked, searched) or bool(set(norm(asked).split()) & set(norm(searched).split()))
    if gold["name"] == "update_block" and "text" not in gold["args"] and "text" in pred["args"]:
        return False  # a status alone: the line's text stays
    return all(key in pred["args"] and same_argument(gold["name"], key, value, pred["args"][key])
               for key, value in gold["args"].items() if key != "limit")


def passes(check, words):
    """The bench's checks of an answer (checkText in tests/assistantbench/bench.cpp)."""
    folded = fold(words)
    if any(fold(w) not in folded for w in check.get("contains", [])):
        return False
    if any(not any(fold(w) in folded for w in group) for group in check.get("anyOf", [])):
        return False
    if any(fold(w) in folded for w in check.get("notContains", [])):
        return False
    if any(re.search(p, plain(words), re.I | re.M) for p in check.get("notMatches", [])):
        return False
    return all(re.search(p, plain(words), re.I | re.M) for p in check.get("matches", []))


def reward(step, answer):
    calls, words = parse(answer)
    if not step["calls"]:  # the answer: the task's last step
        return 1.0 if (not calls and words and passes(step["check"], words) and not unfounded(step, words)
                       and not misstated(step, words)) else 0.0
    if len(calls) == len(step["calls"]) and all(any(same_call(g, p) for p in calls) for g in step["calls"]):
        return 1.0
    if any(len(calls) == len(other) and all(any(same_call(g, p) for p in calls) for g in other)
           for other in step.get("alternatives", [])):
        return 1.0  # another call as right at this point (a retry: another word, or the pages listed)
    gold = [c["name"] for c in step["calls"]]
    pred = [c["name"] for c in calls]
    overlap = sum(min(gold.count(n), pred.count(n)) for n in set(gold))
    return 0.0 if overlap == 0 else 0.2 * 2 * overlap / (len(gold) + len(pred))


def why(step, answer):
    """What keeps an answer below 1.0, in a few words."""
    calls, words = parse(answer)
    if step["calls"]:
        return "not the solution's calls"
    if calls:
        return "calls a tool"
    if not words:
        return "says nothing"
    if not passes(step["check"], words):
        return "fails the task's check"
    if unfounded(step, words):
        return f"states {', '.join(unfounded(step, words))}, not in its prompt: {words[:140]!r}"
    return f"{misstated(step, words)}: {words[:140]!r}"


def report(bad):
    """The solutions the reward does not take, by the kind of task (its id without notebook and number)."""
    kinds = collections.defaultdict(list)
    for s in bad:
        kinds[re.sub(r"-\d+$", "", s["id"].split("/")[-1])].append(s)
    for kind, found in sorted(kinds.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        print(f"  {kind}: {len(found)}, e.g. {found[0]['id']} step {found[0]['step']}: {why(found[0], render(found[0]))}")


def draw_weights(steps, args):
    """The steps to draw and how often: one that needs no tool twice as often; with --rates only those
    rated 0 < p < 1 and p ≤ --max-rate, by p(1-p) or alike (--weights)."""
    weights = [2.0 if not s["calls"] and s["step"] == 0 else 1.0 for s in steps]
    if not args.rates:
        return steps, weights
    rates = json.load(open(Path(args.rates).expanduser(), encoding="utf-8"))
    kept, kept_weights, by_rate = [], [], collections.defaultdict(lambda: [0, 0.0])
    for s, w in zip(steps, weights):
        p = rates.get(f"{s['id']}#{s['step']}", 0.0)
        if 0 < p < 1 and p <= args.max_rate:
            kept.append(s)
            kept_weights.append(w * (p * (1 - p) if args.weights == "learnability" else 1.0))
            by_rate[p][0] += 1
            by_rate[p][1] += kept_weights[-1]
    if not kept:
        sys.exit("no step of the --rates file is done sometimes")
    total = sum(kept_weights)
    print(f"{len(kept)} steps the model does sometimes, drawn by {args.weights}: "
          + ", ".join(f"{p:g}: {n} ({100 * w / total:.0f} %)" for p, (n, w) in sorted(by_rate.items())))
    return kept, kept_weights


# ---- Checking the reward ---------------------------------------------------------------------

def self_test():
    """The reward of answers whose score is known; prints what is not as expected and returns how many."""
    def call(tool, **args):
        return render({"calls": [{"name": tool, "args": args}], "words": ""})

    def calling(answer, *others):
        calls, _ = parse(answer)
        return make_step("test", "test", 1, [], "", calls, "", {}, [(calls, [parse(o)[0] for o in others])])

    def answering(read, check, category="search"):
        return make_step("test", category, 1, [], read, [], "…", check)

    def asked(question, *turns):
        return (f"{START}user\n{question}{END}\n"
                + "".join(f"{START}assistant\n{c}{END}\n{START}tool\n<tool_response>\n{r}\n</tool_response>{END}\n"
                          for c, r in turns) + f"{START}assistant\n")

    remind = call("append_to_journal", markdown="- [ ] Call the bank ⏰ 2027-03-14 10:00")
    drill = call("append_to_page", name="Home", markdown="- [ ] Return Tom's drill 📅 2027-01-28")
    move = call("update_block", ref="Home.md#L7@6ebb1278", text="Fix the bugs in the app #urgent 📅 2027-04-04")
    query = call("query_notes", query='type:tasks status:open tag:garden page:"Home"')
    shopping = call("append_to_page", name="Zakupy", markdown="- [ ] 2 kg jabłek\n- [ ] mleko")
    contacts = "<|im_start|>tool\n<tool_response>\n- [[Contacts]], line 4: Babysitter: Rachel Wilson, phone 07700 915000\n"
    gardener = "<|im_start|>tool\n<tool_response>\n- [[Sarah Turner]], line 3: Gardener (ref People/Sarah Turner.md#L3@e8120946)\n"
    hours = "<|im_start|>tool\n<tool_response>\n- [[Peter Wilson]], line 4: Call after 4 pm\n"
    spent = "- [[2026-09-24]]: Shopping at Lidl: £103.36\n- [[2026-09-25]]: Shopping at Tesco: £12.18\n"
    bank = call("append_to_journal", markdown="- [ ] Zadzwonić do banku ⏰ 2027-04-21 16:00")
    calendar = asked("Dziś jest poniedziałek, 2027-04-19. Jutro: wtorek 2027-04-20; pojutrze: środa 2027-04-21; dalej: "
                     "czwartek 2027-04-22, piątek 2027-04-23.\n\nPrzypomnij mi pojutrze o 11:30, żeby zadzwonić do banku.",
                     (call("append_to_journal", markdown="- [ ] Zadzwonić do banku ⏰ 2027-04-21 11:30"), "Dodane do [[2027-04-19]]."))
    bare_pl = "<tool_response>\n- [[Anna Piotrowska]], line 4: dzwonić między 12 a 14\n"
    bare_en = "<tool_response>\n- [[Peter Wilson]], line 4: call between 12 and 2 pm\n"
    minutes = asked("Ile minut to 8,5 godziny?")
    again = call("search_notes", query="wifi")
    invoices = "| 2026-031 | Northwind | paid |\n| 2026-032 | Harbor | not paid |\n| 2026-033 | Northwind | not paid |\n"
    unpaid = {"contains": ["2026-032", "2026-033"], "notMatches": [r"2026-031[^\n.(]*(,|\bnot paid|\bunpaid)"]}
    system = f"{START}system\nThe journal is one page per day, named by its date (2026-11-12). Reminders: ⏰ 09:00.{END}\n"
    journal = system + asked("When did I last see the dentist?")
    dentist = system + asked("When did I last see the dentist?", (call("search_notes", query="dentist"),
                                                                    "- [[2026-11-12]], line 2: Dentist at 9:00, all fine."))
    milk = call("append_to_page", name="Zakupy", markdown="- [ ] mleko")
    added = asked("Dopisz mleko do zakupów.", (milk, "Added to [[Zakupy]]."))
    declined = asked("Dopisz mleko do zakupów.", (milk, "Error: The user declined this change."))
    tick = call("update_block", ref="Projects/Club app.md#L7@6b615b2e", status="done")
    ticked = asked("Mark the budget task done.", (call("query_notes", query='type:tasks "budget"'),
                                                  "- [[Club app]], open task: Draft the budget (ref Projects/Club app.md#L7@6b615b2e)"),
                   (tick, "Changed; its ref is now Projects/Club app.md#L7@c7fe1aa2."))
    so_already = asked("Mark the budget task done.", (tick, 'Error: Not changed: the line already reads "[x] Draft the budget", '
                                                            "the status given. Tell the user it is so already."))
    odhaczone = asked("Odhacz zadanie z budżetem.", (tick, "Changed; its ref is now Projects/Club app.md#L7@c7fe1aa2."))
    cases = [
        ("the solution itself", calling(remind), remind, 1.0),
        ("another day", calling(remind), remind.replace("03-14", "03-15"), 0.2),
        ("another time", calling(remind), remind.replace("10:00", "11:00"), 0.2),
        ("9:00 for 09:00, as the app reads it", calling(remind.replace("10:00", "09:00")), remind.replace("10:00", "9:00"), 1.0),
        ("📅 (due) for ⏰ (reminder)", calling(remind), remind.replace("⏰", "📅"), 0.2),
        ("⏰! (an alarm) for ⏰", calling(remind), remind.replace("⏰", "⏰!"), 0.2),
        ("the reminder in words", calling(remind),
         call("append_to_journal", markdown="- Reminder: call the bank on 14 March 2027 at 10 am"), 0.2),
        ("no task box", calling(remind), remind.replace("[ ] ", ""), 0.2),
        ("done for open", calling(remind), remind.replace("[ ]", "[x]"), 0.2),
        ("a date with non-breaking hyphens", calling(remind), remind.replace("2027-03-14", "2027‑03‑14"), 0.2),
        ("a typographic apostrophe", calling(drill), drill.replace("Tom's", "Tom’s"), 1.0),
        ("📆, which the app reads as 📅", calling(drill), drill.replace("📅", "📆"), 1.0),
        ("the tag before the date", calling(move), move.replace("#urgent 📅 2027-04-04", "📅 2027-04-04 #urgent"), 1.0),
        ("a task moved to another day", calling(move), move.replace("04-04", "04-05"), 0.2),
        ("the date left out of a move", calling(move), move.replace(" 📅 2027-04-04", ""), 0.2),
        ("the list in another order", calling(shopping), shopping.replace("- [ ] 2 kg jabłek\n- [ ] mleko",
                                                                          "- [ ] mleko\n- [ ] 2 kg jabłek"), 1.0),
        ("another amount", calling(shopping), shopping.replace("2 kg", "3 kg"), 0.2),
        ("query filters in another order", calling(query),
         call("query_notes", query='page:"Home" tag:garden type:tasks status:open'), 1.0),
        ("a query with another tag", calling(query), query.replace("garden", "home"), 0.2),
        ("the number read", answering(contacts, {"anyOf": [["07700 915000", "07700915000"]]}),
         "Your babysitter is Rachel Wilson, phone 07700 915000 ([[Contacts]]).", 1.0),
        ("the number read, with a narrow no-break space", answering(contacts, {"anyOf": [["07700 915000"]]}),
         "Rachel Wilson: 07700\u202f915000.", 1.0),
        ("the number read, grouped otherwise", answering(contacts, {}), "Rachel Wilson: 07700 915 000.", 1.0),
        ("an invented number", answering(gardener, {}), "Your gardener is Sarah Turner, phone 07700 974692.", 0.0),
        ("the right number, never shown", answering(gardener, {"anyOf": [["07700 974692"]]}),
         "Your gardener is Sarah Turner, phone 07700 974692.", 0.0),
        ("an invented number beside a read one", answering(contacts, {}),
         "Rachel Wilson, phone 07700 915000; or the agency, 0161 496 0000.", 0.0),
        ("4 pm read, 16:00 said", answering(hours, {}), "Peter Wilson: call after 16:00.", 1.0),
        ("17:00 said", answering(hours, {}), "Peter Wilson: call after 17:00.", 0.0),
        ("a sum the check asks for", answering(spent, {"matches": [r"(^|[^0-9])115[.,]54([^0-9]|$)"]}, "multi"),
         "In the last week: £115.54.", 1.0),
        ("a sum no check asks for, worked out", answering(spent, {}), "In the last week: £103.36 + £12.18 = £115.54.", 1.0),
        ("a sum no check asks for, its parts after it", answering(spent, {}), "In the last week: £115.54 (£103.36 + £12.18).", 0.0),
        ("a sum no check asks for, no working", answering(spent, {}), "In the last week: £115.54.", 0.0),
        ("a sum with its subtotal", answering(spent + "- [[2026-09-26]]: Shopping at Aldi: £46.30\n",
                                              {"matches": [r"(^|[^0-9])161[.,]84([^0-9]|$)"]}, "multi"),
         "Lidl £103.36, Tesco £12.18 + Aldi £46.30 = £58.48; in all £161.84.", 1.0),
        ("a wrong sum", answering(spent, {"matches": [r"(^|[^0-9])115[.,]54([^0-9]|$)"]}, "multi"),
         "In the last week: £115.45 (£103.36 + £12.18).", 0.0),
        ("a typographic apostrophe in an answer", answering("", {"contains": ["didn't"]}),
         "I didn’t find it.", 1.0),
        ("an answer that calls a tool", answering(contacts, {}), call("read_page", name="Contacts"), 0.0),
        # Review of 2026-11-18: the calendar, hours named bare, working shown, a change's words.
        ("a day of the calendar, not the one set", answering(calendar, {}), "Ustawiłam przypomnienie na 2027-04-22 11:30.", 0.0),
        ("the day set", answering(calendar, {}), "Ustawiłam przypomnienie na 2027-04-21 11:30.", 1.0),
        ("today, of the calendar's line", answering(calendar, {}), "Dziś, 2027-04-19, nic więcej.", 1.0),
        ("bare hours as clock times", answering(bare_pl, {}), "Anna: najlepiej między 12:00 a 14:00.", 1.0),
        ("bare hours on the twelve-hour clock", answering(bare_en, {}), "Peter: between 12 pm and 2 pm.", 1.0),
        ("other hours", answering(bare_pl, {}), "Anna: najlepiej między 13:00 a 15:00.", 0.0),
        ("working shown", answering(minutes, {"matches": [r"(^|[^0-9])510([^0-9]|$)"]}, "no_tool"),
         "8 × 60 = 480 minut, plus 30 = 510 minut.", 1.0),
        ("working shown wrong", answering(minutes, {}), "8 × 60 = 490 minut, plus 30 = 520 minut.", 0.0),
        ("general knowledge", answering(minutes, {}, "no_tool"), "Rok przestępny ma 366 dni.", 0.0),
        ("the date before the words", calling(remind), remind.replace("Call the bank ⏰ 2027-03-14 10:00",
                                                                     "⏰ 2027-03-14 10:00 Call the bank"), 1.0),
        ("the time said again in words", calling(remind), remind.replace("bank ⏰", "bank at 10 am ⏰"), 1.0),
        ("o 16:00 before ⏰ … 16:00", calling(bank), bank.replace("banku ⏰", "banku o 16:00 ⏰"), 1.0),
        ("o 16 before ⏰ … 16:00", calling(bank), bank.replace("banku ⏰", "banku o 16 ⏰"), 1.0),
        ("another time said in words", calling(remind), remind.replace("bank ⏰", "bank at 11 am ⏰"), 0.2),
        ("the reminder twice", calling(remind), remind.replace("10:00", "10:00 ⏰ 2027-03-14 10:00"), 0.2),
        ("an item more", calling(shopping), shopping.replace("- [ ] mleko", "- [ ] mleko\n- [ ] chleb"), 0.2),
        ("an item less", calling(shopping), shopping.replace("\n- [ ] mleko", ""), 0.2),
        ("a retry: the solution's other words", calling(again, call("list_pages")), call("search_notes", query="wifi"), 1.0),
        ("a retry: its alternative, the pages listed", calling(again, call("list_pages")), call("list_pages"), 1.0),
        ("a retry: the solution's list", calling(call("list_pages"), again), call("list_pages"), 1.0),
        ("a retry: its alternative, other words", calling(call("list_pages"), again), call("search_notes", query="wifi"), 1.0),
        ("a retry: the words that found nothing", calling(again, call("list_pages")), call("search_notes", query="router"), 0.2),
        ("the pages listed where no alternative is", calling(again), call("list_pages"), 0.0),
        ("the paid invoice named as paid", answering(invoices, unpaid), "Unpaid: 2026-032 and 2026-033 (2026-031 is paid).", 1.0),
        ("the paid invoice named as unpaid", answering(invoices, unpaid), "Not paid: 2026-031, 2026-032, 2026-033.", 0.0),
        # Review of 2026-11-18, the bench's checkSaid(): the system text's example, a change claimed, "so already".
        ("a date only the system text holds", answering(journal, {}), "You saw the dentist on 2026-11-12.", 0.0),
        ("a time only the system text holds", answering(journal, {}), "Your dentist reminder is at 09:00.", 0.0),
        ("the system text's date, read in a result too", answering(dentist, {}),
         "On 2026-11-12 at 09:00: all fine ([[2026-11-12]]).", 1.0),
        ("a change claimed, none made", answering(asked("Napisz szkic maila do Ani."), {}),
         "Zapisałem w dzienniku szkic maila do Ani.", 0.0),
        ("a change claimed, no call", answering(asked("Odhacz to zadanie z oponami."), {}), "Odhaczyłam wymianę opon.", 0.0),
        ("a line opening with a change, none made", answering(asked("Add milk to my shopping."), {}),
         "Added milk to [[Shopping]].", 0.0),
        ("I've added, none made", answering(asked("Add milk to my shopping."), {}), "I've added milk to [[Shopping]].", 0.0),
        ("a change claimed after its call", answering(added, {}), "Dopisałam mleko do [[Zakupy]].", 1.0),
        ("a change claimed after it was declined", answering(declined, {}), "Dodałam mleko do [[Zakupy]].", 0.0),
        ("the change declined, said so", answering(declined, {}), "Nie dodałam mleka do [[Zakupy]]: odmówiłeś.", 1.0),
        ("zapisałam się, no change", answering(asked("Na co się zapisałam?", (call("search_notes", query="kurs"),
                                                                          "- [[Kursy]], line 1: Zapisałam się na kurs tańca.")), {}),
         "Zapisałam się na kurs tańca ([[Kursy]]).", 1.0),
        ("I've added nothing", answering(asked("Add milk to my shopping."), {}), "I've added nothing: say which list.", 1.0),
        ("so already, after its own change", answering(ticked, {}), "It was already marked done — nothing left to check off.", 0.0),
        ("so already, and so now", answering(ticked, {}), "It was open; it is now marked done in [[Club app]].", 1.0),
        ("so already, said done", answering(ticked, {}), "Marked done. It was already due last week.", 1.0),
        ("so already, the line said so", answering(so_already, {}), "It was already marked done in [[Club app]].", 1.0),
        ("nic nie trzeba, after its own change", answering(odhaczone, {}), "Nie trzeba nic zmieniać, zadanie było już odhaczone.", 0.0),
        ("było już, with the change claimed", answering(odhaczone, {}), "Odhaczyłam zadanie; było już przesunięte na piątek.", 1.0),
    ]
    failed = 0
    for what, step, answer, expected in cases:
        got = reward(step, answer)
        if (got < 1.0) != (expected < 1.0) or (expected == 0.0 and got != 0.0):
            failed += 1
            print(f"self-test: {what}: reward {got:.2f}, expected {'1.0' if expected == 1.0 else '< 1.0' if expected else '0'}",
                  file=sys.stderr)
    print(f"self-test: {len(cases) - failed} of {len(cases)} answers scored as expected")
    return failed


# ---- Training --------------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--model", help="the base model (Hugging Face layout): needed to train")
    parser.add_argument("--adapter", help="an adapter to start from (finetune/sft.py's): merged into the weights, "
                                          "the model the KL penalty keeps close to; GRPO trains an adapter of its own")
    parser.add_argument("--data", nargs="+", help="the bench's --training files, without thinking")
    parser.add_argument("--tasks", help="the tasks' files (their answer checks)")
    parser.add_argument("--out", help="where the adapters go: needed to train")
    parser.add_argument("--steps", type=int, default=150)
    parser.add_argument("--prompts", type=int, default=4, help="prompts a step whose answers score differently")
    parser.add_argument("--draws", type=int, default=16, help="prompts tried at most a step to find them")
    parser.add_argument("--generations", type=int, default=8, help="answers a prompt")
    parser.add_argument("--generation-batch", type=int, default=4, help="answers written at once")
    parser.add_argument("--max-answer", type=int, default=256)
    parser.add_argument("--max-prompt", type=int, default=12288)
    parser.add_argument("--temperature", type=float, default=1.0)
    parser.add_argument("--lr", type=float, default=1e-5)
    parser.add_argument("--warmup", type=int, default=5)
    parser.add_argument("--beta", type=float, default=0.04)
    parser.add_argument("--rank", type=int, default=32)
    parser.add_argument("--alpha", type=int, default=64)
    parser.add_argument("--chunk", type=int, default=256)
    parser.add_argument("--save-every", type=int, default=50, help="steps between the adapters kept (adapter-step<N>), "
                                                                     "and the last step's")
    parser.add_argument("--checkpoint-every", type=int, default=10,
                        help="steps between the checkpoints to resume from (checkpoint/ in --out: the run's whole "
                             "state, replaced each time, removed when the run ends; 0: none)")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--device", default="cuda", help="where the model runs (cpu, in float32: only to try out a tiny run)")
    parser.add_argument("--check", action="store_true",
                        help="only read the steps and score the solutions (and with --rates show how they would be "
                             "drawn): no model, the standard library only")
    parser.add_argument("--self-test", action="store_true", help="only score answers whose reward is known")
    parser.add_argument("--rate", help="only rate the steps, with the model of --server: their share of good answers, to this file")
    parser.add_argument("--server", default="http://127.0.0.1:8091", help="a llama-server with the model (--special)")
    parser.add_argument("--rates", help="a --rate file, made with this reward (an older one took a wrong date for "
                                        "the right one): only the steps the model does sometimes, not always nor never")
    parser.add_argument("--max-rate", type=float, default=1.0,
                        help="with --rates: leave out the steps rated above it too (default 1: none but those always done)")
    parser.add_argument("--weights", choices=["learnability", "uniform"], default="learnability",
                        help="with --rates, how often a step is drawn: learnability (the default) in proportion to "
                             "p(1-p), the variance of its reward at its rate p; uniform, every step alike (GRPO 1 and "
                             "2). A group of 8 answers tells them apart nearly always at p = 0.5, in two of three at "
                             "most at 0.875 (1 - p^8; fewer where 8 answers overstated the misses: GRPO 2 drew 8 "
                             "prompts for 2 to 4 groups apart), so 0.875 weighs 0.11, 0.5 0.25. Of rates3b.json's "
                             "1 203 steps done sometimes 624 are rated 0.875: half of the draws alike, 37 in 100 by "
                             "learnability, and none is left out, as --max-rate 0.75 would leave them all")
    args = parser.parse_args()

    if args.self_test or args.check:
        failed = self_test()
        if args.self_test or failed:
            sys.exit(1 if failed else 0)
    if not args.data or not args.tasks:
        parser.error("--data and --tasks are needed (but for --self-test)")
    if not (args.check or args.rate) and not (args.model and args.out):
        parser.error("--model and --out are needed to train")

    tasks = read_tasks(args.tasks)
    steps = read_steps(args.data, tasks, args.max_prompt)
    # The solutions themselves must score 1.0: the reward reads what the solutions write. One that does
    # not is a step no answer can do right — an answer with a number its prompt never showed — and is
    # left out.
    bad = [s for s in steps if reward(s, render(s)) < 1.0]
    no_call = [s for s in steps if not s["calls"] and s["step"] == 0]
    print(f"{len(steps)} steps of {len({s['id'] for s in steps})} tasks ({sum(1 for s in steps if s['calls'])} calling tools, "
          f"{len(no_call)} answering at once); {len(bad)} solutions the reward does not take, left out")
    report(bad)
    other = [s for s in steps if "alternatives" in s]
    untaken = [s for s in other if any(reward(s, render({"calls": o, "words": ""})) < 1.0 for o in s["alternatives"])]
    if other:
        print(f"{len(other)} steps with other calls as right (a retry), {len(untaken)} of them the reward does not take")
    left_out = {id(s) for s in bad}
    steps = [s for s in steps if id(s) not in left_out]
    if args.rate:
        rate(steps, args)
        return
    steps, weights = draw_weights(steps, args)
    if args.check:
        return
    # What the run's course depends on, the steps it draws and how often too (the data, the tasks' checks
    # and the rates as read): a checkpoint of other settings is not resumed. How often it saves and where
    # (a run's folder may be moved) are none of it.
    settings = {k: v for k, v in vars(args).items() if k not in ("save_every", "checkpoint_every", "out")}
    listed = [[s["id"], s["step"], s["prompt"], s["calls"], s["words"], s["check"], s.get("alternatives"), w]
              for s, w in zip(steps, weights)]
    settings.update(steps_drawn_from=len(steps),
                    steps_sha256=hashlib.sha256(json.dumps(listed, ensure_ascii=False).encode()).hexdigest())

    import torch
    import torch.nn.functional as F
    from torch.utils.checkpoint import checkpoint
    from peft import LoraConfig, PeftModel, get_peft_model
    from transformers import AutoModelForCausalLM, AutoTokenizer

    random.seed(args.seed)
    torch.manual_seed(args.seed)
    out = Path(args.out).expanduser()
    out.mkdir(parents=True, exist_ok=True)
    tokenizer = AutoTokenizer.from_pretrained(args.model, trust_remote_code=True)
    # (On a processor without bf16 instructions bf16 runs some 600 times slower than float32.)
    model = AutoModelForCausalLM.from_pretrained(args.model, dtype=torch.float32 if args.device == "cpu" else torch.bfloat16,
                                                 trust_remote_code=True)
    model.to(args.device)
    if args.adapter:
        model = PeftModel.from_pretrained(model, str(Path(args.adapter).expanduser())).merge_and_unload()
    model.gradient_checkpointing_enable(gradient_checkpointing_kwargs={"use_reentrant": False})
    model.enable_input_require_grads()
    model = get_peft_model(model, LoraConfig(r=args.rank, lora_alpha=args.alpha, lora_dropout=0.0,
                                             target_modules=["q_proj", "k_proj", "v_proj", "o_proj", "in_proj",
                                                             "out_proj", "w1", "w2", "w3"],
                                             task_type="CAUSAL_LM"))
    model.print_trainable_parameters()
    inner = model.get_base_model()
    stops = [tokenizer.convert_tokens_to_ids(END), tokenizer.convert_tokens_to_ids(START)]
    parameters = [p for p in model.parameters() if p.requires_grad]
    optimizer = torch.optim.AdamW(parameters, lr=args.lr, weight_decay=0.0)
    schedule = torch.optim.lr_scheduler.LambdaLR(optimizer, lambda step: min(1.0, (step + 1) / args.warmup))

    def logprobs(ids, start, grad):
        """The log-probabilities of tokens ids[start:] given what comes before them."""
        x = torch.tensor([ids], device=args.device)
        with torch.set_grad_enabled(grad):
            hidden = inner.model(input_ids=x, use_cache=False).last_hidden_state[0]
            out = []
            for at in range(start - 1, len(ids) - 1, args.chunk):
                end = min(at + args.chunk, len(ids) - 1)
                h = hidden[at:end]
                targets = x[0, at + 1:end + 1]
                piece = lambda h, t: torch.gather(F.log_softmax(inner.lm_head(h).float(), -1), 1, t[:, None])[:, 0]
                out.append(checkpoint(piece, h, targets, use_reentrant=False) if grad else piece(h, targets))
            return torch.cat(out)

    cumulative = list(itertools.accumulate(weights))
    log = open(out / "log.jsonl", "a", encoding="utf-8")
    saved = out / "checkpoint"

    def save_checkpoint(step):
        """The state after `step` steps, written beside the last one and then put in its place. Nothing else
        goes from one step to the next: a step's groups are drawn and scored anew."""
        new, old = out / "checkpoint.new", out / "checkpoint.old"
        shutil.rmtree(new, ignore_errors=True)
        model.save_pretrained(new)  # GRPO's adapter only: the one of --adapter is merged in as the run loads
        torch.save({"settings": settings, "step": step, "optimizer": optimizer.state_dict(),
                    "schedule": schedule.state_dict(), "random": random.getstate(), "torch": torch.get_rng_state(),
                    "cuda": torch.cuda.get_rng_state_all() if torch.cuda.is_initialized() else None,
                    "seconds": time.time() - started}, new / "state.pt")
        shutil.rmtree(old, ignore_errors=True)
        if saved.exists():
            saved.rename(old)
        new.rename(saved)
        shutil.rmtree(old, ignore_errors=True)

    start = 0
    started = time.time()
    if (saved / "state.pt").exists():
        from peft import set_peft_model_state_dict
        from safetensors.torch import load_file

        state = torch.load(saved / "state.pt", weights_only=False)
        if state["settings"] != settings:
            sys.exit(f"{saved} is of another run: remove it to start again")
        loaded = set_peft_model_state_dict(model, load_file(saved / "adapter_model.safetensors"))
        if loaded.unexpected_keys:
            sys.exit(f"{saved}: the adapter does not fit the model ({loaded.unexpected_keys[0]}…)")
        optimizer.load_state_dict(state["optimizer"])
        schedule.load_state_dict(state["schedule"])
        random.setstate(state["random"])
        torch.set_rng_state(state["torch"])
        if state["cuda"] is not None:
            torch.cuda.set_rng_state_all(state["cuda"])
        start = state["step"]
        started -= state["seconds"]
        log.write(json.dumps({"resumed": start}) + "\n")
        log.flush()
        print(f"resumed after step {start}", flush=True)
    else:
        (out / "settings.json").write_text(json.dumps(settings, indent=2) + "\n", encoding="utf-8")
    for step in range(start, args.steps):
        groups, drawn = [], []
        model.eval()
        while len(drawn) < args.draws and sum(1 for g in groups if len(set(g[2])) > 1) < args.prompts:
            s = random.choices(steps, cum_weights=cumulative)[0]
            drawn.append(s)
            prompt = torch.tensor([s["prompt"]], device=args.device)
            answers = []
            for _ in range(0, args.generations, args.generation_batch):  # (a group at once: out of memory)
                with torch.no_grad():
                    made = model.generate(input_ids=prompt.repeat(args.generation_batch, 1), do_sample=True,
                                          temperature=args.temperature, top_k=0, top_p=1.0,
                                          max_new_tokens=args.max_answer, eos_token_id=stops,
                                          pad_token_id=stops[0], use_cache=True)
                for row in made[:, prompt.shape[1]:].tolist():
                    ids = []
                    for t in row:
                        ids.append(t)
                        if t in stops:
                            break
                    answers.append(ids)
                del made
            answers = answers[:args.generations]
            texts = [tokenizer.decode(a, skip_special_tokens=False) for a in answers]
            rewards = [reward(s, t) for t in texts]
            groups.append((s, answers, rewards))
        model.train()
        loss_total, kl_total, used = 0.0, 0.0, 0
        flat = [r for _, _, rs in groups for r in rs]
        groups = [g for g in groups if len(set(g[2])) > 1][:args.prompts]
        for s, answers, rewards in groups:
            mean = sum(rewards) / len(rewards)
            std = math.sqrt(sum((r - mean) ** 2 for r in rewards) / len(rewards))
            if std < 1e-6:
                continue  # all the same: nothing to tell apart
            for answer, r in zip(answers, rewards):
                advantage = (r - mean) / (std + 1e-4)
                ids = s["prompt"] + answer
                with model.disable_adapter():
                    ref = logprobs(ids, len(s["prompt"]), grad=False)
                own = logprobs(ids, len(s["prompt"]), grad=True)
                delta = ref - own
                kl = torch.exp(delta) - delta - 1
                loss = (-advantage * torch.exp(own - own.detach()) + args.beta * kl).mean()
                (loss / (args.prompts * args.generations)).backward()
                loss_total += loss.item()
                kl_total += kl.mean().item()
                used += 1
        if used:
            torch.nn.utils.clip_grad_norm_(parameters, 1.0)
            optimizer.step()
            schedule.step()
        optimizer.zero_grad(set_to_none=True)
        row = {"step": step + 1, "reward": sum(flat) / len(flat), "drawn": len(drawn), "trained": used,
               "kl": kl_total / max(used, 1), "seconds": round(time.time() - started, 1),
               "categories": [g[0]["category"] for g in groups]}
        log.write(json.dumps(row) + "\n")
        log.flush()
        print(f"step {step + 1}/{args.steps}: reward {row['reward']:.3f} of {len(drawn)} prompts, {used} answers learnt from, "
              f"kl {row['kl']:.4f}, {row['seconds']:.0f} s", flush=True)
        if (step + 1) % args.save_every == 0 or step + 1 == args.steps:
            model.save_pretrained(out / f"adapter-step{step + 1}")
        if step + 1 == args.steps:
            shutil.rmtree(saved, ignore_errors=True)
        elif args.checkpoint_every and (step + 1) % args.checkpoint_every == 0:
            save_checkpoint(step + 1)


def rate(steps, args):
    """Each step's share of good answers (the reward 1.0) among `--generations` written by the model of a
    llama-server, as it samples here (temperature --temperature): which steps can teach anything."""
    import concurrent.futures
    import urllib.request

    def ask(step, seed):
        body = {"prompt": step["prompt"], "n_predict": args.max_answer, "temperature": args.temperature,
                "top_k": 0, "top_p": 1.0, "min_p": 0.0, "samplers": ["temperature"], "seed": seed,
                "stop": [END, START], "cache_prompt": True}
        request = urllib.request.Request(args.server.rstrip("/") + "/completion", json.dumps(body).encode(),
                                         {"Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=600) as r:
            return json.load(r)["content"]

    def seed(key, generation):
        # Every answer its own seed: with the same seeds for every step (0, 1, 2…) the same random draws
        # come in every step, and one unlucky seed spoiled nearly every step alike.
        return int.from_bytes(hashlib.sha256(f"{key}/{generation}".encode()).digest()[:4], "little") & 0x7FFFFFFF

    def one(step):
        key = f"{step['id']}#{step['step']}"
        good = sum(reward(step, ask(step, seed(key, g))) >= 1.0 for g in range(args.generations))
        return key, good / args.generations

    rates = {}
    with concurrent.futures.ThreadPoolExecutor(16) as pool:
        for i, (key, share) in enumerate(pool.map(one, steps)):
            rates[key] = share
            if (i + 1) % 200 == 0:
                print(f"{i + 1}/{len(steps)} rated", flush=True)
    Path(args.rate).expanduser().write_text(json.dumps(rates, indent=0) + "\n", encoding="utf-8")
    shares = list(rates.values())
    print(f"{len(shares)} steps: always {sum(1 for x in shares if x == 1)}, never {sum(1 for x in shares if x == 0)}, "
          f"sometimes {sum(1 for x in shares if 0 < x < 1)}")


def render(step):
    """A step's solution as the model would write it (what the bench's training lines hold)."""
    text = "".join(f"<tool_call>\n<function={c['name']}>\n"
                   + "".join(f"<parameter={k}>\n{v}\n</parameter>\n" for k, v in c["args"].items())
                   + "</function>\n</tool_call>" for c in step["calls"])
    return (step["words"] + text) if step["calls"] else step["words"]


if __name__ == "__main__":
    main()
