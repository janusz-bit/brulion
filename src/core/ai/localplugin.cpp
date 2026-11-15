// The plugin the app loads the first time a model on the device is asked (ai/localrunner.h):
// the llama.cpp runner behind one exported function.

#include "ai/llamarunner.h"

#include <QtCore/qglobal.h>

extern "C" Q_DECL_EXPORT brulion::ai::LocalRunner *
brulion_localai_open(const brulion::ai::LocalRunner::Options *options, QString *error)
{
    return brulion::ai::openLlamaRunner(*options, error).release();
}
