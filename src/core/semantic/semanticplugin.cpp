// The plugin the app and brulion-cli load the first time they embed a note or a query
// (semantic/semanticrunner.h): the llama.cpp Embedder behind one exported function.

#include "semantic/embedder.h"

#include <QtCore/qglobal.h>

extern "C" Q_DECL_EXPORT brulion::semantic::SemanticRunner *
brulion_semantic_open(const brulion::semantic::SemanticRunner::Options *options, QString *error)
{
    brulion::semantic::Embedder::Options o;
    o.device = brulion::semantic::Embedder::Device::Auto;
    o.threads = options->threads;
    o.backendDir = options->backendDir;
    return brulion::semantic::Embedder::open(options->modelPath, o, error).release();
}
