#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///
"""The trained model and its tasks as two folders ready for `hf upload` (docs/FINETUNE.md "Publishing").

    python3 -I finetune/publish.py --owner <owner> --adapter ~/.cache/brulion-finetune/combined-3 \
        [--export ~/.cache/brulion-finetune/combined-3/export --with-gguf BF16 Q4_0 Q4_K_M --with-weights] \
        --eval runs/a4-final="Brulion Assistant 1.7B" --eval runs/a3-base-nothink="LFM3-1.7B, no thinking" \
        --test data/test --sft data/train data/train2 --grpo data/train3 \
        [--training runs/taught-training.jsonl …] --out ~/.cache/brulion-publish

Writes into --out:

* `model/` — the adapter as GGUF for llama.cpp's `--lora` (and in PEFT's format), optionally the
  merged weights and GGUF files of finetune/export.py, the model card (finetune/cards/model.md),
  the base model's LICENSE, a NOTICE of what was changed, SHA256SUMS;
* `dataset/` — the hand-written and generated notebooks and tasks as the bench reads them (a
  split may join several generated folders: `--sft train train2`), the same as JSON lines for
  Hugging Face's viewer, optionally the training sequences, the dataset card
  (finetune/cards/dataset.md).

The evaluation tables are made from the bench's result files (`--out` of brulion-assistantbench:
`<prefix>-test.jsonl` and `<prefix>-hand.jsonl` for each `--eval <prefix>[=<label>]`, the first
one this model); a model run several times (`--attempts`) shows the mean of its runs. The training
is described from the adapters' settings.json and log.jsonl (combined-from.json names them). The
generated folders are made again with finetune/generate.py and compared, so the card says whether
the generator reproduces them. The GGUF files get metadata of their own — this model's name, the
base model it comes from, the license — in place of the local paths and folder names the converters
wrote; their tensors are copied byte for byte. Every file to publish is searched for local paths,
the git user's e-mail address and the machine's name. Nothing is uploaded: the `hf upload`
commands are printed.

Only the standard library: it runs as well with `python3 -I finetune/publish.py`.
"""

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import shutil
import socket
import statistics
import struct
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CARDS = Path(__file__).resolve().parent / "cards"
APP_URL = "https://github.com/janusz-bit/brulion"
BASE_ID = "LiquidAI/LFM3-1.7B"
BASE_URL = f"https://huggingface.co/{BASE_ID}"
BASE_REVISION = "1da06bec6a819da10b54c67e5c90972b46d81871"  # docs/FINETUNE.md "Training"
ADAPTER_FILES = ("adapter_config.json", "adapter_model.safetensors")
SPLITS = {  # the dataset's splits: what each is for
    "hand": "written by hand; measuring only",
    "test": "generated; measuring only",
    "sft": "generated; supervised fine-tuning",
    "grpo": "generated; GRPO",
}
LICENSE_NAMES = {"cc-by-4.0": "CC BY 4.0", "cc0-1.0": "CC0 1.0", "cc-by-sa-4.0": "CC BY-SA 4.0",
                 "agpl-3.0": "AGPL-3.0", "mit": "MIT", "apache-2.0": "Apache-2.0"}
CATEGORIES = ["search", "query", "read", "append", "journal", "update", "write", "multi", "no_tool",
              "clarify", "decline"]


def fail(message):
    sys.exit(f"publish.py: {message}")


def warn(message):
    print(f"warning: {message}", file=sys.stderr)


# ---- Files ------------------------------------------------------------------------------------

def sha256(path, start=0):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        f.seek(start)
        while chunk := f.read(16 << 20):
            digest.update(chunk)
    return digest.hexdigest()


def copy(source, target):
    """A copy (on btrfs or XFS a reflink: no room taken), never a hard link — export.py writing its
    folder again must not change what is about to be uploaded."""
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    os.chmod(target, 0o644)


def size_text(n):
    for unit, scale in (("GB", 1e9), ("MB", 1e6), ("kB", 1e3)):
        if n >= scale:
            return f"{n / scale:.2f} {unit}" if n < 10 * scale else f"{n / scale:.1f} {unit}"
    return f"{n} B"


def thousands(n):
    return f"{n:,}".replace(",", " ")  # a narrow no-break space, as the docs write numbers


# ---- GGUF metadata ----------------------------------------------------------------------------
# A GGUF file: "GGUF", version, tensor count, key count, the keys (name, type, value), the tensors'
# names, shapes, types and offsets, padding to the alignment, then the tensors' data. Offsets count
# from the start of the data, so new keys only move the data as a whole.

GGUF_SCALARS = {0: "<B", 1: "<b", 2: "<H", 3: "<h", 4: "<I", 5: "<i", 6: "<f", 7: "<?", 10: "<Q",
                11: "<q", 12: "<d"}
GGUF_STRING, GGUF_ARRAY = 8, 9
GGUF_TYPES = {0: "f32", 1: "f16", 30: "bf16"}  # a tensor's type, of those an adapter is written in
# general.* keys about the file itself, kept; the rest of general.* (names, paths, tags) is replaced.
GGUF_KEPT = ("general.architecture", "general.type", "general.alignment", "general.file_type",
             "general.quantization_version")


class GgufHeader:
    def __init__(self, path):
        self.path = Path(path)
        self.size = self.path.stat().st_size
        with open(path, "rb") as f:
            self._f, self._buf, self._pos = f, bytearray(), 0
            if self._take(4) != b"GGUF":
                raise ValueError(f"{path}: not a GGUF file")
            self.version = self._num("<I")
            if self.version < 3:
                raise ValueError(f"{path}: GGUF version {self.version}, only 3 is known")
            tensors = self._num("<Q")
            count = self._num("<Q")
            self.keys = []  # (key, value or None, the entry's bytes as written)
            for _ in range(count):
                start = self._pos
                key = self._text()
                kind = self._num("<I")
                read = key.startswith(("general.", "adapter.")) or key == "tokenizer.chat_template"
                value = self._value(kind) if read else self._skip(kind)
                self.keys.append((key, value, bytes(self._buf[start:self._pos])))
            start = self._pos
            self.tensors = []
            for _ in range(tensors):
                name = self._text()
                dims = struct.unpack(f"<{(n := self._num('<I'))}Q", self._take(8 * n))
                self.tensors.append((name, dims, self._num("<I"), self._num("<Q")))
            self.infos = bytes(self._buf[start:self._pos])
            self.alignment = int(self.value("general.alignment", 32))
            self.data_start = -(-self._pos // self.alignment) * self.alignment
        del self._f, self._buf

    def value(self, key, default=None):
        return next((v for k, v, _ in self.keys if k == key), default)

    def _take(self, n):
        while len(self._buf) < self._pos + n:
            chunk = self._f.read(max(1 << 20, n))
            if not chunk:
                raise ValueError(f"{self.path}: the file ends inside its header")
            self._buf += chunk
        out = bytes(self._buf[self._pos:self._pos + n])
        self._pos += n
        return out

    def _num(self, fmt):
        return struct.unpack(fmt, self._take(struct.calcsize(fmt)))[0]

    def _text(self):
        return self._take(self._num("<Q")).decode("utf-8")

    def _value(self, kind):
        if kind in GGUF_SCALARS:
            return self._num(GGUF_SCALARS[kind])
        if kind == GGUF_STRING:
            return self._text()
        if kind == GGUF_ARRAY:
            item, n = self._num("<I"), self._num("<Q")
            return [self._value(item) for _ in range(n)]
        raise ValueError(f"{self.path}: unknown GGUF value type {kind}")

    def _skip(self, kind):
        if kind in GGUF_SCALARS:
            self._take(struct.calcsize(GGUF_SCALARS[kind]))
        elif kind == GGUF_STRING:
            self._take(self._num("<Q"))
        elif kind == GGUF_ARRAY:
            item, n = self._num("<I"), self._num("<Q")
            if item in GGUF_SCALARS:
                self._take(n * struct.calcsize(GGUF_SCALARS[item]))
            else:
                for _ in range(n):
                    self._skip(item)
        else:
            raise ValueError(f"{self.path}: unknown GGUF value type {kind}")


def gguf_entry(key, value):
    def text(s):
        b = s.encode("utf-8")
        return struct.pack("<Q", len(b)) + b
    if isinstance(value, str):
        return text(key) + struct.pack("<I", GGUF_STRING) + text(value)
    if isinstance(value, int):
        return text(key) + struct.pack("<II", 4, value)  # uint32, as gguf-py writes counts
    if isinstance(value, list) and all(isinstance(v, str) for v in value):
        return text(key) + struct.pack("<IIQ", GGUF_ARRAY, GGUF_STRING, len(value)) + b"".join(map(text, value))
    raise TypeError(f"{key}: {value!r}")


def write_gguf(source, target, metadata):
    """`source` with its general.* names replaced by `metadata`; the tensors byte for byte. Returns
    the new file's SHA-256."""
    old = GgufHeader(source)
    kept = [(key, raw) for key, _, raw in old.keys
            if not key.startswith("general.") or key in GGUF_KEPT or key.startswith("general.sampling.")]
    keys = [raw for _, raw in kept]
    at = next((i + 1 for i, (key, _) in enumerate(kept) if key == "general.type"), 0)
    keys[at:at] = [gguf_entry(k, v) for k, v in metadata.items()]  # where the converters write names
    head = b"GGUF" + struct.pack("<IQQ", old.version, len(old.tensors), len(keys)) + b"".join(keys) + old.infos
    head += bytes(-len(head) % old.alignment)
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.name + ".partial")
    data = hashlib.sha256()
    with open(source, "rb") as fin, open(partial, "wb") as fout:
        fout.write(head)
        fin.seek(old.data_start)
        while chunk := fin.read(16 << 20):
            data.update(chunk)
            fout.write(chunk)
    new = GgufHeader(partial)
    if new.infos != old.infos or new.data_start != len(head) or new.size - new.data_start != old.size - old.data_start \
            or sha256(partial, new.data_start) != data.hexdigest():
        partial.unlink()
        fail(f"{target.name}: the rewritten file's tensors differ from {source}")
    for key, value in metadata.items():
        if new.value(key) != value:
            partial.unlink()
            fail(f"{target.name}: {key} reads back as {new.value(key)!r}")
    os.chmod(partial, 0o644)
    os.replace(partial, target)
    return sha256(target)


# ---- safetensors metadata ---------------------------------------------------------------------
# A safetensors file: the header's length (8 bytes, little-endian), the header (JSON: each tensor's
# dtype, shape and data_offsets, and "__metadata__", strings only), then the tensors' data. Offsets
# count from the start of the data, so a longer header only moves the data as a whole.

def safetensors_header(path):
    with open(path, "rb") as f:
        n = struct.unpack("<Q", f.read(8))[0]
        header = json.loads(f.read(n).decode("utf-8"))
    return header, 8 + n


def write_safetensors(source, target, metadata):
    """`source` with `metadata` added to its __metadata__ (the "format" and the rest kept); the tensors
    byte for byte. Returns the new file's SHA-256."""
    old, start = safetensors_header(source)
    meta = dict(old.get("__metadata__") or {})
    meta.update(metadata)
    if not all(isinstance(k, str) and isinstance(v, str) for k, v in meta.items()):
        fail(f"{source}: safetensors metadata must be strings")
    header = {"__metadata__": meta, **{k: v for k, v in old.items() if k != "__metadata__"}}
    raw = json.dumps(header, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    raw += b" " * (-(8 + len(raw)) % 8)  # the data 8-byte aligned, as safetensors writes it
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.name + ".partial")
    data = hashlib.sha256()
    with open(source, "rb") as fin, open(partial, "wb") as fout:
        fout.write(struct.pack("<Q", len(raw)) + raw)
        fin.seek(start)
        while chunk := fin.read(16 << 20):
            data.update(chunk)
            fout.write(chunk)
    new, new_start = safetensors_header(partial)
    if {k: v for k, v in new.items() if k != "__metadata__"} != {k: v for k, v in old.items() if k != "__metadata__"} \
            or new.get("__metadata__") != meta or sha256(partial, new_start) != data.hexdigest() \
            or partial.stat().st_size - new_start != source.stat().st_size - start:
        partial.unlink()
        fail(f"{target.name}: the rewritten file differs from {source} in more than its metadata")
    os.chmod(partial, 0o644)
    os.replace(partial, target)
    return sha256(target)


# ---- What was measured ------------------------------------------------------------------------

def read_results(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            if line.strip():
                try:
                    rows.append(json.loads(line))
                except json.JSONDecodeError as e:
                    fail(f"{path}:{number}: {e}")
    if not rows:
        fail(f"{path}: no results")
    for row in rows:
        for key in ("id", "category", "language", "passed", "rounds"):
            if key not in row:
                fail(f"{path}: a line without {key!r} — not the bench's --out")
    return rows


def summarize(rows, path):
    """A results file in numbers. A file of `--attempts N` has N runs: every number a task count
    stands for is of one run (the runs' mean), never of the N runs together."""
    tasks = {r["id"] for r in rows}
    attempts = defaultdict(int)
    for r in rows:
        attempts[r["id"]] += 1
    if len(set(attempts.values())) != 1:
        fail(f"{path}: the tasks were not run the same number of times ({sorted(set(attempts.values()))})")
    per = len(rows) // len(tasks)

    def tally(key):
        t = defaultdict(lambda: [0, 0])
        for r in rows:
            t[r[key]][0] += 1 if r["passed"] else 0
            t[r[key]][1] += 1
        return dict(t)
    by_run = defaultdict(int)
    for r in rows:
        by_run[r.get("attempt", 0)] += 1 if r["passed"] else 0
    return {
        "runs": per,  # runs of the whole set: --attempts
        "tasks": len(tasks),
        "ids": tasks,
        "passed": sum(1 for r in rows if r["passed"]),  # of all the runs
        "by_run": [by_run[k] for k in sorted(by_run)],
        "language": tally("language"),
        "category": tally("category"),
        "requests": statistics.mean(len(r["rounds"]) for r in rows),
        "failed_calls": sum(r.get("toolErrors", 0) for r in rows) / per,  # a run's
        "written": statistics.mean(sum(x.get("outputTokens", 0) for x in r["rounds"]) for r in rows),
        "temperatures": sorted({round(r["temperature"], 4) for r in rows if "temperature" in r}),
        "seeds": sorted({r["seed"] for r in rows if "seed" in r}),
        "models": sorted({r.get("model", "") for r in rows}),
    }


def load_evals(specs):
    evals = []
    for spec in specs:
        prefix, _, label = spec.partition("=")
        prefix = Path(prefix).expanduser()
        found = {}
        for part in ("test", "hand"):
            path = prefix.with_name(f"{prefix.name}-{part}.jsonl")
            if path.exists():
                found[part] = (path, summarize(read_results(path), path))
        if not found:
            fail(f"--eval {spec}: neither {prefix.name}-test.jsonl nor {prefix.name}-hand.jsonl in {prefix.parent}")
        runs = {s["runs"] for _, s in found.values()}
        if len(runs) != 1:
            fail(f"--eval {spec}: its test and hand results are of different numbers of runs")
        date = dt.date.fromtimestamp(max(p.stat().st_mtime for p, _ in found.values()))
        evals.append({"label": label or prefix.name, "date": date, "runs": runs.pop(),
                      **{part: summary for part, (_, summary) in found.items()}})
    return evals


def mean(part, runs):
    """Tasks passed in a run: `part` passes of `runs` runs, as 232.25."""
    return f"{part / runs:.2f}".rstrip("0").rstrip(".")


def done_text(s):
    if s is None:
        return "–"
    return f"{mean(s['passed'], s['runs'])} ({100 * s['passed'] / (s['runs'] * s['tasks']):.1f} %)"


def eval_table(evals):
    hand_n = max((e["hand"]["tasks"] for e in evals if "hand" in e), default=0)
    test_n = max((e["test"]["tasks"] for e in evals if "test" in e), default=0)
    languages = {}
    for e in evals:
        for code, (_, n) in e.get("test", {}).get("language", {}).items():
            languages[code] = n // e["runs"]
    lines = [f"| Model | Runs | {hand_n} hand-written | {test_n} generated (test) "
             f"| Polish (test, {languages.get('pl', '–')}) | English (test, {languages.get('en', '–')}) "
             "| Requests per task | Failed tool calls per run | Tokens written per task |",
             "|---|---|---|---|---|---|---|---|---|"]
    for i, e in enumerate(evals):
        test = e.get("test")
        lang = (lambda code: mean(test["language"][code][0], e["runs"]) if test and code in test["language"] else "–")
        label = f"**{e['label']}**" if i == 0 else e["label"]
        lines.append(f"| {label} | {e['runs']} | {done_text(e.get('hand'))} | {done_text(test)} | {lang('pl')} | "
                     f"{lang('en')} | "
                     + (f"{test['requests']:.1f} | {mean(test['failed_calls'], 1)} | {test['written']:.0f} |" if test
                        else "– | – | – |"))
    return "\n".join(lines)


def runs_text(evals):
    """Each run of the models measured more than once, under the table."""
    lines = []
    for e in evals:
        if e["runs"] > 1:
            parts = [f"{', '.join(map(str, e[part]['by_run']))} of the {e[part]['tasks']} {name}"
                     for part, name in (("test", "generated"), ("hand", "hand-written")) if part in e]
            lines.append(f"{e['label']}, its {e['runs']} runs: " + "; ".join(parts) + ".")
    return " ".join(lines)


def category_table(evals):
    tested = [e for e in evals if "test" in e]
    if not tested:
        return "(no test set measured)"
    seen = {c for e in tested for c in e["test"]["category"]}
    order = [c for c in CATEGORIES if c in seen] + sorted(seen - set(CATEGORIES))
    lines = ["| Category (tasks) | " + " | ".join(e["label"] for e in tested) + " |",
             "|---|" + "---|" * len(tested)]
    for c in order:
        count = next(e["test"]["category"][c][1] // e["runs"] for e in tested if c in e["test"]["category"])
        cells = [mean(e["test"]["category"][c][0], e["runs"]) if c in e["test"]["category"] else "–" for e in tested]
        lines.append(f"| `{c}` ({count}) | " + " | ".join(cells) + " |")
    return "\n".join(lines)


def weakest(evals):
    first = evals[0] if evals else None
    if not first or "test" not in first:
        return "Not measured on the generated test set yet."
    runs = first["runs"]
    ranked = sorted(((p / n, c, p, n) for c, (p, n) in first["test"]["category"].items() if n >= 3 * runs))[:3]
    return ("The weakest kinds of task on the generated test set" + (f" (a run's mean of {runs})" if runs > 1 else "")
            + ": " + ", ".join(f"`{c}` ({mean(p, runs)} of {n // runs})" for _, c, p, n in ranked) + ".")


def eval_setup(evals, context, base_gguf):
    parts = [s for e in evals for s in (e.get("test"), e.get("hand")) if s]
    temps = sorted({t for s in parts for t in s["temperatures"]})
    sampling = f"temperature {', '.join(f'{t:g}' for t in temps)}" if temps else "the app's sampling"
    dates = sorted({e["date"].isoformat() for e in evals})
    text = (f"Measured {' to '.join([dates[0], dates[-1]] if len(dates) > 1 else dates)} with the bench's "
            f"llama-server runner (llama.cpp on a GPU), every model over `{base_gguf}` (the fine-tuned ones with "
            f"their adapter added by `--lora`): the app's prompt and sampling ({sampling}, top-k 50, repetition "
            f"penalty 1.1 over 64 tokens), {thousands(context)} tokens of context, at most 2 048 tokens per answer. ")

    def seeds(group):
        return ranges(x for e in group for s in (e.get("test"), e.get("hand")) if s for x in s["seeds"])
    groups = defaultdict(list)  # the models run alike: (runs, seeds) → labels
    for e in evals:
        groups[(e["runs"], seeds([e]))].append(e["label"])
    if len(groups) == 1 and evals[0]["runs"] == 1:
        return text + f"One run per task, seed {seeds(evals)}. Requests, failed tool calls and tokens written are of " \
                      "the test set."
    words = {2: "two", 3: "three", 4: "four", 5: "five", 6: "six", 7: "seven", 8: "eight"}
    several = [(runs, seed, labels) for (runs, seed), labels in groups.items() if runs > 1]
    once = [(seed, labels) for (runs, seed), labels in groups.items() if runs == 1]
    said = [f"{' and '.join(labels)} ran every task {words.get(runs, runs)} times (seeds {seed}: the app's and the "
            f"next {words.get(runs - 1, runs - 1)})" for runs, seed, labels in several]
    said += [(f"{' and '.join(labels)} once" if len(once) > 1 else "the others once") + f" (seed {seed})"
             for seed, labels in once]
    return (text + "; ".join(said) + ". Where a model ran several times, its cells are a run's mean. Requests, failed "
            "tool calls and tokens written are of the test set.")


def eval_results_yaml(evals, dataset_id, repo_id, note):
    first = evals[0]
    entries = []
    for part in ("hand", "test"):
        s = first.get(part)
        if s is None:
            continue
        entries.append("\n".join([
            "- dataset:",
            f"    id: {dataset_id}",
            f"    task_id: {part}",
            f"  value: {100 * s['passed'] / (s['runs'] * s['tasks']):.2f}",
            f"  date: \"{first['date'].isoformat()}\"",
            "  source:",
            f"    url: https://huggingface.co/{repo_id}",
            "    name: \"Model card\"",
            f"  notes: {json.dumps(note, ensure_ascii=False)}",
        ]))
    return "\n".join(entries) + "\n"


# ---- How it was trained -----------------------------------------------------------------------

def step_of(folder):
    m = re.search(r"step(\d+)$", folder.name)
    return int(m.group(1)) if m else None


def source_adapters(adapter):
    combined = adapter / "combined-from.json"
    if combined.exists():
        return [Path(p).expanduser() for p in json.loads(combined.read_text(encoding="utf-8"))]
    return [adapter]


def adapter_settings(adapter):
    """(the adapter's folder, its run's settings.json or None, its log) for each adapter it is made of."""
    out = []
    for folder in source_adapters(adapter):
        path = folder.parent / "settings.json"
        settings = json.loads(path.read_text(encoding="utf-8")) if path.exists() else None
        out.append((folder, settings, read_log(folder.parent / "log.jsonl")))
    return out


def duration(seconds):
    minutes = round(seconds / 60)
    return f"{minutes // 60} h {minutes % 60} min" if minutes >= 60 else f"{minutes} min"


def need(settings, folder, *keys):
    missing = [k for k in keys if k not in settings]
    if missing:
        fail(f"{folder.parent / 'settings.json'}: no {', '.join(missing)} — of another version of the training "
             "script than publish.py describes")


def rated(path):
    """A `--rates` file: (steps rated, always done, never done, answers a step), or None when it is not there."""
    path = Path(path).expanduser()
    if not path.exists():
        return None
    rates = list(json.loads(path.read_text(encoding="utf-8")).values())
    answers = next((n for n in range(1, 65) if all(abs(r * n - round(r * n)) < 1e-6 for r in rates)), None)
    return len(rates), sum(1 for r in rates if r >= 1), sum(1 for r in rates if r <= 0), answers


def training_text(adapter, config, hardware):
    lines = []
    sources = adapter_settings(adapter)
    modules = ", ".join(f"`{m}`" for m in sorted(config.get("target_modules", [])))
    for n, (folder, s, log) in enumerate(sources, 1):
        if s is None:
            lines.append(f"{n}. `{folder.name}` (no settings.json beside it: its training is not described here).")
            continue
        step = step_of(folder)
        data = "the dataset's " + ", ".join(f"`training/{Path(p).name}`" for p in s.get("data", []))
        seconds = max((x.get("seconds", 0) for x in log if step is None or x.get("step", 0) <= step), default=0)
        took = f"; {duration(seconds)} on {hardware}" if seconds else ""
        of_steps = f" of {s['steps']}" if s.get("steps") and s["steps"] != step else ""
        if "generations" in s:  # finetune/grpo.py
            need(s, folder, "rank", "alpha", "generations", "temperature", "max_answer", "beta", "lr", "warmup",
                 "prompts", "draws", "steps")
            drawn = " A step that answers at once (no tool needed, or asking back) is drawn twice as often."
            if s.get("rates"):
                r = rated(s["rates"])
                how = ("in proportion to p(1 − p), p the share of its answers that scored 1"
                       if s.get("weights", "learnability") == "learnability" else "uniformly")
                rating = (f" (it answered every step {r[3]} times before: {thousands(r[1])} steps scored 1 every "
                          f"time, {thousands(r[2])} never)" if r and r[3] else "")
                count = f" {thousands(s['steps_drawn_from'])}" if "steps_drawn_from" in s else ""
                drawn = (f" Only the{count} steps the model it starts from does sometimes are drawn{rating}, {how}; "
                         "a step that answers at once (no tool needed, or asking back) twice as often.")
            lines += [
                f"{n}. **GRPO** (`finetune/grpo.py`) over the base model with the adapter above merged into its "
                f"weights: a second LoRA, rank {s['rank']}, alpha {s['alpha']} — the adapter of step {step}"
                f"{of_steps}{took}.",
                f"   * *Prompts*: the steps of the `grpo` split's solutions ({data}: each answer of a sequence, with "
                f"everything before it), less those whose own solution the reward does not take.{drawn} For each "
                f"prompt {s['generations']} answers at temperature {s['temperature']:.1f}, at most {s['max_answer']} "
                "tokens.",
                "   * *The reward of a step that calls tools*: 1.0 for the solution's calls — the tool and each "
                "argument compared by its kind: a page's name, a query's filters in any order, a ref or a status "
                "exactly, the words a change writes with some tolerance but its dates with their markers (📅 due, "
                "⏰ reminder…), times, task boxes and numbers as the solution's — or for other calls the task names "
                "as right at that step (a retry); else 0.2 × the F1 of the tools' names.",
                "   * *The reward of an answer*: 1.0 when it calls no tool, passes the task's checks of the answer, "
                "states no date, time or number of three digits or more that its prompt does not show (unless "
                "worked out with the working shown, or, in a task that works its answer out, a value its check asks "
                "for) and says nothing untrue of the changes (a change claimed when none was made, the run's own "
                "change said to have been so already); else 0.",
                "   * *Learning*: each answer's advantage is its reward against its group's mean, over the group's "
                "standard deviation; a group whose answers all score the same is skipped, and a step draws prompts "
                f"(at most {s['draws']}) until {s['prompts']} groups tell answers apart. A KL penalty of "
                f"{s['beta']:g} towards the model it starts from; AdamW, learning rate {number(s['lr'])}, "
                f"{s['warmup']} steps of warm-up.",
            ]
        else:  # finetune/sft.py
            need(s, folder, "rank", "alpha", "dropout", "lr", "warmup", "batch", "epochs", "training", "validation",
                 "per_task", "weighting")
            validation = [x["validation"] for x in log
                          if "validation" in x and (step is None or x.get("step", 0) <= step)]
            loss = f" Validation loss {validation[0]:.3f} → {validation[-1]:.3f}." if len(validation) > 1 else ""
            weighting = ("each sequence's mean weighing the same, a short answer as much as a long one"
                         if s["weighting"] == "sequence" else "averaged over the learnt tokens")
            epochs = "one epoch" if s["epochs"] == 1 else f"{s['epochs']:g} epochs"
            lines += [
                f"{n}. **Supervised fine-tuning** (`finetune/sft.py`) of the base model's bf16 weights: LoRA rank "
                f"{s['rank']}, alpha {s['alpha']}, dropout {s['dropout']:g} — the adapter of step {step}{of_steps}"
                f"{took}.",
                f"   * *Data*: {data}. Of each task's passed attempts the best {s['per_task']} (no failed tool call "
                f"first, then the fewest tokens written): {thousands(s['training'])} sequences to learn from, "
                f"{thousands(s['validation'])} more, of the tasks held out, for a validation loss.",
                f"   * *Learning*: the loss on the model's turns only, {weighting}; AdamW, learning rate "
                f"{number(s['lr'])}, warm-up {100 * s['warmup']:g} %, cosine; {s['batch']} sequences a step, "
                f"{epochs}.{loss}",
            ]
    if len(sources) > 1:
        scales = {s["alpha"] / s["rank"] for _, s, _ in sources if s}
        scale = f", all at the scale alpha / r = {scales.pop():g}" if len(scales) == 1 else ""
        lines.append(f"{len(sources) + 1}. **One adapter**: the two summed as one LoRA of rank {config['r']}, alpha "
                     f"{config['lora_alpha']:g}, on {modules} (`finetune/combine.py`: their A stacked, their B side by "
                     f"side{scale}) — the same sum of weights the two adapters make.")
    else:
        lines.append(f"The adapter: rank {config['r']}, alpha {config['lora_alpha']:g}, on {modules}.")
    return "\n".join(lines)


def training_stages(adapter):
    """Which stage read which training file: its name → "SFT" or "GRPO"."""
    stages = {}
    for _, s, _ in adapter_settings(adapter):
        for p in (s or {}).get("data", []):
            stages[Path(p).name] = "GRPO (prompts)" if "generations" in s else "SFT"
    return stages


def number(x):
    """2e-4 as 0.0002, 1e-5 as 1e-5."""
    return re.sub(r"e-0*(\d)", r"e-\1", f"{x:g}")


def read_log(path):
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


# ---- The dataset ------------------------------------------------------------------------------

def read_split(folders):
    """Folders of notebooks/ and tasks/ as one split: (tasks as rows, notebook files as rows, the
    files to copy as (source, path in the split)). Two folders may not have a notebook or a task file
    of the same name: the split holds them side by side."""
    tasks, files, sources, languages, seen = [], [], [], {}, {}
    for folder in folders:
        tasks_dir, notebooks_dir = folder / "tasks", folder / "notebooks"
        if not tasks_dir.is_dir() or not notebooks_dir.is_dir():
            fail(f"{folder}: no tasks/ and notebooks/ in it")
        names = [f"tasks/{p.name}" for p in tasks_dir.glob("*.json")] + \
                [f"notebooks/{p.name}" for p in notebooks_dir.iterdir()]
        for name in names:
            if name in seen:
                fail(f"{folder}: {name} is in {seen[name]} too — the two cannot be one split")
            seen[name] = folder
        for path in sorted(tasks_dir.glob("*.json")):
            sources.append((path, f"tasks/{path.name}"))
            doc = json.loads(path.read_text(encoding="utf-8"))
            for t in doc["tasks"]:
                notebook = t.get("notebook", doc.get("notebook"))
                language = t.get("language", doc.get("language"))
                languages.setdefault(notebook, language)
                tasks.append({
                    "id": t["id"], "notebook": notebook, "language": language, "now": t.get("now", doc.get("now")),
                    "category": t["category"], "ask": t.get("ask"),
                    "messages": [{"role": m["role"], "content": m["content"]} for m in t["messages"]]
                    if "messages" in t else None,
                    "page": t.get("page"), "answers": t.get("answers"),
                    "expect": json.dumps(t.get("expect", {}), ensure_ascii=False),
                    "solution": json.dumps(t.get("solution", []), ensure_ascii=False),
                    "note": t.get("note"),
                })
        for path in sorted(notebooks_dir.rglob("*")):
            if not path.is_file():
                continue
            if path.suffix != ".md":
                fail(f"{path}: not a note — only the Markdown files of a notebook are published")
            rel = path.relative_to(notebooks_dir)
            sources.append((path, f"notebooks/{rel.as_posix()}"))
            notebook = rel.parts[0]
            files.append({"notebook": notebook, "path": rel.relative_to(notebook).as_posix(),
                          "language": languages.get(notebook), "text": path.read_text(encoding="utf-8")})
    used = {t["notebook"] for t in tasks}
    missing = used - {f["notebook"] for f in files}
    if missing:
        fail(f"{', '.join(map(str, folders))}: tasks on notebooks that are not there: {', '.join(sorted(missing))}")
    return tasks, files, sources


def generator_runs(notebooks):
    """The finetune/generate.py runs that make these notebooks, [(--split, --count, --seed)], or None when
    they are not all of the generator (gen-<split>-<pl|en>-<seed>, both languages of every seed)."""
    found = defaultdict(set)
    for n in notebooks:
        m = re.fullmatch(r"gen-(\w+)-(pl|en)-(\d+)", n)
        if not m:
            return None
        found[(m.group(1), int(m.group(3)))].add(m.group(2))
    if not found or any(languages != {"pl", "en"} for languages in found.values()):
        return None
    runs = []
    for split in sorted({s for s, _ in found}):
        start = None
        for seed in sorted(seed for s, seed in found if s == split) + [None]:
            if start is not None and seed != last + 1:
                runs.append((split, 2 * (last - start + 1), start))
                start = None
            if start is None:
                start = seed
            last = seed
    return runs


def regenerated(folders, runs):
    """What finetune/generate.py writes for `runs` against what `folders` hold: (notebook files that
    differ, tasks missing or new, {a task's key: the tasks it differs in}, tasks compared)."""
    def files(root):
        return {p.relative_to(root).as_posix(): p.read_bytes() for p in root.rglob("*") if p.is_file()}

    def tasks(roots):
        out = {}
        for root in roots:
            for path in sorted((root / "tasks").glob("*.json")):
                doc = json.loads(path.read_text(encoding="utf-8"))
                shared = {k: v for k, v in doc.items() if k != "tasks"}
                for t in doc["tasks"]:
                    out[t["id"]] = {**shared, **t}
        return out
    with tempfile.TemporaryDirectory(prefix="brulion-publish-") as tmp:
        for split, count, seed in runs:
            done = subprocess.run([sys.executable, "-I", str(ROOT / "finetune/generate.py"), "--out", tmp, "--split",
                                   split, "--count", str(count), "--seed", str(seed)], capture_output=True, text=True)
            if done.returncode:
                fail(f"finetune/generate.py --split {split} --count {count} --seed {seed}: {done.stderr.strip()}")
        made = files(Path(tmp) / "notebooks")
        have = {}
        for folder in folders:
            have.update(files(folder / "notebooks"))
        notebook_files = sum(1 for k in made.keys() | have.keys() if made.get(k) != have.get(k))
        new, old = tasks([Path(tmp)]), tasks(folders)
    keys = defaultdict(int)
    for i in new.keys() & old.keys():
        for k in new[i].keys() | old[i].keys():
            if new[i].get(k) != old[i].get(k):
                keys[k] += 1
    return notebook_files, len(new.keys() ^ old.keys()), dict(keys), len(old)


def regenerated_text(split, folders, runs):
    """A sentence on how the generator makes the split again (for the dataset card)."""
    commands = " and ".join(f"`--split {s} --count {c} --seed {seed}`" for s, c, seed in runs)
    notebook_files, ids, keys, total = regenerated(folders, runs)
    if not notebook_files and not ids and not keys:
        return f"`{split}`: {commands} — the same files."
    if notebook_files or ids or set(keys) - {"expect"}:
        warn(f"the generator makes the {split} split otherwise: {notebook_files} notebook files, {ids} tasks "
             f"missing or new, tasks differing in {keys}")
        return (f"`{split}`: {commands}; the generator of the commit above makes it otherwise ({notebook_files} "
                f"notebook files and {sum(keys.values()) + ids} tasks differ) — the files here are the ones used.")
    return (f"`{split}`: {commands} — the same notebooks, tasks and solutions; for {thousands(keys['expect'])} "
            f"of the {thousands(total)} tasks the generator writes later checks (`expect`). The "
            f"files here are the ones {'training' if split in ('sft', 'grpo') else 'the results below'} used.")


def write_jsonl(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=False) + "\n")


def ranges(numbers):
    numbers, out = sorted(set(numbers)), []
    for n in numbers:
        if out and n == out[-1][1] + 1:
            out[-1][1] = n
        else:
            out.append([n, n])
    return ", ".join(str(a) if a == b else f"{a}–{b}" for a, b in out)


def seeds_of(notebooks):
    return [int(m.group(1)) for n in notebooks if (m := re.fullmatch(r"gen-\w+-(?:pl|en)-(\d+)", n))]


def split_of_ids(ids, all_tasks):
    """The splits (and their seeds) the tasks of `ids` are from, as "`sft` (seeds 1–12)"; ids of no
    published split are counted apart."""
    where = {t["id"]: (split, t["notebook"]) for split, tasks in all_tasks.items() for t in tasks}
    found = defaultdict(set)
    for i in ids:
        if i in where:
            found[where[i][0]].add(where[i][1])
    parts = []
    for split, notebooks in found.items():
        seeds = seeds_of(notebooks)
        parts.append(f"`{split}`" + (f" (seeds {ranges(seeds)})" if seeds else ""))
    return ", ".join(parts), sum(1 for i in ids if i not in where)


def size_category(n):
    for limit, name in ((1_000, "n<1K"), (10_000, "1K<n<10K"), (100_000, "10K<n<100K")):
        if n < limit:
            return name
    return "100K<n<1M"


# ---- Paths that must not be published --------------------------------------------------------

LEAK = re.compile(r"/home/[\w.-]+|/Users/[\w.-]+|/root/|/nix/store/|/tmp/|[A-Za-z]:\\\\?Users")
TEXT = {".md", ".json", ".jsonl", ".txt", ".jinja", ".py", ".yaml", ""}


def private_words():
    """What names this machine and its user: the home folder, the git user's e-mail address, the host's name."""
    words = [re.escape(str(Path.home()))]
    email = git("config", "user.email")
    if email:
        words.append(re.escape(email))
    host = socket.gethostname().split(".")[0]
    if len(host) >= 4 and host != "localhost":
        words.append(rf"\b{re.escape(host)}\b")
    return re.compile("|".join(words))


def leaks(folder):
    found = []
    private = private_words()
    for path in sorted(p for p in folder.rglob("*") if p.is_file()):
        if path.suffix in TEXT:
            text = path.read_text(encoding="utf-8", errors="replace")
        elif path.suffix == ".safetensors":
            with open(path, "rb") as f:
                text = f.read(struct.unpack("<Q", f.read(8))[0]).decode("utf-8", errors="replace")
        elif path.suffix == ".gguf":
            text = json.dumps([v for _, v, _ in GgufHeader(path).keys if v is not None], ensure_ascii=False)
        else:
            continue
        for m in [*LEAK.finditer(text), *private.finditer(text)][:3]:
            found.append(f"{path.relative_to(folder)}: …{text[max(0, m.start() - 40):m.end() + 40]}…")
    return found


# ---- Cards ------------------------------------------------------------------------------------

def render(template, values, flags):
    """`{{name}}` from values; `{{#flag}}…{{/flag}}` kept only when the flag is set."""
    def block(m):
        if m.group(1) not in flags:
            fail(f"{template.name}: unknown section {m.group(1)!r}")
        return m.group(2) if flags[m.group(1)] else ""
    text = re.sub(r"\{\{#(\w+)\}\}\n?(.*?)\{\{/\1\}\}\n?", block, template.read_text(encoding="utf-8"), flags=re.S)

    def value(m):
        if m.group(1) not in values:
            fail(f"{template.name}: nothing for {{{{{m.group(1)}}}}}")
        return str(values[m.group(1)])
    text = re.sub(r"\{\{(\w+)\}\}", value, text)
    return re.sub(r"\n{3,}", "\n\n", text)


def app_base_gguf():
    """The base model's 4-bit file as the app pins it (src/core/ai/local.cpp, the text model)."""
    source = (ROOT / "src/core/ai/local.cpp").read_text(encoding="utf-8")
    revision = re.search(r'textRevision = u"([0-9a-f]{40})"', source)
    file = re.search(r'text\.file = \{u"([^"]+)"_s,.*?"([0-9a-f]{64})", (\d+)\}', source, re.S)
    if not revision or not file:
        fail("src/core/ai/local.cpp: the text model's file and revision not found")
    return {"name": file.group(1), "sha256": file.group(2), "bytes": int(file.group(3)),
            "revision": revision.group(1)}


def base_revision(base):
    metadata = base / ".cache/huggingface/download/config.json.metadata"
    if metadata.exists():
        first = metadata.read_text(encoding="utf-8").split("\n", 1)[0].strip()
        if re.fullmatch(r"[0-9a-f]{40}", first):
            return first
    return BASE_REVISION


def git(*args):
    try:
        return subprocess.run(["git", "-C", str(ROOT), *args], check=True, capture_output=True,
                              text=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


# ---- Main -------------------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--adapter", required=True, type=Path,
                        help="the adapter, as finetune/sft.py, grpo.py or combine.py saved it (PEFT)")
    parser.add_argument("--adapter-gguf", type=Path,
                        help="the adapter as GGUF (default: <export>/<name>-lora.gguf, else the one .gguf in --adapter)")
    parser.add_argument("--export", type=Path, help="what finetune/export.py wrote for this adapter")
    parser.add_argument("--with-gguf", nargs="+", default=[], metavar="KIND",
                        help="merged GGUF files of --export to publish too: BF16, Q4_0, Q4_K_M")
    parser.add_argument("--with-weights", action="store_true", help="the merged weights of --export (hf/) too")
    parser.add_argument("--no-peft", action="store_true", help="leave out the adapter in PEFT's format")
    parser.add_argument("--base", type=Path, default=Path("~/.cache/brulion-finetune/base/LFM3-1.7B"),
                        help="the base model's folder (its LICENSE; default %(default)s)")
    parser.add_argument("--eval", action="append", default=[], metavar="PREFIX[=LABEL]",
                        help="the bench's results <PREFIX>-test.jsonl, <PREFIX>-hand.jsonl; the first: this model")
    parser.add_argument("--eval-note", default="", help="a sentence under the results tables")
    parser.add_argument("--context", type=int, default=16384, help="the tokens of context measured with")
    parser.add_argument("--hand", type=Path, default=ROOT / "finetune",
                        help="the hand-written notebooks/ and tasks/ (default: the repository's)")
    parser.add_argument("--test", nargs="+", type=Path, metavar="FOLDER",
                        help="the generated test split: folders of notebooks/ and tasks/")
    parser.add_argument("--sft", nargs="+", type=Path, metavar="FOLDER",
                        help="the generated folders supervised fine-tuning's data were made from")
    parser.add_argument("--grpo", nargs="+", type=Path, metavar="FOLDER",
                        help="the generated folders of GRPO's prompts")
    parser.add_argument("--training", nargs="+", default=[], type=Path, metavar="JSONL",
                        help="training sequences (the bench's --training) to publish with the tasks")
    parser.add_argument("--owner", default="<owner>", help="the Hugging Face user or organization")
    parser.add_argument("--author", help="who the NOTICE names (default: --owner)")
    parser.add_argument("--name", default="brulion-assistant-1.7b", help="the model's repository and files")
    parser.add_argument("--title", default="Brulion Assistant 1.7B")
    parser.add_argument("--dataset-name", default="brulion-assistant-tasks")
    parser.add_argument("--dataset-license", default="cc-by-4.0", help="a Hugging Face license id (default %(default)s)")
    parser.add_argument("--hardware", default="one NVIDIA GeForce RTX 5060 Laptop GPU (8 GB)")
    parser.add_argument("--out", required=True, type=Path, help="model/ and dataset/ are written into it")
    parser.add_argument("--force", action="store_true", help="replace model/ and dataset/ when they are there")
    args = parser.parse_args()

    adapter = args.adapter.expanduser()
    base = args.base.expanduser()
    export = args.export.expanduser() if args.export else None
    out = args.out.expanduser()
    author = args.author or args.owner
    repo_id = f"{args.owner}/{args.name}"
    dataset_id = f"{args.owner}/{args.dataset_name}"
    repo_url = f"https://huggingface.co/{repo_id}"
    dataset_url = f"https://huggingface.co/datasets/{dataset_id}"
    if "<" in args.owner:
        warn(f"--owner not given: the cards and the GGUF metadata name {repo_id}")

    # What goes in, checked before anything is written.
    for name in ADAPTER_FILES:
        if not (adapter / name).exists():
            fail(f"{adapter}: no {name}")
    config = json.loads((adapter / "adapter_config.json").read_text(encoding="utf-8"))
    if args.adapter_gguf:
        adapter_gguf = args.adapter_gguf.expanduser()
    elif export and (export / f"{args.name}-lora.gguf").exists():
        adapter_gguf = export / f"{args.name}-lora.gguf"
    else:
        found = sorted(adapter.glob("*.gguf"))
        if len(found) != 1:
            fail(f"{adapter}: {len(found)} .gguf files; give --adapter-gguf, or --export of finetune/export.py")
        adapter_gguf = found[0]
    header = GgufHeader(adapter_gguf)
    if header.value("general.type") != "adapter" or header.value("adapter.type") != "lora":
        fail(f"{adapter_gguf}: not a LoRA adapter")
    ranks = {min(dims) for name, dims, _, _ in header.tensors if name.endswith(".lora_a")}
    if ranks != {config["r"]}:
        fail(f"{adapter_gguf}: rank {', '.join(map(str, sorted(ranks)))}, the adapter's is {config['r']} — not the "
             "same adapter")
    if abs(float(header.value("adapter.lora.alpha", 0)) - float(config["lora_alpha"])) > 1e-6:
        fail(f"{adapter_gguf}: alpha {header.value('adapter.lora.alpha')}, the adapter's is {config['lora_alpha']}"
             " — not the same adapter")
    if (args.with_gguf or args.with_weights) and not export:
        fail("--with-gguf and --with-weights take the files of --export")
    merged_ggufs = []
    for kind in args.with_gguf:
        path = export / f"{args.name}-{kind}.gguf"
        if not path.exists():
            fail(f"{path}: not there (finetune/export.py --quantize {kind})")
        merged_ggufs.append((kind, path))
    if args.with_weights and not (export / "hf" / "model.safetensors").exists():
        fail(f"{export / 'hf'}: no merged weights")
    license_file = base / "LICENSE"
    if not license_file.exists():
        fail(f"{license_file}: the base model's license is needed")
    if "LFM Open License v1.0" not in license_file.read_text(encoding="utf-8"):
        fail(f"{license_file}: not the LFM Open License v1.0 — the cards would be wrong")
    evals = load_evals(args.eval)
    for path in args.training:
        if not path.expanduser().exists():
            fail(f"{path}: not there")
    commit = git("rev-parse", "HEAD") or "main"
    dirty = git("status", "--porcelain", "--", "finetune", "tests/assistantbench",
                "src/core/extensions/builtin/asystent.luau", "src/core/ai")
    if dirty:
        warn(f"uncommitted changes under finetune/, tests/assistantbench/ or the assistant: the cards link "
             f"commit {commit[:10]}, which does not have them — commit first, then run this again")
    code_url = f"{APP_URL}/tree/{commit}"
    base_gguf = app_base_gguf()
    revision = base_revision(base)
    today = dt.date.today()
    adapter_date = dt.date.fromtimestamp((adapter / "adapter_model.safetensors").stat().st_mtime)

    # The dataset read and checked first: nothing is written when it is wrong.
    splits = {"hand": [args.hand.expanduser()]}
    for name in ("test", "sft", "grpo"):
        if getattr(args, name):
            splits[name] = [p.expanduser() for p in getattr(args, name)]
    counts, all_tasks, split_files = {}, {}, {}
    for split, folders in splits.items():
        tasks, files, sources = read_split(folders)
        if not tasks:
            fail(f"{', '.join(map(str, folders))}: no tasks")
        all_tasks[split] = tasks
        split_files[split] = (files, sources)
        notebooks = sorted({f["notebook"] for f in files})
        counts[split] = {
            "notebooks": len(notebooks), "tasks": len(tasks),
            "pl": sum(1 for t in tasks if t["language"] == "pl"), "en": sum(1 for t in tasks if t["language"] == "en"),
            "seeds": seeds_of(notebooks), "categories": defaultdict(int),
            "runs": generator_runs(notebooks) if split != "hand" else None,
        }
        for t in tasks:
            counts[split]["categories"][t["category"]] += 1
    ids = [t["id"] for tasks in all_tasks.values() for t in tasks]
    if len(ids) != len(set(ids)):
        twice = sorted(i for i in set(ids) if ids.count(i) > 1)
        fail(f"two tasks of the dataset have the same id: {', '.join(twice[:3])}")
    for split in ("test", "hand"):  # the results must be of the tasks published
        if split not in all_tasks:
            continue
        published = {t["id"] for t in all_tasks[split]}
        for e in evals:
            if split in e and e[split]["ids"] != published:
                warn(f"--eval {e['label']}: its {split} results are of other tasks than the {split} split "
                     f"({len(e[split]['ids'] - published)} not published, {len(published - e[split]['ids'])} not run)")
    measuring = {t["id"] for split in ("test", "hand") for t in all_tasks.get(split, [])}
    stages = training_stages(adapter)
    training = []  # (path, sequences, ids, kinds, attempts, temperatures)
    for path in args.training:
        path = path.expanduser()
        rows, learnt, kinds, attempts, temperatures = 0, set(), set(), set(), set()
        with open(path, encoding="utf-8") as f:
            for line in f:
                if line.strip():
                    row = json.loads(line)
                    rows += 1
                    learnt.add(row["id"])
                    kinds.add(row.get("model", ""))
                    attempts.add(row.get("attempt", 0))
                    temperatures.add(round(row.get("temperature", 0), 4))
        if learnt & measuring:  # a model measured on what it learnt from
            fail(f"--training {path}: sequences of {len(learnt & measuring)} tasks of the test or hand split, "
                 f"e.g. {sorted(learnt & measuring)[0]}")
        where, unpublished = split_of_ids(learnt, all_tasks)
        if unpublished:
            warn(f"--training {path.name}: {unpublished} of its tasks are in no split published")
        if path.name not in stages:
            warn(f"--training {path.name}: no adapter's settings.json names it — not this model's training data")
        training.append((path, rows, learnt, kinds, attempts, temperatures, where))
    for name, stage in stages.items():
        if name not in {p.name for p, *_ in training}:
            warn(f"{stage} trained on {name}, which --training does not publish: the model card names it")
    regenerate = [regenerated_text(split, splits[split], c["runs"]) for split, c in counts.items() if c["runs"]]

    targets = [out / "model", out / "dataset"]
    for target in targets:
        if target.exists():
            if not args.force:
                fail(f"{target} is there already (--force replaces it)")
            shutil.rmtree(target)
    model_dir, dataset_dir = targets

    # ---- The model ----
    print(f"model → {model_dir}")
    sums = {}  # published path → SHA-256
    described = []  # (path, what)
    common = {
        "general.author": author,
        "general.license": "other",
        "general.license.name": "lfm1.0",
        "general.license.link": f"{repo_url}/blob/main/LICENSE",
        "general.repo_url": repo_url,
        "general.source.repo_url": APP_URL,
        "general.base_model.count": 1,
        "general.base_model.0.name": "LFM3 1.7B",
        "general.base_model.0.organization": "LiquidAI",
        "general.base_model.0.repo_url": BASE_URL,
        "general.dataset.count": 1,
        "general.dataset.0.name": "Brulion assistant tasks",
        "general.dataset.0.repo_url": dataset_url,
        "general.languages": ["pl", "en"],
    }
    origin = ("A Derivative Work of LiquidAI/LFM3-1.7B under the LFM Open License v1.0, its weights changed by "
              "fine-tuning; not made or endorsed by Liquid AI.")
    def tensors_notice(what):
        """The __metadata__ a safetensors file of this model gets: what it is, that its weights were changed."""
        return {"name": args.title, "author": author, "base_model": BASE_ID, "base_model_revision": revision,
                "license": f"LFM Open License v1.0 (LICENSE, {repo_url}/blob/main/LICENSE)",
                "repo_url": repo_url, "notice": f"{what}, by {author}. {origin}"}
    lora_name = f"{args.name}-lora.gguf"
    lora_types = {GGUF_TYPES.get(t[2], f"type {t[2]}") for t in header.tensors}
    lora_type = lora_types.pop() if len(lora_types) == 1 else "mixed types"
    sums[lora_name] = write_gguf(adapter_gguf, model_dir / lora_name, {
        "general.name": args.title,
        "general.basename": args.name.rsplit("-", 1)[0],
        "general.size_label": "1.7B",
        "general.description": f"LoRA adapter for LFM3-1.7B: the assistant of the Brulion notes app, by {author}. "
                               + origin,
        **common,
        "general.tags": ["brulion", "lora", "tool-calling", "text-generation"],
    })
    described.append((lora_name, f"the adapter for llama.cpp (`--lora`, {lora_type}) over `{base_gguf['name']}`: "
                                 "what the app runs"))
    if not args.no_peft:
        for name in ADAPTER_FILES:
            if name == "adapter_config.json":
                # Hugging Face's name of the base model in place of the folder it was trained from.
                peft = dict(config, base_model_name_or_path=BASE_ID, revision=revision)
                (model_dir / name).write_text(json.dumps(peft, indent=2) + "\n", encoding="utf-8")
            else:
                sums[name] = write_safetensors(adapter / name, model_dir / name, tensors_notice(
                    "LoRA adapter (PEFT) for LFM3-1.7B: the assistant of the Brulion notes app"))
        described.append(("adapter_model.safetensors", f"the same adapter for PEFT (rank {config['r']}, alpha "
                                                       f"{config['lora_alpha']:g}, bf16), with `adapter_config.json`"))
    merged_what = {"BF16": "the adapter merged into the bf16 weights, one file for other programs",
                   "Q4_0": "the merged weights quantized to Q4_0 by `llama-quantize` (not Liquid AI's QAD file)",
                   "Q4_K_M": "the merged weights quantized to Q4_K_M by `llama-quantize`"}
    for kind, path in sorted(merged_ggufs, key=lambda x: ["BF16", "Q4_0", "Q4_K_M"].index(x[0])
                             if x[0] in ("BF16", "Q4_0", "Q4_K_M") else 9):
        name = path.name
        print(f"  {name} (its metadata rewritten: {size_text(path.stat().st_size)} copied)")
        sums[name] = write_gguf(path, model_dir / name, {
            "general.name": f"{args.title} {kind}",
            "general.basename": args.name.rsplit("-", 1)[0],
            "general.size_label": "1.7B",
            "general.description": f"LFM3-1.7B with the LoRA adapter of the Brulion notes app's assistant merged "
                                   f"into its weights, {kind}, by {author}. " + origin,
            **common,
            "general.tags": ["brulion", "tool-calling", "text-generation"],
        })
        described.append((name, merged_what.get(kind, f"the adapter merged into the weights, {kind}")))
    merged_files = []
    if args.with_weights:
        for path in sorted((export / "hf").iterdir()):
            if path.is_file():
                same = (base / path.name).exists() and sha256(base / path.name) == sha256(path)
                if path.suffix == ".safetensors" and not same:
                    print(f"  merged/{path.name} (its metadata written: {size_text(path.stat().st_size)} copied)")
                    sums[f"merged/{path.name}"] = write_safetensors(
                        path, model_dir / "merged" / path.name, tensors_notice(
                            "LFM3-1.7B with the LoRA adapter of the Brulion notes app's assistant merged into its "
                            "weights"))
                else:
                    copy(path, model_dir / "merged" / path.name)
                merged_files.append((path.name, same))
        described.append(("merged/", "the adapter merged into the bf16 weights (safetensors), with the base "
                                     "model's config, tokenizer, chat template and code"))
    copy(license_file, model_dir / "LICENSE")
    described.append(("LICENSE", "LFM Open License v1.0, the base model's license, unchanged"))

    changed = [f"New: {lora_name}" + ("" if args.no_peft else ", adapter_config.json, adapter_model.safetensors")
               + " — the LoRA adapter, the change itself; applied to the base model it changes its weights."]
    if merged_ggufs:
        quantized = [p.name for kind, p in merged_ggufs if kind != "BF16"]
        changed.append("Changed: " + ", ".join(p.name for _, p in merged_ggufs) + " — the base model's weights "
                       "with the adapter merged in, converted to GGUF by llama.cpp"
                       + (f" ({', '.join(quantized)} quantized by its llama-quantize)." if quantized else "."))
    if merged_files:
        changed.append("Changed: " + ", ".join(f"merged/{n}" for n, same in merged_files if not same)
                       + " — the base model's weights with the adapter merged in.")
        changed.append("Unchanged copies of the base model's files: " + ", ".join(
            f"merged/{n}" for n, same in merged_files if same) + ". The model code (*.py) is Apache-2.0, "
            "Copyright 2026 The HuggingFace Team, as its headers say.")
    changed.append("The GGUF files' general.* metadata and the safetensors files' __metadata__ name this model, "
                   "its author, base model and license, and say that its weights were changed (written by "
                   "finetune/publish.py of the Brulion repository).")
    notice = render(CARDS / "NOTICE", {
        "title": args.title, "year": today.year, "author": author, "base_revision": revision,
        "changes_date": adapter_date.isoformat(), "dataset_url": dataset_url,
        "changed_files": "\n".join(wrap(c, "- ") for c in changed),
    }, {})
    (model_dir / "NOTICE").write_text(notice, encoding="utf-8")
    if merged_files:
        (model_dir / "merged" / "NOTICE").write_text(notice, encoding="utf-8")
    described.append(("NOTICE", "what was changed, and by whom (license section 4)"))

    note = (f"No thinking, over {base_gguf['name']}; the app's prompt and sampling, "
            f"{thousands(args.context)} tokens of context"
            + (f"; the mean of {evals[0]['runs']} runs." if evals and evals[0]["runs"] > 1 else "."))
    if evals:
        (model_dir / ".eval_results").mkdir()
        (model_dir / ".eval_results" / f"{args.dataset_name}.yaml").write_text(
            eval_results_yaml(evals, dataset_id, repo_id, note), encoding="utf-8")

    for path in sorted(p for p in model_dir.rglob("*") if p.is_file()):
        rel = path.relative_to(model_dir).as_posix()
        if rel not in sums:
            sums[rel] = sha256(path)
    sizes = {rel: (model_dir / rel).stat().st_size for rel in sums}
    files_table = ["| File | Size | SHA-256 | What |", "|---|---|---|---|"]
    for rel, what in described:
        if rel.endswith("/"):
            total = sum(v for k, v in sizes.items() if k.startswith(rel))
            files_table.append(f"| `{rel}` | {size_text(total)} | (SHA256SUMS) | {what} |")
        else:
            files_table.append(f"| `{rel}` | {size_text(sizes[rel])} | `{sums[rel][:16]}…` | {what} |")

    merged_names = [rel for rel, _ in described if rel.endswith(".gguf") and rel != lora_name]
    values = {
        "base_model_relation": "finetune" if (merged_ggufs or merged_files) else "adapter",
        "library_name": "gguf" if args.no_peft else "peft",
        "dataset_id": dataset_id, "title": args.title, "repo_id": repo_id, "app_url": APP_URL,
        "code_url": code_url, "commit_short": commit[:10],
        "base_gguf": base_gguf["name"], "base_gguf_revision": base_gguf["revision"],
        "base_gguf_sha256": base_gguf["sha256"], "base_gguf_bytes": thousands(base_gguf["bytes"]),
        "base_gguf_size": size_text(base_gguf["bytes"]),
        "base_revision": revision,
        "adapter_file": lora_name, "adapter_sha256": sums[lora_name],
        "adapter_bytes": thousands(sizes[lora_name]), "adapter_size": size_text(sizes[lora_name]),
        "adapter_type": lora_type, "adapter_rank": config["r"],
        "files_table": "\n".join(files_table),
        "merged_gguf_list": ", ".join(f"`{n}`" for n in merged_names),
        "merged_gguf_example": next((n for n in merged_names if "Q4_K_M" in n),
                                    merged_names[0] if merged_names else ""),
        "training": training_text(adapter, config, args.hardware),
        "hardware": args.hardware,
        "weakest": weakest(evals),
        "eval_setup": eval_setup(evals, args.context, base_gguf["name"]) if evals else "Not measured yet.",
        "eval_table": eval_table(evals) if evals else "",
        "eval_after": " ".join(x for x in (runs_text(evals), args.eval_note) if x),
        "category_table": category_table(evals) if evals else "(not measured yet)",
        "dataset_license_name": LICENSE_NAMES.get(args.dataset_license, args.dataset_license),
    }
    flags = {"merged_gguf": bool(merged_ggufs), "peft": not args.no_peft, "merged_weights": bool(merged_files)}
    (model_dir / "README.md").write_text(render(CARDS / "model.md", values, flags), encoding="utf-8")
    with open(model_dir / "SHA256SUMS", "w", encoding="utf-8") as f:
        for rel in sorted(sums):
            if rel != "README.md":
                f.write(f"{sums[rel]}  {rel}\n")

    # ---- The dataset ----
    print(f"dataset → {dataset_dir}")
    for split, (files, sources) in split_files.items():
        for source, rel in sources:
            copy(source, dataset_dir / split / rel)
        write_jsonl(dataset_dir / "data" / f"tasks-{split}.jsonl", [dict(t, split=split) for t in all_tasks[split]])
        write_jsonl(dataset_dir / "data" / f"notebooks-{split}.jsonl", files)
    training_rows = []
    for path, rows, learnt, kinds, attempts, temperatures, where in training:
        copy(path, dataset_dir / "training" / path.name)
        sampled = (f"{len(attempts)} at {', '.join(f'{t:g}' for t in sorted(temperatures))}"
                   if kinds - {"solution"} else "–")
        training_rows.append(f"| `training/{path.name}` | {', '.join(f'`{k}`' for k in sorted(kinds))} | {where} | "
                             f"{thousands(len(learnt))} | {thousands(rows)} | {sampled} | "
                             f"{stages.get(path.name, '–')} | {size_text(path.stat().st_size)} |")

    split_lines = ["| Split | Use | Notebooks | Tasks | Polish | English | Seeds |", "|---|---|---|---|---|---|---|"]
    for split, c in counts.items():
        seeds = ranges(c["seeds"]) if c["seeds"] else "–"
        split_lines.append(f"| `{split}` | {SPLITS[split]} | {c['notebooks']} | {thousands(c['tasks'])} | "
                           f"{thousands(c['pl'])} | {thousands(c['en'])} | {seeds} |")
    seen = {k for c in counts.values() for k in c["categories"]}
    order = [k for k in CATEGORIES if k in seen] + sorted(seen - set(CATEGORIES))
    category_lines = ["| Category | " + " | ".join(f"`{s}`" for s in counts) + " |", "|---|" + "---|" * len(counts)]
    for k in order:
        category_lines.append(f"| `{k}` | " + " | ".join(str(c["categories"].get(k, 0)) for c in counts.values()) + " |")
    configs = []
    for kind in ("tasks", "notebooks"):
        configs += [f"- config_name: {kind}"] + (["  default: true"] if kind == "tasks" else []) + ["  data_files:"]
        for split in counts:
            configs += [f"  - split: {split}", f"    path: data/{kind}-{split}.jsonl"]
    if training:
        configs += ["- config_name: training", "  data_files:", "  - split: train", "    path: training/*.jsonl"]
    example = next((t for t in all_tasks["hand"] if t["category"] == "update"), all_tasks["hand"][0])
    example_doc = {"notebook": example["notebook"], "language": example["language"], "now": example["now"],
                   "tasks": [{k: v for k, v in {
                       "id": example["id"], "category": example["category"], "ask": example["ask"],
                       "expect": json.loads(example["expect"]), "solution": json.loads(example["solution"])}.items()
                       if v is not None}]}
    dataset_values = {
        "dataset_license": args.dataset_license,
        "dataset_license_name": LICENSE_NAMES.get(args.dataset_license, args.dataset_license),
        "author": author, "year": today.year,
        "size_category": size_category(sum(c["tasks"] for c in counts.values())),
        "configs": "\n".join(configs), "app_url": APP_URL, "code_url": code_url, "commit_short": commit[:10],
        "model_id": repo_id, "dataset_id": dataset_id, "base_gguf": base_gguf["name"], "adapter_file": lora_name,
        "hand_tasks": counts["hand"]["tasks"],
        "splits_table": "\n".join(split_lines), "categories_table": "\n".join(category_lines),
        "training_layout": "training/*.jsonl                  the training sequences, as the bench wrote them\n"
                           if training else "",
        "task_example": json.dumps(example_doc, ensure_ascii=False, indent=2),
        "seeds_note": "\n".join(f"* {line}" for line in regenerate),
        "eval_setup": values["eval_setup"], "eval_table": values["eval_table"], "eval_after": values["eval_after"],
        "training_files": "\n".join(["| File | Kind (`model`) | Tasks of | Tasks | Sequences | Attempts, temperature "
                                     "| Learnt in | Size |", "|---|---|---|---|---|---|---|---|", *training_rows]),
    }
    dataset_flags = {"eval": bool(evals), "training": bool(training), "regenerate": bool(regenerate)}
    (dataset_dir / "README.md").write_text(render(CARDS / "dataset.md", dataset_values, dataset_flags),
                                           encoding="utf-8")

    # ---- Checks and what to run ----
    problems = leaks(model_dir) + leaks(dataset_dir)
    if problems:
        print("\n".join(problems), file=sys.stderr)
        fail("local paths or private names in the files to publish (above): fix their source, then run this again")
    model_bytes = sum(p.stat().st_size for p in model_dir.rglob("*") if p.is_file())
    dataset_bytes = sum(p.stat().st_size for p in dataset_dir.rglob("*") if p.is_file())
    print(f"\nmodel/: {sum(1 for p in model_dir.rglob('*') if p.is_file())} files, {size_text(model_bytes)}")
    print(f"dataset/: {sum(1 for p in dataset_dir.rglob('*') if p.is_file())} files, {size_text(dataset_bytes)}, "
          + ", ".join(f"{s} {c['tasks']} tasks" for s, c in counts.items()))
    # (huggingface_hub 2.x's `hf`: `uvx --from 'huggingface_hub[cli]' hf …`; `hf upload` splits a big
    # folder into several commits itself, and a repository it makes is public unless --private)
    print("\nTo publish (--private: check the pages first, then make them public in the repositories' settings):\n")
    print("  hf auth login   # a token with write access, once")
    print(f"  hf upload {repo_id} {model_dir} . --repo-type model --commit-message \"{args.title}\"")
    print(f"  hf upload {dataset_id} {dataset_dir} . --repo-type dataset "
          f"--commit-message \"Brulion assistant tasks\"")
    print(f"\nFor the app (knownModels(), src/core/ai/local.cpp): {lora_name}, {sizes[lora_name]} bytes, SHA-256 "
          f"{sums[lora_name]},\n  {repo_url}/resolve/<the upload's commit>/{lora_name}")


def wrap(text, first, width=78):
    words, lines, line = text.split(), [], first
    for word in words:
        if len(line) + len(word) + 1 > width and line.strip() not in ("", first.strip()):
            lines.append(line.rstrip())
            line = " " * len(first)
        line += word + " "
    lines.append(line.rstrip())
    return "\n".join(lines)


if __name__ == "__main__":
    main()
