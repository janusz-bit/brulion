#include "widgetpublisher.h"

#include "widgetactions.h"
#include "widgetsnapshot.h"

#include "appcontroller.h"
#include "appsettings.h"
#include "workspace.h"
#include "workspacereminders.h"

#include "android/androidbridge.h"
#include "notebook/journals.h"
#include "vault/vault.h"

#include <QDir>
#include <QDateTime>
#include <QGuiApplication>
#include <QLoggingCategory>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::widgets {

namespace {

Q_STATIC_LOGGING_CATEGORY(lcWidgets, "brulion.widgets")

/// Quick add writes into the daily notes of today and the next days (a week: a snapshot a few days
/// old still knows today's note).
constexpr int CaptureDays = 7;

QList<PageRef> pageRefs(const QVariantList &list)
{
    QList<PageRef> out;
    for (const QVariant &value : list) {
        const QVariantMap page = value.toMap();
        out.append(PageRef{page.value(u"path"_s).toString(), page.value(u"title"_s).toString()});
    }
    return out;
}

} // namespace

WidgetPublisher::WidgetPublisher(shell::AppController *app) : QObject(app), m_app(app)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(2000);
    connect(&m_debounce, &QTimer::timeout, this, &WidgetPublisher::publish);
    m_midnight.setSingleShot(true);
    connect(&m_midnight, &QTimer::timeout, this, [this] {
        publish();
        armMidnight();
    });
    armMidnight();

    shell::Workspace *ws = app->workspace();
    connect(ws, &shell::Workspace::filesChanged, this, &WidgetPublisher::schedule);
    connect(ws, &shell::Workspace::favoritesChanged, this, &WidgetPublisher::schedule);
    connect(ws, &shell::Workspace::recentPagesChanged, this, &WidgetPublisher::schedule);
    connect(ws, &shell::Workspace::configChanged, this, &WidgetPublisher::schedule);
    connect(app->settings(), &shell::AppSettings::languageChanged, this, &WidgetPublisher::schedule);
    connect(ws->reminders(), &shell::WorkspaceReminders::doneTargetChanged, this, [this] {
        if (target().isEmpty())
            return;
        publish();
        takeActions();
    });
    connect(app, &shell::AppController::statusChanged, this, [this] {
        if (m_app && m_app->status() == u"onboarding")
            publishEmpty();
    });
    if (auto *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(gui, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            // Leaving the app: the home screen comes next.
            if (state != Qt::ApplicationActive && m_debounce.isActive())
                publish();
        });
    }

    auto *bridge = platform::AndroidBridge::instance();
    connect(bridge, &platform::AndroidBridge::widgetActionsQueued, this, &WidgetPublisher::takeActions);
    connect(bridge, &platform::AndroidBridge::widgetNoteWritten, this, [this](const QString &path) {
        if (!m_app)
            return;
        Vault *vault = m_app->workspace()->vault();
        if (!vault)
            return;
        const QString root = QDir::cleanPath(vault->rootPath()) + u'/';
        if (!QDir::cleanPath(path).startsWith(root))
            return;
        qCInfo(lcWidgets) << "Quick add wrote" << path << "- reading the notebook again";
        vault->rescan();
    });
}

WidgetPublisher::~WidgetPublisher() = default;

QString WidgetPublisher::target() const
{
    return m_app ? m_app->workspace()->reminders()->doneTarget() : QString();
}

void WidgetPublisher::schedule()
{
    if (!target().isEmpty())
        m_debounce.start();
}

void WidgetPublisher::publish()
{
    m_debounce.stop();
    if (!m_app || target().isEmpty())
        return;
    shell::Workspace *ws = m_app->workspace();
    Vault *vault = ws->vault();
    VaultIndex *index = ws->index();
    if (!vault || !index)
        return;
    const QString language = m_app->settings()->language();
    SnapshotOptions options;
    options.now = ws->now();
    options.weekStart = vault->config().weekStartsOn;
    options.locale = language;
    options.vaultId = vault->id();
    options.vaultName = ws->name();
    options.vaultRoot = QDir::cleanPath(vault->rootPath());
    options.favorites = pageRefs(ws->favorites());
    options.recent = pageRefs(ws->recentPages());
    options.captureDays =
            captureDays(*vault, options.now.date(), CaptureDays, options.now, notebook::contentLocale(language));
    const int placed = platform::AndroidBridge::instance()->updateWidgets(buildSnapshot(*index, options));
    qCInfo(lcWidgets) << "Snapshot published," << placed << "widget(s) placed";
}

void WidgetPublisher::publishEmpty()
{
    m_debounce.stop();
    const QString language = m_app ? m_app->settings()->language() : u"pl"_s;
    platform::AndroidBridge::instance()->updateWidgets(emptySnapshot(QDateTime::currentDateTime(), language));
    qCInfo(lcWidgets) << "Empty snapshot published (no notebook)";
}

void WidgetPublisher::takeActions()
{
    const QString vault = target();
    if (!m_app || vault.isEmpty())
        return; // kept in the persisted queue until the notebook is ready
    const QList<WidgetAction> actions = widgetActions(platform::AndroidBridge::instance()->takeWidgetActions(vault));
    if (actions.isEmpty())
        return;
    int added = 0;
    for (const WidgetAction &action : actions) {
        const QString block = widgetActionBlock(action);
        if (block.isEmpty())
            continue;
        if (m_app->workspace()->appendJournalBlocks(block).isEmpty()) {
            qCWarning(lcWidgets) << "Could not write the widget's entry" << action.id;
            m_app->toast(widgetActionFailedMessage(action.text), u"error"_s);
        } else {
            ++added;
        }
    }
    if (added > 0)
        m_app->toast(widgetActionsAddedMessage(added), u"success"_s);
}

void WidgetPublisher::armMidnight()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime next(now.date().addDays(1), QTime(0, 0, 5));
    m_midnight.start(int(std::clamp<qint64>(now.msecsTo(next), 1000, 24LL * 3600 * 1000)));
}

} // namespace brulion::widgets
