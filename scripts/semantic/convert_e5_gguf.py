#!/usr/bin/env python3
"""Convert a multilingual-e5-style embedding model (Hugging Face `BertModel` weights with the XLM-R
SentencePiece tokenizer, e.g. intfloat/multilingual-e5-small, sdadas/mmlw-e5-small) to GGUF for
llama.cpp, which Brulion's semantic module loads.

llama.cpp's own converter maps `BertModel` to the WordPiece tokenizer and `XLMRobertaModel` to
position ids starting at pad+1 — both wrong for these models (SentencePiece vocabulary, positions
from 0). This wrapper registers a converter class with the SentencePiece vocabulary and unshifted
positions.

    convert_e5_gguf.py --llama-src <llama.cpp source> <hf model dir> <out.gguf> [--outtype f16]

<llama.cpp source>: `nix build --no-link --print-out-paths nixpkgs#llama-cpp.src` (same version as
the linked libllama). Needs torch, transformers, sentencepiece, protobuf, numpy.
"""
import argparse, json, shutil, sys, tempfile
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument("--llama-src", required=True)
ap.add_argument("model_dir")
ap.add_argument("out")
ap.add_argument("--outtype", default="f16")
args = ap.parse_args()

src = Path(args.llama_src)
sys.path.insert(0, str(src / "gguf-py"))
sys.path.insert(0, str(src))
import convert_hf_to_gguf as conv  # noqa: E402
import gguf  # noqa: E402
import conversion  # noqa: E402
from conversion import ModelBase  # noqa: E402
from conversion.bert import BertModel  # noqa: E402


@ModelBase.register("BrulionE5BertModel")
class E5BertModel(BertModel):
    model_arch = gguf.MODEL_ARCH.BERT

    def set_vocab(self):
        self._xlmroberta_set_vocab()


conversion.TEXT_MODEL_MAP["BrulionE5BertModel"] = "bert"  # module imported above: no-op import

# A copy of the model directory whose config names the class above (the weights are symlinked).
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    for f in Path(args.model_dir).iterdir():
        (tmp / f.name).symlink_to(f.resolve())
    cfg = json.loads((Path(args.model_dir) / "config.json").read_text())
    cfg["architectures"] = ["BrulionE5BertModel"]
    (tmp / "config.json").unlink()
    (tmp / "config.json").write_text(json.dumps(cfg))
    sys.argv = ["convert_hf_to_gguf.py", str(tmp), "--outfile", args.out, "--outtype", args.outtype]
    conv.main()
