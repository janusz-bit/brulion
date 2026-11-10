// UiModels and PaletteFilter — the data shaping of the design system's components (docs/UI.md
// "Compiled QML"): their public APIs take JavaScript arrays of objects (or plain strings) from
// their callers, and compiled QML can read no member of such an object. These turn them into rows
// that carry every key a delegate requires (`required property string title`), and read single
// fields as typed values.
//
// UiModels — QML singleton `UiModels` (qmlforeign.h): pure functions.
// ToastListModel — QML type `ToastListModel`: BToastHost's toasts (ListModel's insert() and remove()
// are not compiled).
// PaletteFilter — QML type `PaletteFilter`: BPalette's fuzzy filter (TextHelper's matcher) over its
// items, with the results as rows; the titles are extracted once per change of the items, not on
// every keystroke (a notebook can have thousands of pages).
//
// GUI-free (QtCore; colours are "#rrggbb" strings), library brulion_ui_helpers, unit-tested in
// tests/app (tst_uihelpers). Objects from QML arrive as QVariantMap (QJSValue objects convert).
#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace brulion::ui {

/// `value` as a map: a QVariantMap, or a JavaScript object (QJSValue) converted; {} otherwise.
QVariantMap objectOf(const QVariant &value);
/// JavaScript's `a === b` for values from QML: undefined, null, booleans, numbers (any numeric
/// type: 1 === 1.0), strings; other values compare with QVariant's ==.
bool jsStrictEquals(const QVariant &a, const QVariant &b);
/// JavaScript's String(value) for the values above ("undefined", "null", "true", "1.5"…).
QString jsString(const QVariant &value);
/// JavaScript's !!value.
bool jsTruthy(const QVariant &value);

class UiModels : public QObject {
    Q_OBJECT

public:
    explicit UiModels(QObject *parent = nullptr);

    /// JavaScript's `a === b` (jsStrictEquals()).
    Q_INVOKABLE bool strictEquals(const QVariant &a, const QVariant &b) const;
    /// `object[key]` as text: String(value) of a present, defined, non-null key, else "" (a
    /// non-object `object`: "").
    Q_INVOKABLE QString stringField(const QVariant &object, const QString &key) const;
    /// `!!object[key]`.
    Q_INVOKABLE bool boolField(const QVariant &object, const QString &key) const;
    /// `object[key]` as a number; `fallback` when it is missing or not a number.
    Q_INVOKABLE double numberField(const QVariant &object, const QString &key, double fallback) const;
    /// `object[key]` as a list of strings (each String(v)); [] when it is missing or not a list.
    Q_INVOKABLE QStringList stringListField(const QVariant &object, const QString &key) const;
    /// `object[key]` as it is (undefined when missing).
    Q_INVOKABLE QVariant field(const QVariant &object, const QString &key) const;
    /// `list[index][key]` as it is (undefined when out of range or missing).
    Q_INVOKABLE QVariant fieldAt(const QVariantList &list, int index, const QString &key) const;

    /// Every row of `rows` with the keys of `defaults` it lacks or holds as undefined or null (the
    /// others keep their values) — rows that differ in their keys for delegates that
    /// require them all (`required property string title` would read a missing one as "undefined").
    Q_INVOKABLE QVariantList withDefaults(const QVariantList &rows, const QVariantMap &defaults) const;

    /// BSegmented's model: rows { value, text, iconName } from { value, text, icon }. An object row
    /// keeps its value as it is (undefined when missing), its text and icon as strings ("" when
    /// missing); any other row (a plain string, a number) is { value: it, text: String(it),
    /// iconName: "" }.
    Q_INVOKABLE QVariantList segmentRows(const QVariantList &model) const;
    /// The first row whose `value` is `value` (===); -1 when none.
    Q_INVOKABLE int indexOfValue(const QVariantList &rows, const QVariant &value) const;
    /// BBreadcrumb's model: rows { title, icon } (strings) from objects { title, icon? } or plain
    /// values (String(value)).
    Q_INVOKABLE QVariantList crumbRows(const QVariantList &model) const;
    /// The rows of `rows` that are not disabled (`enabled: false` hides one; a missing `enabled`
    /// keeps it), as they are — BCommandPalette's commands.
    Q_INVOKABLE QVariantList enabledRows(const QVariantList &rows) const;
    /// The parts BBreadcrumb shows of crumbRows() `rows`: { title, icon, partIndex } — all of them, or
    /// when `collapsed` the first, a "…" part (partIndex -1) and the last `visibleTail` ones.
    Q_INVOKABLE QVariantList shownCrumbs(const QVariantList &rows, int visibleTail, bool collapsed) const;
    /// The rows of `rows` whose `key` (as text) is one of `values`, in the order of `values` (a value
    /// no row has is left out) — e.g. the segments of the modes a platform offers.
    Q_INVOKABLE QVariantList rowsByKey(const QVariantList &rows, const QString &key, const QStringList &values) const;
    /// The `key` strings of `rows` (BBreadcrumb: the titles it measures).
    Q_INVOKABLE QStringList stringFields(const QVariantList &rows, const QString &key) const;
};

/// BPalette's results: the items whose title (or subtitle, at half weight) matches `query` fuzzily,
/// best first (all of them in order for an empty query), at most `maxResults`, then `extras`.
class PaletteFilter : public QObject {
    Q_OBJECT
    /// { title, subtitle?, icon?, emoji?, shortcut?, … } (other keys: itemString(), itemBool()).
    Q_PROPERTY(QVariantList items READ items WRITE setItems NOTIFY itemsChanged FINAL)
    /// Rows after the matches, in their order, whatever the query (e.g. "Create page …").
    Q_PROPERTY(QVariantList extras READ extras WRITE setExtras NOTIFY extrasChanged FINAL)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged FINAL)
    Q_PROPERTY(int maxResults READ maxResults WRITE setMaxResults NOTIFY maxResultsChanged FINAL)
    /// The colour of matched characters in `markup` ("" : bold only).
    Q_PROPERTY(QString markColor READ markColor WRITE setMarkColor NOTIFY markColorChanged FINAL)
    /// The icon of rows whose item has none.
    Q_PROPERTY(QString defaultIcon READ defaultIcon WRITE setDefaultIcon NOTIFY defaultIconChanged FINAL)
    /// { text, titleMarkup, subtitle, iconName, iconEmoji, shortcut } — strings, every key in every
    /// row (BListItem's property names): text = the item's title, titleMarkup = the title as rich
    /// text with the matched characters marked ("" when nothing is marked), iconName = its icon or
    /// defaultIcon, iconEmoji = its emoji.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY rowsChanged FINAL)

public:
    explicit PaletteFilter(QObject *parent = nullptr);

    QVariantList items() const { return m_items; }
    void setItems(const QVariantList &items);
    QVariantList extras() const { return m_extras; }
    void setExtras(const QVariantList &extras);
    QString query() const { return m_query; }
    void setQuery(const QString &query) { setInput(m_query, query, &PaletteFilter::queryChanged); }
    int maxResults() const { return m_maxResults; }
    void setMaxResults(int maxResults) { setInput(m_maxResults, maxResults, &PaletteFilter::maxResultsChanged); }
    QString markColor() const { return m_markColor; }
    void setMarkColor(const QString &color) { setInput(m_markColor, color, &PaletteFilter::markColorChanged); }
    QString defaultIcon() const { return m_defaultIcon; }
    void setDefaultIcon(const QString &icon) { setInput(m_defaultIcon, icon, &PaletteFilter::defaultIconChanged); }

    QVariantList rows() const { return m_rows; }
    int count() const { return int(m_rows.size()); }

    /// Where row `row` comes from: its index in `items` (>= 0), or -1 - its index in `extras`;
    /// INT_MIN out of range. (QML takes the object itself from its own list: functions in it stay
    /// functions.)
    Q_INVOKABLE int sourceIndex(int row) const;
    /// stringField() of the item (or extra) of row `row` ("" out of range).
    Q_INVOKABLE QString itemString(int row, const QString &key) const;
    /// boolField() of the item (or extra) of row `row` (false out of range).
    Q_INVOKABLE bool itemBool(int row, const QString &key) const;
    /// Row `row` shows one of `extras`.
    Q_INVOKABLE bool isExtra(int row) const;
    /// Some item's title is `title`, folded (case and diacritics ignored: TextHelper.fold).
    Q_INVOKABLE bool hasTitle(const QString &title) const;

Q_SIGNALS:
    void itemsChanged();
    void extrasChanged();
    void queryChanged();
    void maxResultsChanged();
    void markColorChanged();
    void defaultIconChanged();
    /// The rows were computed again (any input changed).
    void rowsChanged();

private:
    /// `input` = `value`: a change emits `changed`, then the rows follow (update()).
    template<typename T>
    void setInput(T &input, const T &value, void (PaletteFilter::*changed)())
    {
        if (input == value)
            return;
        input = value;
        Q_EMIT(this->*changed)();
        update();
    }
    void update();
    /// The item (or extra) row `row` shows; invalid out of range.
    QVariant sourceItem(int row) const;

    QVariantList m_items;
    QVariantList m_extras;
    QStringList m_titles;
    QStringList m_subtitles;
    QString m_query;
    int m_maxResults = 60;
    QString m_markColor;
    QString m_defaultIcon;
    QVariantList m_rows;
    /// Per row: the index in m_items, or -1 - (index in m_extras).
    QList<int> m_sources;
};

/// BToastHost's toasts, newest first: roles toastId, text, kind, actionText, timeout.
class ToastListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role { ToastIdRole = Qt::UserRole + 1, TextRole, KindRole, ActionTextRole, TimeoutRole };

    explicit ToastListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_toasts.size()); }

    /// A toast at the top (row 0).
    Q_INVOKABLE void prepend(int toastId, const QString &text, const QString &kind, const QString &actionText,
                             int timeout);
    /// Removes row `row` (nothing out of range).
    Q_INVOKABLE void removeAt(int row);
    /// The row of toast `toastId`; -1 when it is not there.
    Q_INVOKABLE int indexOf(int toastId) const;
    /// The id of row `row`; 0 out of range.
    Q_INVOKABLE int idAt(int row) const;

Q_SIGNALS:
    void countChanged();

private:
    struct Toast {
        int id = 0;
        QString text;
        QString kind;
        QString actionText;
        int timeout = 0;
    };
    QList<Toast> m_toasts;
};

} // namespace brulion::ui
