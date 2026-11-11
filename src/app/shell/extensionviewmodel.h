#pragma once
// ExtensionViewModel — an extension's view as a list for QML (QML type `ExtensionView`, docs/UI.md
// "Extension views"): the node tree of b.view's render function (b.ui: column, row, heading,
// text, link, button, check, stepper, chart, table, empty, open, run) flattened into lines, a row's
// controls in a model of their own. It renders again when its extension, view or params change,
// after Workspace.extensions.changed() (a command or an action finished) and when the index
// changed (a note written anywhere: coalesced).
//
//   ExtensionView { id: today; extensions: Workspace.extensions; extension: "silownia"; view: "today" }
//   Repeater { model: today; delegate: … lineType, lineText, controls … }
//   today.activate(row) / today.activateControl(row, control, value)
//
// Lines (role lineType): heading, text, empty (lineTitle + lineText), link, open, run, row (controls),
// chart (chartData + chartSource for ChartView), tableHead / tableRow (cells).

#include <QAbstractListModel>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace brulion {
class VaultIndex;
}

namespace brulion::shell {

class WorkspaceExtensions;
class ExtensionViewModel;

/// The controls of one row (roles controlType, controlText, controlPage, controlChecked,
/// controlValue, controlStep, controlUnit, controlIndex).
class ExtensionControlsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { TypeRole = Qt::UserRole + 1, TextRole, PageRole, CheckedRole, ValueRole, StepRole, UnitRole, IndexRole, MutedRole, DisplayRole };
    /// `comma`: numbers shown with a decimal comma (Polish).
    ExtensionControlsModel(QList<QVariantMap> controls, bool comma, QObject *parent);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariantMap control(int i) const { return m_controls.value(i); }

private:
    QList<QVariantMap> m_controls;
    bool m_comma = false;
};

/// A number as the UI shows it: at most two decimals, a decimal comma when `comma` ("82,5").
QString formatExtensionNumber(double value, bool comma);

class ExtensionViewModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(brulion::shell::WorkspaceExtensions *extensions READ extensions WRITE setExtensions NOTIFY extensionsChanged FINAL)
    Q_PROPERTY(QString extension READ extension WRITE setExtension NOTIFY extensionChanged FINAL)
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged FINAL)
    Q_PROPERTY(QVariantMap params READ params WRITE setParams NOTIFY paramsChanged FINAL)
    /// One parameter by bindings (compiled QML builds no maps): `paramKey: "exercise"; paramValue: …`.
    Q_PROPERTY(QString paramKey READ paramKey WRITE setParamKey NOTIFY paramsChanged FINAL)
    Q_PROPERTY(QString paramValue READ paramValue WRITE setParamValue NOTIFY paramsChanged FINAL)
    /// The view could not be drawn: the first line of the extension's error ("" = fine).
    Q_PROPERTY(QString error READ error NOTIFY renderedChanged FINAL)
    Q_PROPERTY(int revision READ revision NOTIFY renderedChanged FINAL)

public:
    enum Role {
        TypeRole = Qt::UserRole + 1,
        TextRole,
        TitleRole,
        MutedRole,
        PageRole,
        CellsRole,
        ChartDataRole,
        ChartSourceRole,
        ControlsRole,
        HasControlsRole,
    };

    explicit ExtensionViewModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    WorkspaceExtensions *extensions() const { return m_extensions; }
    void setExtensions(WorkspaceExtensions *extensions);
    QString extension() const { return m_extension; }
    void setExtension(const QString &extension);
    QString view() const { return m_view; }
    void setView(const QString &view);
    QVariantMap params() const { return m_params; }
    QString paramKey() const { return m_paramKey; }
    void setParamKey(const QString &key);
    QString paramValue() const { return m_params.value(m_paramKey).toString(); }
    void setParamValue(const QString &value);
    void setParams(const QVariantMap &params);
    QString error() const { return m_error; }
    int revision() const { return m_revision; }

    /// Renders now (also what the model does by itself after a change).
    Q_INVOKABLE void refresh();
    /// One parameter of the view ("" removes it).
    Q_INVOKABLE void setParam(const QString &key, const QString &value);
    /// A line's own action: open (another view of the extension), run (a command), link (the page).
    Q_INVOKABLE void activate(int row);
    /// A control of a row: a button, a check, a stepper (`value`: the stepper's new value), open,
    /// run, link.
    Q_INVOKABLE void activateControl(int row, int control, double value = 0);
    /// A stepper's next value one step up (direction 1) or down (-1), never below 0.
    Q_INVOKABLE double stepped(int row, int control, int direction) const;

Q_SIGNALS:
    void extensionsChanged();
    void extensionChanged();
    void viewChanged();
    void paramsChanged();
    void renderedChanged();
    /// A link of the view: the page to open (its name, a [[link]] target).
    void pageRequested(const QString &page);
    /// An "open" node: the view and params the extension asks for.
    void viewRequested(const QString &view, const QVariantMap &params);

private:
    struct Line {
        QString type, text, title, page, chartSource;
        bool muted = false;
        QStringList cells;
        QVariantMap chartData;
        QVariantMap node; // the node itself (actions, args, views to open)
        ExtensionControlsModel *controls = nullptr;
    };
    void scheduleRefresh();
    void flatten(const QVariantMap &node, int depth);
    void addLine(const QVariantMap &node);
    void run(const QVariantMap &node, double value);
    static QVariantMap chartDataOf(const QVariantMap &node);

    QPointer<WorkspaceExtensions> m_extensions;
    QString m_extension, m_view, m_paramKey;
    QVariantMap m_params;
    QString m_error;
    int m_revision = 0;
    QList<Line> m_lines;
    QTimer m_refreshTimer;
};

} // namespace brulion::shell
