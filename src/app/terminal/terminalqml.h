// QML registration of the terminal area (library brulion_terminal, terminal/area.cmake) in the app's
// QML module "Brulion" (the src/app/ui pattern: the classes know nothing about QML):
//
//   TerminalSession        creatable — TerminalSession { folder: …; command: …; label: … }
//   TerminalSessionModel   creatable — the panel's tabs: add(), remove(), at()
//   TerminalView           creatable — TerminalView { session: …; dark: Theme.dark }
#pragma once

#include "terminalsession.h"
#include "terminalsessionmodel.h"
#include "terminalview.h"

#include <QtQml/qqmlregistration.h>

namespace brulion::terminal {

struct TerminalSessionForeign {
    Q_GADGET
    QML_FOREIGN(brulion::terminal::TerminalSession)
    QML_NAMED_ELEMENT(TerminalSession)
};

struct TerminalSessionModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::terminal::TerminalSessionModel)
    QML_NAMED_ELEMENT(TerminalSessionModel)
};

struct TerminalViewForeign {
    Q_GADGET
    QML_FOREIGN(brulion::terminal::TerminalView)
    QML_NAMED_ELEMENT(TerminalView)
};

} // namespace brulion::terminal
