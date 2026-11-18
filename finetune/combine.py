#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12,<3.14"
# dependencies = [
#     "torch==2.14.1",
#     "safetensors>=0.6",
#     "numpy>=2.2.6",
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
"""Adapters trained one over another as one adapter (docs/FINETUNE.md "Training").

    uv run finetune/combine.py --adapters sft-nothink-2/adapter-step262 grpo-2/adapter-step100 \
        --out combined-1

GRPO trains a second adapter over the SFT adapter merged into the weights; the model is both of
them over the base model, which llama.cpp can run as two `--lora` files. One file is easier to
download and to name: a LoRA adapter adds `scale · B · A · x` to a layer, so two adapters of the
same scale (alpha / r) are one adapter whose A is theirs stacked and whose B is theirs side by
side — rank the sum of theirs, the same scale, the very same sum. A layer one of them leaves alone
gets zeros from it.

The combined adapter goes to llama.cpp as any other: `export.py --adapter combined-1` converts it
with `convert_lora_to_gguf.py --outtype f16` (f16, not bf16: llama.cpp has no fast path for bf16 on
ARM).
"""

import argparse
import json
import sys
from pathlib import Path

WEIGHTS = "adapter_model.safetensors"
CONFIG = "adapter_config.json"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--adapters", required=True, nargs="+", help="PEFT adapters (finetune/sft.py, grpo.py)")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    import torch
    from safetensors.torch import load_file, save_file

    folders = [Path(a).expanduser() for a in args.adapters]
    configs = [json.loads((f / CONFIG).read_text(encoding="utf-8")) for f in folders]
    weights = [load_file(f / WEIGHTS) for f in folders]
    for folder, c in zip(folders, configs):
        if c.get("use_rslora") or c.get("use_dora") or c.get("rank_pattern") or c.get("alpha_pattern") \
                or c.get("bias", "none") != "none" or c.get("modules_to_save"):
            sys.exit(f"{folder}: only plain LoRA adapters can be combined")
    scales = [c["lora_alpha"] / c["r"] for c in configs]
    if any(abs(s - scales[0]) > 1e-9 for s in scales):
        sys.exit(f"the adapters' scales differ ({scales}): they cannot be one adapter")
    if len({c["base_model_name_or_path"] for c in configs}) > 1:
        print("warning: the adapters name different base models", file=sys.stderr)

    # Every layer some adapter has, by its name without ".lora_A.weight" / ".lora_B.weight".
    layers = sorted({key.rsplit(".lora_", 1)[0] for w in weights for key in w})
    out = {}
    for layer in layers:
        parts_a, parts_b = [], []
        shapes = [(w[f"{layer}.lora_A.weight"].shape[1], w[f"{layer}.lora_B.weight"].shape[0])
                  for w in weights if f"{layer}.lora_A.weight" in w]
        inputs, outputs = shapes[0]
        for w, c in zip(weights, configs):
            a, b = w.get(f"{layer}.lora_A.weight"), w.get(f"{layer}.lora_B.weight")
            if a is None:
                a = torch.zeros(c["r"], inputs, dtype=torch.bfloat16)
                b = torch.zeros(outputs, c["r"], dtype=torch.bfloat16)
            parts_a.append(a)
            parts_b.append(b)
        dtype = parts_a[0].dtype
        out[f"{layer}.lora_A.weight"] = torch.cat([a.to(dtype) for a in parts_a], dim=0).contiguous()
        out[f"{layer}.lora_B.weight"] = torch.cat([b.to(dtype) for b in parts_b], dim=1).contiguous()

    rank = sum(c["r"] for c in configs)
    config = dict(configs[0])
    config["r"] = rank
    config["lora_alpha"] = scales[0] * rank
    config["target_modules"] = sorted({m for c in configs for m in c["target_modules"]})
    config["lora_dropout"] = 0.0
    target = Path(args.out).expanduser()
    target.mkdir(parents=True, exist_ok=True)
    save_file(out, target / WEIGHTS, metadata={"format": "pt"})
    (target / CONFIG).write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
    (target / "combined-from.json").write_text(json.dumps([str(f) for f in folders], indent=2) + "\n",
                                               encoding="utf-8")
    print(f"{len(layers)} layers, rank {rank}, alpha {config['lora_alpha']:g} → {target}")


if __name__ == "__main__":
    main()
