#pragma once
// WorkspaceExtensions — the extensions of the open notebook (docs/EXTENSIONS.md, QML:
// Workspace.extensions): the Luau engine (core extensions::Engine) over a NotebookHost bound to the
// Workspace's vault and index, the built-in extensions the settings switch on (the gym), their
// palette commands, the question a command or an action waits on (b.ask / b.choose / b.form, shown
// by ExtensionQuestionDialog.qml), messages (toasts) and the rest timer (b.timer: a countdown in the
// app and a notification when it ends).
//
//   extensions->runCommand(u"silownia"_s, u"start"_s);                 // the palette
//   extensions->runCommand(u"silownia"_s, u"add-set"_s, path, line);   // on the block under the caret
//   QML: Workspace.extensions.questionKind ("" | "ask" | "choose" | "form") → answerText / answerChoice /
//        setFieldValue + answerForm / cancelQuestion
//
// Views (ExtensionViewModel) render through render() and act(); a view renders again after
// changed() (a command or an action finished, an extension was switched on or off).
//
// Single-threaded (the GUI thread, the Workspace's).

#include "extensions/engine.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace brulion {
class Vault;
class VaultIndex;
namespace extensions {
class NotebookHost;
}
} // namespace brulion

namespace brulion::shell {

/// The fields of a b.form question (roles key, label, kind, value, step, unit).
class ExtensionFormModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { KeyRole = Qt::UserRole + 1, LabelRole, KindRole, ValueRole, StepRole, UnitRole, TextRole, DisplayRole };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reset(const QVariantList &fields);
    QVariantMap values() const;
    Q_INVOKABLE void setNumber(int row, double value);
    Q_INVOKABLE void setText(int row, const QString &text);
    /// A number typed in ("82,5", "82.5"); ignored when it is not one.
    Q_INVOKABLE void setNumberText(int row, const QString &text);
    Q_INVOKABLE void step(int row, int direction);
    void setComma(bool comma) { m_comma = comma; }

private:
    struct Field {
        QString key, label, kind, unit, text;
        double value = 0;
        double step = 1;
    };
    QList<Field> m_fields;
    bool m_comma = false;
};

class WorkspaceExtensions : public QObject {
    Q_OBJECT
    /// The built-in extensions switched on ("silownia").
    Q_PROPERTY(QStringList enabled READ enabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool gymEnabled READ gymEnabled NOTIFY enabledChanged FINAL)
    /// The language of the extensions' words: "pl" | "en".
    Q_PROPERTY(QString language READ language NOTIFY commandsChanged FINAL)
    /// The question waiting for an answer: "" (none), "ask", "choose", "form".
    Q_PROPERTY(QString questionKind READ questionKind NOTIFY questionChanged FINAL)
    /// Counts the questions asked: a dialog closing late cancels only the question it showed.
    Q_PROPERTY(int questionSerial READ questionSerial NOTIFY questionChanged FINAL)
    Q_PROPERTY(QString questionTitle READ questionTitle NOTIFY questionChanged FINAL)
    Q_PROPERTY(QStringList questionOptions READ questionOptions NOTIFY questionChanged FINAL)
    Q_PROPERTY(brulion::shell::ExtensionFormModel *questionFields READ questionFields CONSTANT FINAL)
    /// The rest timer: seconds left (0 = none), its length and label.
    Q_PROPERTY(int restRemaining READ restRemaining NOTIFY restChanged FINAL)
    Q_PROPERTY(int restTotal READ restTotal NOTIFY restChanged FINAL)
    Q_PROPERTY(QString restLabel READ restLabel NOTIFY restChanged FINAL)
    /// The time left as "1:47".
    Q_PROPERTY(QString restText READ restText NOTIFY restChanged FINAL)

public:
    using Clock = std::function<QDateTime()>;
    explicit WorkspaceExtensions(Clock clock, QObject *parent = nullptr);
    ~WorkspaceExtensions() override;

    void attach(Vault *vault, VaultIndex *index);
    void detach();
    void setLanguage(const QString &language);
    void setEnabled(const QStringList &ids);
    /// Shows a notification now (the rest timer's end): Workspace connects its reminders' sink.
    void setNotifier(std::function<void(const QString &title, const QString &body)> notify);

    QStringList enabled() const { return m_enabled; }
    QString language() const { return m_language; }
    bool gymEnabled() const { return m_enabled.contains(QStringLiteral("silownia")); }
    QList<extensions::CommandInfo> commands() const;
    /// The title of a built-in extension in the UI language ("Siłownia"), its id when unknown.
    QString title(const QString &extension) const;

    /// Runs a command; `path`/`line` name the block under the caret (ctx.block) when given.
    Q_INVOKABLE void runCommand(const QString &extension, const QString &name, const QString &path = QString(), int line = -1);
    /// runCommand() of `command` "<extension>/<name>" on the block at `line` of `path` (the editor).
    Q_INVOKABLE void runBlockCommand(const QString &command, const QString &path, int line);
    /// A view's node tree ({} and `error` when it failed). Never while a question waits (the waiting
    /// call would be abandoned): {} then, and the view renders again after changed().
    QVariantMap render(const QString &extension, const QString &view, const QVariantMap &params, QString *error);
    /// One of a view's actions; false when it failed (the message was shown).
    bool act(const QString &extension, const QString &view, const QString &action, const QVariantMap &args);
    bool isWaiting() const;

    QString questionKind() const { return m_questionKind; }
    int questionSerial() const { return m_questionSerial; }
    QString questionTitle() const { return m_questionTitle; }
    QStringList questionOptions() const { return m_questionOptions; }
    ExtensionFormModel *questionFields() const { return m_fields; }
    Q_INVOKABLE void answerText(const QString &text);
    Q_INVOKABLE void answerChoice(const QString &option);
    Q_INVOKABLE void answerForm();
    Q_INVOKABLE void cancelQuestion();

    int restRemaining() const { return m_restRemaining; }
    int restTotal() const { return m_restTotal; }
    QString restLabel() const { return m_restLabel; }
    QString restText() const;
    Q_INVOKABLE void startRest(int seconds, const QString &label);
    Q_INVOKABLE void stopRest();

Q_SIGNALS:
    void enabledChanged();
    void commandsChanged();
    void questionChanged();
    void restChanged();
    /// A command's result or an error, for a toast (kind "info" | "error").
    void message(const QString &text, const QString &kind);
    /// Something changed what views show (a command or action finished, extensions on or off).
    void changed();

private:
    void reload();
    void handle(const extensions::Result &result, bool fromAction);
    void setQuestion(const QVariantMap &question);
    void answer(const QVariant &value);
    void tickRest();

    Clock m_clock;
    Vault *m_vault = nullptr;
    VaultIndex *m_index = nullptr;
    QString m_language = QStringLiteral("pl");
    QStringList m_enabled;
    std::unique_ptr<extensions::NotebookHost> m_host;
    std::unique_ptr<extensions::Engine> m_engine;
    QString m_questionKind, m_questionTitle;
    QStringList m_questionOptions;
    int m_questionSerial = 0;
    ExtensionFormModel *m_fields = nullptr;
    QTimer m_restTimer;
    QDateTime m_restEnd;
    int m_restRemaining = 0;
    int m_restTotal = 0;
    QString m_restLabel;
    std::function<void(const QString &, const QString &)> m_notify;
};

} // namespace brulion::shell
