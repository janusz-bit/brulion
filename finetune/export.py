#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12,<3.14"
# dependencies = [
#     "torch==2.14.1",
#     "transformers==5.20.2",
#     "peft==0.21.2",
#     "numpy>=2.2.6",
#     "pyyaml>=5.1",
#     "tqdm>=4.27",
#     "requests>=2.25",
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
"""A trained adapter as the files to publish and to run (docs/FINETUNE.md "Training").

    uv run finetune/export.py --model ~/.cache/brulion-finetune/base/LFM3-1.7B \
        --adapter ~/.cache/brulion-finetune/sft-1/adapter-step240 --out ~/.cache/brulion-finetune/sft-1/export \
        --llama-cpp <llama.cpp v0.9.0 sources>

Writes into --out:

* `hf/` — the adapter merged into the base model's weights (safetensors), with the base model's
  tokenizer, chat template, model code and LICENSE;
* `<name>-BF16.gguf`, `<name>-Q4_0.gguf`, `<name>-Q4_K_M.gguf` — that model for llama.cpp;
* `<name>-lora.gguf` — the adapter alone, for llama.cpp's `--lora` on another file of the base
  model (LFM3-1.7B-QAD-Q4_0.gguf), in f16: llama.cpp has no fast path for bf16 on ARM (the phones),
  and on x86 f16 runs as fast and comes closer to the f32 sum.

The GGUF files are written by the scripts of the llama.cpp release the app is built with
(3rdparty/llama/pin.json), quantized by its `llama-quantize` (on PATH).
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

# The base model's files that go with its weights unchanged.
KEPT = ["config.json", "generation_config.json", "tokenizer.json", "tokenizer_config.json",
        "special_tokens_map.json", "chat_template.jinja", "configuration_lfm3.py", "modeling_lfm3.py",
        "lfm3_conv.py", "LICENSE"]


def run(command, **kwargs):
    print("+", " ".join(str(c) for c in command), flush=True)
    subprocess.run([str(c) for c in command], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--model", required=True, help="the base model's directory (Hugging Face layout)")
    parser.add_argument("--adapter", required=True, help="an adapter saved by finetune/sft.py")
    parser.add_argument("--out", required=True)
    parser.add_argument("--llama-cpp", required=True, help="llama.cpp's sources (convert_hf_to_gguf.py)")
    parser.add_argument("--name", default="brulion-assistant-1.7b")
    parser.add_argument("--quantize", nargs="*", default=["Q4_0", "Q4_K_M"])
    args = parser.parse_args()

    import torch
    from peft import PeftModel
    from transformers import AutoModelForCausalLM

    base = Path(args.model).expanduser()
    adapter = Path(args.adapter).expanduser()
    out = Path(args.out).expanduser()
    source = Path(args.llama_cpp).expanduser()
    hf = out / "hf"
    hf.mkdir(parents=True, exist_ok=True)

    model = AutoModelForCausalLM.from_pretrained(base, dtype=torch.bfloat16, trust_remote_code=True)
    model = PeftModel.from_pretrained(model, adapter).merge_and_unload()
    model.save_pretrained(hf, safe_serialization=True)
    for name in KEPT:
        if (base / name).exists():
            shutil.copy2(base / name, hf / name)

    env = dict(os.environ, PYTHONPATH=os.pathsep.join([str(source / "gguf-py"), str(source)]))
    bf16 = out / f"{args.name}-BF16.gguf"
    run([sys.executable, source / "convert_hf_to_gguf.py", hf, "--outtype", "bf16", "--outfile", bf16], env=env)
    for kind in args.quantize:
        run(["llama-quantize", bf16, out / f"{args.name}-{kind}.gguf", kind])
    run([sys.executable, source / "convert_lora_to_gguf.py", adapter, "--base", base, "--outtype", "f16",
         "--outfile", out / f"{args.name}-lora.gguf"], env=env)


if __name__ == "__main__":
    main()
