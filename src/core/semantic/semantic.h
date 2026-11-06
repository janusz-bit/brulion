// semantic — search by meaning (prototype): notes are cut into chunks (chunker.h), each chunk is
// turned into a vector by a small multilingual embedding model (embedder.h, llama.cpp + a GGUF
// file of multilingual-e5-small / mmlw-e5-small), vectors are kept in VectorIndex (brute force
// cosine, a note scores as its best chunk) and a query's hits are fused with the full-text
// SearchIndex hits (fusion.h).
//
// Calibration (calibration.h) is a gradient-free edit of the model's output, measured in the
// search experiments: subtract the mean query / passage vectors and project out the direction
// that separates Polish from English notes (estimated on the user's own notes). It costs two
// vectors and lifted cross-language search from 0.43 to 0.83 MRR for e5-small.
//
// Library layout: brulion_core_semantic (this module: chunker, calibration, vector index, fusion
// — GUI-free and dependency-free, built everywhere) and brulion_semantic_llama (embedder.cpp,
// built only when CMake finds llama.cpp; not part of brulion_core, so the app and brulion-cli do
// not depend on it). The benchmark tool tests/searchbench uses both.
#pragma once

#include "semantic/calibration.h"
#include "semantic/chunker.h"
#include "semantic/fusion.h"
#include "semantic/vectorindex.h"
