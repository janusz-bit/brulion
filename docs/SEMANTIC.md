# Semantic search (prototype)

Search by meaning next to the full-text `SearchIndex`: a query such as "ciasto z jabłkami" finds
the note "Szarlotka babci", an English query finds a Polish note, and inflected forms
("kupiłem opony" / "kupić oponę") match without a stemmer. Status: a prototype on the branch
`semantic-search-prototype` — core module, model runner, tests and benchmark tools; no UI, and
neither the app nor `brulion-cli` links it yet.

## Pieces

| Piece | Where | What |
|---|---|---|
| chunks | `src/core/semantic/chunker.h` | `chunkNote()`: a note in chunks of ≤ 120 words (blank lines, headings and top-level bullets end parts; frontmatter skipped), each prefixed with the title |
| calibration | `calibration.h` | `Calibration`: subtract the mean query / passage vector and project out the Polish–English direction; `estimate()` from the notebook's own chunks |
| vector index | `vectorindex.h` | `VectorIndex`: raw and calibrated unit vectors, brute-force cosine, a note scores as its best chunk |
| fusion | `fusion.h` | `fuseConvex()`: 0.8 × semantic + 0.2 × full text, each min-max scaled |
| model runner | `embedder.h` (library `brulion_semantic_llama`) | `Embedder`: GGUF model through llama.cpp, several texts per call, GPU when there is one |
| conversion | `scripts/semantic/convert_e5_gguf.py` | Hugging Face e5-style model → GGUF (see below) |
| benchmark | `tests/searchbench` (`brulion-searchbench`) | quality runs (BM25 / semantic / hybrid) and timings on a folder of notes + queries |
| device benchmark | `scripts/semantic/embedbench`, `nix build .#embedbench-android` | model speed on CPU and GPU, also on a phone |

The core module (`brulion_core_semantic`) has no dependencies beyond QtCore and builds on every
platform; only `brulion_semantic_llama` needs llama.cpp (`llama-cpp-vulkan` in the dev shell).
Tests: `tst_semantic` (pure logic) and `tst_semantic_embedder` (skipped unless
`BRULION_SEMANTIC_MODEL` names a GGUF file).

## Models

`intfloat/multilingual-e5-small` (MIT) and `sdadas/mmlw-e5-small` (Apache-2.0, tuned for Polish):
118M parameters, 384 dimensions, 512 tokens, prefixes `query: ` / `passage: `. e5 pools by mean,
mmlw by the CLS token — the Embedder takes the pooling stored in the GGUF file.

Conversion: llama.cpp's own converter maps `BertModel` to a WordPiece vocabulary and
`XLMRobertaModel` to position ids starting at 2 — both wrong for these models (SentencePiece
vocabulary, positions from 0). `convert_e5_gguf.py` registers a converter class with the
SentencePiece vocabulary and unshifted positions; token ids match the Hugging Face tokenizer
exactly. Sizes: f16 242 MB, q8_0 132 MB (82% of the weights are the 250k-token vocabulary; keeping
only Latin-script tokens would bring q8_0 to about 70 MB — not done yet).

```sh
src=$(nix build --no-link --print-out-paths nixpkgs#llama-cpp.src)   # same version as libllama
python scripts/semantic/convert_e5_gguf.py --llama-src $src <hf model dir> e5-small-f16.gguf
llama-quantize e5-small-f16.gguf e5-small-q8_0.gguf q8_0
```

Fidelity: on the synthetic benchmark the C++ prototype gives the same ranking as PyTorch for every
query (top hit identical for 341/341 queries, scores within 2·10⁻⁴); q8_0 loses nothing measurable.

## Hardware acceleration

`Embedder::Options::device = Auto` puts the whole model on the first discrete GPU, else runs it on
the CPU; `Device::AnyGpu` also takes an integrated GPU. CPU runs pass llama.cpp an empty device
list and turn `op_offload` off: otherwise llama.cpp sets up every GPU anyway and moves big
operations there — the "CPU" run silently used the laptop's NVIDIA GPU, and on the phone it copied
the weights to the Mali GPU on every call (3x slower). Threads: passages at most 4 (half the
logical cores), queries 1 — SMT siblings and little cores slow llama.cpp's barriers down.

Measured with e5-small q8_0 (`embedbench`; GPU vectors equal the CPU's, cosine ≥ 0.9999):

| Device | Passages (tokens/s) | Query (median) |
|---|---|---|
| Laptop CPU i5-13450HX, 4 threads (1 for queries) | 3,900–4,200 | 9 ms with 1 thread, 3.7 ms with 4 |
| **Laptop NVIDIA RTX 5060, Vulkan** | 41,000–48,000 (85,000 on big corpora) | 2–2.5 ms |
| Laptop Intel iGPU (Raptor Lake), Vulkan | 900–1,500 | 22–34 ms |
| **Pixel 9 Pro (Tensor G4) CPU, 3 threads / 1 for queries** | 1,600 (1 thread: 1,300) | **6 ms** |
| Pixel 9 Pro CPU, 6–8 threads | 700–850 | 150–300 ms |
| Pixel 9 Pro Mali-G715, Vulkan | 560–650 | 60 ms |

* A desktop with a discrete GPU indexes 10–20x faster than its CPU: a 26k-note notebook (8.3M
  tokens) takes 100 s on the RTX 5060, about 35 min on the CPU.
* Integrated GPUs lose to the CPU through Vulkan — the Intel iGPU 3x, the phone's Mali 2.5x —
  hence `Auto` takes only discrete GPUs. On the Pixel 9 Pro the CPU wins: queries in 6 ms, and a
  typical notebook (1–2k notes, ~1M tokens) is indexed once in about 10 minutes, in the background.
  Other phones (Adreno) may differ: the app should measure once, as embedbench does, and keep the
  faster device.
* q8_0 is 2x faster than f16 on the phone; `armv8.6-a+i8mm` adds about 7%.

Backends:

* **Desktop (Linux, Windows):** Vulkan — one build for NVIDIA, AMD and Intel GPUs (`llama-cpp-vulkan`
  in the dev shell). CUDA is possible but nixpkgs does not cache it (an hours-long local build).
* **Android:** Vulkan (Vulkan 1.1 is guaranteed on 64-bit devices from Android 10) plus the CPU
  backend with NEON dotprod/fp16 (`armv8.2-a`; `armv8.6-a+i8mm` for 2021+ cores, a few % faster).
  llama.cpp also
  has an OpenCL backend tuned for Adreno GPUs — not built yet. `nix build .#embedbench-android`
  builds the device benchmark with the app's NDK (r27c); run it on a phone:

  ```sh
  adb push result/bin/embedbench e5-small-q8_0.gguf /data/local/tmp/
  adb shell 'cd /data/local/tmp && ./embedbench -m e5-small-q8_0.gguf'
  adb shell 'cd /data/local/tmp && ./embedbench -m e5-small-q8_0.gguf -t 1 --cpu-only'
  ```

  Size: the Vulkan backend's compiled shaders add about 35 MB to the executable (the CPU-only
  build is a few MB) — a real cost next to a 100 MB APK.
* **Browser (WebAssembly):** no model, or CPU only (ggml builds for WebAssembly; its WebGPU backend
  is experimental). Search falls back to full text alone.

## Calibration

A gradient-free edit of the model's output, from the search experiments: e5 vectors share a large
common component, and e5-small pulls a query towards notes in the same language ("sourdough
bread recipe" found the English "Thai green curry" before the Polish "Chleb na zakwasie"). Subtracting
the means and projecting out the language direction (two 384-float vectors, estimated from the
notebook's own chunks, no queries or labels) lifted cross-language search on the synthetic notes
from 0.43 to 0.83 MRR@10. On real data it is not a free win — see the results.

Calibration is applied only to queries of at most 5 words (`Calibration::maxQueryWords`): the query
mean comes from short, note-search-style queries, and question-style queries lost by it (below).
The vector index keeps raw and calibrated vectors, so the choice per query is free.

## Benchmark results

MRR@10 (higher is better), multi-domain database of 11 sets, 81k notes, 4,477 queries
(scripts/semantic/bench). "+BM25" is `fuseConvex` with the full-text `SearchIndex`; "rule" is
calibration for queries of ≤ 5 words.

| System | 6 public sets, real queries (3,341) | 5 note vaults, generated queries (1,136) |
|---|---|---|
| BM25 (`SearchIndex`, today's search) | 0.471 | 0.462 |
| e5-small | 0.650 | 0.568 |
| e5-small + BM25 | 0.667 | 0.594 |
| **e5-small + rule + BM25** | **0.667** | **0.637** |
| mmlw-e5-small + calibration + BM25 | 0.629 | 0.644 |
| e5-base (278M) + BM25 | 0.681 | 0.641 |
| bge-m3 (568M) + BM25 | 0.721 | 0.749 |

By query kind (note vaults): exact words — BM25 1.00, e5-small + rule + BM25 0.92; typos and
missing diacritics — 0.96 / 0.92; inflected forms — 0.22 / 0.67; the other language — 0.17 / 0.53
(mmlw 0.72); paraphrases — 0.00 / 0.14 (bge-m3 0.33). Hybrid search keeps what full text does well
and adds inflection and cross-language search; paraphrases remain hard for small models.

Findings:
* e5-small beats the Polish-tuned mmlw-e5-small on real question-style queries (0.650 vs 0.613);
  mmlw is better for English notes searched in Polish. e5-small is the default.
* Calibration helps short queries and mixed-language notebooks (Logseq docs searched in Polish:
  0.54 → 0.67) and costs 0.02 on long questions — hence the 5-word rule.
* On long wiki-like articles whose titles are place names (Wikivoyage) semantic search alone is
  worse than BM25 (0.39 vs 0.51); only the hybrid wins (0.61). Search should never be semantic-only.
* Full-text search itself is slow on big notebooks: 67–125 ms per query at 4–26k notes (fuzzy
  matching), against 3–11 ms for query embedding plus the vector scan on the GPU (13 ms for the
  query embedding on the laptop's CPU).
* The C++ prototype reproduces Python (q8_0 on Vulkan against f16 PyTorch): MRR within 0.004, the
  same top hit for 97–99% of queries.
