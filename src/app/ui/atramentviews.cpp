// AtramentView's views other than the editor's (docs/ATRAMENT.md "The views in Atrament (M3)"):
// `view: "tasks"` — TasksView over the TaskListModel in `viewModel`, its look from `viewLook` and
// the item's colours, its rows' requests as `viewAction` (x, y in this item: the Atrament window's
// coordinates are the item's).
#include "atramentview.h"

#ifdef BRULION_HAS_ATRAMENT_VIEWS
#include "tasklistmodel.h"
#include "tasksview.h"
#endif

#include <QDebug>

using namespace Qt::StringLiterals;
namespace at = brulion::atrament;

#ifdef BRULION_HAS_ATRAMENT_VIEWS
struct AtramentView::Views {
    at::Entity<brulion::views::atr::TasksView> tasks;
};

#else
struct AtramentView::Views {};
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
    if (m_view != u"tasks")
        return false;
    auto *model = qobject_cast<brulion::planning::TaskListModel *>(m_viewModel.data());
    if (!model) {
        if (m_viewModel)
            qWarning() << "AtramentView: the tasks view needs a TaskListModel as viewModel";
        return true; // (attached once the model is set)
    }
    at::App &app = atramentApp();
    m_views = std::make_shared<Views>();
    m_views->tasks = app.newEntity<brulion::views::atr::TasksView>(model);
    QPointer<AtramentView> guard = this;
    m_views->tasks.update(app, [guard](brulion::views::atr::TasksView &v, at::Context<brulion::views::atr::TasksView> &) {
        v.onAction = [guard](const QString &action, const QVariantMap &args) {
            if (guard)
                Q_EMIT guard->viewAction(action, args);
        };
    });
    applyViewLook();
    attach(app, at::AnyView{m_views->tasks.id(), &at::renderView<brulion::views::atr::TasksView>});
    m_attached = true;
    return true;
#else
    return false;
#endif
}

void AtramentView::applyViewLook()
{
#ifdef BRULION_HAS_ATRAMENT_VIEWS
    if (!m_views || !m_views->tasks)
        return;
    QVariantMap look = m_viewLook;
    look.insert({{u"text"_s, m_textColor}, {u"muted"_s, m_mutedColor}, {u"link"_s, m_linkColor}});
    const brulion::views::atr::TasksLook tasks = brulion::views::atr::TasksLook::fromMap(look);
    m_views->tasks.update(atramentApp(), [&tasks](brulion::views::atr::TasksView &v, at::Context<brulion::views::atr::TasksView> &cx) {
        v.setLook(tasks, cx);
    });
#endif
}

QRectF AtramentView::viewPartRect(int row, const QString &part) const
{
#ifdef BRULION_HAS_ATRAMENT_VIEWS
    if (!m_views || !m_views->tasks)
        return {};
    const std::optional<at::Rect> b =
            m_views->tasks.read(atramentApp()).partBounds(row, brulion::views::atr::TasksView::partNamed(part));
    return b ? QRectF(b->left(), b->top(), b->width(), b->height()) : QRectF();
#else
    Q_UNUSED(row)
    Q_UNUSED(part)
    return {};
#endif
}

void AtramentView::viewRevealRow(int row)
{
#ifdef BRULION_HAS_ATRAMENT_VIEWS
    if (m_views && m_views->tasks)
        m_views->tasks.update(atramentApp(), [row](brulion::views::atr::TasksView &v, at::Context<brulion::views::atr::TasksView> &cx) {
            v.revealRow(row, cx);
        });
#else
    Q_UNUSED(row)
#endif
}

int AtramentView::viewCurrentRow() const
{
#ifdef BRULION_HAS_ATRAMENT_VIEWS
    return m_views && m_views->tasks ? m_views->tasks.read(atramentApp()).currentRow() : -1;
#else
    return -1;
#endif
}
