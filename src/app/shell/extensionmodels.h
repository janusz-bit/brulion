#pragma once
// The lists of Workspace.extensions (docs/UI.md "Extensions"): ExtensionListModel — every
// extension of the open notebook with its state (Settings → Extensions), ExtensionConsoleModel —
// the Luau console's lines (the console view: what was evaluated, results, print() and b.log of
// every extension, errors), Emacs' *Messages* and *ielm* in one. Each line has a number that only
// grows (also over clear() and the oldest lines dropped): the remote control's `console` and
// `events` read the lines after one (docs/CLI.md "Live changes").

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantList>

namespace brulion::shell {

/// One extension as Settings → Extensions lists it.
struct ExtensionRow
{
    QString id; ///< the engine's id ("silownia", "licznik-slow")
    QString title; ///< "Siłownia", the page's title
    QString kind; ///< "builtin" | "page"
    /// builtin: "running" | "overridden" (a page of the notebook runs instead) | "builtinOff";
    /// page: "running" | "error" (it failed to load; an older version may still run) |
    /// "approval" (new or changed elsewhere: waits for approval on this device) | "off" |
    /// "problem" (no code) | "duplicate" (another page has its id) | "builtinOff" (it overrides a
    /// built-in that is off)
    QString state;
    QString message; ///< the error or the problem, one line
    QString path; ///< the page ("" for a built-in without one)
    QString permissions; ///< "write, ai" as the UI shows them
    QString description;
    int commands = 0, views = 0, hooks = 0;
    int errorLine = -1; ///< 0-based line of the page the error names, -1 none
    int settings = 0;   ///< the settings it declared (b.settings): Settings → Extensions shows them
    QStringList modules; ///< the extension pages it requires (b.require), their titles
    int types = 0;       ///< warnings of Luau's type checker (typecheck.h)
    QString typeMessage; ///< the first one: "line 14: Unknown global 'x'"

    friend bool operator==(const ExtensionRow &, const ExtensionRow &) = default;
};

class ExtensionListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        KindRole,
        StateRole,
        MessageRole,
        PathRole,
        PermissionsRole,
        DescriptionRole,
        CommandsRole,
        ViewsRole,
        HooksRole,
        ErrorLineRole,
        SettingsRole,
        ModulesRole,
        TypesRole,
        TypeMessageRole,
    };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_rows.size()); }

    void setRows(const QList<ExtensionRow> &rows);
    const QList<ExtensionRow> &rows() const { return m_rows; }

Q_SIGNALS:
    void countChanged();

private:
    QList<ExtensionRow> m_rows;
};

class ExtensionConsoleModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// How many inputs the history has (inputAt(0) is the latest).
    Q_PROPERTY(int inputCount READ inputCount NOTIFY countChanged FINAL)
public:
    enum Role { KindRole = Qt::UserRole + 1, TextRole, SourceRole };
    static constexpr int MaxLines = 1000;
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_lines.size()); }
    int inputCount() const { return int(m_inputs.size()); }

    /// `kind`: "input" (evaluated), "result", "print" (print / b.log), "error", "info".
    void append(const QString &kind, const QString &text, const QString &source = QString());
    Q_INVOKABLE void clear();
    /// The `back`-th latest input (0 = the last), "" outside the history.
    Q_INVOKABLE QString inputAt(int back) const;
    /// Every line as plain text ("> 1 + 1\n2"): Copy in the console view.
    Q_INVOKABLE QString text() const;

    /// The number of the newest line (0 before the first one).
    qint64 lastNumber() const { return m_next - 1; }
    /// The number of the oldest line kept (lastNumber() + 1 when none is).
    qint64 firstNumber() const { return m_lines.isEmpty() ? m_next : m_lines.first().number; }
    /// The lines numbered after `since`, at most `limit` (the first ones): [{n, kind, text,
    /// source}]. `since` < 0: the newest `limit` lines.
    QVariantList linesAfter(qint64 since, int limit) const;

Q_SIGNALS:
    void countChanged();
    /// A line was added (its number, as linesAfter() gives it).
    void lineAppended(qint64 number, const QString &kind, const QString &text, const QString &source);

private:
    struct Line
    {
        QString kind, text, source;
        qint64 number = 0;
    };
    QList<Line> m_lines;
    QStringList m_inputs;
    qint64 m_next = 1;
};

} // namespace brulion::shell
