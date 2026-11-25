// The plugin the app loads the first time a model on the device is asked (ai/localrunner.h):
// the llama.cpp runner behind two exported functions — one opens a model, one checks that a file
// of the user's own can be loaded.

#include "ai/llamarunner.h"

#include <QtCore/qglobal.h>

extern "C" Q_DECL_EXPORT brulion::ai::LocalRunner *
brulion_localai_open(const brulion::ai::LocalRunner::Options *options, QString *error)
{
    return brulion::ai::openLlamaRunner(*options, error).release();
}

extern "C" Q_DECL_EXPORT bool brulion_localai_probe(const QString *path, const QString *backendDir, QString *error)
{
    return brulion::ai::probeLlamaModel(*path, *backendDir, error);
}
