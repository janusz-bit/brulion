#include "extensionviewmodel.h"

#include "workspaceextensions.h"

#include <algorithm>
#include <cmath>
#include <utility>

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

// ---- Numbers -------------------------------------------------------------------------------------

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

std::optional<double> parseExtensionNumber(const QString &text, const QString &unit)
{
    // Spaces go (a keyboard's "82, 5", a thousands' space), then the unit the field shows anyway.
    QString t;
    t.reserve(text.size());
    for (const QChar c : text) {
        if (!c.isSpace())
            t.append(c);
    }
    QString u;
    for (const QChar c : unit) {
        if (!c.isSpace())
            u.append(c);
    }
    if (!u.isEmpty() && t.size() > u.size() && t.endsWith(u, Qt::CaseInsensitive))
        t.chop(u.size());
    // Digits and one decimal separator, nothing else: no sign (a stepper never goes below 0), no
    // exponent, no "inf" — what toDouble() would take as well.
    int digits = 0;
    int points = 0;
    for (QChar &c : t) {
        if (c == u',')
            c = u'.';
        if (c == u'.')
            ++points;
        else if (c >= u'0' && c <= u'9')
            ++digits;
        else
            return std::nullopt;
    }
    if (digits == 0 || points > 1)
        return std::nullopt;
    if (t.startsWith(u'.'))
        t.prepend(u'0');
    if (t.endsWith(u'.'))
        t.chop(1);
    bool ok = false;
    const double value = t.toDouble(&ok);
    if (!ok || !std::isfinite(value))
        return std::nullopt;
    return value;
}

double stepExtensionNumber(double value, double step, int direction)
{
    if (!std::isfinite(step) || step <= 0)
        step = 1;
    if (!std::isfinite(value))
        value = 0;
    // The value's place on the step's grid; one on it — up to the rounding of binary fractions
    // (0,1 × 3) — is a whole number of steps, one between two grid values goes to the next of them.
    double steps = value / step;
    const double nearest = std::round(steps);
    if (std::abs(steps - nearest) < 1e-6)
        steps = nearest;
    const double next = direction < 0 ? (std::ceil(steps) - 1) * step : (std::floor(steps) + 1) * step;
    return std::max(0.0, next);
}

// ---- ExtensionControlsModel --------------------------------------------------------------------

ExtensionControlsModel::ExtensionControlsModel(QList<QVariantMap> controls, bool comma, QObject *parent)
    : QAbstractListModel(parent), m_controls(std::move(controls)), m_comma(comma)
{
}

int ExtensionControlsModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_controls.size()); }

QVariant ExtensionControlsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_controls.size())
        return {};
    return value(index.row(), role);
}

QVariant ExtensionControlsModel::value(int row, int role) const
{
    const QVariantMap &c = m_controls[row];
    switch (role) {
    case TypeRole: return c.value(u"type"_s).toString();
    case TextRole: return textOf(c);
    case PageRole: return c.value(u"page"_s).toString();
    case CheckedRole: return c.value(u"checked"_s).toBool();
    case ValueRole: return c.value(u"value"_s).toDouble();
    case StepRole: return c.value(u"step"_s, 1).toDouble();
    case UnitRole: return c.value(u"unit"_s).toString();
    case IndexRole: return row;
    case MutedRole: return c.value(u"muted"_s).toBool();
    case NumberRole: return formatExtensionNumber(c.value(u"value"_s).toDouble(), m_comma);
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
    return {{TypeRole, "controlType"},       {TextRole, "controlText"},   {PageRole, "controlPage"},
            {CheckedRole, "controlChecked"}, {ValueRole, "controlValue"}, {StepRole, "controlStep"},
            {UnitRole, "controlUnit"},       {IndexRole, "controlIndex"}, {MutedRole, "controlMuted"},
            {DisplayRole, "controlDisplay"}, {NumberRole, "controlNumber"}};
}

bool ExtensionControlsModel::sameKinds(const QList<QVariantMap> &controls) const
{
    if (controls.size() != m_controls.size())
        return false;
    for (qsizetype i = 0; i < controls.size(); ++i) {
        if (controls[i].value(u"type"_s).toString() != m_controls[i].value(u"type"_s).toString())
            return false;
    }
    return true;
}

void ExtensionControlsModel::update(QList<QVariantMap> controls, bool comma)
{
    if (!sameKinds(controls)) {
        beginResetModel();
        m_controls = std::move(controls);
        m_comma = comma;
        endResetModel();
        return;
    }
    const QList<int> roles = roleNames().keys();
    QList<QVariantList> before;
    before.reserve(m_controls.size());
    for (int i = 0; i < m_controls.size(); ++i) {
        QVariantList values;
        for (const int role : roles)
            values << value(i, role);
        before << values;
    }
    m_controls = std::move(controls);
    m_comma = comma;
    for (int i = 0; i < m_controls.size(); ++i) {
        QList<int> changed;
        for (qsizetype r = 0; r < roles.size(); ++r) {
            if (value(i, roles[r]) != before[i][r])
                changed << roles[r];
        }
        if (!changed.isEmpty())
            Q_EMIT dataChanged(index(i), index(i), changed);
    }
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

void ExtensionViewModel::setRouteParams(const QVariantMap &route)
{
    QVariantMap params = route;
    for (const QString &key : {u"view"_s, u"extension"_s, u"name"_s, u"title"_s})
        params.remove(key);
    setParams(params);
}

QVariantMap ExtensionViewModel::routeFor(const QString &view, const QVariantMap &params) const
{
    QVariantMap route = params;
    route.insert(u"view"_s, u"extension"_s);
    route.insert(u"extension"_s, m_extension);
    route.insert(u"name"_s, view);
    route.insert(u"title"_s, m_extensions ? m_extensions->viewTitle(m_extension, view) : view);
    return route;
}

void ExtensionViewModel::refresh() { render(true); }

void ExtensionViewModel::render(bool reshape)
{
    if (m_updating) {
        scheduleRefresh(); // asked while the lines change (a delegate's handler): once they are done
        return;
    }
    m_refreshTimer.stop();
    if (m_extensions && m_extensions->isWaiting())
        return; // after the question (questionChanged)
    QString error;
    const QVariantMap root = m_extensions && !m_extension.isEmpty() && !m_view.isEmpty()
                                     ? m_extensions->render(m_extension, m_view, m_params, &error)
                                     : QVariantMap();
    QList<Line> lines;
    flatten(root, 0, &lines);
    const bool comma = m_extensions && m_extensions->language() == u"pl";

    // The same view drawn again, its lines of the same kinds: changed in place. Another view (or
    // params: another exercise) starts afresh, at its top.
    bool inPlace = m_shown && m_shownExtension == m_extension && m_shownView == m_view && m_shownParams == m_params
                   && lines.size() == m_lines.size();
    for (qsizetype i = 0; inPlace && i < lines.size(); ++i) {
        inPlace = lines[i].type == m_lines[i].type
                  && (reshape || !m_lines[i].controls || m_lines[i].controls->sameKinds(lines[i].controlNodes));
    }
    if (!inPlace && !reshape) {
        scheduleRefresh();
        return;
    }

    m_updating = true;
    if (inPlace) {
        for (qsizetype i = 0; i < lines.size(); ++i) {
            Line &next = lines[i];
            const QList<int> roles = changedRoles(m_lines[i], next);
            next.controls = m_lines[i].controls;
            if (next.controls)
                next.controls->update(std::exchange(next.controlNodes, {}), comma); // a reset when its kinds changed
            m_lines[i] = std::move(next);
            if (!roles.isEmpty())
                Q_EMIT dataChanged(index(int(i)), index(int(i)), roles);
        }
    } else {
        beginResetModel();
        for (const Line &l : std::as_const(m_lines)) {
            if (l.controls)
                l.controls->deleteLater();
        }
        m_lines = std::move(lines);
        for (Line &l : m_lines) {
            if (l.type == u"row")
                l.controls = new ExtensionControlsModel(std::exchange(l.controlNodes, {}), comma, this);
        }
        m_shown = true;
        m_shownExtension = m_extension;
        m_shownView = m_view;
        m_shownParams = m_params;
        endResetModel();
    }
    m_updating = false;
    m_error = error;
    ++m_revision;
    Q_EMIT renderedChanged();
}

QList<int> ExtensionViewModel::changedRoles(const Line &before, const Line &after)
{
    QList<int> roles;
    if (before.text != after.text)
        roles << TextRole;
    if (before.title != after.title)
        roles << TitleRole;
    if (before.muted != after.muted)
        roles << MutedRole;
    if (before.page != after.page)
        roles << PageRole;
    if (before.cells != after.cells)
        roles << CellsRole;
    if (before.chartData != after.chartData)
        roles << ChartDataRole;
    if (before.chartSource != after.chartSource)
        roles << ChartSourceRole;
    return roles;
}

void ExtensionViewModel::flatten(const QVariantMap &node, int depth, QList<Line> *lines)
{
    if (node.isEmpty() || depth > 32 || lines->size() >= kMaxLines)
        return;
    const QString type = node.value(u"type"_s).toString();
    if (type == u"column") {
        for (const QVariant &child : node.value(u"children"_s).toList())
            flatten(child.toMap(), depth + 1, lines);
        return;
    }
    addLine(node, lines);
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

void ExtensionViewModel::addLine(const QVariantMap &node, QList<Line> *lines)
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
        for (const QVariant &c : node.value(u"children"_s).toList()) {
            const QVariantMap map = c.toMap();
            const QString inner = map.value(u"type"_s).toString();
            if (inner == u"row" || inner == u"column") {
                for (const QVariant &child : map.value(u"children"_s).toList())
                    if (line.controlNodes.size() < kMaxControls)
                        line.controlNodes << child.toMap();
            } else if (line.controlNodes.size() < kMaxControls) {
                line.controlNodes << map;
            }
        }
    } else if (type == u"chart") {
        line.chartData = chartDataOf(node);
        line.title = node.value(u"title"_s).toString();
        const QString kind = node.value(u"kind"_s).toString() == u"bar" ? u"bar"_s : u"line"_s;
        line.chartSource = u"type: "_s + kind + (line.title.isEmpty() ? QString() : u"\ntitle: "_s + line.title);
    } else if (type == u"table") {
        Line head = line;
        head.type = u"tableHead"_s;
        head.cells = node.value(u"columns"_s).toStringList();
        lines->append(head);
        for (const QVariant &row : node.value(u"rows"_s).toList()) {
            if (lines->size() >= kMaxLines)
                return;
            Line cells;
            cells.type = u"tableRow"_s;
            cells.cells = row.toStringList();
            lines->append(cells);
        }
        return;
    } else if (type != u"heading" && type != u"text" && type != u"link" && type != u"open" && type != u"run"
               && type != u"button") {
        return; // unknown nodes are left out
    }
    lines->append(line);
}

void ExtensionViewModel::activate(int row)
{
    if (!m_updating && row >= 0 && row < m_lines.size())
        run(m_lines[row].node, 0);
}

void ExtensionViewModel::activateControl(int row, int control, double value)
{
    if (m_updating || row < 0 || row >= m_lines.size() || !m_lines[row].controls)
        return;
    run(m_lines[row].controls->control(control), value);
}

double ExtensionViewModel::stepped(int row, int control, int direction) const
{
    if (row < 0 || row >= m_lines.size() || !m_lines[row].controls)
        return 0;
    const QVariantMap c = m_lines[row].controls->control(control);
    return stepExtensionNumber(c.value(u"value"_s).toDouble(), c.value(u"step"_s, 1).toDouble(), direction);
}

bool ExtensionViewModel::enterValue(int row, int control, const QString &text)
{
    if (m_updating || row < 0 || row >= m_lines.size() || !m_lines[row].controls)
        return false;
    const QVariantMap c = m_lines[row].controls->control(control);
    if (c.value(u"type"_s).toString() != u"stepper")
        return false;
    const std::optional<double> value = parseExtensionNumber(text, c.value(u"unit"_s).toString());
    // The number shown, typed again ("82,50" for 82,5; a field left as it was): nothing to write.
    if (!value || formatExtensionNumber(*value, false) == formatExtensionNumber(c.value(u"value"_s).toDouble(), false))
        return false;
    return run(c, *value);
}

bool ExtensionViewModel::run(const QVariantMap &node, double value)
{
    const QString type = node.value(u"type"_s).toString();
    if (type == u"link") {
        Q_EMIT pageRequested(node.value(u"page"_s).toString());
    } else if (type == u"open") {
        const QString view = node.value(u"view"_s).toString();
        Q_EMIT viewRequested(view.isEmpty() ? m_view : view, node.value(u"params"_s).toMap());
    } else if (type == u"run") {
        if (!m_extensions)
            return false;
        m_extensions->runCommand(m_extension, node.value(u"command"_s).toString());
    } else if (type == u"button" || type == u"check" || type == u"stepper") {
        const QString action = node.value(u"action"_s).toString();
        if (!m_extensions || action.isEmpty())
            return false;
        QVariantMap args = node.value(u"args"_s).toMap();
        if (type == u"stepper")
            args.insert(u"value"_s, value);
        m_extensions->act(m_extension, m_view, action, args);
        // Drawn again at once, in place: the next tap acts on the lines as they are now — a number
        // typed into a stepper is written when its field loses the focus to the button pressed
        // next ("+ set"), whose action then reads the line with it. Lines of other kinds wait for
        // the timer: a reset would take away the delegate whose handler called this.
        render(false);
    } else {
        return false;
    }
    return true;
}

} // namespace brulion::shell
