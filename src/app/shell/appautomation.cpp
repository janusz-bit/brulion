// AppController: brulion:// addresses (docs/UI.md "Automation") — from a link on a web page or in
// another app (the system starts Brulion with the address, or a second launch hands it over: the
// single-instance channel's `uri` message), a link clicked in a note, the browser's protocol
// handler (web+brulion:), Android's VIEW intent and its automation intents (docs/ANDROID.md
// "Automation"), brulion-cli `uri open`. The pure parts — reading the address, what it may do,
// the page it names, what it writes — are core/automation; here: which notebook, the device's
// policy and the notebook's key, the confirmation dialog, the write through the open notebook's
// vault (compare-and-swap, History; an extension page's new code is never approved by it), the
// toasts and the x-callback addresses.
#include "appcontroller.h"

#include "log/categories.h"

#include "automation/automation.h"
#include "notebook/journals.h"
#include "notebook/session.h"
#include "util/paths.h"
#include "vault/vault.h"
#include "workspacechats.h"
#include "workspaceextensions.h"

#ifdef Q_OS_ANDROID
#include "android/androidbridge.h"
#endif
#ifdef Q_OS_WASM
#include "wasm/wasmshell.h"
#endif

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringDecoder>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

using logging::lcApp;

namespace {

constexpr char kPolicyKey[] = "automation/links";
/// Addresses kept while no notebook is ready (a burst from a script is not a queue).
constexpr int kMaxWaitingUris = 8;

/// The open notebook's vault written through as notebook::Notebook::edit() does: the current text
/// read, the edit applied, written only while the file still holds what was read (another
/// program's change meanwhile: read again), never over text that is not UTF-8. Through the vault
/// itself, not the editor's save path: a block added to an extension page never approves its
/// code (Workspace::writeIfUnchanged() would count it as this device's own edit).
class VaultWriter final : public automation::Writer {
public:
    explicit VaultWriter(Vault &vault) : m_vault(vault) {}

    notebook::Notebook::WriteResult edit(const VaultPath &path, const notebook::Notebook::Edit &edit,
                                         const notebook::Notebook::WriteOptions &options) override
    {
        using W = notebook::Notebook::WriteResult;
        W out;
        out.path = path;
        for (int attempt = 0; attempt < std::max(1, options.attempts); ++attempt) {
            const bool exists = m_vault.exists(path) || QFileInfo(m_vault.absolutePath(path)).isFile();
            if (!exists && !options.mayCreate) {
                out.status = W::NotFound;
                return out;
            }
            QString current;
            if (exists) {
                bool ok = false;
                const QByteArray bytes = m_vault.readBinary(path, &ok);
                if (!ok) {
                    out.status = W::Failed;
                    out.error = m_vault.errorString();
                    return out;
                }
                QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
                const QString decoded = decoder.decode(bytes);
                if (decoder.hasError()) {
                    out.status = W::Encoding;
                    return out;
                }
                current = notebook::decodeText(bytes);
                Q_UNUSED(decoded);
            }
            QString next;
            if (!edit(exists ? current : options.initialText, exists, &next)) {
                out.status = W::Refused;
                return out;
            }
            out.before = current;
            out.after = next;
            if (exists && next == current) {
                out.status = W::Unchanged;
                return out;
            }
            switch (m_vault.writeTextIfUnchanged(path, next, exists ? current : QString(), ChangeSource::User)) {
            case WriteOutcome::Written:
                out.status = W::Written;
                out.created = !exists;
                return out;
            case WriteOutcome::Changed: continue; // another program wrote meanwhile: again
            case WriteOutcome::Failed:
                out.status = W::Failed;
                out.error = m_vault.errorString();
                return out;
            }
        }
        out.status = W::Conflict;
        return out;
    }

private:
    Vault &m_vault;
};

/// A callback address Brulion may open: a valid URL whose scheme can do nothing to this device by
/// itself (never Brulion's own — a loop —, files, data or scripts).
bool callbackAllowed(const QUrl &url)
{
    static const QStringList refused{u"brulion"_s, u"web+brulion"_s, u"file"_s, u"data"_s, u"javascript"_s,
                                     u"vbscript"_s, u"blob"_s, u"about"_s, u"content"_s, u"intent"_s};
    return url.isValid() && !url.scheme().isEmpty() && !refused.contains(url.scheme().toLower());
}

/// The device's known notebooks: the open one first, then the recent ones (not the documentation).
QList<automation::KnownNotebook> knownNotebooks(const QString &open, const QVariantList &recent,
                                                const std::function<bool(const QString &)> &isDocs)
{
    QList<automation::KnownNotebook> out;
    const auto add = [&out, &isDocs](const QString &root, const QString &name) {
        if (root.isEmpty() || isDocs(root))
            return;
        for (const automation::KnownNotebook &k : std::as_const(out)) {
            if (Vault::idForPath(k.root) == Vault::idForPath(root))
                return;
        }
        out.append({root, name.isEmpty() ? QFileInfo(root).fileName() : name});
    };
    add(open, {});
    for (const QVariant &v : recent) {
        const QVariantMap m = v.toMap();
        add(m.value(u"path"_s).toString(), m.value(u"name"_s).toString());
    }
    return out;
}

} // namespace

void AppController::initAutomation()
{
    // "Open" of a toast after a write: the block written.
    connect(this, &AppController::toastActionTriggered, this, [this](int id) {
        if (const QVariantMap route = m_automationToasts.take(id); !route.isEmpty() && m_status == u"ready")
            navigate(route);
    });
    connect(m_workspace.get(), &Workspace::automationUriRequested, this,
            [this](const QString &uri) { receiveUri(uri, u"note"_s); });
}

QString AppController::automationPolicy() const
{
    return automation::policyName(automation::policyFromName(m_settings->value(QLatin1String(kPolicyKey)).toString()));
}

void AppController::setAutomationPolicy(const QString &policy)
{
    const QString name = automation::policyName(automation::policyFromName(policy));
    if (name == automationPolicy())
        return;
    m_settings->setValue(QLatin1String(kPolicyKey), name);
    Q_EMIT automationChanged();
    publishAutomationTarget();
}

QString AppController::automationKey() const
{
    if (!m_workspace->isOpen() || inDocsVault())
        return {};
    return automation::KeyStore(automation::KeyStore::fileIn(m_dataDir)).key(m_workspace->vault()->id());
}

QString AppController::createAutomationKey()
{
    if (!m_workspace->isOpen() || inDocsVault())
        return {};
    automation::KeyStore store(automation::KeyStore::fileIn(m_dataDir));
    const QString key = store.ensureKey(m_workspace->vault()->id());
    if (key.isEmpty())
        toast(tr("Could not save the automation key: %1").arg(store.error()), u"error"_s);
    Q_EMIT automationChanged();
    publishAutomationTarget();
    return key;
}

QString AppController::renewAutomationKey()
{
    if (!m_workspace->isOpen() || inDocsVault())
        return {};
    automation::KeyStore store(automation::KeyStore::fileIn(m_dataDir));
    const QString key = store.renew(m_workspace->vault()->id());
    if (key.isEmpty())
        toast(tr("Could not save the automation key: %1").arg(store.error()), u"error"_s);
    else
        toast(tr("New automation key: links and automations with the old one no longer write."), u"info"_s);
    Q_EMIT automationChanged();
    publishAutomationTarget();
    return key;
}

void AppController::removeAutomationKey()
{
    if (!m_workspace->isOpen() || inDocsVault())
        return;
    automation::KeyStore store(automation::KeyStore::fileIn(m_dataDir));
    if (!store.remove(m_workspace->vault()->id()))
        toast(tr("Could not save the automation key: %1").arg(store.error()), u"error"_s);
    Q_EMIT automationChanged();
    publishAutomationTarget();
}

QString AppController::automationLink(const QString &action, const QVariantMap &params, bool withKey) const
{
    const auto parsed = automation::actionFromName(action);
    if (!parsed)
        return {};
    automation::Request r;
    r.action = *parsed;
    // The usual order (where, then what, then when); other parameters after them.
    static const QStringList order{u"page"_s, u"title"_s, u"date"_s, u"heading"_s, u"block"_s, u"text"_s, u"due"_s,
                                   u"remind"_s, u"alarm"_s, u"query"_s};
    QStringList keys = params.keys();
    std::ranges::stable_sort(keys, {}, [](const QString &key) {
        const qsizetype at = order.indexOf(key);
        return at < 0 ? order.size() : at;
    });
    for (const QString &key : std::as_const(keys)) {
        const QString value = params.value(key).toString();
        if (!value.isEmpty())
            r.params.append({key, value});
    }
    if (m_workspace->isOpen() && !inDocsVault())
        r.vault = QFileInfo(m_workspace->rootPath()).fileName();
    if (withKey)
        r.key = automationKey();
    return automation::buildUri(r);
}

QString AppController::pageAutomationLink(const QString &path, const QString &heading, const QString &blockId) const
{
    if (!m_workspace->isOpen() || path.isEmpty())
        return {};
    automation::Request r;
    r.action = automation::Action::Open;
    r.params.append({u"page"_s, paths::stripMarkdownExtension(paths::normalize(path))});
    if (!blockId.isEmpty())
        r.params.append({u"block"_s, blockId});
    else if (!heading.isEmpty())
        r.params.append({u"heading"_s, heading});
    if (!inDocsVault())
        r.vault = QFileInfo(m_workspace->rootPath()).fileName();
    return automation::buildUri(r);
}

QString AppController::chatPromptLink(const QString &prompt, const QString &partner, const QString &page) const
{
    return automation::chatLinkUri(prompt, partner, page);
}

void AppController::receiveUri(const QString &uri, const QString &origin)
{
    const automation::ParsedUri parsed = automation::parseUri(uri);
    if (!parsed.request) {
        qCInfo(lcApp).noquote() << u"Ignoring a brulion:// address from %1: %2 (%3)"_s.arg(
                origin, automation::uriErrorCode(parsed.error), parsed.detail);
        if (parsed.error != automation::UriError::NotBrulion)
            toast(tr("This Brulion link cannot be used: %1")
                          .arg(automationErrorText(automation::uriErrorCode(parsed.error), parsed.detail)),
                  u"warning"_s);
        return;
    }
    const automation::Request &request = *parsed.request;
    qCInfo(lcApp).noquote() << u"brulion:// address from %1: %2"_s.arg(origin, automation::actionName(request.action));
    if (automation::policyFromName(automationPolicy()) == automation::LinkPolicy::Off) {
        toast(automationErrorText(u"links_off"_s, {}), u"warning"_s);
        return;
    }
    const auto wait = [this, &uri, &origin] {
        if (m_waitingUris.size() < kMaxWaitingUris)
            m_waitingUris.append({uri, origin});
        else
            qCWarning(lcApp) << "Dropping a brulion:// address: too many wait for the notebook";
    };
    // A notebook still opening: once it is ready (finishOpening()).
    if (m_status == u"boot" || m_status == u"loading") {
        wait();
        return;
    }
    if (m_status != u"ready" || !m_workspace->isOpen()) {
        wait();
        toast(tr("Open or create a notebook first: the Brulion link runs once one is open."), u"warning"_s);
        return;
    }
    // Which notebook: the one named, else the open one — never the documentation.
    QString root = inDocsVault() ? lastUserVault() : m_workspace->rootPath();
    if (!request.vault.isEmpty()) {
        const QList<automation::KnownNotebook> known =
                knownNotebooks(inDocsVault() ? QString() : m_workspace->rootPath(), m_settings->recentVaults(),
                               [this](const QString &path) { return isDocsPath(path); });
        const automation::NotebookMatch match = automation::matchNotebook(request.vault, known);
        if (match.index < 0) {
            toast(automationErrorText(match.error, request.vault), u"warning"_s);
            return;
        }
        root = known.at(match.index).root;
    }
    if (root.isEmpty()) {
        toast(tr("Brulion links write into your own notebook. Open or create one first."), u"warning"_s);
        return;
    }
    if (inDocsVault() || Vault::idForPath(root) != Vault::idForPath(m_workspace->rootPath())) {
        // Another notebook of this device: open it, the address runs once it is ready.
        wait();
        if (!openRecentVault(root))
            m_waitingUris.removeIf([&uri](const auto &w) { return w.first == uri; });
        return;
    }
    handleAutomation(request, origin);
}

void AppController::applyPendingUris()
{
    if (m_status != u"ready")
        return;
    for (const auto &[uri, origin] : std::exchange(m_waitingUris, {}))
        receiveUri(uri, origin);
}

void AppController::handleAutomation(const automation::Request &request, const QString &origin)
{
    // Names resolve against the whole notebook.
    m_workspace->finishIndexing();
    const automation::Context context{now(), notebook::contentLocale(m_settings->language())};
    const automation::Plan plan = automation::plan(request, *m_workspace->vault(), *m_workspace->index(), context);
    const automation::KeyState key =
            automation::keyState(request.key, automation::KeyStore(automation::KeyStore::fileIn(m_dataDir))
                                                      .key(m_workspace->vault()->id()));
    const automation::Verdict verdict =
            automation::decide(plan.access, automation::policyFromName(automationPolicy()), key, true);
    if (verdict.decision == automation::Decision::Refuse) {
        toast(automationErrorText(verdict.error, {}), u"warning"_s);
        return;
    }
    const bool keyed = key == automation::KeyState::Valid;
    if (!plan.ok()) {
        toast(automationErrorText(plan.error, plan.detail), u"warning"_s);
        // A new page whose name is taken: that page (nothing is written over it).
        if (plan.error == u"exists" && !plan.route.isEmpty()) {
            showWindow();
            navigate(plan.route.toVariantMap());
        }
        if (keyed) {
            automation::Outcome failed = automation::failure(plan.error, plan.detail, plan.action);
            failed.id = request.id;
            callBack(request, failed);
        }
        return;
    }
    if (verdict.decision == automation::Decision::Confirm) {
        m_automation = PendingAutomation{request, plan, origin};
        Q_EMIT pendingAutomationChanged();
        showWindow();
        requestUiAction(u"automation"_s);
        return;
    }
    const automation::Outcome outcome = carryOutAutomation(request, plan);
    if (keyed)
        callBack(request, outcome);
}

automation::Outcome AppController::carryOutAutomation(const automation::Request &request,
                                                      const automation::Plan &plan)
{
    using automation::Access;
    if (plan.access == Access::Open) {
        QVariantMap route = plan.route.toVariantMap();
        // A prompt link's partner only when it is set up here (by its id or its name), else the
        // device's default (WorkspaceChats::defaultPartner) with a note.
        if (route.value(u"view"_s).toString() == u"chat" && route.contains(u"partner"_s)) {
            const QString wanted = route.take(u"partner"_s).toString();
            for (const QVariant &p : m_workspace->chats()->partners()) {
                const QVariantMap partner = p.toMap();
                const QString id = partner.value(u"id"_s).toString();
                if (id.compare(wanted, Qt::CaseInsensitive) == 0
                    || partner.value(u"label"_s).toString().compare(wanted, Qt::CaseInsensitive) == 0) {
                    route.insert(u"partner"_s, id);
                    break;
                }
            }
            if (!route.contains(u"partner"_s))
                route.insert(u"partnerMissing"_s, wanted);
        }
        // (No partner named, or one not here: the device's default answers.)
        if (route.value(u"view"_s).toString() == u"chat" && !route.contains(u"partner"_s))
            route.insert(u"partner"_s, m_workspace->chats()->defaultPartner());
        if (route.value(u"view"_s).toString() == u"capture")
            quickCapture();
        else {
            showWindow();
            navigate(m_workspace->resolveRoute(route));
        }
        VaultWriter writer(*m_workspace->vault());
        return automation::execute(plan, writer, request.id);
    }
    if (plan.access == Access::Run) {
        automation::Outcome out;
        out.action = automation::actionName(plan.action);
        out.id = request.id;
        const QList<extensions::CommandInfo> commands = m_workspace->extensions()->commands();
        const bool approved = std::ranges::any_of(commands, [&plan](const extensions::CommandInfo &c) {
            return c.extension == plan.extension && c.name == plan.command;
        });
        if (!approved) {
            out.error = u"not_approved"_s;
            out.message = plan.extension + u'/' + plan.command;
            toast(automationErrorText(out.error, out.message), u"warning"_s);
            return out;
        }
        showWindow();
        m_workspace->extensions()->runCommand(plan.extension, plan.command, plan.page, -1, plan.args.toVariantMap());
        out.ok = true;
        out.path = plan.page;
        return out;
    }
    VaultWriter writer(*m_workspace->vault());
    automation::Outcome outcome = automation::execute(plan, writer, request.id);
    if (!outcome.ok) {
        toast(automationErrorText(outcome.error, outcome.message), u"warning"_s);
        return outcome;
    }
    // Whole sentences (each language words them its own way).
    const bool today = plan.journal && plan.day == now().date();
    const QString day = plan.day.toString(Qt::ISODate);
    const QString page = paths::pageTitle(plan.path);
    const bool reminder = plan.kind == u"reminder" || plan.kind == u"alarm";
    const bool task = plan.kind == u"task" || plan.kind == u"due";
    QString text;
    if (plan.newPage)
        text = tr("New page “%1”").arg(page);
    else if (today)
        text = reminder ? tr("Reminder added to today's journal")
               : task   ? tr("Task added to today's journal")
                        : tr("Added to today's journal");
    else if (plan.journal)
        text = reminder ? tr("Reminder added to the journal of %1").arg(day)
               : task   ? tr("Task added to the journal of %1").arg(day)
                        : tr("Added to the journal of %1").arg(day);
    else
        text = reminder ? tr("Reminder added to “%1”").arg(page)
               : task   ? tr("Task added to “%1”").arg(page)
                        : tr("Added to “%1”").arg(page);
    const QVariantMap route = outcome.route.isEmpty() ? plan.route.toVariantMap() : outcome.route.toVariantMap();
    if (plan.open) {
        showWindow();
        navigate(m_workspace->resolveRoute(route));
        toast(text, u"success"_s);
    } else {
        m_automationToasts.insert(toastWithAction(text, u"success"_s, tr("Open")), route);
    }
    return outcome;
}

void AppController::callBack(const automation::Request &request, const automation::Outcome &outcome)
{
    const QString base = outcome.ok ? request.xSuccess : request.xError;
    if (base.isEmpty())
        return;
    const QUrl url(automation::withQuery(base, outcome.callbackParams()));
    if (!callbackAllowed(url)) {
        qCWarning(lcApp) << "Not opening a brulion:// request's callback address of the scheme" << url.scheme();
        return;
    }
    QDesktopServices::openUrl(url);
}

QVariantMap AppController::pendingAutomation() const
{
    if (!m_automation)
        return {};
    const automation::Plan &plan = m_automation->plan;
    const QString notebook = QFileInfo(m_workspace->rootPath()).fileName();
    const QString target = plan.journal || plan.path.isEmpty() ? QString() : paths::pageTitle(plan.path);
    return {{u"action"_s, automation::actionName(plan.action)},
            {u"access"_s, automation::accessName(plan.access)},
            {u"kind"_s, plan.kind},
            {u"notebook"_s, notebook},
            {u"target"_s, target},
            {u"path"_s, plan.path},
            {u"heading"_s, plan.insert.heading.trimmed()},
            {u"blockId"_s, plan.insert.blockId.trimmed()},
            {u"place"_s, plan.insert.place == automation::Place::Start ? u"start"_s : u"end"_s},
            {u"block"_s, plan.newPage ? plan.initialText : plan.insert.block},
            {u"journal"_s, plan.journal},
            {u"day"_s, plan.day.isValid() ? plan.day.toString(Qt::ISODate) : QString()},
            {u"today"_s, plan.day == now().date()},
            {u"newPage"_s, plan.newPage},
            {u"exists"_s, plan.exists},
            {u"extension"_s, plan.extension},
            {u"command"_s, plan.command},
            {u"when"_s, plan.reminder.isValid() ? plan.reminder.toString() : QString()},
            {u"source"_s, m_automation->request.source},
            {u"origin"_s, m_automation->origin}};
}

QVariantMap AppController::confirmAutomation()
{
    if (!m_automation)
        return {{u"ok"_s, false}, {u"error"_s, tr("Nothing to write.")}};
    const PendingAutomation pending = *std::exchange(m_automation, std::nullopt);
    Q_EMIT pendingAutomationChanged();
    if (m_status != u"ready" || !m_workspace->isOpen())
        return {{u"ok"_s, false}, {u"error"_s, tr("Open or create a notebook first.")}};
    // The plan again: the notebook may have changed while the dialog was open.
    m_workspace->finishIndexing();
    const automation::Context context{now(), notebook::contentLocale(m_settings->language())};
    const automation::Plan plan =
            automation::plan(pending.request, *m_workspace->vault(), *m_workspace->index(), context);
    if (!plan.ok()) {
        const QString message = automationErrorText(plan.error, plan.detail);
        toast(message, u"warning"_s);
        return {{u"ok"_s, false}, {u"error"_s, message}};
    }
    qCInfo(lcApp).noquote() << u"brulion:// %1 confirmed by the user"_s.arg(automation::actionName(plan.action));
    const automation::Outcome outcome = carryOutAutomation(pending.request, plan);
    return {{u"ok"_s, outcome.ok},
            {u"error"_s, outcome.ok ? QString() : automationErrorText(outcome.error, outcome.message)},
            {u"path"_s, outcome.path}};
}

void AppController::dismissAutomation()
{
    if (!m_automation)
        return;
    qCInfo(lcApp).noquote()
            << u"brulion:// %1 cancelled by the user"_s.arg(automation::actionName(m_automation->plan.action));
    m_automation.reset();
    Q_EMIT pendingAutomationChanged();
}

QString AppController::automationErrorText(const QString &error, const QString &detail) const
{
    const QString d = detail.left(120);
    if (error == u"too_long")
        return tr("the link is too long");
    if (error == u"no_action" || error == u"unknown_action")
        return tr("Brulion does not know what it asks for (%1)").arg(d);
    if (error == u"bad_encoding")
        return tr("it is not encoded correctly");
    if (error == u"duplicate")
        return tr("it gives “%1” twice").arg(d);
    if (error == u"unknown_parameter")
        return tr("The Brulion link has a part Brulion does not know: %1").arg(d);
    if (error == u"missing_parameter")
        return tr("The Brulion link misses “%1”.").arg(d);
    if (error == u"missing_text")
        return tr("The Brulion link has no text to write.");
    if (error == u"missing_title")
        return tr("The Brulion link gives no title for the new page.");
    if (error == u"page_not_found")
        return tr("No page “%1” in this notebook.").arg(d);
    if (error == u"not_a_page")
        return tr("“%1” is not a page.").arg(d);
    if (error == u"heading_not_found")
        return tr("The page has no such heading.");
    if (error == u"block_not_found")
        return tr("The page has no such block.");
    if (error == u"exists")
        return tr("A page with this name already exists: %1").arg(paths::pageTitle(d));
    if (error == u"template_not_found")
        return tr("No template “%1”.").arg(d);
    if (error == u"date_not_understood")
        return tr("Brulion does not understand the date in the link (%1).").arg(d);
    if (error == u"recurrence_not_understood")
        return tr("Brulion does not understand the repetition “%1”.").arg(d);
    if (error == u"bad_priority")
        return tr("Unknown priority “%1”.").arg(d);
    if (error == u"bad_args")
        return tr("The command's arguments in the link are not a JSON object.");
    if (error == u"unknown_view")
        return tr("Brulion has no view “%1”.").arg(d);
    if (error == u"not_utf8")
        return tr("The page is not UTF-8 text: Brulion does not change it.");
    if (error == u"conflict")
        return tr("The page kept changing: nothing was written.");
    if (error == u"links_off")
        return tr("Brulion links are turned off on this device (Settings → Automation).");
    if (error == u"key_required")
        return tr("Writing through Brulion links needs the notebook's automation key (Settings → Automation).");
    if (error == u"wrong_key")
        return tr("The Brulion link's automation key is not this notebook's: nothing was done.");
    if (error == u"unknown_notebook")
        return tr("No notebook “%1” on this device.").arg(d);
    if (error == u"ambiguous_notebook")
        return tr("Several notebooks are called “%1”: the link should name its folder.").arg(d);
    if (error == u"not_approved")
        return tr("The extension's command %1 is not available: the extension is not approved on this device, or "
                  "has no such command.")
                .arg(d);
    return tr("Could not write: %1").arg(d.isEmpty() ? error : d);
}

void AppController::registerWebLinks()
{
#ifdef Q_OS_WASM
    if (!platform::registerProtocolHandler())
        toast(tr("This browser cannot open web+brulion: links with Brulion."), u"warning"_s);
#endif
}

void AppController::publishAutomationTarget()
{
#ifdef Q_OS_ANDROID
    // Android's automation intents write while the app is closed (AutomationService.kt): into
    // the notebook open here, as the policy and the language of this device say.
    // The documentation never is the target: the user's notebook stays it while that is open.
    if (inDocsVault())
        return;
    QJsonObject target;
    if (m_workspace->isOpen() && m_status == u"ready") {
        const QString root = m_workspace->rootPath();
        target = QJsonObject{{u"root"_s, root},
                             {u"stateDir"_s, deviceStatePath(root)},
                             {u"name"_s, QFileInfo(root).fileName()},
                             {u"policy"_s, automationPolicy()},
                             {u"language"_s, m_settings->language()},
                             {u"followOutsideLinks"_s, m_settings->followsOutsideLinks()}};
    }
    platform::AndroidBridge::instance()->setAutomationTarget(target);
#endif
}

} // namespace brulion::shell
