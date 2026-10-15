// CommandModel — the commands of the command palette (Ctrl+K, QML singleton `CommandModel`):
// navigation, journals, new page, quick capture, the page's source and note mode, theme,
// language, sync, panels, settings, about. `commands` is the array BCommandPalette takes ({ id, title, subtitle, icon,
// shortcut, enabled }); QML runs the chosen one with run(id). Commands that only move around or change state run here;
// those that open shell popups (new page, quick switcher, capture, about, theme toggle — the effective theme is known
// to QML only) come back as AppController::uiActionRequested(action).
//
// Titles are translated (tr); `commands` is re-read after a language switch.
#pragma once

#include <QObject>
#include <QVariantList>

namespace brulion::shell {

class AppController;

class CommandModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList commands READ commands NOTIFY commandsChanged FINAL)

public:
    explicit CommandModel(AppController *app);
    ~CommandModel() override;

    QVariantList commands() const;
    /// Run the command `id`; false when unknown or disabled.
    Q_INVOKABLE bool run(const QString &id);
    /// Ids of all commands (tests).
    QStringList ids() const;

Q_SIGNALS:
    void commandsChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    AppController *m_app;
};

} // namespace brulion::shell
