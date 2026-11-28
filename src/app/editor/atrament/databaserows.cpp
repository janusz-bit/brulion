// Database views drawn by Atrament (docs/ATRAMENT.md "Database views", step a): a ```query block
// with `view:` whose ViewResultModel lays it out as a table — the columns' labels (the sorted
// one's arrow), the rows' cells as rendered text (read from the model's HTML once), the columns'
// summaries — sideways scrolled when wider than the text column. Other layouts, a view still
// running or failed, and a table being edited stay the QML delegate's (DatabaseView): a click on
// the table hands its row to the QML view (onDatabaseCell) until the row leaves the view.
#include "mediarows_p.h"

#include "blocklistmodel.h"
#include "pagecontroller.h"

namespace brulion::editor::atr {

namespace at = brulion::atrament;
using namespace Qt::StringLiterals;

namespace {

// A column's field by its role's name (ViewResultModel's columnModel is a list of maps).
QVariant columnField(const QAbstractItemModel *columns, int c, const QByteArray &name)
{
    const QHash<int, QByteArray> roles = columns->roleNames();
    return columns->index(c, 0).data(roles.key(name, -1));
}

} // namespace

ViewResultModel *PageView::databaseModel(int row) const
{
    if (!m_media || !m_model || row < 0 || row >= m_model->rowCount())
        return nullptr;
    const QModelIndex index = m_model->index(row, 0);
    const quint64 uid = index.data(BlockListModel::UidRole).toString().toULongLong();
    const QString source = m_model->editTextAt(row);
    Media::View &v = m_media->views[uid];
    v.frame = m_frame;
    if (!v.model) {
        v.model = std::make_unique<ViewResultModel>();
        ViewResultModel *model = v.model.get();
        model->classBegin();
        model->setController(m_controller);
        model->setLanguage(u"query"_s);
        model->setBlockRow(row);
        model->setSource(source);
        model->componentComplete();
        at::App *app = m_media->app;
        const at::EntityId self = m_media->self;
        auto redraw = [app, self] {
            if (app)
                app->updateEntity<PageView>(self, [](PageView &, at::Context<PageView> &c) { c.notify(); });
        };
        v.connections.push_back(QObject::connect(model, &ViewResultModel::resultChanged, model, redraw));
        v.connections.push_back(QObject::connect(model, &QAbstractItemModel::modelReset, model, redraw));
        v.connections.push_back(QObject::connect(model, &QAbstractItemModel::dataChanged, model, redraw));
        return model;
    }
    if (v.model->controller() != m_controller.data())
        v.model->setController(m_controller);
    v.model->setSource(source); // (edited elsewhere: debounced by the model)
    v.model->setBlockRow(row);
    return v.model.get();
}

bool PageView::drawsDatabase(int row) const
{
    if (!(m_mediaKinds & Databases))
        return false;
    ViewResultModel *model = databaseModel(row);
    if (!model || !model->ready() || !model->ok() || model->kind() != u"table" || model->held())
        return false;
    const quint64 uid = m_model->index(row, 0).data(BlockListModel::UidRole).toString().toULongLong();
    return !m_media->views[uid].qml;
}

// The table: its header, its rows, its summaries; a click hands it to the QML view.
at::AnyElement PageView::databaseRow(std::size_t row, const QModelIndex &index, float textX)
{
    ViewResultModel *model = databaseModel(int(row));
    QAbstractItemModel *columns = model->columnModel();
    const int columnCount = columns->rowCount();
    const MediaLook &look = m_mediaLook;
    const float columnWidth = 160;
    at::Div table = at::div().flexCol().minW(columnWidth * float(columnCount)).child(databaseHeader(columns, columnWidth));
    for (int r = 0; r < model->rowCount(); ++r)
        std::move(table).child(databaseCells(row, r, model->data(model->index(r), ViewResultModel::CellsRole).toStringList(),
                                             columnCount, columnWidth));
    // The summaries under the columns (the ones shown).
    at::Div summaries = at::div().flexRow().textSize(look.tinySize).textColor(m_palette.muted);
    bool anySummary = false;
    for (int c = 0; c < columnCount; ++c) {
        const QString label = columnField(columns, c, "summaryLabel").toString();
        const QString text = columnField(columns, c, "summaryText").toString();
        anySummary = anySummary || !text.isEmpty();
        std::move(summaries).child(at::div().w(columnWidth).flexShrink0().px(8).py(4).child(
                at::text(u16View(text.isEmpty() ? QString() : label + u' ' + text)).maxLines(1)));
    }
    if (anySummary)
        std::move(table).child(std::move(summaries));
    const quint64 uid = index.data(BlockListModel::UidRole).toString().toULongLong();
    const at::EntityId selfId = m_selfId;
    return at::div()
            .id(at::ElementId::number(uid))
            .relative()
            .wFull()
            .ml(textX)
            .flexCol()
            .bg(look.queryBackground)
            .border(1)
            .borderColor(look.border)
            .rounded(look.radius)
            .overflowXScroll()
            .role(at::Role::Table)
            .ariaLabel(u16View(translated("DatabaseView", "Table")))
            .when(!look.uiFamily.empty(), [&look](at::Div &&d) { return std::move(d).fontFamily(look.uiFamily); })
            .onClick([selfId, row, uid](const at::ClickEvent &, at::Window &, at::App &a) {
                a.updateEntity<PageView>(selfId, [&](PageView &v, at::Context<PageView> &c) {
                    // Handed to the QML view: its row is the delegate's until it leaves the view.
                    v.m_media->views[uid].qml = true;
                    v.m_extents.markAllEstimated();
                    c.notify();
                    if (v.onDatabaseCell)
                        v.onDatabaseCell(int(row), -1, -1);
                });
            })
            .child(std::move(table));
}

// The columns' labels, the sorted one's arrow.
at::AnyElement PageView::databaseHeader(QAbstractItemModel *columns, float columnWidth)
{
    const MediaLook &look = m_mediaLook;
    at::Div header = at::div().flexRow().borderB(1).borderColor(look.border).textSize(look.smallSize).fontSemibold()
                             .textColor(m_palette.muted);
    for (int c = 0; c < columns->rowCount(); ++c) {
        const int sorted = columnField(columns, c, "sorted").toInt();
        const QString label = columnField(columns, c, "label").toString() + (sorted > 0 ? u" ↑"_s : sorted < 0 ? u" ↓"_s : QString());
        std::move(header).child(at::div()
                                        .w(columnWidth)
                                        .flexShrink0()
                                        .px(8)
                                        .py(6)
                                        .role(at::Role::ColumnHeader)
                                        .ariaLabel(u16View(label))
                                        .child(at::text(u16View(label)).maxLines(1)));
    }
    return std::move(header);
}

// A row's cells as rendered text (three lines at most).
at::AnyElement PageView::databaseCells(std::size_t row, int viewRow, const QStringList &cells, int columns, float columnWidth)
{
    const MediaLook &look = m_mediaLook;
    const quint64 uid = m_model->index(int(row), 0).data(BlockListModel::UidRole).toString().toULongLong();
    Media::View &v = m_media->views[uid];
    if (v.cells.size() <= std::size_t(viewRow))
        v.cells.resize(std::size_t(viewRow) + 1);
    at::Div line = at::div().flexRow().borderB(1).borderColor(look.frame).role(at::Role::Row);
    for (int c = 0; c < columns; ++c) {
        const auto rt = renderedOnce(v.cells[std::size_t(viewRow)], std::size_t(c), cells.value(c));
        std::move(line).child(at::div()
                                      .w(columnWidth)
                                      .flexShrink0()
                                      .px(8)
                                      .py(5)
                                      .role(at::Role::Cell)
                                      .child(at::text(u16View(rt->text)).spans(renderedSpans(*rt, m_palette)).maxLines(3)));
    }
    return std::move(line);
}

} // namespace brulion::editor::atr
