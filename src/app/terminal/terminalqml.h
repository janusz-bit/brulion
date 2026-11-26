// QML registration of the terminal area (library brulion_terminal, terminal/area.cmake) in the app's
// QML module "Brulion" (the src/app/ui pattern: the classes know nothing about QML):
//
//   TerminalSession        creatable — TerminalSession { folder: …; command: …; label: … }
//   TerminalView           creatable — TerminalView { session: …; dark: Theme.dark }
//   TerminalRegistry       singleton — every thread's terminals (TerminalRegistry::instance(), tied
//                          to the notebook by terminalwiring.cpp: renames, deletions, archiving)
//   TerminalThread         uncreatable — TerminalRegistry.thread(key, folder): a thread's tabs
//   TerminalGroup          uncreatable — TerminalThread.current / .group(i): a tab's terminals
#pragma once

#include "terminalgroup.h"
#include "terminalregistry.h"
#include "terminalsession.h"
#include "terminalthread.h"
#include "terminalview.h"

#include <QJSEngine>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;

namespace brulion::terminal {

/// The registry, tied to the app's notebook the first time (terminalwiring.cpp).
TerminalRegistry *registryForQml();

struct TerminalSessionForeign {
    Q_GADGET
    QML_FOREIGN(brulion::terminal::TerminalSession)
    QML_NAMED_ELEMENT(TerminalSession)
};

struct TerminalViewForeign {
    Q_GADGET
    QML_FOREIGN(brulion::terminal::TerminalView)
    QML_NAMED_ELEMENT(TerminalView)
};

struct TerminalThreadForeign {
    Q_GADGET
    QML_FOREIGN(brulion::terminal::TerminalThread)
    QML_NAMED_ELEMENT(TerminalThread)
    QML_UNCREATABLE("Use TerminalRegistry.thread(key, folder)")
};

struct TerminalGroupForeign {
    Q_GADGET
    QML_FOREIGN(brulion::terminal::TerminalGroup)
    QML_NAMED_ELEMENT(TerminalGroup)
    QML_UNCREATABLE("Use TerminalThread.current")
};

struct TerminalRegistryForeign {
    Q_GADGET
    QML_FOREIGN(brulion::terminal::TerminalRegistry)
    QML_NAMED_ELEMENT(TerminalRegistry)
    QML_SINGLETON
public:
    static TerminalRegistry *create(QQmlEngine *, QJSEngine *)
    {
        TerminalRegistry *registry = registryForQml();
        QJSEngine::setObjectOwnership(registry, QJSEngine::CppOwnership);
        return registry;
    }
};

} // namespace brulion::terminal
