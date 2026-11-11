#include "extensionviewmodel.h"

#include "workspaceextensions.h"

#include <cmath>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace brulion::shell {

namespace {

constexpr int kMaxLines = 2000; // a view showing more is cut (notes come from sync)
constexpr int kMaxControls = 24;

QString textOf(const QVariantMap &node)
{
    const QString type = node.value(u"type"_s).toString();
    if (type == u"link")
        return node.value(u"label"_s).toString().isEmpty() ? node.value(u"page"_s).toString() : node.value(u"label"_s).toString();
    if (type == u"button" || type == u"open" || type == u"run")
        return node.value(u"label"_s).toString();
    return node.value(u"text"_s).toString();
}

// A list from the script: a sequence, or a table with holes (nil values) as numbered keys.
QVariantList listOf(const QVariant &value, int size)
{
    if (value.metaType().id() == QMetaType::QVariantList)
        return value.toList();
    QVariantList out;
    const QVariantMap map = value.toMap();
    for (int i = 1; i <= size; ++i)
        out << map.value(QString::number(i));
    return out;
}

} // namespace

// ---- ExtensionControlsModel --------------------------------------------------------------------

QString formatExtensionNumber(double value, bool comma)
{
    if (!std::isfinite(value))
        return QString();
    QString s = QString::number(value, 'f', 2);
    while (s.contains(u'.') && (s.endsWith(u'0') || s.endsWith(u'.')))
        s.chop(1);
    if (s == u"-0")
        s = u"0"_s;
    return comma ? s.replace(u'.', u',') : s;
}

ExtensionControlsModel::ExtensionControlsModel(QList<QVariantMap> controls, bool comma, QObject *parent)
    : QAbstractListModel(parent), m_controls(std::move(controls)), m_comma(comma)
{
}

int ExtensionControlsModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_controls.size()); }

QVariant ExtensionControlsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_controls.size())
        return {};
    const QVariantMap &c = m_controls[index.row()];
    switch (role) {
    case TypeRole: return c.value(u"type"_s).toString();
    case TextRole: return textOf(c);
    case PageRole: return c.value(u"page"_s).toString();
    case CheckedRole: return c.value(u"checked"_s).toBool();
    case ValueRole: return c.value(u"value"_s).toDouble();
    case StepRole: return c.value(u"step"_s, 1).toDouble();
    case UnitRole: return c.value(u"unit"_s).toString();
    case IndexRole: return index.row();
    case MutedRole: return c.value(u"muted"_s).toBool();
    case DisplayRole: {
        const QString unit = c.value(u"unit"_s).toString();
        const QString number = formatExtensionNumber(c.value(u"value"_s).toDouble(), m_comma);
        return unit.isEmpty() ? number : number + u' ' + unit;
    }
    }
    return {};
}

QHash<int, QByteArray> ExtensionControlsModel::roleNames() const
{
    return {{TypeRole, "controlType"}, {TextRole, "controlText"},   {PageRole, "controlPage"},
            {CheckedRole, "controlChecked"}, {ValueRole, "controlValue"}, {StepRole, "controlStep"},
            {UnitRole, "controlUnit"}, {IndexRole, "controlIndex"}, {MutedRole, "controlMuted"},
            {DisplayRole, "controlDisplay"}};
}

// ---- ExtensionViewModel ------------------------------------------------------------------------

ExtensionViewModel::ExtensionViewModel(QObject *parent) : QAbstractListModel(parent)
{
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(30ms); // coalesces the index's changes of one write
    connect(&m_refreshTimer, &QTimer::timeout, this, &ExtensionViewModel::refresh);
}

int ExtensionViewModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_lines.size()); }

QVariant ExtensionViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_lines.size())
        return {};
    const Line &l = m_lines[index.row()];
    switch (role) {
    case TypeRole: return l.type;
    case TextRole: return l.text;
    case TitleRole: return l.title;
    case MutedRole: return l.muted;
    case PageRole: return l.page;
    case CellsRole: return l.cells;
    case ChartDataRole: return l.chartData;
    case ChartSourceRole: return l.chartSource;
    case ControlsRole: return QVariant::fromValue(static_cast<QObject *>(l.controls));
    case HasControlsRole: return l.controls != nullptr;
    }
    return {};
}

QHash<int, QByteArray> ExtensionViewModel::roleNames() const
{
    return {{TypeRole, "lineType"},          {TextRole, "lineText"},       {TitleRole, "lineTitle"},
            {MutedRole, "lineMuted"},        {PageRole, "linePage"},       {CellsRole, "lineCells"},
            {ChartDataRole, "lineChartData"}, {ChartSourceRole, "lineChartSource"}, {ControlsRole, "lineControls"},
            {HasControlsRole, "lineHasControls"}};
}

void ExtensionViewModel::setExtensions(WorkspaceExtensions *extensions)
{
    if (m_extensions == extensions)
        return;
    if (m_extensions)
        disconnect(m_extensions, nullptr, this, nullptr);
    m_extensions = extensions;
    if (m_extensions) {
        connect(m_extensions, &WorkspaceExtensions::changed, this, &ExtensionViewModel::scheduleRefresh);
        connect(m_extensions, &WorkspaceExtensions::questionChanged, this, &ExtensionViewModel::scheduleRefresh);
    }
    Q_EMIT extensionsChanged();
    scheduleRefresh();
}

void ExtensionViewModel::setExtension(const QString &extension)
{
    if (m_extension == extension)
        return;
    m_extension = extension;
    Q_EMIT extensionChanged();
    scheduleRefresh();
}

void ExtensionViewModel::setView(const QString &view)
{
    if (m_view == view)
        return;
    m_view = view;
    Q_EMIT viewChanged();
    scheduleRefresh();
}

void ExtensionViewModel::setParams(const QVariantMap &params)
{
    if (m_params == params)
        return;
    m_params = params;
    Q_EMIT paramsChanged();
    scheduleRefresh();
}

void ExtensionViewModel::scheduleRefresh() { m_refreshTimer.start(); }

void ExtensionViewModel::setParamKey(const QString &key)
{
    if (m_paramKey == key)
        return;
    m_paramKey = key;
    Q_EMIT paramsChanged();
}

void ExtensionViewModel::setParamValue(const QString &value)
{
    if (!m_paramKey.isEmpty())
        setParam(m_paramKey, value);
}

void ExtensionViewModel::setParam(const QString &key, const QString &value)
{
    QVariantMap params = m_params;
    if (value.isEmpty())
        params.remove(key);
    else
        params.insert(key, value);
    setParams(params);
}

void ExtensionViewModel::refresh()
{
    m_refreshTimer.stop();
    if (m_extensions && m_extensions->isWaiting())
        return; // after the question (questionChanged)
    QString error;
    const QVariantMap root = m_extensions && !m_extension.isEmpty() && !m_view.isEmpty()
                                     ? m_extensions->render(m_extension, m_view, m_params, &error)
                                     : QVariantMap();
    beginResetModel();
    for (const Line &l : std::as_const(m_lines))
        if (l.controls)
            l.controls->deleteLater();
    m_lines.clear();
    flatten(root, 0);
    endResetModel();
    m_error = error;
    ++m_revision;
    Q_EMIT renderedChanged();
}

void ExtensionViewModel::flatten(const QVariantMap &node, int depth)
{
    if (node.isEmpty() || depth > 32 || m_lines.size() >= kMaxLines)
        return;
    const QString type = node.value(u"type"_s).toString();
    if (type == u"column") {
        for (const QVariant &child : node.value(u"children"_s).toList())
            flatten(child.toMap(), depth + 1);
        return;
    }
    addLine(node);
}

QVariantMap ExtensionViewModel::chartDataOf(const QVariantMap &node)
{
    const QStringList labels = node.value(u"labels"_s).toStringList();
    QVariantList series;
    for (const QVariant &s : node.value(u"series"_s).toList()) {
        const QVariantMap map = s.toMap();
        QVariantList values;
        for (const QVariant &v : listOf(map.value(u"values"_s), int(labels.size())))
            values << (v.isValid() && !v.isNull() ? QVariant(v.toDouble()) : QVariant());
        while (values.size() < labels.size())
            values << QVariant();
        series << QVariantMap{{u"name"_s, map.value(u"name"_s).toString()}, {u"values"_s, values}};
    }
    return {{u"ok"_s, true}, {u"labels"_s, labels}, {u"labelsName"_s, QString()}, {u"series"_s, series}};
}

void ExtensionViewModel::addLine(const QVariantMap &node)
{
    const QString type = node.value(u"type"_s).toString();
    Line line;
    line.type = type;
    line.node = node;
    line.text = textOf(node);
    line.muted = node.value(u"muted"_s).toBool();
    line.page = node.value(u"page"_s).toString();
    if (type == u"empty") {
        line.title = node.value(u"title"_s).toString();
    } else if (type == u"row") {
        QList<QVariantMap> controls;
        for (const QVariant &c : node.value(u"children"_s).toList()) {
            const QVariantMap map = c.toMap();
            const QString inner = map.value(u"type"_s).toString();
            if (inner == u"row" || inner == u"column") {
                for (const QVariant &child : map.value(u"children"_s).toList())
                    if (controls.size() < kMaxControls)
                        controls << child.toMap();
            } else if (controls.size() < kMaxControls) {
                controls << map;
            }
        }
        line.controls = new ExtensionControlsModel(controls, m_extensions && m_extensions->language() == u"pl", this);
    } else if (type == u"chart") {
        line.chartData = chartDataOf(node);
        line.title = node.value(u"title"_s).toString();
        const QString kind = node.value(u"kind"_s).toString() == u"bar" ? u"bar"_s : u"line"_s;
        line.chartSource = u"type: "_s + kind + (line.title.isEmpty() ? QString() : u"\ntitle: "_s + line.title);
    } else if (type == u"table") {
        Line head = line;
        head.type = u"tableHead"_s;
        head.cells = node.value(u"columns"_s).toStringList();
        m_lines << head;
        for (const QVariant &row : node.value(u"rows"_s).toList()) {
            if (m_lines.size() >= kMaxLines)
                return;
            Line cells;
            cells.type = u"tableRow"_s;
            cells.cells = row.toStringList();
            m_lines << cells;
        }
        return;
    } else if (type != u"heading" && type != u"text" && type != u"link" && type != u"open" && type != u"run"
               && type != u"button") {
        return; // unknown nodes are left out
    }
    m_lines << line;
}

void ExtensionViewModel::activate(int row)
{
    if (row >= 0 && row < m_lines.size())
        run(m_lines[row].node, 0);
}

void ExtensionViewModel::activateControl(int row, int control, double value)
{
    if (row < 0 || row >= m_lines.size() || !m_lines[row].controls)
        return;
    run(m_lines[row].controls->control(control), value);
}

double ExtensionViewModel::stepped(int row, int control, int direction) const
{
    if (row < 0 || row >= m_lines.size() || !m_lines[row].controls)
        return 0;
    const QVariantMap c = m_lines[row].controls->control(control);
    double step = c.value(u"step"_s, 1).toDouble();
    if (!std::isfinite(step) || step <= 0)
        step = 1;
    const double base = std::round(c.value(u"value"_s).toDouble() / step) * step;
    return std::max(0.0, base + (direction < 0 ? -step : step));
}

void ExtensionViewModel::run(const QVariantMap &node, double value)
{
    const QString type = node.value(u"type"_s).toString();
    if (type == u"link") {
        Q_EMIT pageRequested(node.value(u"page"_s).toString());
    } else if (type == u"open") {
        const QString view = node.value(u"view"_s).toString();
        Q_EMIT viewRequested(view.isEmpty() ? m_view : view, node.value(u"params"_s).toMap());
    } else if (type == u"run") {
        if (m_extensions)
            m_extensions->runCommand(m_extension, node.value(u"command"_s).toString());
    } else if (type == u"button" || type == u"check" || type == u"stepper") {
        const QString action = node.value(u"action"_s).toString();
        if (!m_extensions || action.isEmpty())
            return;
        QVariantMap args = node.value(u"args"_s).toMap();
        if (type == u"stepper")
            args.insert(u"value"_s, value);
        m_extensions->act(m_extension, m_view, action, args);
        // the view renders again after changed()
    }
}

} // namespace brulion::shell
