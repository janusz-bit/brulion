#pragma once
// ExtensionViewModel — an extension's view as a list for QML (QML type `ExtensionView`, docs/UI.md
// "Extension views"): the node tree of b.view's render function (b.ui: column, row, card, heading,
// text, link, button, check, stepper, menu, month, chart, table, empty, open, run) flattened into
// lines, a row's controls in a model of their own. A card's lines stay lines of the flat list, each
// with its part of the card (lineCard: "first", "middle", "last", "only"; "" outside cards). It renders again when its extension, view or params change,
// after Workspace.extensions.changed() (a command or an action finished) and when the index
// changed (a note written anywhere: coalesced) — and at once after an action of its own controls.
// The same view drawn again with lines of the same kinds changes them in place (dataChanged): the
// delegates stay, so a number being typed into a stepper keeps its field and the on-screen keyboard.
//
//   ExtensionView { id: today; extensions: Workspace.extensions; extension: "silownia"; view: "today" }
//   Repeater { model: today; delegate: … lineType, lineText, controls … }
//   today.activate(row) / today.activateControl(row, control, value) / today.enterValue(row, control, "82,5")
//
// Lines (role lineType): heading, text (lineIcon, lineTone), empty (lineTitle + lineText, lineIcon),
// link, open, run, button, menu (a "⋯": menuLabels() / activateMenuItem()), row (controls — a
// lone input, select, slider, date, toggle or progress is a row of its own, `lineBare`: no card), month
// (lineMonth "2026-11", lineDays — a map per day of the month { date, day, state, tappable, menu } —
// and lineTitle its label; activateDay(), a day's menu), chart (chartData + chartSource for
// ChartView), tableHead / tableRow (cells).
//
// Menus (a menu node's items, a check's menu, a day's menu) are lists of { label, action, args } (a
// view action), { label, page, line? } (a page at its 1-based line) or { label, journal = date } (a
// day's journal), each with an optional icon.

#include <QAbstractListModel>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <optional>

namespace brulion {
class VaultIndex;
}

namespace brulion::shell {

class WorkspaceExtensions;
class ExtensionViewModel;

/// The controls of one row (roles controlType, controlText, controlPage, controlChecked,
/// controlValue, controlStep, controlUnit, controlIndex, controlMuted, controlDisplay — a stepper's
/// number with its unit, "82,5 kg" —, controlNumber — the number alone, "82,5" —, controlState — a
/// check's "done" | "open" | "skipped" —, controlLabel — a check's or a menu's name —, controlIcon,
/// controlTone — a text's —, controlHasMenu — a check with a menu, a menu with items; the controls
/// that take what is typed or chosen: an input's controlText (its value), controlPlaceholder,
/// controlMultiline, controlSubmit (its button's label), controlKeep (the text stays after a
/// submit); a select's controlText (the chosen option's label: menuLabels() lists them); a slider's
/// controlValue, controlMin, controlMax, controlStep; a date's controlText ("YYYY-MM-DD", "" none);
/// a toggle's controlChecked; a progress bar's controlValue, controlMax, controlLabel, controlTone).
class ExtensionControlsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        TypeRole = Qt::UserRole + 1,
        TextRole,
        PageRole,
        CheckedRole,
        ValueRole,
        StepRole,
        UnitRole,
        IndexRole,
        MutedRole,
        DisplayRole,
        NumberRole,
        StateRole,
        LabelRole,
        IconRole,
        ToneRole,
        HasMenuRole,
        PlaceholderRole,
        MultilineRole,
        SubmitRole,
        KeepRole,
        MinRole,
        MaxRole,
    };
    /// `comma`: numbers shown with a decimal comma (Polish).
    ExtensionControlsModel(QList<QVariantMap> controls, bool comma, QObject *parent);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariantMap control(int i) const { return m_controls.value(i); }
    /// The same kinds of controls in the same order: update() changes them in place.
    bool sameKinds(const QList<QVariantMap> &controls) const;
    /// New controls: in place when they are of the same kinds (dataChanged with the roles that
    /// changed — a control that did not change says nothing), else a reset.
    void update(QList<QVariantMap> controls, bool comma);

private:
    QVariant value(int row, int role) const;

    QList<QVariantMap> m_controls;
    bool m_comma = false;
};

/// A number as the UI shows it: at most two decimals, a decimal comma when `comma` ("82,5").
QString formatExtensionNumber(double value, bool comma);
/// A number typed into a stepper or a form's field: digits with one decimal comma or point
/// ("82,5", "82.5", ",5", "100."), spaces anywhere, the field's own `unit` after it ("82,5 kg",
/// "82,5KG"). nullopt for anything else: empty text, a sign, an exponent, "inf", other words.
std::optional<double> parseExtensionNumber(const QString &text, const QString &unit = QString());
/// One step up (direction > 0) or down from `value` onto the step's grid, never below 0: 31 with
/// a step of 2,5 goes to 32,5 or 30, 30 to 32,5 or 27,5. A step of 0 or less (or not finite) is 1.
double stepExtensionNumber(double value, double step, int direction);

class ExtensionViewModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(brulion::shell::WorkspaceExtensions *extensions READ extensions WRITE setExtensions NOTIFY extensionsChanged FINAL)
    Q_PROPERTY(QString extension READ extension WRITE setExtension NOTIFY extensionChanged FINAL)
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged FINAL)
    Q_PROPERTY(QVariantMap params READ params WRITE setParams NOTIFY paramsChanged FINAL)
    /// One parameter by bindings (compiled QML builds no maps): `paramKey: "exercise"; paramValue: …`.
    Q_PROPERTY(QString paramKey READ paramKey WRITE setParamKey NOTIFY paramsChanged FINAL)
    Q_PROPERTY(QString paramValue READ paramValue WRITE setParamValue NOTIFY paramsChanged FINAL)
    /// A ```view block's text (docs/SYNTAX.md §13 "View blocks"): its first line names the view,
    /// `extension/view`, the lines after it are params (`key: value`). Sets extension, view, params.
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged FINAL)
    /// Why `source` names no view ("" fine).
    Q_PROPERTY(QString sourceProblem READ sourceProblem NOTIFY sourceChanged FINAL)
    /// The note the view is drawn beside or in (a ```view block's page, the side panel's): its
    /// `page`, `path` and `date` (a daily note's) join the params, unless they set them.
    Q_PROPERTY(QString notePath READ notePath WRITE setNotePath NOTIFY notePathChanged FINAL)
    /// False: nothing is drawn again (a block off the screen, a hidden page or panel); a change
    /// meanwhile draws it when it is live again.
    Q_PROPERTY(bool live READ live WRITE setLive NOTIFY liveChanged FINAL)
    /// The view's extension does not run: "approval" | "off" | "error" | "unknown"; "" it runs.
    Q_PROPERTY(QString extensionState READ extensionState NOTIFY renderedChanged FINAL)
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
        IconRole,
        ToneRole,
        CardRole,
        MonthRole,
        DaysRole,
        BareRole,
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
    QString source() const { return m_source; }
    void setSource(const QString &source);
    QString sourceProblem() const { return m_sourceProblem; }
    QString notePath() const { return m_notePath; }
    void setNotePath(const QString &path);
    bool live() const { return m_live; }
    void setLive(bool live);
    QString extensionState() const { return m_extensionState; }

    /// Renders now (also what the model does by itself after a change).
    Q_INVOKABLE void refresh();
    /// One parameter of the view ("" removes it).
    Q_INVOKABLE void setParam(const QString &key, const QString &value);
    /// The params from a route of the view "extension" ({extension, name, title, …}): every key
    /// but those four (ExtensionRouteView).
    Q_INVOKABLE void setRouteParams(const QVariantMap &route);
    /// The route that shows `view` of this extension with `params` (an "open" node's request).
    Q_INVOKABLE QVariantMap routeFor(const QString &view, const QVariantMap &params) const;
    /// A line's own action: open (another view of the extension), run (a command), link (the page).
    Q_INVOKABLE void activate(int row);
    /// A control of a row: a button, a check, a stepper (`value`: the stepper's new value), open,
    /// run, link. The view is drawn again at once: in place when its lines keep their kinds — the
    /// next tap acts on the lines as they are now —, else after the timer.
    Q_INVOKABLE void activateControl(int row, int control, double value = 0);
    /// A stepper's next value one step up (direction 1) or down (-1) — stepExtensionNumber().
    Q_INVOKABLE double stepped(int row, int control, int direction) const;
    /// A number typed into a stepper ("82,5", "82.5", "82,5 kg" — parseExtensionNumber()): its
    /// action gets it as the new value, as from activateControl(). False, and nothing done, for
    /// text that is no such number, the value the stepper shows (at its two decimals), a control
    /// that is no stepper, and while the lines are being changed (a field that loses the focus
    /// because its delegate goes says nothing to another line).
    Q_INVOKABLE bool enterValue(int row, int control, const QString &text);
    /// What was typed into an input (Enter, its button) or picked in a date ("YYYY-MM-DD", "" for
    /// none): its action gets it as `value`. False when the control is neither, while the lines
    /// change, and for a date that is none.
    Q_INVOKABLE bool submitText(int row, int control, const QString &text);
    /// A slider's value snapped onto its step within its min and max (what activateControl() gets).
    Q_INVOKABLE double snapped(int row, int control, double value) const;
    /// A number as the view shows it: at most two decimals, a decimal comma in Polish.
    Q_INVOKABLE QString formatNumber(double value) const;
    /// A day of a month line tapped: its action with its args (nothing for a day without one).
    Q_INVOKABLE void activateDay(int row, const QString &date);
    /// The labels of a menu: of the control `control` of a row (a menu, a check's menu), else of the
    /// day `date` of a month line, else of a menu line.
    Q_INVOKABLE QStringList menuLabels(int row, int control, const QString &date) const;
    /// The icons of that menu's items ("" none): the item's own, else a journal's or a page's.
    Q_INVOKABLE QStringList menuIcons(int row, int control, const QString &date) const;
    /// That menu's item `item` chosen: its view action, its page (pageRequested) or its day's
    /// journal (journalRequested).
    Q_INVOKABLE void activateMenuItem(int row, int control, const QString &date, int item);

Q_SIGNALS:
    void extensionsChanged();
    void extensionChanged();
    void viewChanged();
    void paramsChanged();
    void renderedChanged();
    void sourceChanged();
    void notePathChanged();
    void liveChanged();
    /// A link of the view, a menu's page: the page to open (its name, a [[link]] target), at the
    /// 0-based `line` when ≥ 0 (the node's 1-based line).
    void pageRequested(const QString &page, int line);
    /// A menu's journal item: the daily note of "YYYY-MM-DD".
    void journalRequested(const QString &date);
    /// An "open" node: the view and params the extension asks for.
    void viewRequested(const QString &view, const QVariantMap &params);

private:
    struct Line {
        QString type, text, title, page, chartSource, icon, tone, card, month;
        bool muted = false;
        bool bare = false; // a lone control's row: no card of its own
        QStringList cells;
        QVariantMap chartData;
        QVariantList days; // a month's days for QML
        QVariantMap node; // the node itself (actions, args, views to open)
        QList<QVariantMap> controlNodes; // a row's controls, until they are in `controls`
        ExtensionControlsModel *controls = nullptr;
    };
    void scheduleRefresh();
    /// Draws the view again. `reshape`: lines of other kinds may replace the shown ones (a reset);
    /// without it only a change in place is made, anything else waits for the timer.
    void render(bool reshape);
    static void flatten(const QVariantMap &node, int depth, QList<Line> *lines);
    static void addLine(const QVariantMap &node, QList<Line> *lines);
    /// A node's action (activate(), activateControl()); false when it has none to do.
    bool run(const QVariantMap &node, double value);
    /// The view's action `action` with `args`, then the view drawn again in place.
    bool runAction(const QString &action, const QVariantMap &args);
    /// The items of a menu (menuLabels()).
    QVariantList menuItems(int row, int control, const QString &date) const;
    static QVariantList daysOf(const QVariantMap &node, QString *month);
    static QVariantMap chartDataOf(const QVariantMap &node);
    static QList<int> changedRoles(const Line &before, const Line &after);

    QPointer<WorkspaceExtensions> m_extensions;
    QString m_extension, m_view, m_paramKey;
    QVariantMap m_params;
    /// What the shown lines are a drawing of: only the same view is changed in place.
    QString m_shownExtension, m_shownView;
    QVariantMap m_shownParams;
    bool m_shown = false;
    /// The lines are being changed (render()): controls do nothing.
    bool m_updating = false;
    QString m_error;
    int m_revision = 0;
    QList<Line> m_lines;
    QTimer m_refreshTimer;
    QString m_source, m_sourceProblem, m_notePath, m_extensionState;
    bool m_live = true;
    bool m_stale = false; // asked to draw while not live
};

} // namespace brulion::shell
