// TerminalSessionModel — the terminal panel's tabs (qml/terminal/TerminalPanel.qml): the sessions
// it owns, in order, with their titles and states for the tab strip.
//
//   TerminalSessionModel { id: sessions }
//   const i = sessions.add(folder, "", "")         // a shell; add(folder, "npm test", qsTr("Tests"))
//   view.session = sessions.at(i); view.session.start()
//   sessions.remove(i)                             // hangs up on its program, deleted later
//
// Roles: session (TerminalSession), title, running, hasExited, exitCode.
#pragma once

#include "terminalsession.h"

#include <QAbstractListModel>
#include <QList>

namespace brulion::terminal {

class TerminalSessionModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// Whether this device can run a terminal (TerminalSession::available()).
    Q_PROPERTY(bool available READ available CONSTANT FINAL)

public:
    enum Role { SessionRole = Qt::UserRole + 1, TitleRole, RunningRole, HasExitedRole, ExitCodeRole };

    explicit TerminalSessionModel(QObject *parent = nullptr);
    ~TerminalSessionModel() override;

    int count() const { return int(m_sessions.size()); }
    static bool available();

    /// A new session (not started) at the end; its row.
    Q_INVOKABLE int add(const QString &folder, const QString &command, const QString &label);
    /// The session of `row` stopped and removed (deleted once the event loop is back).
    Q_INVOKABLE void remove(int row);
    /// The session of `row` (null out of range).
    Q_INVOKABLE brulion::terminal::TerminalSession *at(int row) const;
    Q_INVOKABLE int indexOf(brulion::terminal::TerminalSession *session) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void countChanged();

private:
    void sessionChanged(TerminalSession *session);

    QList<TerminalSession *> m_sessions;
};

} // namespace brulion::terminal
