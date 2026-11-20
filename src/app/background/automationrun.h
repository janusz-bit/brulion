// Android's background run of automation requests (docs/ANDROID.md "Automation (Tasker …)"): an
// intent of Tasker, Automate, MacroDroid or `adb` asks Brulion to write while it is closed; the
// Kotlin side (AutomationReceiver.kt, AutomationActivity.kt) hands the request to a process of its
// own (AutomationService.kt, a QtService: main() → runAutomationService()), where one
// AutomationRunner carries it out:
//
//   job    { uri: "brulion://append?…&key=…", id: "…", target: { root, stateDir, name, vault,
//            policy, language, followOutsideLinks, remindersOn } }   (the target: what the app
//            published — AppController::publishAutomationTarget())
//   result automation::Outcome::toJson() + { keyed (the notebook's key was valid: the result may
//            say what was written), vault, name, file (the note's absolute path) }
//
// Only writes run here, and only with the notebook's key: nobody can be asked in the background
// (automation::decide(…, canAsk = false)); opening something, a request without the key and an
// extension's command go to the app's window instead (the Kotlin side routes them; one that
// reaches here anyway is refused: needs_app, key_required). The notebook is the one the app last
// published — `vault=` may name it, never another. Built on every platform (the desktop tests
// run it); only Android starts it.
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <memory>

namespace brulion::notebook {
class Notebook;
}

namespace brulion::background {

class AutomationRunner {
public:
    /// `appDataDir`: where the keys are (secrets/automation-keys.json); `clock`: now (default: the
    /// current local time).
    explicit AutomationRunner(QString appDataDir, std::function<QDateTime()> clock = {});
    ~AutomationRunner();

    /// Carry out one job (see the file comment); never throws, always answers.
    QJsonObject run(const QJsonObject &job);

private:
    QString m_appDataDir;
    std::function<QDateTime()> m_clock;
    /// The notebook of the last job, kept open for the next one (the same root).
    std::unique_ptr<notebook::Notebook> m_notebook;
    QString m_root;
};

} // namespace brulion::background
