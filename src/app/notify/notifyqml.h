// QML registration of the notify area's C++ (library brulion_notify, src/app/notify/area.cmake)
// in the app's QML module "Brulion" (the src/app/ui/qmlforeign.h pattern):
//
//   ReminderSounds   singleton — the instance the shell's wiring created
//                    (shell/workspaceintegration.cpp); QML tooling without it gets a private one
#pragma once

#include "remindersounds.h"

#include <QJSEngine>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;

namespace brulion::notify {

struct ReminderSoundsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::notify::ReminderSounds)
    QML_NAMED_ELEMENT(ReminderSounds)
    QML_SINGLETON
public:
    static ReminderSounds *create(QQmlEngine *, QJSEngine *)
    {
        ReminderSounds *sounds = ReminderSounds::instance();
        if (!sounds) {
            static ReminderSounds fallback;
            sounds = &fallback;
        }
        QJSEngine::setObjectOwnership(sounds, QJSEngine::CppOwnership);
        return sounds;
    }
};

} // namespace brulion::notify
