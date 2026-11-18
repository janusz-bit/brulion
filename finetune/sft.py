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
"""LoRA fine-tuning of LFM3 on the passed attempts of brulion-assistantbench (docs/FINETUNE.md "Training").

    LD_LIBRARY_PATH=/run/opengl-driver/lib uv run finetune/sft.py \
        --model ~/.cache/brulion-finetune/base/LFM3-1.7B \
        --data runs/train-t08-training.jsonl --out ~/.cache/brulion-finetune/sft-1

The data are the bench's `--training` lines: the sequences as the model read and wrote them, in
llama.cpp's tokens. They are trained on as they are, never tokenized again: `input_ids` are the
segments' tokens, the loss is taken on the `train` segments (the model's own answers and the end of
each turn). `--check` only reads the data and the tokenizer and says whether the two agree.

Of a task's passed attempts the best `--per-task` are kept — no failed tool call, fewest tokens
written — so a task the model always solves does not outweigh one it solves once, and the cleaner,
shorter way is the one learnt. An answer whose tool call failed is read, not learnt.

Every `--save-every` steps the run's state goes to `checkpoint/` in `--out` (the adapter, the
optimizer, the schedule, the random states, the sequences not yet trained on this epoch): the same
command run again after a stop goes on from there, exactly as the run would have.
"""

import argparse
import hashlib
import json
import math
import random
import shutil
import sys
import time
from pathlib import Path

LORA_MODULES = ["q_proj", "k_proj", "v_proj", "o_proj", "in_proj", "out_proj", "w1", "w2", "w3"]


# A change the user declined: the call was right (asystent.luau's DECLINED).
DECLINED = "\nError: The user declined this change.\n"


def failed(segments):
    """The indexes of a sequence's tool results that are errors (the app writes them "Error: …"),
    other than a change the user declined."""
    return [i for i in range(1, len(segments))
            if segments[i - 1]["special"] and segments[i - 1]["text"] == "<tool_response>"
            and segments[i]["text"].startswith("\nError: ") and segments[i]["text"] != DECLINED]


def learnt(segments):
    """Which segments are learnt: the `train` ones, but not an answer whose call failed — the attempt
    passed after it, the call itself is not to be learnt."""
    learn = [bool(s["train"]) for s in segments]
    for error in failed(segments):
        i = error - 1
        while i >= 0 and not learn[i]:
            i -= 1
        while i >= 0 and learn[i]:
            learn[i] = False
            i -= 1
    return learn


def read_attempts(files):
    """The training lines grouped by task and attempt, duplicates dropped."""
    attempts = {}
    seen = set()
    for file in files:
        with open(file, encoding="utf-8") as f:
            for line in f:
                row = json.loads(line)
                ids, train = [], []
                for segment, learn in zip(row["segments"], learnt(row["segments"])):
                    ids += segment["tokens"]
                    train += [learn] * len(segment["tokens"])
                key = hashlib.sha256(json.dumps(ids).encode()).hexdigest()
                if key in seen or not any(train):
                    continue  # the same as another, or nothing left to learn in it
                seen.add(key)
                attempt = (row["id"], file, row.get("temperature"), row["attempt"])
                attempts.setdefault(attempt, []).append({
                    "id": row["id"], "language": row["language"], "category": row["category"],
                    "ids": ids, "train": train, "segments": row["segments"],
                    "failed": len(failed(row["segments"])),
                })
    return attempts


def choose(attempts, per_task, max_tokens):
    """The best `per_task` attempts of every task whose sequences all fit in `max_tokens`: those with
    no failed tool call first, then the shortest (fewest tokens written)."""
    by_task = {}
    too_long = 0
    for sequences in attempts.values():
        if any(len(s["ids"]) > max_tokens for s in sequences):
            too_long += 1
            continue
        by_task.setdefault(sequences[0]["id"], []).append(sequences)
    chosen = []
    for task in sorted(by_task):
        ranked = sorted(by_task[task], key=lambda seqs: (sum(s["failed"] for s in seqs),
                                                         sum(sum(s["train"]) for s in seqs)))
        for sequences in ranked[:per_task]:
            chosen += sequences
    return chosen, len(by_task), too_long


def held_out(task_id, share):
    """Whether a task's sequences are kept for the validation loss (by its id, so every run agrees)."""
    digest = hashlib.sha256(task_id.encode()).digest()
    return int.from_bytes(digest[:4], "little") / 2**32 < share


def check(tokenizer, sequences):
    """The tokenizer's ids are llama.cpp's: every segment's tokens decode to its text."""
    bad = 0
    for s in sequences:
        for segment in s["segments"]:
            text = tokenizer.decode(segment["tokens"], skip_special_tokens=False,
                                    clean_up_tokenization_spaces=False)
            if text != segment["text"]:
                bad += 1
                if bad <= 3:
                    print(f"  {s['id']}: {segment['text'][:60]!r} decodes as {text[:60]!r}", file=sys.stderr)
    return bad


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--model", required=True, help="the base model's directory (Hugging Face layout)")
    parser.add_argument("--data", required=True, nargs="+", help="the bench's --training files")
    parser.add_argument("--out", help="where the adapters and the log go")
    parser.add_argument("--check", action="store_true", help="only check the data against the tokenizer")
    parser.add_argument("--per-task", type=int, default=2)
    parser.add_argument("--max-tokens", type=int, default=16384)
    parser.add_argument("--validation", type=float, default=0.05, help="share of tasks held out")
    parser.add_argument("--epochs", type=float, default=2)
    parser.add_argument("--batch", type=int, default=16, help="sequences per optimizer step")
    parser.add_argument("--lr", type=float, default=2e-4)
    parser.add_argument("--warmup", type=float, default=0.03, help="share of the steps")
    parser.add_argument("--rank", type=int, default=16)
    parser.add_argument("--alpha", type=int, default=32)
    parser.add_argument("--dropout", type=float, default=0.05)
    parser.add_argument("--chunk", type=int, default=1024, help="positions per piece of the loss")
    parser.add_argument("--weighting", choices=["token", "sequence"], default="token",
                        help="a step's loss: the mean over its learnt tokens, or over its sequences (each its "
                             "tokens' mean: a short answer weighs as much as a long one)")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--save-every", type=int, default=20, help="steps between the checkpoints to resume from")
    parser.add_argument("--device", default="cuda")
    args = parser.parse_args()

    import torch
    import torch.nn.functional as F
    from torch.utils.checkpoint import checkpoint
    from transformers import AutoModelForCausalLM, AutoTokenizer

    attempts = read_attempts(args.data)
    sequences, tasks, too_long = choose(attempts, args.per_task, args.max_tokens)
    tokenizer = AutoTokenizer.from_pretrained(args.model, trust_remote_code=True)
    bad = check(tokenizer, sequences)
    trained_tokens = sum(sum(s["train"]) for s in sequences)
    print(f"{len(attempts)} attempts of {tasks} tasks ({too_long} too long); {len(sequences)} sequences, "
          f"{sum(len(s['ids']) for s in sequences)} tokens, {trained_tokens} learnt; "
          f"{bad} segments decode otherwise")
    if bad:
        sys.exit("the tokenizer's ids are not llama.cpp's")
    if args.check:
        return
    if not args.out:
        sys.exit("--out is needed")

    out = Path(args.out).expanduser()
    out.mkdir(parents=True, exist_ok=True)
    validation = [s for s in sequences if held_out(s["id"], args.validation)]
    training = [s for s in sequences if not held_out(s["id"], args.validation)]
    random.seed(args.seed)
    torch.manual_seed(args.seed)

    from peft import LoraConfig, get_peft_model

    model = AutoModelForCausalLM.from_pretrained(args.model, dtype=torch.bfloat16, trust_remote_code=True)
    model.config.use_cache = False
    model.to(args.device)
    model.gradient_checkpointing_enable(gradient_checkpointing_kwargs={"use_reentrant": False})
    model.enable_input_require_grads()
    model = get_peft_model(model, LoraConfig(r=args.rank, lora_alpha=args.alpha, lora_dropout=args.dropout,
                                             target_modules=LORA_MODULES, task_type="CAUSAL_LM"))
    model.print_trainable_parameters()
    inner = model.get_base_model()  # Lfm3ForCausalLM with the LoRA layers in it

    def piece_loss(hidden, targets):
        logits = inner.lm_head(hidden).float()
        return F.cross_entropy(logits, targets, reduction="sum")

    def sequence_loss(s):
        """The summed loss of a sequence's learnt tokens; the logits are made a piece at a time."""
        ids = torch.tensor([s["ids"]], device=args.device)
        positions = [i - 1 for i in range(1, len(s["ids"])) if s["train"][i]]
        hidden = inner.model(input_ids=ids).last_hidden_state[0]
        total = hidden.new_zeros((), dtype=torch.float32)
        for start in range(0, len(positions), args.chunk):
            at = torch.tensor(positions[start:start + args.chunk], device=args.device)
            total = total + checkpoint(piece_loss, hidden[at], ids[0, at + 1], use_reentrant=False)
        return total, len(positions)

    def validate():
        model.eval()
        loss, count = 0.0, 0
        with torch.no_grad():
            for s in validation:
                l, n = sequence_loss(s)
                loss += l.item()
                count += n
        model.train()
        return loss / max(count, 1)

    steps_per_epoch = math.ceil(len(training) / args.batch)
    steps = max(1, round(steps_per_epoch * args.epochs))
    warmup = max(1, round(steps * args.warmup))
    parameters = [p for p in model.parameters() if p.requires_grad]
    optimizer = torch.optim.AdamW(parameters, lr=args.lr, weight_decay=0.0)
    schedule = torch.optim.lr_scheduler.LambdaLR(
        optimizer, lambda step: (step + 1) / warmup if step < warmup
        else 0.5 * (1 + math.cos(math.pi * (step - warmup) / max(1, steps - warmup))))

    log = open(out / "log.jsonl", "a", encoding="utf-8")
    settings = {k: v for k, v in vars(args).items() if k != "save_every"}
    settings.update(training=len(training), validation=len(validation), steps=steps)
    print(f"{len(training)} sequences to train on, {len(validation)} to validate; {steps} steps")
    index = {id(s): i for i, s in enumerate(training)}
    saved = out / "checkpoint"

    def save_checkpoint(step):
        """The state after `step` steps, written beside the last one and then put in its place."""
        new, old = out / "checkpoint.new", out / "checkpoint.old"
        shutil.rmtree(new, ignore_errors=True)
        model.save_pretrained(new)
        torch.save({"settings": settings, "step": step, "optimizer": optimizer.state_dict(),
                    "schedule": schedule.state_dict(), "random": random.getstate(),
                    "torch": torch.get_rng_state(),
                    "cuda": torch.cuda.get_rng_state_all() if torch.cuda.is_available() else None,
                    "order": [index[id(s)] for s in order], "seconds": time.time() - started},
                   new / "state.pt")
        shutil.rmtree(old, ignore_errors=True)
        if saved.exists():
            saved.rename(old)
        new.rename(saved)
        shutil.rmtree(old, ignore_errors=True)

    order = []
    start = 0
    started = time.time()
    if not saved.exists() and (out / "checkpoint.old" / "state.pt").exists():
        (out / "checkpoint.old").rename(saved)  # stopped between the two renames of save_checkpoint
    if (saved / "state.pt").exists():
        from peft import set_peft_model_state_dict
        from safetensors.torch import load_file

        state = torch.load(saved / "state.pt", weights_only=False)
        if state["settings"] != settings:
            sys.exit(f"{saved} is of another run: remove it to start again")
        set_peft_model_state_dict(model, load_file(saved / "adapter_model.safetensors"))
        optimizer.load_state_dict(state["optimizer"])
        schedule.load_state_dict(state["schedule"])
        random.setstate(state["random"])
        torch.set_rng_state(state["torch"])
        if state["cuda"] is not None:
            torch.cuda.set_rng_state_all(state["cuda"])
        order = [training[i] for i in state["order"]]
        start = state["step"]
        started -= state["seconds"]
        log.write(json.dumps({"resumed": start}) + "\n")
        log.flush()
        print(f"resumed after step {start}")
    else:
        (out / "settings.json").write_text(json.dumps(settings, indent=2) + "\n", encoding="utf-8")
        first = validate() if validation else None
        log.write(json.dumps({"step": 0, "validation": first}) + "\n")
        log.flush()
        print(f"step 0: validation {first}")
        started = time.time()

    model.train()
    for step in range(start, steps):
        batch = []
        while len(batch) < args.batch:
            if not order:
                order = training[:]
                random.shuffle(order)
            batch.append(order.pop())
        learnt = sum(sum(s["train"]) for s in batch)
        total = 0.0
        tokens = 0
        for s in batch:
            loss, n = sequence_loss(s)
            share = loss / learnt if args.weighting == "token" else loss / n / len(batch)
            share.backward()
            total += share.item()
            tokens += len(s["ids"])
        norm = torch.nn.utils.clip_grad_norm_(parameters, 1.0).item()
        optimizer.step()
        schedule.step()
        optimizer.zero_grad(set_to_none=True)
        row = {"step": step + 1, "loss": total, "lr": schedule.get_last_lr()[0], "grad_norm": norm,
               "tokens": tokens, "seconds": round(time.time() - started, 1)}
        epoch_end = (step + 1) % steps_per_epoch == 0 or step + 1 == steps
        if epoch_end and validation:
            row["validation"] = validate()
        log.write(json.dumps(row) + "\n")
        log.flush()
        print(f"step {step + 1}/{steps}: loss {row['loss']:.4f}"
              + (f", validation {row['validation']:.4f}" if "validation" in row else "")
              + f", {row['seconds']:.0f} s", flush=True)
        if epoch_end:
            name = f"adapter-step{step + 1}"
            model.save_pretrained(out / name)
            print(f"saved {out / name}", flush=True)
        if step + 1 == steps:
            shutil.rmtree(saved, ignore_errors=True)
        elif args.save_every and (step + 1) % args.save_every == 0:
            save_checkpoint(step + 1)


if __name__ == "__main__":
    main()
