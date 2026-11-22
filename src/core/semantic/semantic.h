// semantic — search by meaning (docs/SEMANTIC.md): notes are cut into chunks (chunker.h,
// notes.h), each chunk is turned into a vector by a small multilingual embedding model
// (semanticrunner.h: the plugin brulion_semantic, llama.cpp + a GGUF file of multilingual-e5-small —
// models.h), vectors are kept in VectorIndex (brute force cosine, a note scores as its best chunk;
// saved on the device, updated note by note: notes.h diffIndex()) and a query's hits are fused with
// the full-text SearchIndex hits (fusion.h hybridSearch()) — never semantic alone.
//
// Calibration (calibration.h) is a gradient-free edit of the model's output, measured in the
// search experiments: subtract the mean query / passage vectors and project out the direction
// that separates Polish from English notes (estimated on the user's own notes). It costs two
// vectors and lifted cross-language search from 0.43 to 0.83 MRR for e5-small.
//
// Library layout: brulion_core_semantic (this module: chunks, calibration, vector index, fusion,
// the models the app downloads, the plugin's interface and loader — GUI-free and without
// llama.cpp, built everywhere) and, in builds with BRULION_LOCAL_AI only, brulion_semantic_llama
// (embedder.h: llama.cpp; not part of brulion_core) and the plugin brulion_semantic over it, which
// the app (shell/semanticsearch.h) and brulion-cli load on first use. The benchmark tool
// tests/searchbench links brulion_semantic_llama directly.
#pragma once

#include "semantic/calibration.h"
#include "semantic/chunker.h"
#include "semantic/fusion.h"
#include "semantic/models.h"
#include "semantic/notes.h"
#include "semantic/semanticrunner.h"
#include "semantic/vectorindex.h"
