// QML registration of the "Code" mode's git area (library brulion_gitpanel, gitpanel/area.cmake)
// in the app's QML module "Brulion" (the src/app/ui pattern: the class knows nothing about QML):
//
//   CodeGit   creatable — CodeGit { folder: …; turnCheckpoint: … }: one folder's git, its changes,
//             diffs, commit / push / pull, branches, pull requests, worktrees and checkpoints
#pragma once

#include "codegit.h"

#include <QtQml/qqmlregistration.h>

namespace brulion::gitpanel {

struct CodeGitForeign {
    Q_GADGET
    QML_FOREIGN(brulion::gitpanel::CodeGit)
    QML_NAMED_ELEMENT(CodeGit)
};

} // namespace brulion::gitpanel
