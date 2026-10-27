// QML registration of the sync and import/export controllers (library brulion_ioviews,
// io/area.cmake) in the app's QML module "Brulion" — the classes know nothing about QML (the
// src/app/ui pattern). All are creatable; give them the shell's singletons:
//
//   ImportController { workspace: Workspace }
//   MsTodoImport { settings: AppSettings; importer: importController }
//   ExportController { workspace: Workspace }
//   NextcloudSetup { settings: AppSettings }
//   SyncthingSetup { settings: AppSettings; workspace: Workspace }
//   ConflictResolver { workspace: Workspace; copy: conflict.copy }
#pragma once

#include "conflictresolver.h"
#include "exportcontroller.h"
#include "importcontroller.h"
#include "mstodoimport.h"
#include "syncsetup.h"

#include <QtQml/qqmlregistration.h>

namespace brulion::ioviews {

struct ImportControllerForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ioviews::ImportController)
    QML_NAMED_ELEMENT(ImportController)
};

struct MsTodoImportForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ioviews::MsTodoImport)
    QML_NAMED_ELEMENT(MsTodoImport)
};

struct ExportControllerForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ioviews::ExportController)
    QML_NAMED_ELEMENT(ExportController)
};

struct NextcloudSetupForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ioviews::NextcloudSetup)
    QML_NAMED_ELEMENT(NextcloudSetup)
};

struct SyncthingSetupForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ioviews::SyncthingSetup)
    QML_NAMED_ELEMENT(SyncthingSetup)
};

struct ConflictResolverForeign {
    Q_GADGET
    QML_FOREIGN(brulion::ioviews::ConflictResolver)
    QML_NAMED_ELEMENT(ConflictResolver)
};

} // namespace brulion::ioviews
