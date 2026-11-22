# Search by meaning

Search by meaning next to the full-text `SearchIndex`: a query such as "ciasto z jabłkami" finds
the note "Szarlotka babci", an English query finds a Polish note, and inflected forms
("kupiłem opony" / "kupić oponę") match without a stemmer. Shipped: Settings → Search → **Search by
meaning** (off by default; docs/UI.md "Search by meaning" for the UI, the user guide's page "Search"),
`brulion-cli search --semantic` (docs/CLI.md). This page: the pieces, how the app runs them, the model
file, and the measurements behind the choices.

## Pieces

| Piece | Where | What |
|---|---|---|
| chunks | `src/core/semantic/chunker.h`, `notes.h` | `chunkNote()`: a note in chunks of ≤ 120 words (blank lines, headings and top-level bullets end parts; frontmatter skipped), each prefixed with the title; `noteChunks()`: encrypted fragments blanked first, at most 256 chunks; `ChunkerVersion` |
| keys, diff | `notes.h` | `noteKey()` (FNV-1a 64 of the title and the text: a note whose key changed is embedded again), `diffIndex()` (what to embed, what to drop), `passageExcerpt()` (a result's text where its chunk starts) |
| calibration | `calibration.h` | `Calibration`: subtract the mean query / passage vector and project out the Polish–English direction; `estimate()` from the notebook's own chunks |
| vector index | `vectorindex.h` | `VectorIndex`: raw unit vectors, brute-force cosine, a note scores as its best chunk; incremental (`set` / `remove` with lazy compaction), the calibrated search without a calibrated copy (below), `save()` / `load()` in half floats |
| fusion | `fusion.h` | `fuseConvex()`: 0.8 × semantic + 0.2 × full text, each min-max scaled; with `known`: a result without vectors ranks by its words alone; `hybridSearch()`: what the app and the CLI list |
| models | `models.h` | `semanticModels()`: Multilingual E5 Small, q8_0 GGUF, as an `ai::LocalModel` (URL, SHA-256, size, licence); `queryMeanOf()`: its shipped query mean; `indexFileName()` |
| plugin interface | `semanticrunner.h` | `SemanticRunner` (passages and queries, each on its own model context and thread), `hasSemanticRunner()` / `loadSemanticRunner()` (`QLibrary`, as the model runner's and the speech recognizer's) |
| model runner | `embedder.h` (library `brulion_semantic_llama`, plugin `brulion_semantic`) | `Embedder`: GGUF model through the pinned llama.cpp (`3rdparty/llama`), several texts per call; built only with `BRULION_LOCAL_AI` |
| the app | `src/app/shell/semanticsearch.h` (`Workspace.semantic`), `src/app/content/searchmodel.h`, `qml/shell/ShellSemanticSettings.qml` | the model's download, the setting, the notebook's vectors built and kept, the search view's and the quick switcher's hybrid search |
| the CLI | `src/cli/commands_index.cpp` | `search --semantic`: the app's vectors of the notebook, the plugin for the query |
| conversion | `scripts/semantic/convert_e5_gguf.py` | Hugging Face e5-style model → GGUF ("The model file") |
| benchmark | `tests/searchbench` (`brulion-searchbench`) | quality runs (BM25 / semantic / hybrid / the app's `hybridSearch`) and timings on a folder of notes + queries |
| device benchmark | `scripts/semantic/embedbench`, `nix build .#embedbench-android` | model speed on CPU and GPU, also on a phone |

The core module (`brulion_core_semantic`, deps `ai` and `markdown`) builds on every platform; only
`brulion_semantic_llama` and the plugin need llama.cpp. Tests: `tst_semantic` (pure logic),
`tst_content_semantic` (the app with a fake model), `tst_cli_process` (`searchSemantic`: the CLI's
fallbacks), `tst_semantic_plugin` (the plugin as the app loads it) and `tst_semantic_embedder` (the
Embedder) — the last two run a real model when `BRULION_SEMANTIC_MODEL` names its GGUF file.

## In the app

* **The model** — Multilingual E5 Small (`intfloat/multilingual-e5-small`, MIT), 8-bit, 126 MB (132 million bytes):
  downloaded into `<app data>/models/semantic` with `ai::LocalModelFiles` when the user turns the
  setting on (resumable, checked against its SHA-256, the same UI as the speech models). **Remove**
  deletes it and every notebook's vectors on the device.
* **The plugin** — `brulion_semantic` next to llama.cpp's libraries (`<lib>/brulion`, the APK's
  `lib/<abi>/`), loaded the first time a note or a query is embedded; the app and brulion-cli link no
  llama.cpp. It loads the processor plugin the model on the device or the speech recognizer loaded
  already, else the best one this processor runs. The model leaves memory five minutes after its last
  use (`SemanticRunnerHolder`, shared by the two threads below). The browser and builds without
  `BRULION_LOCAL_AI` have no plugin: `available` false, search is full text, exactly as before.
* **Two threads, two model contexts** — notes are embedded on `brulion-semantic`, started with
  `QThread::IdlePriority` (SCHED_IDLE on Linux; llama.cpp's own threads, started from it, inherit it):
  4 threads (one fewer than the fast physical cores — `ai::computeCores` — at most 4: 3 on a Pixel 9
  Pro), up to 16 notes / 8,000 characters per call so a notebook closing or the setting turned off
  waits for at most a second or two. Queries are embedded on `brulion-semantic-query` at normal
  priority with a context of their own and one thread: a query never waits for the indexing.
* **Building the index** — 3 s after the notebook's index is ready: the kept vectors are read on the
  worker thread, every note's `noteKey` is compared with the stored one (`diffIndex`), the new and
  changed notes are queued — the newest journal days first, then the pages by path — and notes gone
  are dropped. Edits follow `VaultIndex::changed` through `changesSince()` 1.5 s after the last change:
  a changed note is queued first, a deleted one forgotten; an incomplete change log (a config change, a
  big sync) compares every note again. A note edited while it was being embedded is embedded again.
* **Kept on the device, never in the notebook** — `<app cache>/semantic/<notebook id>-<model id>.vectors`
  (`VectorIndex::save`: "BRSV", the format version, the model's id, `ChunkerVersion`, the dimension,
  then per note its id, key and chunks — line, language, half-precision vector; ~770 bytes a chunk:
  0.8 MB for a 1,000-chunk notebook). Written when the queue empties, every minute of a long build and
  when the notebook closes; read with bounds checks — another model, version or a damaged file is
  built again.
* **Calibration** — the shipped query mean of the model (`queryMeanOf`: 519 short training queries of
  the experiments) with the passage mean and the language direction estimated from the notebook's own
  chunks whenever the index is up to date after a change; applied to queries of ≤ 5 words. The index
  keeps only raw vectors: with q′ the calibrated query, P the projection without the language
  direction d and m the passage mean, a chunk's calibrated cosine is
  q′·unit(P(v − m)) = (q′·v − q′·m) / ‖P(v − m)‖ (P q′ = q′), so the index keeps one length per chunk
  instead of a calibrated copy of every vector.
* **The search** — `SearchResultModel` (the search view and the quick switcher's "Search the notes for
  …") asks `SemanticSearch::queryVector()` for the query's vector: known (the last 32 are kept) — the
  hybrid list at once; not yet — it is asked of the query thread and the list waits up to 150 ms
  (`MeaningWaitMs`), then shows the full-text order and is sorted again when it comes. `hybridSearch`:
  the full-text hits (notes and attachments, as many as the full-text search lists) fused with the same
  number of notes closest in meaning (`fuseConvex`, 0.8 meaning; an attachment or a note not embedded
  yet ranks by its words alone — so a notebook being indexed is searched sensibly); every full-text hit
  stays, of the notes found by meaning alone the best 20 (`MeaningAloneHits`: a query of words no note
  has lists a few close notes, not a page of them) — marked "Close in meaning", their line and excerpt
  where their closest chunk starts. Scopes, folders and `in:` filters apply as before; `in:pdf` asks
  nothing of the model.
* **brulion-cli** — `search --semantic` reads the same vectors file (read-only; a note whose key no
  longer matches is searched by its words alone), loads the plugin and the model for the query (about
  a second), estimates the calibration and runs the same `hybridSearch`. Without the plugin, the model or
  the app's vectors it lists the full-text hits with a warning.

## The model file

The app downloads `multilingual-e5-small-q8_0.gguf` — 132,440,544 bytes, SHA-256
`3a98dd889ee152d923991c60df2fad21e39f7441a5ee302a0a050d5137993b60` — from
https://huggingface.co/janusz-bit/multilingual-e5-small-gguf (with its model card and licence), at
the commit that added it, `57c0a8f1481a66ca991375e4f87311c9e4bd0cc5` (`kRepository` / `kRevision`
in `src/core/semantic/models.cpp`; the checksum pins the bytes whatever the repository does). Made
from `intfloat/multilingual-e5-small` at revision `614241f622f53c4eeff9890bdc4f31cfecc418b3` with
the llama.cpp release `3rdparty/llama/pin.json` pins (v0.9.0; the dev shell's `llama-quantize` is
the same release):

```sh
rev=614241f622f53c4eeff9890bdc4f31cfecc418b3
for f in config.json model.safetensors sentencepiece.bpe.model special_tokens_map.json tokenizer.json \
         tokenizer_config.json modules.json sentence_bert_config.json 1_Pooling/config.json; do
  curl -sSfL --create-dirs -o e5/$f https://huggingface.co/intfloat/multilingual-e5-small/resolve/$rev/$f
done
nix shell --impure --expr 'with (builtins.getFlake "nixpkgs").legacyPackages.x86_64-linux;
  python3.withPackages (p: with p; [torch transformers sentencepiece protobuf numpy safetensors pyyaml requests])' \
  -c python3 scripts/semantic/convert_e5_gguf.py --llama-src "$BRULION_LLAMA_SOURCE_DIR" e5 e5-small-f16.gguf
nix develop -c llama-quantize e5-small-f16.gguf multilingual-e5-small-q8_0.gguf q8_0
sha256sum multilingual-e5-small-q8_0.gguf
```

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

(The commands: "The model file" above.)

Fidelity: on the synthetic benchmark the C++ prototype gives the same ranking as PyTorch for every
query (top hit identical for 341/341 queries, scores within 2·10⁻⁴); q8_0 loses nothing measurable.

## Hardware acceleration

`Embedder::Options::device = Auto` puts the whole model on the first discrete GPU, else runs it on
the CPU; `Device::AnyGpu` also takes an integrated GPU. CPU runs pass llama.cpp an empty device
list and turn `op_offload` off: otherwise llama.cpp sets up every GPU anyway and moves big
operations there — the "CPU" run silently used the laptop's NVIDIA GPU, and on the phone it copied
the weights to the Mali GPU on every call (3x slower). Threads: passages at most 4 (one fewer than
the fast physical cores, else half the logical cores), queries 1 — SMT siblings and little cores slow
llama.cpp's barriers down.

The app's llama.cpp (`3rdparty/llama`) is built with its CPU backends only, so `Auto` runs on the
CPU there — the measurements below show why that is enough on phones and integrated GPUs; a discrete
GPU would index a big notebook 10–20x faster (a Vulkan build of the plugin: not done, see the
backends). The GPU numbers come from the prototype's runs with nixpkgs' `llama-cpp-vulkan` (the dev
shell's, which `scripts/semantic/embedbench` builds against).

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
* **Browser (WebAssembly):** no model (ggml builds for WebAssembly, its WebGPU backend is
  experimental; the app's browser build has no llama.cpp at all). Search is full text alone.

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

## The shipped pipeline, measured

Quality — the synthetic test notes (`scripts/semantic/bench/data`, MRR@10, `brulion-searchbench`'s
"app" run: `hybridSearch` exactly as the app calls it, the shipped query mean and each set's own
calibration, the GGUF above through the pinned llama.cpp):

| Set | BM25 (today's full text) | e5-small alone | the app's hybrid |
|---|---|---|---|
| testA (70 notes, 173 queries) | 0.543 | 0.792 | 0.787 |
| testB (70 notes, 169 queries) | 0.560 | 0.823 | 0.824 |

The same as the prototype's hybrid with the calibration file of the experiments, to the third
decimal: the conversion, the half-precision index and the calibrated search without a calibrated
copy change nothing measurable. By kind (testA / testB): exact words 0.978 / 1.000 (BM25 1.000 /
1.000), typos 1.000 / 1.000, inflected forms 0.913 / 1.000 (BM25 0.569 / 0.721), the other language
0.748 / 0.530 (BM25 0.176 / 0.078), synonyms 0.454 / 0.574 (BM25 0.099 / 0.077).

Speed — the laptop's i5-13450HX (Debug build, the machine shared with other builds: load 5–7):

* Indexing: 173 notes (the synthetic notes and the user guide, 1,102 chunks, 174k tokens) in 46 s at
  normal priority, 4 threads (~3,800 tokens/s — the prototype's 3,900–4,200 on the idle machine; at
  load 12–30 it fell to ~2,000). The app itself, at SCHED_IDLE in calls of 16 notes: 1,063 chunks in
  68 s (218 s under the heavier load). The next start reads the kept vectors (0.83 MB) in a few
  milliseconds and embeds nothing.
* A query: 10.6 ms to embed (one thread), 1.8 ms to scan 1,102 chunks (the core at -O0), BM25 9 ms;
  the model opens in 0.4–1.0 s (the first query after it left memory). `brulion-cli search
  --semantic`: about 1 s in all, most of it loading the model.
* The Pixel 9 Pro's numbers above (1,600 tokens/s with 3 threads, 6 ms a query) put a 1,000-note
  notebook at about 10 minutes of background work, once.

