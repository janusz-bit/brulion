#include "automationrun.h"

#include "automation/automation.h"
#include "index/vaultindex.h"
#include "notebook/journals.h"
#include "notebook/session.h"
#include "vault/vault.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>

#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::background {

namespace {

Q_STATIC_LOGGING_CATEGORY(lcAutomationRun, "brulion.automation")

QJsonObject failure(const QString &error, const QString &message, const QString &id, const QString &action = {})
{
    QJsonObject out{{u"ok"_s, false}, {u"error"_s, error}, {u"keyed"_s, false}};
    if (!message.isEmpty())
        out.insert(u"message"_s, message);
    if (!id.isEmpty())
        out.insert(u"id"_s, id);
    if (!action.isEmpty())
        out.insert(u"action"_s, action);
    return out;
}

} // namespace

AutomationRunner::AutomationRunner(QString appDataDir, std::function<QDateTime()> clock)
    : m_appDataDir(std::move(appDataDir)),
      m_clock(clock ? std::move(clock) : [] { return QDateTime::currentDateTime(); })
{
}

AutomationRunner::~AutomationRunner() = default;

QJsonObject AutomationRunner::run(const QJsonObject &job)
{
    const QString id = job.value(u"id"_s).toString().left(200);
    const automation::ParsedUri parsed = automation::parseUri(job.value(u"uri"_s).toString());
    if (!parsed.request)
        return failure(u"bad_uri"_s, automation::uriErrorCode(parsed.error), id);
    const automation::Request &request = *parsed.request;
    const QString action = automation::actionName(request.action);
    const QString requestId = request.id.isEmpty() ? id : request.id;
    const QJsonObject target = job.value(u"target"_s).toObject();
    const QString root = target.value(u"root"_s).toString();
    if (root.isEmpty() || !QDir::isAbsolutePath(root))
        return failure(u"no_notebook"_s, u"Brulion has not opened a notebook on this device yet"_s, requestId, action);
    const automation::LinkPolicy policy = automation::policyFromName(target.value(u"policy"_s).toString());
    if (!request.vault.isEmpty()) {
        const automation::NotebookMatch match =
                automation::matchNotebook(request.vault, {{root, target.value(u"name"_s).toString()}});
        if (match.index < 0)
            return failure(u"unknown_notebook"_s, request.vault, requestId, action);
    }
    const QString vaultId = Vault::idForPath(root);
    const automation::KeyState key =
            automation::keyState(request.key, automation::KeyStore(automation::KeyStore::fileIn(m_appDataDir)).key(vaultId));
    const automation::Access access = automation::accessOf(request);
    const automation::Verdict verdict = automation::decide(access, policy, key, false);
    if (verdict.decision == automation::Decision::Refuse)
        return failure(verdict.error, {}, requestId, action);
    // Opening and commands need the app's window.
    if (access != automation::Access::Write)
        return failure(u"needs_app"_s, {}, requestId, action);

    if (!m_notebook || m_root != root) {
        m_notebook = std::make_unique<notebook::Notebook>();
        notebook::Notebook::Options options;
        options.stateDir = target.value(u"stateDir"_s).toString();
        options.historyReason = QString(); // this device's own edit, as the app's
        options.followOutsideLinks = target.value(u"followOutsideLinks"_s).toBool();
        options.clock = m_clock;
        QString error;
        if (!m_notebook->open(root, options, &error)) {
            m_notebook.reset();
            m_root.clear();
            qCWarning(lcAutomationRun).noquote() << u"Cannot open the notebook: %1"_s.arg(error);
            return failure(u"no_notebook"_s, error, requestId, action);
        }
        m_root = root;
    }
    const automation::Context context{m_clock(),
                                      notebook::contentLocale(target.value(u"language"_s).toString())};
    const automation::Plan plan = automation::plan(request, m_notebook->vault(), m_notebook->index(), context);
    automation::NotebookWriter writer(*m_notebook);
    const automation::Outcome outcome = automation::execute(plan, writer, requestId);
    qCInfo(lcAutomationRun).noquote() << u"%1: %2"_s.arg(action, outcome.ok ? u"done"_s : outcome.error);
    QJsonObject out = outcome.toJson();
    out.insert(u"keyed"_s, true);
    out.insert(u"vault"_s, vaultId);
    out.insert(u"name"_s, target.value(u"name"_s).toString());
    out.insert(u"remindersOn"_s, target.value(u"remindersOn"_s).toBool(true));
    if (!outcome.path.isEmpty())
        out.insert(u"file"_s, QDir(root).filePath(outcome.path));
    return out;
}

} // namespace brulion::background
