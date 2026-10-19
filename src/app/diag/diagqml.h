// QML registration of the diagnostics area (library brulion_diag, diag/area.cmake) in the app's
// QML module "Brulion" (the src/app/ui pattern):
//
//   Logs       singleton — main()'s LogController (LogController::instance()); a private,
//              unavailable one when there is none (QML tools)
//   LogModel   creatable — LogModel { group: "sync"; minLevel: "warning"; search: text }
#pragma once

#include "logcontroller.h"
#include "logmodel.h"

#include <QJSEngine>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;

namespace brulion::diag {

struct LogsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::diag::LogController)
    QML_NAMED_ELEMENT(Logs)
    QML_SINGLETON
public:
    static LogController *create(QQmlEngine *, QJSEngine *)
    {
        LogController *logs = LogController::instance();
        if (!logs) {
            static LogController fallback;
            logs = &fallback;
        }
        QJSEngine::setObjectOwnership(logs, QJSEngine::CppOwnership);
        return logs;
    }
};

struct LogModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::diag::LogModel)
    QML_NAMED_ELEMENT(LogModel)
};

} // namespace brulion::diag
