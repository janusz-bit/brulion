#include "extensionviewmodel.h"

#include "workspaceextensions.h"

#include "index/vaultindex.h"
#include "markdown/blockdocument.h"
#include "markdown/inlinerenderer.h"

#include <QColor>
#include <QDate>
#include <QRegularExpression>
#include <QTextDocumentFragment>

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
    if (type == u"button" || type == u"open" || type == u"run" || type == u"menu" || type == u"check")
        return node.value(u"label"_s).toString();
    return node.value(u"text"_s).toString();
}

/// A text node's or a button's tone: "success" | "danger" | "accent" | "warning" | "info", else "".
QString toneOf(const QVariantMap &node)
{
    const QString tone = node.value(u"tone"_s).toString();
    return tone == u"success" || tone == u"danger" || tone == u"accent" || tone == u"warning" || tone == u"info"
                   ? tone
                   : QString();
}

/// A keyboard shortcut as the view matches it: lower case, "" for none.
QString keyOf(const QVariantMap &node)
{
    return node.value(u"key"_s).toString().trimmed().toLower();
}

/// Renderer colours from QML (BlockEditor.rendererPalette's keys); missing keys keep the defaults.
markdown::Palette paletteOf(const QVariantMap &map)
{
    markdown::Palette p;
    const auto get = [&map](const QString &key, const QString &fallback) {
        const QVariant v = map.value(key);
        if (!v.isValid())
            return fallback;
        const QColor c = v.value<QColor>();
        return c.isValid() ? c.name(QColor::HexArgb) : v.toString();
    };
    p.link = get(u"link"_s, p.link);
    p.unresolved = get(u"unresolved"_s, p.unresolved);
    p.muted = get(u"muted"_s, p.muted);
    p.codeBackground = get(u"codeBackground"_s, p.codeBackground);
    p.highlightBackground = get(u"highlightBackground"_s, p.highlightBackground);
    const auto chip = [&get](const QString &name, markdown::ChipColors &c) {
        c.foreground = get(name + u"Fg"_s, c.foreground);
        c.background = get(name + u"Bg"_s, c.background);
    };
    chip(u"tag"_s, p.tag);
    chip(u"due"_s, p.due);
    chip(u"overdue"_s, p.overdue);
    chip(u"scheduled"_s, p.scheduled);
    chip(u"reminder"_s, p.reminder);
    chip(u"alarm"_s, p.alarm);
    chip(u"recurrence"_s, p.recurrence);
    chip(u"done"_s, p.done);
    chip(u"neutral"_s, p.neutral);
    return p;
}

/// Markdown as b.ui.markdown shows it: its blocks as rich text, one paragraph each — indented by
/// their depth, a list item after its bullet or number, a heading in bold, code and tables as
/// preformatted text (escaped: InlineRenderer's HTML, never the text's own).
QString markdownHtml(const QString &markdown, const markdown::RenderContext &context)
{
    const markdown::BlockDocument doc(markdown);
    const markdown::InlineRenderer renderer(context);
    static const QRegularExpression ordered(u"^\\s*(\\d{1,9}[.)])"_s);
    QStringList parts;
    for (int i = 0; i < doc.count(); ++i) {
        const markdown::BlockEntry &b = doc.at(i);
        const int indent = b.depth * 18;
        const int below = b.trailing.contains(u'\n') ? 10 : b.kind == BlockKind::ListItem ? 2 : 6;
        switch (b.kind) {
        case BlockKind::Code:
        case BlockKind::Table:
        case BlockKind::Html:
        case BlockKind::Frontmatter:
            parts.append(u"<pre style=\"margin-top:0px; margin-bottom:%1px; margin-left:%2px;\">%3</pre>"_s.arg(below).arg(indent).arg(
                    b.body.toHtmlEscaped()));
            continue;
        case BlockKind::Rule:
            parts.append(u"<hr/>"_s);
            continue;
        default:
            break;
        }
        QString html = renderer.renderBlock(b.body, b.kind);
        if (b.kind == BlockKind::Heading)
            html = u"<b>"_s + html + u"</b>"_s;
        if (b.kind == BlockKind::ListItem && !b.taskStatus) {
            const auto number = ordered.match(b.prefix);
            html = (number.hasMatch() ? number.captured(1) + u"&nbsp;"_s : u"•&nbsp;"_s) + html;
        }
        if (html.isEmpty())
            continue;
        parts.append(u"<p style=\"margin-top:0px; margin-bottom:%1px; margin-left:%2px;\">%3</p>"_s.arg(below).arg(indent).arg(html));
    }
    if (parts.isEmpty())
        return QString();
    static const QRegularExpression room(u"margin-bottom:\\d+px;"_s);
    parts.last().replace(room, u"margin-bottom:0px;"_s);
    return parts.join(QString());
}

/// A check's state: "done" | "open" | "skipped" (a node without one: from `checked`).
QString stateOf(const QVariantMap &node)
{
    const QString state = node.value(u"state"_s).toString();
    if (state == u"done" || state == u"open" || state == u"skipped")
        return state;
    return node.value(u"checked"_s).toBool() ? u"done"_s : u"open"_s;
}

/// The items of a menu node (`items`) or of a check's or a day's menu (`menu`) that can be chosen.
QVariantList itemsOf(const QVariant &value)
{
    // A list with a nil in it reaches the app as a table with numbered keys: those in order.
    QVariantList items = value.toList();
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        QList<int> keys;
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            bool ok = false;
            const int key = it.key().toInt(&ok);
            if (ok && key >= 1)
                keys << key;
        }
        std::sort(keys.begin(), keys.end());
        for (const int key : std::as_const(keys))
            items << map.value(QString::number(key));
    }
    QVariantList out;
    for (const QVariant &v : std::as_const(items)) {
        const QVariantMap item = v.toMap();
        if (item.value(u"label"_s).toString().isEmpty())
            continue;
        if (!item.value(u"action"_s).toString().isEmpty() || !item.value(u"page"_s).toString().isEmpty()
            || !item.value(u"view"_s).toString().isEmpty()
            || QDate::fromString(item.value(u"journal"_s).toString(), Qt::ISODate).isValid())
            out << item;
    }
    return out;
}

QVariantList menuOfNode(const QVariantMap &node)
{
    return itemsOf(node.value(node.value(u"type"_s).toString() == u"menu" ? u"items"_s : u"menu"_s));
}

/// A list of tables from the script (a sequence, or numbered keys of a table with holes).
QVariantList itemsOfList(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QVariantList)
        return value.toList();
    const QVariantMap map = value.toMap();
    QList<int> keys;
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        bool ok = false;
        const int key = it.key().toInt(&ok);
        if (ok && key >= 1)
            keys << key;
    }
    std::sort(keys.begin(), keys.end());
    QVariantList out;
    for (const int key : std::as_const(keys))
        out << map.value(QString::number(key));
    return out;
}

const QStringList kDayStates{u"outside"_s, u"future"_s, u"rest"_s, u"due"_s, u"done"_s, u"skipped"_s, u"missed"_s};

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
    case StateRole: return c.value(u"type"_s).toString() == u"check" ? stateOf(c) : QString();
    case LabelRole: return c.value(u"label"_s).toString();
    case IconRole: return c.value(u"icon"_s).toString();
    case ToneRole: return toneOf(c);
    case HasMenuRole: return !menuOfNode(c).isEmpty();
    case KeyRole: return keyOf(c);
    case PrimaryRole: return c.value(u"primary"_s).toBool();
    case HintRole: return c.value(u"hint"_s).toString();
    case StyledRole: return c.value(u"button"_s).toBool();
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
            {DisplayRole, "controlDisplay"}, {NumberRole, "controlNumber"}, {StateRole, "controlState"},
            {LabelRole, "controlLabel"},     {IconRole, "controlIcon"},   {ToneRole, "controlTone"},
            {HasMenuRole, "controlHasMenu"}, {KeyRole, "controlKey"},     {PrimaryRole, "controlPrimary"},
            {HintRole, "controlHint"},       {StyledRole, "controlStyled"}};
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
    case IconRole: return l.icon;
    case ToneRole: return l.tone;
    case CardRole: return l.card;
    case MonthRole: return l.month;
    case DaysRole: return l.days;
    case HtmlRole: return l.html;
    case SizeRole: return l.size;
    case AlignRole: return l.align;
    case ValueRole: return l.value;
    case MaxRole: return l.max;
    case ItemsRole: return l.items;
    case KeyRole: return l.key;
    case PrimaryRole: return l.primary;
    case HintRole: return l.hint;
    case StyledRole: return l.styled;
    }
    return {};
}

QHash<int, QByteArray> ExtensionViewModel::roleNames() const
{
    return {{TypeRole, "lineType"},          {TextRole, "lineText"},       {TitleRole, "lineTitle"},
            {MutedRole, "lineMuted"},        {PageRole, "linePage"},       {CellsRole, "lineCells"},
            {ChartDataRole, "lineChartData"}, {ChartSourceRole, "lineChartSource"}, {ControlsRole, "lineControls"},
            {HasControlsRole, "lineHasControls"}, {IconRole, "lineIcon"}, {ToneRole, "lineTone"},
            {CardRole, "lineCard"},          {MonthRole, "lineMonth"},     {DaysRole, "lineDays"},
            {HtmlRole, "lineHtml"},          {SizeRole, "lineSize"},       {AlignRole, "lineAlign"},
            {ValueRole, "lineValue"},        {MaxRole, "lineMax"},         {ItemsRole, "lineItems"},
            {KeyRole, "lineKey"},            {PrimaryRole, "linePrimary"}, {HintRole, "lineHint"},
            {StyledRole, "lineStyled"}};
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

void ExtensionViewModel::setPalette(const QVariantMap &palette)
{
    if (m_palette == palette)
        return;
    m_palette = palette;
    Q_EMIT paletteChanged();
    scheduleRefresh();
}

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

QVariantMap ExtensionViewModel::tabRoute(const QString &routeView, const QString &view, const QVariantMap &params) const
{
    QVariantMap route;
    for (auto it = params.cbegin(); it != params.cend(); ++it) {
        const QString value = it.value().toString();
        if (!value.isEmpty())
            route.insert(it.key(), value);
    }
    route.insert(u"view"_s, routeView);
    route.insert(u"tab"_s, view);
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
    // Markdown lines as rich text, their links resolved in the notebook.
    std::optional<markdown::RenderContext> context;
    for (Line &l : lines) {
        if (l.type != u"markdown")
            continue;
        if (!context) {
            context.emplace();
            const VaultIndex *index = m_extensions ? m_extensions->index() : nullptr;
            if (index) {
                context->resolvePage = [index](const QString &target) -> std::optional<QString> {
                    return index->resolveLink(target);
                };
                context->linkTitle = markdown::journalLinkTitle(index->config(),
                                                                comma ? dates::UiLocale::Polish : dates::UiLocale::English,
                                                                m_extensions->today());
            }
            context->today = m_extensions ? m_extensions->today() : QDate::currentDate();
            context->locale = comma ? dates::UiLocale::Polish : dates::UiLocale::English;
            context->palette = paletteOf(m_palette);
        }
        l.html = markdownHtml(l.text, *context);
        l.title = QTextDocumentFragment::fromHtml(l.html).toPlainText().simplified(); // for screen readers
    }

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
    if (before.icon != after.icon)
        roles << IconRole;
    if (before.tone != after.tone)
        roles << ToneRole;
    if (before.card != after.card)
        roles << CardRole;
    if (before.month != after.month)
        roles << MonthRole;
    if (before.days != after.days)
        roles << DaysRole;
    if (before.html != after.html)
        roles << HtmlRole;
    if (before.size != after.size)
        roles << SizeRole;
    if (before.align != after.align)
        roles << AlignRole;
    if (before.value != after.value)
        roles << ValueRole;
    if (before.max != after.max)
        roles << MaxRole;
    if (before.items != after.items)
        roles << ItemsRole;
    if (before.key != after.key)
        roles << KeyRole;
    if (before.primary != after.primary)
        roles << PrimaryRole;
    if (before.hint != after.hint)
        roles << HintRole;
    if (before.styled != after.styled)
        roles << StyledRole;
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
    if (type == u"card") {
        // Its lines stay lines of the list; each knows its part of the card (a card in a card
        // goes on as the outer one).
        const qsizetype start = lines->size();
        for (const QVariant &child : node.value(u"children"_s).toList())
            flatten(child.toMap(), depth + 1, lines);
        const qsizetype end = lines->size();
        for (qsizetype i = start; i < end; ++i) {
            (*lines)[i].card = end - start == 1 ? u"only"_s
                               : i == start     ? u"first"_s
                               : i == end - 1   ? u"last"_s
                                                : u"middle"_s;
        }
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
    line.icon = node.value(u"icon"_s).toString();
    line.tone = toneOf(node);
    line.key = keyOf(node);
    line.primary = node.value(u"primary"_s).toBool();
    line.hint = node.value(u"hint"_s).toString();
    line.styled = node.value(u"button"_s).toBool();
    if (type == u"empty") {
        line.title = node.value(u"title"_s).toString();
    } else if (type == u"month") {
        line.days = daysOf(node, &line.month);
        line.title = node.value(u"label"_s).toString();
        if (line.days.isEmpty())
            return; // no month to draw
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
    } else if (type == u"markdown") {
        const QString size = node.value(u"size"_s).toString();
        line.size = size == u"large" || size == u"small" ? size : QString();
        line.align = node.value(u"align"_s).toString() == u"center" ? u"center"_s : QString();
    } else if (type == u"progress") {
        const double max = node.value(u"max"_s).toDouble();
        line.max = std::isfinite(max) && max > 0 ? max : 1;
        const double value = node.value(u"value"_s).toDouble();
        line.value = std::isfinite(value) ? std::clamp(value, 0.0, line.max) : 0;
        line.text = node.value(u"label"_s).toString();
    } else if (type == u"stats") {
        for (const QVariant &v : itemsOfList(node.value(u"items"_s))) {
            const QVariantMap item = v.toMap();
            const QVariant value = item.value(u"value"_s);
            QString shown = value.toString();
            if (value.metaType().id() == QMetaType::Double || value.metaType().id() == QMetaType::Float)
                shown = formatExtensionNumber(value.toDouble(), false);
            if (item.value(u"label"_s).toString().isEmpty() && shown.isEmpty())
                continue;
            line.items << QVariantMap{{u"label"_s, item.value(u"label"_s).toString()},
                                      {u"value"_s, shown},
                                      {u"tone"_s, toneOf(item)}};
            if (line.items.size() >= 8)
                break;
        }
        if (line.items.isEmpty())
            return;
    } else if (type != u"heading" && type != u"text" && type != u"link" && type != u"open" && type != u"run"
               && type != u"button" && type != u"menu" && type != u"divider") {
        return; // unknown nodes are left out
    }
    lines->append(line);
}

QVariantList ExtensionViewModel::daysOf(const QVariantMap &node, QString *month)
{
    const QDate first = QDate::fromString(node.value(u"month"_s).toString() + u"-01"_s, Qt::ISODate);
    if (!first.isValid())
        return {};
    *month = first.toString(u"yyyy-MM"_s);
    const QVariantMap days = node.value(u"days"_s).toMap();
    QVariantList out;
    out.reserve(first.daysInMonth());
    for (int d = 1; d <= first.daysInMonth(); ++d) {
        const QString date = QDate(first.year(), first.month(), d).toString(Qt::ISODate);
        const QVariantMap day = days.value(date).toMap();
        const QString state = day.value(u"state"_s).toString();
        out << QVariantMap{{u"date"_s, date},
                           {u"day"_s, d},
                           {u"state"_s, kDayStates.contains(state) ? state : u"rest"_s},
                           {u"tappable"_s, !day.value(u"action"_s).toString().isEmpty()},
                           {u"menu"_s, !itemsOf(day.value(u"menu"_s)).isEmpty()}};
    }
    return out;
}

QVariantList ExtensionViewModel::menuItems(int row, int control, const QString &date) const
{
    if (row < 0 || row >= m_lines.size())
        return {};
    const Line &l = m_lines[row];
    if (control >= 0)
        return l.controls ? menuOfNode(l.controls->control(control)) : QVariantList();
    if (!date.isEmpty())
        return l.type == u"month" ? itemsOf(l.node.value(u"days"_s).toMap().value(date).toMap().value(u"menu"_s))
                                  : QVariantList();
    return l.type == u"menu" ? menuOfNode(l.node) : QVariantList();
}

QStringList ExtensionViewModel::menuLabels(int row, int control, const QString &date) const
{
    QStringList out;
    for (const QVariant &item : menuItems(row, control, date))
        out << item.toMap().value(u"label"_s).toString();
    return out;
}

QStringList ExtensionViewModel::menuIcons(int row, int control, const QString &date) const
{
    QStringList out;
    for (const QVariant &v : menuItems(row, control, date)) {
        const QVariantMap item = v.toMap();
        QString icon = item.value(u"icon"_s).toString();
        if (icon.isEmpty() && item.contains(u"journal"_s))
            icon = u"book-open"_s;
        else if (icon.isEmpty() && item.contains(u"page"_s) && item.value(u"action"_s).toString().isEmpty())
            icon = u"file-text"_s;
        out << icon;
    }
    return out;
}

void ExtensionViewModel::activateMenuItem(int row, int control, const QString &date, int item)
{
    if (m_updating)
        return;
    const QVariantMap chosen = menuItems(row, control, date).value(item).toMap();
    if (!chosen.value(u"action"_s).toString().isEmpty()) {
        runAction(chosen.value(u"action"_s).toString(), chosen.value(u"args"_s).toMap());
    } else if (!chosen.value(u"view"_s).toString().isEmpty()) {
        Q_EMIT viewRequested(chosen.value(u"view"_s).toString(), chosen.value(u"params"_s).toMap());
    } else if (!chosen.value(u"page"_s).toString().isEmpty()) {
        const int line = chosen.value(u"line"_s).toInt();
        Q_EMIT pageRequested(chosen.value(u"page"_s).toString(), line > 0 ? line - 1 : -1);
    } else if (chosen.contains(u"journal"_s)) {
        Q_EMIT journalRequested(chosen.value(u"journal"_s).toString());
    }
}

void ExtensionViewModel::activateDay(int row, const QString &date)
{
    if (m_updating || row < 0 || row >= m_lines.size() || m_lines[row].type != u"month")
        return;
    const QVariantMap day = m_lines[row].node.value(u"days"_s).toMap().value(date).toMap();
    if (!day.value(u"action"_s).toString().isEmpty())
        runAction(day.value(u"action"_s).toString(), day.value(u"args"_s).toMap());
}

bool ExtensionViewModel::activateKey(const QString &key)
{
    const QString wanted = key.trimmed().toLower();
    if (m_updating || wanted.isEmpty())
        return false;
    for (int row = 0; row < m_lines.size(); ++row) {
        const Line &l = m_lines[row];
        if (l.controls) {
            for (int c = 0; c < l.controls->rowCount(); ++c) {
                const QVariantMap control = l.controls->control(c);
                if (keyOf(control) == wanted)
                    return run(control, 0);
            }
        } else if (l.key == wanted) {
            return run(l.node, 0);
        }
    }
    return false;
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
        const int line = node.value(u"line"_s).toInt();
        Q_EMIT pageRequested(node.value(u"page"_s).toString(), line > 0 ? line - 1 : -1);
    } else if (type == u"open") {
        const QString view = node.value(u"view"_s).toString();
        Q_EMIT viewRequested(view.isEmpty() ? m_view : view, node.value(u"params"_s).toMap());
    } else if (type == u"run") {
        if (!m_extensions)
            return false;
        m_extensions->runCommand(m_extension, node.value(u"command"_s).toString());
    } else if (type == u"button" || type == u"check" || type == u"stepper") {
        QVariantMap args = node.value(u"args"_s).toMap();
        if (type == u"stepper")
            args.insert(u"value"_s, value);
        return runAction(node.value(u"action"_s).toString(), args);
    } else {
        return false;
    }
    return true;
}

bool ExtensionViewModel::runAction(const QString &action, const QVariantMap &args)
{
    if (!m_extensions || action.isEmpty())
        return false;
    m_extensions->act(m_extension, m_view, action, args);
    // Drawn again at once, in place: the next tap acts on the lines as they are now — a number
    // typed into a stepper is written when its field loses the focus to the button pressed
    // next ("+ set"), whose action then reads the line with it. Lines of other kinds wait for
    // the timer: a reset would take away the delegate whose handler called this.
    render(false);
    return true;
}

} // namespace brulion::shell
