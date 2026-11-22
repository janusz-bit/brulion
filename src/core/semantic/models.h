// The embedding models the app downloads for search by meaning. Part of the semantic module
// (semantic/semantic.h).
//
// A model is an ai::LocalModel of the format "e5" (only `file` set: a GGUF converted by
// scripts/semantic/convert_e5_gguf.py, docs/SEMANTIC.md "The model file"): the app downloads and
// removes it with ai::LocalModelFiles, as it does the language and speech models, in a folder of
// its own (<app data>/models/semantic).
#pragma once

#include "ai/local.h"
#include "semantic/calibration.h"

#include <QList>
#include <QString>

namespace brulion::semantic {

/// The models the app knows, the default first: multilingual-e5-small (8-bit, 132 MB).
const QList<ai::LocalModel> &semanticModels();
/// The model `id` names ("": the default); nullptr for an unknown one.
const ai::LocalModel *semanticModel(const QString &id);

/// The mean embedding of short, note-search-style queries of the model `id` (Calibration's
/// queryMean; the passage mean and the language direction come from the notebook itself). Empty
/// for a model without one: its queries are searched uncalibrated.
Vector queryMeanOf(const QString &id);

/// The file name of a notebook's vector index kept on the device (VectorIndex::save): one per
/// notebook (its Vault::id()) and model.
QString indexFileName(const QString &vaultId, const QString &modelId);

} // namespace brulion::semantic
