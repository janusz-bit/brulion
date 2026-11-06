# Search quality experiments (semantic search prototype)

Python scripts behind the numbers in docs/SEMANTIC.md. Not part of the build; run with
`uv run python <script>` (pyproject.toml: torch with CUDA 12.8, sentence-transformers;
`LD_LIBRARY_PATH=/run/opengl-driver/lib` on NixOS for the GPU). Paths to `brulion-cli` /
`brulion-searchbench` at the top of bench.py and run_db.py point to local build directories — adjust.

## Synthetic notes (data/)

LLM-written personal notes, mostly Polish, with queries in five categories (data/SPEC.md):
`testA` + `testB` (140 notes, 342 queries) for testing, `trainC` + `trainD` (160 notes, 519 queries,
other topics) for every choice (fusion weight, calibration vectors in calibration/, fine-tuning).

* `run_baselines.py`, `run_main.py`, `run_extra.py` — BM25 vs e5-small / mmlw-e5-small / MiniLM /
  e5-base, fusion, gradient-free edits (edits.py: centering, language direction, whitening,
  in-weight ablation, diacritic token rows), fine-tuning (ft.py), bootstrap confidence intervals.
* `export_calib.py` — the calibration JSON files the C++ prototype reads.

## Multi-domain database (db/)

`build_db.py` turns downloaded public sets into `db/<name>/notes/*.md` + `queries.jsonl`
(downloads: see the URLs in build_db.py and docs/SEMANTIC.md; the database itself is not in the
repository — licences vary, several sets are research-only). Sets with real queries and relevance
judgements: nfcorpus-pl, scifact-pl (BEIR-PL), PolEval 2022 legal-questions and allegro-faq,
PoliChat (PWr regulations), WebFAQ pol (sample). Note vaults with LLM-written queries
(db-queries/, rules in data/QUERY_SPEC.md): D&D 5.2.1 SRD (+ galgilor's English queries),
lyz-code/blue-book, logseq/docs, Polish Wikibooks recipes, Polish Wikivoyage (sample).

* `run_db.py` — every set: BM25 through brulion-searchbench (Brulion's real SearchIndex), models on
  the GPU with chunks exactly as the C++ chunker (chunker.py), raw / calibrated / hybrid.
* `summarize.py` → results_db.md; `calib_variants.py`, `length_rule.py` — which part of the
  calibration helps, and the query-length rule; `eval_db.py` — C++ runs against Python.
