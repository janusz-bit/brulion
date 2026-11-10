#include "uimodels.h"

#include "texthelper.h"

#include "text/text.h"

#include <QLocale>
#include <QMetaType>

#include <algorithm>
#include <climits>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

bool isNumber(const QVariant &v)
{
    switch (v.metaType().id()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Long:
    case QMetaType::ULong: return true;
    default: return false;
    }
}

bool isString(const QVariant &v)
{
    return v.metaType().id() == QMetaType::QString;
}

bool isNull(const QVariant &v)
{
    return v.metaType().id() == QMetaType::Nullptr;
}

bool isUndefined(const QVariant &v)
{
    return !v.isValid();
}

bool isJsValue(const QVariant &v)
{
    return v.metaType().name() == QByteArrayView("QJSValue");
}

bool isObject(const QVariant &v)
{
    return v.metaType().id() == QMetaType::QVariantMap || isJsValue(v);
}

/// UiModels::stringField() of an object already converted (objectOf(): once per row).
QString textOf(const QVariantMap &object, const QString &key)
{
    const QVariant v = object.value(key);
    return isUndefined(v) || isNull(v) ? QString() : jsString(v);
}

/// BBreadcrumb's { title, icon } (strings) of an object row.
QVariantMap crumbOf(const QVariant &row)
{
    const QVariantMap object = objectOf(row);
    return {{u"title"_s, textOf(object, u"title"_s)}, {u"icon"_s, textOf(object, u"icon"_s)}};
}

} // namespace

QVariantMap objectOf(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QVariantMap || (isJsValue(value) && value.canConvert<QVariantMap>()))
        return value.toMap();
    return {};
}

bool jsStrictEquals(const QVariant &a, const QVariant &b)
{
    if (isUndefined(a) || isUndefined(b))
        return isUndefined(a) && isUndefined(b);
    if (isNull(a) || isNull(b))
        return isNull(a) && isNull(b);
    if (isNumber(a) || isNumber(b))
        return isNumber(a) && isNumber(b) && a.toDouble() == b.toDouble();
    if (isString(a) || isString(b))
        return isString(a) && isString(b) && a.toString() == b.toString();
    if (a.metaType().id() == QMetaType::Bool || b.metaType().id() == QMetaType::Bool)
        return a.metaType() == b.metaType() && a.toBool() == b.toBool();
    return a == b;
}

QString jsString(const QVariant &value)
{
    if (isUndefined(value))
        return u"undefined"_s;
    if (isNull(value))
        return u"null"_s;
    if (value.metaType().id() == QMetaType::Bool)
        return value.toBool() ? u"true"_s : u"false"_s;
    if (isNumber(value))
        return QString::number(value.toDouble(), 'g', QLocale::FloatingPointShortest);
    return value.toString();
}

bool jsTruthy(const QVariant &value)
{
    if (isUndefined(value) || isNull(value))
        return false;
    if (value.metaType().id() == QMetaType::Bool)
        return value.toBool();
    if (isNumber(value)) {
        const double x = value.toDouble();
        return x != 0 && !std::isnan(x);
    }
    if (isString(value))
        return !value.toString().isEmpty();
    return true;
}

// ---- UiModels -------------------------------------------------------------------------------

UiModels::UiModels(QObject *parent) : QObject(parent) { }

bool UiModels::strictEquals(const QVariant &a, const QVariant &b) const
{
    return jsStrictEquals(a, b);
}

QString UiModels::stringField(const QVariant &object, const QString &key) const
{
    return textOf(objectOf(object), key);
}

bool UiModels::boolField(const QVariant &object, const QString &key) const
{
    return jsTruthy(objectOf(object).value(key));
}

double UiModels::numberField(const QVariant &object, const QString &key, double fallback) const
{
    const QVariant v = objectOf(object).value(key);
    return isNumber(v) ? v.toDouble() : fallback;
}

QStringList UiModels::stringListField(const QVariant &object, const QString &key) const
{
    const QVariant v = objectOf(object).value(key);
    if (v.metaType().id() == QMetaType::QStringList)
        return v.toStringList();
    if (v.metaType().id() != QMetaType::QVariantList && !(isJsValue(v) && v.canConvert<QVariantList>()))
        return {};
    const QVariantList items = v.toList();
    QStringList out;
    out.reserve(items.size());
    for (const QVariant &item : items)
        out.append(jsString(item));
    return out;
}

QVariant UiModels::field(const QVariant &object, const QString &key) const
{
    return objectOf(object).value(key);
}

QVariant UiModels::fieldAt(const QVariantList &list, int index, const QString &key) const
{
    if (index < 0 || index >= list.size())
        return {};
    return objectOf(list.at(index)).value(key);
}

QVariantList UiModels::withDefaults(const QVariantList &rows, const QVariantMap &defaults) const
{
    QVariantList out;
    out.reserve(rows.size());
    for (const QVariant &r : rows) {
        QVariantMap row = objectOf(r);
        for (auto [key, value] : defaults.asKeyValueRange()) {
            const auto found = row.constFind(key);
            if (found == row.cend() || isUndefined(*found) || isNull(*found))
                row.insert(key, value);
        }
        out.append(row);
    }
    return out;
}

QVariantList UiModels::segmentRows(const QVariantList &model) const
{
    QVariantList out;
    out.reserve(model.size());
    for (const QVariant &m : model) {
        if (isObject(m)) {
            const QVariantMap o = objectOf(m);
            out.append(QVariantMap{{u"value"_s, o.value(u"value"_s)},
                                   {u"text"_s, textOf(o, u"text"_s)},
                                   {u"iconName"_s, textOf(o, u"icon"_s)}});
        } else {
            out.append(QVariantMap{{u"value"_s, m}, {u"text"_s, jsString(m)}, {u"iconName"_s, QString()}});
        }
    }
    return out;
}

int UiModels::indexOfValue(const QVariantList &rows, const QVariant &value) const
{
    const auto it = std::ranges::find_if(
            rows, [&value](const QVariant &row) { return jsStrictEquals(objectOf(row).value(u"value"_s), value); });
    return it == rows.cend() ? -1 : int(it - rows.cbegin());
}

QVariantList UiModels::crumbRows(const QVariantList &model) const
{
    QVariantList out;
    out.reserve(model.size());
    for (const QVariant &m : model)
        out.append(isObject(m) ? crumbOf(m) : QVariantMap{{u"title"_s, jsString(m)}, {u"icon"_s, QString()}});
    return out;
}

QVariantList UiModels::shownCrumbs(const QVariantList &rows, int visibleTail, bool collapsed) const
{
    const auto part = [](const QVariant &row, int index) {
        QVariantMap shown = crumbOf(row);
        shown.insert(u"partIndex"_s, index);
        return shown;
    };
    QVariantList out;
    const int n = int(rows.size());
    if (!collapsed || n == 0) {
        for (int i = 0; i < n; ++i)
            out.append(part(rows.at(i), i));
        return out;
    }
    out.append(part(rows.at(0), 0));
    out.append(QVariantMap{{u"title"_s, u"…"_s}, {u"icon"_s, QString()}, {u"partIndex"_s, -1}});
    for (int i = std::max(1, n - visibleTail); i < n; ++i)
        out.append(part(rows.at(i), i));
    return out;
}

QVariantList UiModels::enabledRows(const QVariantList &rows) const
{
    QVariantList out;
    out.reserve(rows.size());
    for (const QVariant &r : rows) {
        const QVariant enabled = objectOf(r).value(u"enabled"_s);
        // JavaScript's `c.enabled !== false`.
        if (!(enabled.metaType().id() == QMetaType::Bool && !enabled.toBool()))
            out.append(r);
    }
    return out;
}

QVariantList UiModels::rowsByKey(const QVariantList &rows, const QString &key, const QStringList &values) const
{
    QVariantList out;
    for (const QString &value : values) {
        const auto it = std::ranges::find_if(rows, [&](const QVariant &row) { return stringField(row, key) == value; });
        if (it != rows.cend())
            out.append(*it);
    }
    return out;
}

QStringList UiModels::stringFields(const QVariantList &rows, const QString &key) const
{
    QStringList out;
    out.reserve(rows.size());
    for (const QVariant &r : rows)
        out.append(stringField(r, key));
    return out;
}

// ---- PaletteFilter --------------------------------------------------------------------------

PaletteFilter::PaletteFilter(QObject *parent) : QObject(parent) { }

void PaletteFilter::setItems(const QVariantList &items)
{
    m_items = items;
    m_titles.clear();
    m_subtitles.clear();
    m_titles.reserve(m_items.size());
    m_subtitles.reserve(m_items.size());
    for (const QVariant &item : std::as_const(m_items)) {
        const QVariantMap object = objectOf(item);
        m_titles.append(textOf(object, u"title"_s));
        m_subtitles.append(textOf(object, u"subtitle"_s));
    }
    Q_EMIT itemsChanged();
    update();
}

void PaletteFilter::setExtras(const QVariantList &extras)
{
    m_extras = extras;
    Q_EMIT extrasChanged();
    update();
}

void PaletteFilter::update()
{
    const auto row = [this](const QVariant &item, const QString &markup) {
        const QVariantMap object = objectOf(item);
        const QString icon = textOf(object, u"icon"_s);
        return QVariantMap{
                {u"text"_s, textOf(object, u"title"_s)},        {u"titleMarkup"_s, markup},
                {u"subtitle"_s, textOf(object, u"subtitle"_s)}, {u"iconName"_s, icon.isEmpty() ? m_defaultIcon : icon},
                {u"iconEmoji"_s, textOf(object, u"emoji"_s)},   {u"shortcut"_s, textOf(object, u"shortcut"_s)}};
    };
    m_rows.clear();
    m_sources.clear();
    const QList<FuzzyRanked> ranked = fuzzyRankList(m_query, m_titles, m_maxResults, m_subtitles);
    for (const FuzzyRanked &r : ranked) {
        const QString markup =
                r.positions.isEmpty() ? QString() : markMatchesHtml(m_titles.at(r.index), r.positions, m_markColor);
        m_rows.append(row(m_items.at(r.index), markup));
        m_sources.append(r.index);
    }
    for (int i = 0; i < m_extras.size(); ++i) {
        m_rows.append(row(m_extras.at(i), QString()));
        m_sources.append(-1 - i);
    }
    Q_EMIT rowsChanged();
}

int PaletteFilter::sourceIndex(int row) const
{
    return row >= 0 && row < m_sources.size() ? m_sources.at(row) : INT_MIN;
}

QVariant PaletteFilter::sourceItem(int row) const
{
    const int source = sourceIndex(row);
    if (source == INT_MIN)
        return {};
    return source >= 0 ? m_items.at(source) : m_extras.at(-1 - source);
}

QString PaletteFilter::itemString(int row, const QString &key) const
{
    return textOf(objectOf(sourceItem(row)), key);
}

bool PaletteFilter::itemBool(int row, const QString &key) const
{
    return jsTruthy(objectOf(sourceItem(row)).value(key));
}

bool PaletteFilter::isExtra(int row) const
{
    const int source = sourceIndex(row);
    return source != INT_MIN && source < 0;
}

bool PaletteFilter::hasTitle(const QString &title) const
{
    const QString folded = text::foldKey(title);
    return std::ranges::any_of(m_titles, [&folded](const QString &t) { return text::foldKey(t) == folded; });
}

// ---- ToastListModel -------------------------------------------------------------------------

ToastListModel::ToastListModel(QObject *parent) : QAbstractListModel(parent) { }

int ToastListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant ToastListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_toasts.size())
        return {};
    const Toast &t = m_toasts.at(index.row());
    switch (role) {
    case ToastIdRole: return t.id;
    case TextRole: return t.text;
    case KindRole: return t.kind;
    case ActionTextRole: return t.actionText;
    case TimeoutRole: return t.timeout;
    default: return {};
    }
}

QHash<int, QByteArray> ToastListModel::roleNames() const
{
    static const QHash<int, QByteArray> names{{ToastIdRole, "toastId"},
                                              {TextRole, "text"},
                                              {KindRole, "kind"},
                                              {ActionTextRole, "actionText"},
                                              {TimeoutRole, "timeout"}};
    return names;
}

void ToastListModel::prepend(int toastId, const QString &text, const QString &kind, const QString &actionText,
                             int timeout)
{
    beginInsertRows({}, 0, 0);
    m_toasts.prepend({toastId, text, kind, actionText, timeout});
    endInsertRows();
    Q_EMIT countChanged();
}

void ToastListModel::removeAt(int row)
{
    if (row < 0 || row >= m_toasts.size())
        return;
    beginRemoveRows({}, row, row);
    m_toasts.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
}

int ToastListModel::indexOf(int toastId) const
{
    const auto it = std::ranges::find(m_toasts, toastId, &Toast::id);
    return it == m_toasts.cend() ? -1 : int(it - m_toasts.cbegin());
}

int ToastListModel::idAt(int row) const
{
    return row >= 0 && row < m_toasts.size() ? m_toasts.at(row).id : 0;
}

} // namespace brulion::ui
