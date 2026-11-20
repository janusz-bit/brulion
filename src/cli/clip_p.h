// The Web Clipper in brulion-cli (docs/CLIPPER.md): a clip written into the notebook (commands_clip.cpp
// — `clip save` and the host's `clip` / `preview` messages share it), the native messaging host
// (cliphost.cpp) and the host manifests installed for the browsers (clipinstall.cpp).
#pragma once

#include "cli_p.h"
#include "clipper/request.h"

#include <QJsonObject>

#include <functional>

namespace brulion::cli {

/// Pictures downloaded so far: `done` of `total`.
using ClipProgress = std::function<void(int done, int total)>;

/// Write the clip `request` into the call's notebook (or, with `preview`, only compute what would be
/// written: no picture is downloaded, nothing written). The result: {path, title, created, kind,
/// place, line, ref, journal?, pictures {total, saved, kept, failed [{source, reason}]}, template,
/// open {path, line}, markdown (preview), dryRun?, diff?}.
Result runClip(Call &call, const clipper::ClipRequest &request, bool preview, const ClipProgress &progress = {});

/// A request problem as the command line's error (exit 2).
Result requestFailure(Call &call, const clipper::RequestProblem &problem);

/// The notebooks a clip may go to: the app's current one and its recent ones (not the app's
/// documentation), and BRULION_VAULT / --vault: [{path, name, current, exists}].
QJsonArray knownNotebooks(Context &context);

/// The native messaging host on standard input and output until the browser closes it.
int serveClipHost(Context &context);

/// Templates the notebook offers for clips: the built-ins and the pages of its templates folder
/// [{id | page, name, builtin}].
QJsonArray clipTemplates(Call &call);

/// Show `path` (a vault path) of the call's notebook in the app: the running app is asked
/// (`app open`), else the app is started there. {opened, via: "message" | "control" | "launch"}.
Result openInApp(Call &call, const QString &path, int line);

} // namespace brulion::cli
