// Helpers of the editor's block views (TableBlockView, CalcBlockView, QueryBlockView,
// TransclusionView, EmbedView, the properties panel, EditorDialogs) that let their QML be
// compiled to C++ by qmlcachegen (docs/UI.md "Compiled QML"): the shaping of the model's
// QVariantMap / QVariantList roles that the QML could only do with JavaScript objects and arrays.
//
//   BlockViews.tableColumnWidths(table, headerFont, cellFont, width, padding)   (singleton)
//   ChipFlow { keepWith: addButton }    places its children as a Flow, at once (no polish)
//   KeyListModel { keys: […] }          rows by key, kept while the key stays (moves, not resets)
//
// Registered as QML types by editorforeign.h (BlockViews, ChipFlow, KeyListModel); maps are read through
// MapHolder (src/app/ui/mapholder.h).
#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QFont>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace brulion::editor {

/// Pure helpers of the block views (singleton `BlockViews`): the roles' maps and lists shaped
/// for typed QML. Every function takes what the view has and returns plain values.
class BlockViews : public QObject {
    Q_OBJECT

public:
    explicit BlockViews(QObject *parent = nullptr);

    // --- Tables (TableBlockView: BlockListModel's `tableData`) ----------------------------------
    /// The rows shown, in order: those passing the filters (`shown`), and row `editRow`.
    Q_INVOKABLE QList<int> tableShownRows(const QVariantMap &table, int editRow) const;
    /// The shown row after (`step` 1) or before (-1) row `row` (-1 the header): the row count
    /// after the last, -1 before the first.
    Q_INVOKABLE int tableNextShownRow(const QVariantMap &table, int row, int step) const;
    /// Natural column widths (header in `headerFont`, cells in `cellFont`, plus `padding` on both
    /// sides, 56…280 px), stretched to fill `width` when they are narrower together.
    Q_INVOKABLE QList<qreal> tableColumnWidths(const QVariantMap &table, const QFont &headerFont, const QFont &cellFont,
                                               qreal width, int padding) const;
    /// Cell (`row`, `column`): { text, display, html, plain, error, formula }; row -1 the header
    /// (display and text: the header's text, html its rendering); outside the table empty values.
    Q_INVOKABLE QVariantMap tableCell(const QVariantMap &table, int row, int column) const;

    // --- Text ---------------------------------------------------------------------------------
    /// The widest of `texts` in `font` (FontMetrics.advanceWidth()); 0 for none.
    Q_INVOKABLE qreal widestText(const QFont &font, const QStringList &texts) const;
    /// The family `font` is drawn in (QFontInfo: "serif" resolved, e.g. "Noto Serif"; Text.fontInfo).
    Q_INVOKABLE QString resolvedFamily(const QFont &font) const;
    /// `color` as text, as QML's String(color) gives it ("#rrggbb", "#aarrggbb" when translucent).
    Q_INVOKABLE QString colorName(const QColor &color) const;
    /// An empty string list (a value for "no items").
    Q_INVOKABLE QStringList noItems() const { return {}; }

    // --- Calculations (CalcBlockView: `calcResults`) ----------------------------------------------
    /// What each result shows: its error, else its value.
    Q_INVOKABLE QStringList calcResultTexts(const QVariantList &results) const;
    /// The offset of line `line` (0-based) of `text`; 0 past the last line.
    Q_INVOKABLE int lineStart(const QString &text, int line) const;

    // --- Page properties (FrontmatterView, PropertiesPanel, PropertyRow, PropertyListValue) ------
    /// The entry of `key` in a `properties` role; empty when there is none.
    Q_INVOKABLE QVariantMap propertyOf(const QVariantList &properties, const QString &key) const;
    /// String(entry[field]) of each entry ("" for a missing field).
    Q_INVOKABLE QStringList fieldTexts(const QVariantList &entries, const QString &field) const;
    /// "source:property" of the page header's tags and aliases (PageController.pageProperties).
    Q_INVOKABLE QStringList headerListKeys(const QVariantMap &pageProperties) const;
    /// A property's value as text, as JavaScript's String(value) gives it for the role's types
    /// (a string, a number, a bool, a list joined with ","); "" when it has none.
    Q_INVOKABLE QString valueText(const QVariant &value) const;
    /// valueText() of `map[key]` ("" when the map has no such key).
    Q_INVOKABLE QString textAt(const QVariantMap &map, const QString &key) const;
    /// What is typed in a list's field, split on commas outside [[…]] and quotes; trimmed, no
    /// empty items.
    Q_INVOKABLE QStringList splitListItems(const QString &text) const;
    /// The target of a `[[Page]]` (or `[[Page|label]]`) item; "" for any other item.
    Q_INVOKABLE QString listLinkTarget(const QString &item) const;
    /// PageController.suggestPropertyKeys() as BSuggestField rows { title, subtitle, icon,
    /// value }: the key, how many pages use it (`withCount`; "%n page(s)"), its type's icon
    /// (`unsupportedIcon`: "code" for an unsupported type, else "type" as for a text).
    Q_INVOKABLE QVariantList propertyKeyRows(const QVariantList &suggestions, bool withCount,
                                             bool unsupportedIcon) const;
    /// PageController.suggestPropertyValues() as BSuggestField rows: a page with its icon and
    /// "page" (in the translation context `context`); `forList` (PropertyListValue): tags as
    /// "#tag", how many pages use a value, values already in `held` (any case) left out.
    Q_INVOKABLE QVariantList propertyValueRows(const QVariantList &suggestions, bool forList, const QStringList &held,
                                               const QString &context) const;

    // --- Drawings on a picture or PDF (EditorDialogs: DrawingBackdrops' prepared items) ---------
    /// The items without an error (`error` empty).
    Q_INVOKABLE QVariantList preparedItems(const QVariantList &items) const;
    /// The error code of the first item that has one ("" when none has).
    Q_INVOKABLE QString firstItemError(const QVariantList &items) const;

    // --- Embeds (TransclusionView: `transclusions`) ------------------------------------------------
    /// The line of the embedded task of `path` showing `html` in `transclusions` (the nearest to
    /// `line` when the text repeats); -1 when it is not there.
    Q_INVOKABLE int transclusionTaskLine(const QVariantList &transclusions, const QString &path, const QString &html,
                                         int line) const;
};

/// Places its visible children (those with a width) left to right, a new line when one does not
/// fit, as a Flow — at once whenever a child's size or visibility or the width changes (a Flow's
/// places come with its next polish: docs/UI.md "Scrolling performance"). `keepWith` (the "+"
/// after chips) never starts a line alone: the item before it goes down with it when both fit.
class ChipFlow : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(qreal spacing READ spacing WRITE setSpacing NOTIFY spacingChanged FINAL)
    Q_PROPERTY(QQuickItem *keepWith READ keepWith WRITE setKeepWith NOTIFY keepWithChanged FINAL)
    /// The tallest item of a line.
    Q_PROPERTY(qreal lineHeight READ lineHeight NOTIFY layoutChanged FINAL)
    /// The height of all lines (also the implicit height).
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY layoutChanged FINAL)

public:
    explicit ChipFlow(QQuickItem *parent = nullptr);

    qreal spacing() const { return m_spacing; }
    void setSpacing(qreal spacing);
    QQuickItem *keepWith() const { return m_keepWith; }
    void setKeepWith(QQuickItem *item);
    qreal lineHeight() const { return m_lineHeight; }
    qreal contentHeight() const { return m_contentHeight; }

    /// Where the items would go in `width`: { items: [{ item, x, y }], height, lineHeight }.
    Q_INVOKABLE QVariantMap layoutFor(qreal width) const;

Q_SIGNALS:
    void spacingChanged();
    void keepWithChanged();
    void layoutChanged();

protected:
    void itemChange(ItemChange change, const ItemChangeData &data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void componentComplete() override;

private:
    struct Place {
        QQuickItem *item = nullptr;
        qreal x = 0;
        qreal y = 0;
    };
    struct Layout {
        QList<Place> places;
        qreal height = 0;
        qreal lineHeight = 0;
    };
    Layout compute(qreal width) const;
    void watch(QQuickItem *child);
    void relayout();

    qreal m_spacing = 0;
    QPointer<QQuickItem> m_keepWith;
    qreal m_lineHeight = 0;
    qreal m_contentHeight = 0;
    bool m_laying = false;
};

/// A list model of keys (role `key`) whose rows stay while their key stays: setting `keys`
/// removes, moves and inserts rows — a Repeater keeps the delegates of the keys that stayed.
class KeyListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QStringList keys READ keys WRITE setKeys NOTIFY keysChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY keysChanged FINAL)

public:
    explicit KeyListModel(QObject *parent = nullptr);

    QStringList keys() const { return m_keys; }
    void setKeys(const QStringList &keys);
    int count() const { return int(m_keys.size()); }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void keysChanged();

private:
    QStringList m_keys;
};

} // namespace brulion::editor
