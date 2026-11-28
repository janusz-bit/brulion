// AtramentView's views other than the editor's (docs/ATRAMENT.md "The views in Atrament (M3)"),
// each a kind of kKinds: `view: "tasks"` — TasksView over the TaskListModel in `viewModel` —,
// `view: "search"` — SearchResultsView over the SearchResultModel —, `view: "agenda"` — AgendaView
// over the CalendarModel —; their look from `viewLook` and
// the item's colours, their rows' requests as `viewAction` (x, y in this item: the Atrament window's
// coordinates are the item's).
#include "atramentview.h"

#ifdef BRULION_HAS_ATRAMENT_VIEWS
#include "agendaview.h"
#include "calendarmodel.h"
#include "dateshelper.h"
#include "searchmodel.h"
#include "searchview.h"
#include "tasklistmodel.h"
#include "tasksview.h"

#include <QQmlEngine>
#endif

#include <QDebug>

#include <algorithm>

using namespace Qt::StringLiterals;
namespace at = brulion::atrament;
namespace views = brulion::views::atr;

// A view other than the editor's as AtramentView drives it: its look, where its rows' parts are, its
// current row.
struct AtramentView::Views {
    virtual ~Views() = default;
    virtual at::AnyView view() const = 0;
    virtual void setLook(const QVariantMap &look) = 0;
    virtual std::optional<at::Rect> partBounds(int row, const QString &part) const = 0;
    virtual int currentRow() const = 0;
    virtual void setCurrentRow(int row) = 0;
    virtual void showRow(int row) = 0;
};

#ifdef BRULION_HAS_ATRAMENT_VIEWS
namespace {

// One of views/atrament's views: its entity, made over its model, its requests the item's
// viewAction; its look from the map, its parts by name, its current row.
template<class View>
struct EntityViews final : AtramentView::Views {
    at::App &app;
    at::Entity<View> entity;
    template<class Model>
    EntityViews(at::App &a, Model *model, QPointer<AtramentView> item)
        : app(a)
        , entity(a.newEntity<View>(model))
    {
        entity.update(app, [item](View &v, at::Context<View> &) {
            v.onAction = [item](const QString &action, const QVariantMap &args) {
                if (item)
                    Q_EMIT item->viewAction(action, args);
            };
        });
    }
    at::AnyView view() const override { return at::AnyView{entity.id(), &at::renderView<View>}; }
    void setLook(const QVariantMap &map) override
    {
        const typename View::Look look = View::Look::fromMap(map);
        entity.update(app, [&look](View &v, at::Context<View> &cx) { v.setLook(look, cx); });
    }
    std::optional<at::Rect> partBounds(int row, const QString &part) const override
    {
        return entity.read(app).partBounds(row, View::partNamed(part));
    }
    int currentRow() const override { return entity.read(app).currentRow(); }
    void setCurrentRow(int row) override
    {
        entity.update(app, [row](View &v, at::Context<View> &cx) { v.revealRow(row, cx); });
    }
    void showRow(int row) override
    {
        entity.update(app, [row](View &v, at::Context<View> &cx) {
            if constexpr (requires { v.showRow(row, cx); })
                v.showRow(row, cx);
            else
                v.revealRow(row, cx);
        });
    }
};

// The app's QML singleton `name` (null: none, or no engine).
QPointer<QObject> singleton(QQmlEngine *engine, const char *name)
{
    return engine ? engine->singletonInstance<QObject *>("Brulion", name) : nullptr;
}

// A date in words as the app's QML says it (its DatesHelper): `style` formatLong, formatHuman…
std::function<QString(const QString &)> dateWords(QQmlEngine *engine, QString (brulion::ui::DatesHelper::*style)(const QString &) const)
{
    QPointer<brulion::ui::DatesHelper> dates = qobject_cast<brulion::ui::DatesHelper *>(singleton(engine, "DatesHelper"));
    return [dates, style](const QString &date) { return dates ? (dates.data()->*style)(date) : date; };
}

// A daily note's day in short words (Workspace.journalDate, DatesHelper.formatShort; "": not one).
std::function<QString(const QString &)> journalDayWords(QQmlEngine *engine)
{
    const QPointer<QObject> workspace = singleton(engine, "Workspace");
    const auto shortDate = dateWords(engine, &brulion::ui::DatesHelper::formatShort);
    return [workspace, shortDate](const QString &path) {
        QString date;
        if (workspace)
            QMetaObject::invokeMethod(workspace.data(), "journalDate", Q_RETURN_ARG(QString, date), Q_ARG(QString, path));
        return date.isEmpty() ? date : shortDate(date);
    };
}

// What a view needs from the app beside its model (its hooks).
void configure(views::TasksView &, QQmlEngine *) { }
void configure(views::SearchResultsView &v, QQmlEngine *engine)
{
    v.formatDate = dateWords(engine, &brulion::ui::DatesHelper::formatLong);
}
void configure(views::AgendaView &v, QQmlEngine *engine)
{
    v.formatHuman = dateWords(engine, &brulion::ui::DatesHelper::formatHuman);
    v.journalDay = journalDayWords(engine);
}

// A view's kind: its name (AtramentView.view) and how it is made over `viewModel` (null: the model
// is not its kind's).
using Maker = std::shared_ptr<AtramentView::Views> (*)(at::App &, QObject *, QPointer<AtramentView>, QQmlEngine *);
template<class View, class Model>
std::shared_ptr<AtramentView::Views> make(at::App &app, QObject *model, QPointer<AtramentView> item, QQmlEngine *engine)
{
    auto *m = qobject_cast<Model *>(model);
    if (!m)
        return nullptr;
    auto made = std::make_shared<EntityViews<View>>(app, m, item);
    made->entity.update(app, [engine](View &v, at::Context<View> &) { configure(v, engine); });
    return made;
}
struct Kind {
    QStringView name;
    Maker make;
};
const Kind kKinds[] = {{u"tasks", &make<views::TasksView, brulion::planning::TaskListModel>},
                       {u"search", &make<views::SearchResultsView, brulion::content::SearchResultModel>},
                       {u"agenda", &make<views::AgendaView, brulion::planning::CalendarModel>}};

const Kind *kindNamed(const QString &name)
{
    const auto it = std::find_if(std::begin(kKinds), std::end(kKinds), [&name](const Kind &k) { return k.name == name; });
    return it != std::end(kKinds) ? it : nullptr;
}

} // namespace
#endif

void AtramentView::setViewModel(QObject *model)
{
    if (setBeforeView(m_viewModel, model, "the view's model"))
        Q_EMIT viewModelChanged();
}

void AtramentView::setViewLook(const QVariantMap &look)
{
    m_viewLook = look;
    Q_EMIT viewLookChanged();
    applyViewLook();
}

// The views of attachView() other than the gallery and the editor's; false: not one of them.
bool AtramentView::attachOtherView()
{
#ifdef BRULION_HAS_ATRAMENT_VIEWS
    const Kind *kind = kindNamed(m_view);
    if (!kind)
        return false;
    m_views = kind->make(atramentApp(), m_viewModel.data(), this, qmlEngine(this));
    if (!m_views) {
        if (m_viewModel)
            qWarning() << "AtramentView: the" << m_view << "view needs its model as viewModel";
        return true; // (attached once the model is set)
    }
    applyViewLook();
    attach(atramentApp(), m_views->view());
    m_attached = true;
    return true;
#else
    return false;
#endif
}

void AtramentView::applyViewLook()
{
    if (!m_views)
        return;
    QVariantMap look = m_viewLook;
    if (!look.contains(u"text"_s))
        look.insert({{u"text"_s, m_textColor}, {u"muted"_s, m_mutedColor}, {u"link"_s, m_linkColor}});
    m_views->setLook(look);
}

QRectF AtramentView::viewPartRect(int row, const QString &part) const
{
    const std::optional<at::Rect> b = m_views ? m_views->partBounds(row, part) : std::nullopt;
    return b ? QRectF(b->left(), b->top(), b->width(), b->height()) : QRectF();
}

void AtramentView::viewRevealRow(int row)
{
    if (m_views)
        m_views->setCurrentRow(row);
}

void AtramentView::viewShowRow(int row)
{
    if (m_views)
        m_views->showRow(row);
}

int AtramentView::viewCurrentRow() const
{
    return m_views ? m_views->currentRow() : -1;
}
