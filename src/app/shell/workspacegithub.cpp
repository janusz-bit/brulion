#include "workspacegithub.h"

#include "dates/dates.h"
#include "github/github.h"
#include "log/categories.h"
#include "sync/http.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSaveFile>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

bool writeSecretFile(const QString &file, const QString &value)
{
    if (value.isEmpty())
        return !QFileInfo::exists(file) || QFile::remove(file);
    const QString dir = QFileInfo(file).absolutePath();
    if (!QDir().mkpath(dir))
        return false;
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    // Restricted before the secret is written; the rename keeps the permissions.
    out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    out.write(value.toUtf8());
    if (!out.commit())
        return false;
    QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QString readSecretFile(const QString &file)
{
    QFile in(file);
    return in.open(QIODevice::ReadOnly) ? QString::fromUtf8(in.readAll()).trimmed() : QString();
}

namespace {

const QString kLoginKey = u"github/login"_s;

using github::GitHubError;
using github::RepoLink;

QString titleOf(const VaultPath &path)
{
    return paths::isMarkdown(path) ? paths::pageTitle(path) : paths::basename(path);
}

} // namespace

WorkspaceGitHub::WorkspaceGitHub(QObject *parent) : QObject(parent)
{
    m_pullTimer.setInterval(PullIntervalMs);
    connect(&m_pullTimer, &QTimer::timeout, this, [this] {
        if (m_appActive && m_repo && !m_repo->busy())
            startPull(true);
    });
    m_startTimer.setSingleShot(true);
    m_startTimer.setInterval(StartDelayMs);
    connect(&m_startTimer, &QTimer::timeout, this, [this] {
        if (m_repo && !m_repo->busy())
            startPull(true);
    });
    m_changesTimer.setSingleShot(true);
    m_changesTimer.setInterval(700);
    connect(&m_changesTimer, &QTimer::timeout, this, &WorkspaceGitHub::refreshChanges);
}

WorkspaceGitHub::~WorkspaceGitHub()
{
    m_repo.reset(); // before the client it uses
}

// ---- wiring ------------------------------------------------------------------------------------------

sync::HttpTransport *WorkspaceGitHub::transport()
{
    if (!m_transport) {
        m_ownTransport = std::make_unique<sync::NetworkHttpTransport>();
        m_transport = m_ownTransport.get();
    }
    return m_transport;
}

github::GitHubClient *WorkspaceGitHub::client()
{
    if (!m_client) {
        m_client = std::make_unique<github::GitHubClient>(transport());
        m_client->setBrowser(m_browser);
        m_client->setToken(m_token);
    }
    return m_client.get();
}

void WorkspaceGitHub::setTransport(sync::HttpTransport *transport)
{
    m_repo.reset();
    m_client.reset();
    m_transport = transport;
    rebuild();
}

void WorkspaceGitHub::setSettingsAccess(std::function<QVariant(const QString &, const QVariant &)> get,
                                        std::function<void(const QString &, const QVariant &)> set)
{
    m_get = std::move(get);
    m_set = std::move(set);
    rebuild();
    Q_EMIT accountChanged();
}

void WorkspaceGitHub::setTokenFile(const QString &path)
{
    m_tokenFile = path;
    m_token = path.isEmpty() ? QString() : readSecretFile(path);
    if (m_client)
        m_client->setToken(m_token);
    Q_EMIT accountChanged();
}

void WorkspaceGitHub::setNotebooksRoot(const QString &path)
{
    m_notebooksRoot = QDir::cleanPath(path);
}

void WorkspaceGitHub::setBrowser(bool browser)
{
    m_browser = browser;
    if (m_client)
        m_client->setBrowser(browser);
}

void WorkspaceGitHub::setLanguage(const QString &language)
{
    m_language = language == u"en" ? u"en"_s : u"pl"_s;
    Q_EMIT statusChanged();
    Q_EMIT changesChanged();
}

void WorkspaceGitHub::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
    if (m_repo)
        m_repo->setClock(m_clock);
}

QDateTime WorkspaceGitHub::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

void WorkspaceGitHub::setAutomatic(bool on)
{
    m_automatic = on;
    if (!on) {
        m_pullTimer.stop();
        m_startTimer.stop();
    } else if (m_repo) {
        m_pullTimer.start();
    }
}

QVariant WorkspaceGitHub::setting(const QString &key, const QVariant &fallback) const
{
    return m_get ? m_get(key, fallback) : fallback;
}

void WorkspaceGitHub::setSetting(const QString &key, const QVariant &value)
{
    if (m_set)
        m_set(key, value);
}

QString WorkspaceGitHub::notebookKey(const QString &notebookPath, const QString &name)
{
    return u"github/notebooks/"_s + Vault::idForPath(notebookPath) + u'/' + name;
}

std::optional<RepoLink> WorkspaceGitHub::storedLink(const QString &notebookPath) const
{
    const QString repository = setting(notebookKey(notebookPath, u"repository"_s), QString()).toString().trimmed();
    const qsizetype slash = repository.indexOf(u'/');
    if (slash <= 0)
        return std::nullopt;
    RepoLink link;
    link.repo = github::RepoId{repository.left(slash), repository.mid(slash + 1)};
    link.branch = setting(notebookKey(notebookPath, u"branch"_s), QString()).toString().trimmed();
    link.folder = setting(notebookKey(notebookPath, u"folder"_s), QString()).toString().trimmed();
    if (!link.isValid() || !github::isValidOwner(link.repo.owner) || !github::isValidRepoName(link.repo.name))
        return std::nullopt;
    return link;
}

void WorkspaceGitHub::storeLink(const QString &notebookPath, const std::optional<RepoLink> &link)
{
    setSetting(notebookKey(notebookPath, u"repository"_s), link ? link->repo.fullName() : QString());
    setSetting(notebookKey(notebookPath, u"branch"_s), link ? link->branch : QString());
    setSetting(notebookKey(notebookPath, u"folder"_s), link ? link->folder : QString());
    setSetting(notebookKey(notebookPath, u"path"_s), link ? QDir::cleanPath(notebookPath) : QString());
}

void WorkspaceGitHub::attach(Vault *vault)
{
    detach();
    m_vault = vault;
    if (vault)
        m_vaultConnection = connect(vault, &Vault::changed, this, [this] { scheduleChanges(); });
    rebuild();
}

void WorkspaceGitHub::detach()
{
    disconnect(m_vaultConnection);
    m_pullTimer.stop();
    m_startTimer.stop();
    m_changesTimer.stop();
    const bool had = m_repo != nullptr;
    m_repo.reset(); // a running operation ends without its callback
    m_vault.clear();
    m_operation.clear();
    m_changes.clear();
    m_lastError.clear();
    m_lastErrorCode.clear();
    m_lastReport.clear();
    m_offline = false;
    if (had) {
        Q_EMIT linkChanged();
        Q_EMIT statusChanged();
        Q_EMIT changesChanged();
    }
}

void WorkspaceGitHub::rebuild()
{
    const std::optional<RepoLink> link = m_vault ? storedLink(m_vault->rootPath()) : std::nullopt;
    if (m_repo && link && m_repo->link() == *link)
        return;
    m_repo.reset();
    m_operation.clear();
    m_pullTimer.stop();
    m_startTimer.stop();
    if (link && m_vault) {
        m_repo = std::make_unique<github::RepoSync>(m_vault.data(), client(), m_vault->store());
        m_repo->setClock(m_clock);
        m_repo->setLink(*link);
        connect(m_repo.get(), &github::RepoSync::progress, this, [this](const QString &phase, int done, int total) {
            m_phase = phase;
            m_done = done;
            m_total = total;
            Q_EMIT statusChanged();
        });
        connect(m_repo.get(), &github::RepoSync::stateChanged, this, [this] {
            Q_EMIT statusChanged();
            scheduleChanges();
        });
        if (m_automatic) {
            m_startTimer.start();
            m_pullTimer.start();
        }
    }
    m_changes.clear();
    Q_EMIT linkChanged();
    Q_EMIT statusChanged();
    refreshChanges();
}

void WorkspaceGitHub::setAppActive(bool active)
{
    const bool back = active && !m_appActive;
    m_appActive = active;
    if (back && m_repo && m_automatic && !m_repo->busy()
        && now().toMSecsSinceEpoch() - m_repo->lastPullMs() > ForegroundPullAfterMs) {
        startPull(true);
    }
}

// ---- state ------------------------------------------------------------------------------------------

bool WorkspaceGitHub::linked() const
{
    return m_repo != nullptr;
}

QString WorkspaceGitHub::repository() const
{
    return m_repo ? m_repo->link().repo.fullName() : QString();
}

QString WorkspaceGitHub::branch() const
{
    return m_repo ? m_repo->link().branch : QString();
}

QString WorkspaceGitHub::folder() const
{
    return m_repo ? m_repo->link().folder : QString();
}

QString WorkspaceGitHub::webUrl() const
{
    if (!m_repo)
        return {};
    const RepoLink link = m_repo->link();
    return github::webUrl(link.repo.owner, link.repo.name, link.branch, link.folder).toString(QUrl::FullyEncoded);
}

QString WorkspaceGitHub::linkText() const
{
    if (!m_repo)
        return {};
    const RepoLink link = m_repo->link();
    QString text = link.repo.fullName() + u" · "_s + link.branch;
    if (!link.folder.isEmpty())
        text += u" · "_s + link.folder;
    return text;
}

QString WorkspaceGitHub::login() const
{
    return signedIn() ? setting(kLoginKey, QString()).toString() : QString();
}

QString WorkspaceGitHub::tokenUrl() const
{
    return github::newTokenUrl().toString(QUrl::FullyEncoded);
}

bool WorkspaceGitHub::busy() const
{
    return !m_operation.isEmpty();
}

QString WorkspaceGitHub::state() const
{
    if (!m_repo)
        return u"disabled"_s;
    if (m_operation == u"pull")
        return u"pulling"_s;
    if (m_operation == u"commit")
        return u"committing"_s;
    if (m_operation == u"propose")
        return u"proposing"_s;
    if (m_offline)
        return u"offline"_s;
    if (!m_lastErrorCode.isEmpty())
        return u"error"_s;
    return u"idle"_s;
}

QString WorkspaceGitHub::progressText() const
{
    if (m_operation.isEmpty())
        return {};
    if (m_phase == u"download" && m_total > 0)
        return tr("Downloading %1 of %2…").arg(std::min(m_done + 1, m_total)).arg(m_total);
    if (m_phase == u"upload" && m_total > 0)
        return tr("Uploading %1 of %2…").arg(std::min(m_done + 1, m_total)).arg(m_total);
    if (m_phase == u"tree")
        return tr("Comparing the files…");
    if (m_phase == u"fork")
        return tr("Preparing your copy of the repository…");
    if (m_phase == u"proposal")
        return tr("Opening the pull request…");
    if (m_phase == u"commit")
        return m_operation == u"propose" ? tr("Writing the proposal…") : tr("Committing…");
    return m_operation == u"pull"     ? tr("Getting the newest version from GitHub…")
           : m_operation == u"commit" ? tr("Committing…")
                                      : tr("Proposing the changes…");
}

qreal WorkspaceGitHub::progress() const
{
    if (m_operation.isEmpty() || m_total <= 0 || (m_phase != u"download" && m_phase != u"upload"))
        return -1;
    return std::clamp(qreal(m_done) / qreal(m_total), 0.0, 1.0);
}

QString WorkspaceGitHub::human(const QDateTime &time) const
{
    if (!time.isValid())
        return {};
    const QDateTime local = time.toLocalTime();
    return dates::formatHuman(DateSpec{local.date(), QTime(local.time().hour(), local.time().minute())},
                              m_language == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish, now().date());
}

QString WorkspaceGitHub::lastPullText() const
{
    const qint64 ms = m_repo ? m_repo->lastPullMs() : 0;
    return ms > 0 ? human(QDateTime::fromMSecsSinceEpoch(ms)) : QString();
}

QString WorkspaceGitHub::headText() const
{
    return m_repo && !m_repo->headCommit().isEmpty() ? github::shortSha(m_repo->headCommit()) : QString();
}

QString WorkspaceGitHub::headUrl() const
{
    if (!m_repo || m_repo->headCommit().isEmpty())
        return {};
    const RepoLink link = m_repo->link();
    return github::commitUrl(link.repo.owner, link.repo.name, m_repo->headCommit()).toString(QUrl::FullyEncoded);
}

bool WorkspaceGitHub::pushKnown() const
{
    const std::optional<github::Repository> repo = m_repo ? m_repo->repository() : std::nullopt;
    return repo && repo->permissionsKnown && signedIn();
}

bool WorkspaceGitHub::canPush() const
{
    const std::optional<github::Repository> repo = m_repo ? m_repo->repository() : std::nullopt;
    // Unknown (not asked yet): offered — GitHub says "read-only" when it is so.
    return signedIn() && (!repo || !repo->permissionsKnown || (repo->canPush && !repo->archived));
}

QString WorkspaceGitHub::statusText() const
{
    if (!m_repo)
        return {};
    if (busy())
        return progressText();
    if (m_offline)
        return tr("Offline — Brulion checks GitHub again later");
    if (!m_lastErrorCode.isEmpty())
        return m_lastError;
    if (!m_repo->hasPulled() && m_repo->lastPullMs() <= 0)
        return tr("Not downloaded from GitHub yet");
    const QString when = lastPullText();
    const int changes = changeCount();
    if (changes > 0) {
        return when.isEmpty() ? tr("%n change(s) not on GitHub yet", "", changes)
                              : tr("%n change(s) not on GitHub yet · checked %1", "", changes).arg(when);
    }
    return when.isEmpty() ? tr("The same as on GitHub") : tr("The same as on GitHub · checked %1").arg(when);
}

QVariantMap WorkspaceGitHub::appStatus() const
{
    if (!m_repo)
        return {};
    // As WorkspaceSync::appStatus() (ShellSyncFooter), with an icon of its own.
    QString s = u"idle"_s;
    if (busy())
        s = u"syncing"_s;
    else if (m_offline)
        s = u"offline"_s;
    else if (!m_lastErrorCode.isEmpty())
        s = u"error"_s;
    return {{u"state"_s, s}, {u"text"_s, statusText()}, {u"icon"_s, u"git-branch"_s}};
}

QString WorkspaceGitHub::kindText(const QString &kind) const
{
    if (kind == u"added")
        return tr("New");
    if (kind == u"deleted")
        return tr("Removed");
    return tr("Changed");
}

QVariantMap WorkspaceGitHub::proposal() const
{
    const std::optional<github::Proposal> p = m_repo ? m_repo->proposal() : std::nullopt;
    if (!p)
        return {};
    QString stateText = tr("Open");
    if (p->state == u"merged")
        stateText = tr("Merged");
    else if (p->state == u"closed")
        stateText = tr("Closed");
    return {{u"number"_s, p->number},
            {u"url"_s, p->url.toString(QUrl::FullyEncoded)},
            {u"title"_s, p->title},
            {u"state"_s, p->state},
            {u"stateText"_s, stateText},
            {u"open"_s, p->isOpen()},
            {u"files"_s, int(p->files.size())}};
}

void WorkspaceGitHub::scheduleChanges()
{
    if (m_repo)
        m_changesTimer.start();
}

void WorkspaceGitHub::refreshChanges()
{
    QVariantList list;
    if (m_repo) {
        const QList<github::LocalChange> changes = m_repo->localChanges();
        list.reserve(changes.size());
        for (const github::LocalChange &change : changes) {
            const QString kind = github::changeKindName(change.kind);
            list.append(QVariantMap{{u"path"_s, change.path},
                                    {u"title"_s, titleOf(change.path)},
                                    {u"kind"_s, kind},
                                    {u"kindText"_s, kindText(kind)},
                                    {u"proposed"_s, change.proposed}});
        }
    }
    if (list == m_changes)
        return;
    m_changes = list;
    Q_EMIT changesChanged();
    Q_EMIT statusChanged();
}

QString WorkspaceGitHub::defaultCommitMessage(int count) const
{
    return tr("Update %n note(s) from Brulion", "", std::max(1, count));
}

QString WorkspaceGitHub::defaultProposalTitle(int count) const
{
    return tr("Changes to %n note(s)", "", std::max(1, count));
}

// ---- texts ------------------------------------------------------------------------------------------

QString WorkspaceGitHub::errorText(const QString &code, const QString &message, qint64 resetAt, int retryAfter) const
{
    if (code == u"auth")
        return tr("GitHub did not accept the token. Make a new one and save it again.");
    if (code == u"forbidden")
        return tr("The token may not do this here. A classic token needs the “repo” scope (“public_repo” for public "
                  "repositories); a fine-grained one, access to this repository's contents and pull requests.");
    if (code == u"rate-limited") {
        if (resetAt > 0) {
            return tr("GitHub's limit of requests is used up until %1. With a token the limit is much higher.")
                    .arg(human(QDateTime::fromMSecsSinceEpoch(resetAt)));
        }
        if (retryAfter > 0)
            return tr("GitHub asks to wait a moment: try again in %n minute(s).", "",
                      std::max(1, (retryAfter + 59) / 60));
        return tr("GitHub's limit of requests is used up for now. With a token the limit is much higher.");
    }
    if (code == u"not-found")
        return signedIn() ? tr("The repository, branch or folder was not found on GitHub — or the token cannot see it.")
                          : tr("The repository, branch or folder was not found on GitHub. A private repository needs a "
                               "token.");
    if (code == u"not-fast-forward")
        return tr("The branch changed on GitHub meanwhile. Try again.");
    if (code == u"validation" || code == u"conflict")
        return message.isEmpty() ? tr("GitHub refused the request.")
                                 : tr("GitHub refused the request: %1").arg(message);
    if (code == u"server")
        return tr("GitHub reported an error. Try again later.");
    if (code == u"network")
        return tr("Cannot reach GitHub. Check the connection.");
    if (code == u"timeout")
        return tr("GitHub did not answer in time.");
    if (code == u"parse")
        return tr("GitHub sent an answer Brulion does not understand.");
    if (code == u"integrity")
        return tr("A file was damaged on the way and was not saved. Try again.");
    if (code == u"too-large")
        return tr("This folder has more files than GitHub lists at once. Choose a smaller folder.");
    if (code == u"cancelled")
        return tr("Stopped.");
    if (code == u"busy")
        return tr("Another GitHub operation is running.");
    if (code == u"not-linked")
        return tr("This notebook is not linked to GitHub.");
    if (code == u"vault-unavailable")
        return tr("The notebook folder is not available (an unmounted card or drive?).");
    if (code == u"remote-empty")
        return tr("The folder on GitHub has no files any more, although it had notes: nothing was removed here. Check "
                  "the branch and the folder, or link the notebook again.");
    if (code == u"conflicts")
        return tr("Merge the conflicting notes first (the sync center's Conflicts tab), then commit.");
    if (code == u"pull-incomplete")
        return tr("Some files could not be brought up to date. Get the newest version again first.");
    if (code == u"local-empty")
        return tr("The notes downloaded from GitHub are all gone from this device, so nothing was committed (is the "
                  "notebook's storage missing?).");
    if (code == u"no-token")
        return tr("Committing and proposing changes need a token: save one in the GitHub settings below.");
    if (code == u"read-only")
        return tr("Your token may not write to this repository: propose the changes instead.");
    if (code == u"proposal-moved")
        return tr("Someone else changed the proposal on GitHub. Open it there, or start a new one.");
    return message.isEmpty() ? tr("Something went wrong with GitHub.") : message;
}

QString WorkspaceGitHub::problemText(const QString &code) const
{
    if (code == u"too-large")
        return tr("Too large (over 50 MB): left on GitHub.");
    if (code == u"lfs")
        return tr("Kept in Git LFS: Brulion does not download it.");
    if (code == u"symlink")
        return tr("A symbolic link: left on GitHub.");
    if (code == u"submodule")
        return tr("Another repository (a submodule): left on GitHub.");
    if (code == u"case-clash")
        return tr("Another file has the same name in other letter case: left on GitHub.");
    if (code == u"name")
        return tr("Two files whose names differ only in how their letters are encoded: one is left on GitHub.");
    if (code == u"download-failed")
        return tr("Could not be downloaded; tried again next time.");
    if (code == u"integrity")
        return tr("Damaged on the way; downloaded again next time.");
    if (code == u"read-failed")
        return tr("Could not be read on this device.");
    if (code == u"write-failed")
        return tr("Could not be saved on this device.");
    if (code == u"changed-meanwhile")
        return tr("Changed here meanwhile; merged next time.");
    if (code == u"conflict-copy-failed")
        return tr("GitHub's version could not be saved beside it.");
    return code;
}

void WorkspaceGitHub::setError(const QString &code, const QString &text)
{
    m_lastErrorCode = code;
    m_lastError = text;
}

QVariantList WorkspaceGitHub::problems(const QList<github::FileProblem> &list) const
{
    QVariantList out;
    out.reserve(list.size());
    for (const github::FileProblem &problem : list) {
        out.append(QVariantMap{{u"path"_s, problem.path},
                               {u"title"_s, titleOf(problem.path)},
                               {u"code"_s, problem.code},
                               {u"text"_s, problemText(problem.code)},
                               {u"message"_s, problem.message}});
    }
    return out;
}

// ---- operations --------------------------------------------------------------------------------------

void WorkspaceGitHub::pull()
{
    startPull(false);
}

void WorkspaceGitHub::startPull(bool automatic)
{
    if (!m_repo || busy())
        return;
    m_operation = u"pull"_s;
    m_phase.clear();
    m_done = m_total = 0;
    Q_EMIT statusChanged();
    QPointer<WorkspaceGitHub> self(this);
    m_repo->pull([self, automatic](const github::PullReport &report) {
        if (self)
            self->onPulled(report, automatic);
    });
}

void WorkspaceGitHub::onPulled(const github::PullReport &report, bool automatic)
{
    m_operation.clear();
    m_phase.clear();
    const QString code = !report.refused.isEmpty() ? report.refused : github::errorCodeName(report.error.code);
    m_offline = report.error.code == GitHubError::Code::Network || report.error.code == GitHubError::Code::Timeout;
    if (m_offline || code.isEmpty())
        setError({}, {});
    else
        setError(code, errorText(code, report.error.message, report.error.resetAt, report.error.retryAfterSeconds));

    const QVariantList skipped = problems(report.skipped);
    const QVariantList errors = problems(report.errors);
    QString text;
    if (code.isEmpty()) {
        QStringList parts;
        if (report.downloaded > 0)
            parts.append(tr("%n file(s) downloaded", "", report.downloaded));
        if (report.deletedLocal > 0)
            parts.append(tr("%n removed", "", report.deletedLocal));
        if (report.renamed > 0)
            parts.append(tr("%n renamed", "", report.renamed));
        if (report.merged > 0)
            parts.append(tr("%n merged", "", report.merged));
        if (!report.conflicts.isEmpty())
            parts.append(tr("%n with conflicts", "", int(report.conflicts.size())));
        if (!errors.isEmpty())
            parts.append(tr("%n failed", "", int(errors.size())));
        text = parts.isEmpty() ? tr("No changes on GitHub") : parts.join(u", "_s);
    }
    m_lastReport = {{u"operation"_s, u"pull"_s},
                    {u"text"_s, code.isEmpty() ? text : m_lastError},
                    {u"downloaded"_s, report.downloaded},
                    {u"deletedLocal"_s, report.deletedLocal},
                    {u"renamed"_s, report.renamed},
                    {u"merged"_s, report.merged},
                    {u"kept"_s, report.kept},
                    {u"conflicts"_s, int(report.conflicts.size())},
                    {u"skipped"_s, skipped},
                    {u"errors"_s, errors},
                    {u"folderMissing"_s, report.folderMissing},
                    {u"emptyRepository"_s, report.emptyRepository}};
    refreshChanges();
    Q_EMIT statusChanged();
    Q_EMIT changesChanged();

    if (!code.isEmpty()) {
        // Automatic pulls stay quiet offline (the status says so); the user's own are told.
        if (!automatic || !m_offline)
            Q_EMIT operationFinished(u"pull"_s, false, m_offline ? errorText(code) : m_lastError, u"error"_s);
        return;
    }
    if (!report.conflicts.isEmpty()) {
        Q_EMIT operationFinished(u"pull"_s, true,
                                 tr("%n note(s) changed both here and on GitHub. Both versions are kept: merge them in "
                                    "the sync center.",
                                    "", int(report.conflicts.size())),
                                 u"warning"_s);
        return;
    }
    const int changed = report.downloaded + report.deletedLocal + report.renamed + report.merged;
    if (changed > 0 || !automatic)
        Q_EMIT operationFinished(u"pull"_s, true, changed > 0 ? tr("From GitHub: %1").arg(text) : text, u"success"_s);
}

std::optional<QStringList> WorkspaceGitHub::beginSend(const QString &operation, const QStringList &leaveOut)
{
    if (!m_repo || busy())
        return std::nullopt;
    QStringList selected; // {} = every change
    if (!leaveOut.isEmpty()) {
        for (const QVariant &row : std::as_const(m_changes)) {
            const QString path = row.toMap().value(u"path"_s).toString();
            if (!leaveOut.contains(path))
                selected.append(path);
        }
        if (selected.isEmpty()) {
            Q_EMIT operationFinished(operation, false, tr("Choose at least one note."), u"warning"_s);
            return std::nullopt;
        }
    }
    m_operation = operation;
    m_phase.clear();
    Q_EMIT statusChanged();
    return selected;
}

void WorkspaceGitHub::commit(const QString &message, const QStringList &leaveOut)
{
    const std::optional<QStringList> paths = beginSend(u"commit"_s, leaveOut);
    if (!paths)
        return;
    QPointer<WorkspaceGitHub> self(this);
    m_repo->commit(message, *paths, [self](const github::CommitReport &report) {
        if (self)
            self->onCommitted(report, false);
    });
}

void WorkspaceGitHub::propose(const QString &title, const QString &body, const QStringList &leaveOut)
{
    const std::optional<QStringList> paths = beginSend(u"propose"_s, leaveOut);
    if (!paths)
        return;
    QPointer<WorkspaceGitHub> self(this);
    m_repo->propose(title, body, *paths, [self](const github::CommitReport &report) {
        if (self)
            self->onCommitted(report, true);
    });
}

void WorkspaceGitHub::onCommitted(const github::CommitReport &report, bool proposal)
{
    const QString operation = proposal ? u"propose"_s : u"commit"_s;
    m_operation.clear();
    m_phase.clear();
    QString code = report.refused;
    if (code.isEmpty())
        code = github::errorCodeName(report.error.code);
    if (code.isEmpty() && report.pull.failed())
        code = !report.pull.refused.isEmpty() ? report.pull.refused : github::errorCodeName(report.pull.error.code);
    const GitHubError &error = report.error.isError() ? report.error : report.pull.error;
    m_offline = error.code == GitHubError::Code::Network || error.code == GitHubError::Code::Timeout;
    const QString failure =
            code.isEmpty() ? QString() : errorText(code, error.message, error.resetAt, error.retryAfterSeconds);
    // A refusal is about this attempt, not the notebook's state: the status stays as it was.
    const bool refusal = !report.refused.isEmpty() && report.refused != u"busy" && !report.pull.failed();
    if (refusal)
        setError({}, {});
    else
        setError(code, failure);
    QString text;
    const RepoLink link = m_repo ? m_repo->link() : RepoLink();
    QString commitUrl;
    if (code.isEmpty() && report.nothingToCommit) {
        text = tr("Nothing to commit: the notes are the same as on GitHub.");
    } else if (code.isEmpty() && proposal && report.proposal) {
        text = report.forked ? tr("Proposed as pull request #%1 from your copy of the repository.")
                                       .arg(report.proposal->number)
                             : tr("Proposed as pull request #%1.").arg(report.proposal->number);
    } else if (code.isEmpty()) {
        text = tr("%n file(s) committed to “%1”.", "", int(report.committed.size())).arg(link.branch);
        commitUrl = github::commitUrl(link.repo.owner, link.repo.name, report.commit).toString(QUrl::FullyEncoded);
    }
    m_lastReport = {{u"operation"_s, operation},
                    {u"text"_s, code.isEmpty() ? text : failure},
                    {u"committed"_s, int(report.committed.size())},
                    {u"commit"_s, report.commit},
                    {u"commitUrl"_s, commitUrl},
                    {u"downloaded"_s, report.pull.downloaded},
                    {u"merged"_s, report.pull.merged},
                    {u"conflicts"_s, int(report.pull.conflicts.size() + report.conflictsPending.size())},
                    {u"skipped"_s, QVariantList()},
                    {u"errors"_s, QVariantList()}};
    refreshChanges();
    Q_EMIT statusChanged();
    Q_EMIT changesChanged();
    if (!code.isEmpty())
        Q_EMIT operationFinished(operation, false, failure, refusal ? u"warning"_s : u"error"_s);
    else
        Q_EMIT operationFinished(operation, true, text, report.nothingToCommit ? u"info"_s : u"success"_s);
}

void WorkspaceGitHub::cancel()
{
    if (m_repo)
        m_repo->cancel();
}

void WorkspaceGitHub::refreshProposal()
{
    if (!m_repo || busy() || !m_repo->proposal())
        return;
    QPointer<WorkspaceGitHub> self(this);
    m_repo->refreshProposal([self](const GitHubError &, const std::optional<github::Proposal> &) {
        if (!self)
            return;
        Q_EMIT self->changesChanged();
        self->refreshChanges();
    });
}

void WorkspaceGitHub::forgetProposal()
{
    if (!m_repo)
        return;
    m_repo->forgetProposal();
    refreshChanges();
    Q_EMIT changesChanged();
}

// ---- linking --------------------------------------------------------------------------------------------

QVariantMap WorkspaceGitHub::parseAddress(const QString &address) const
{
    const std::optional<github::RepoAddress> parsed = github::parseRepoAddress(address);
    if (!parsed) {
        return {{u"valid"_s, false},
                {u"error"_s,
                 address.trimmed().isEmpty() ? QString() : tr("This is not the address of a GitHub repository.")}};
    }
    const auto [branch, folder] = github::splitRefPath(parsed->refPath, {}, parsed->file);
    return {{u"valid"_s, true},        {u"owner"_s, parsed->owner},
            {u"name"_s, parsed->name}, {u"fullName"_s, github::fullName(parsed->owner, parsed->name)},
            {u"branch"_s, branch},     {u"folder"_s, folder},
            {u"file"_s, parsed->file}, {u"error"_s, QString()}};
}

QString WorkspaceGitHub::linkErrorText(const GitHubError &error) const
{
    return errorText(github::errorCodeName(error.code), error.message, error.resetAt, error.retryAfterSeconds);
}

void WorkspaceGitHub::resolveLink(const QString &address, const QString &branchText, const QString &folderText,
                                  Resolved done)
{
    const std::optional<github::RepoAddress> parsed = github::parseRepoAddress(address);
    if (!parsed) {
        done(false, {}, tr("This is not the address of a GitHub repository."), false);
        return;
    }
    const std::optional<QString> typedFolder = github::normalizeFolder(folderText);
    if (!typedFolder) {
        done(false, {}, tr("This folder name cannot be used: no “..”, “.” or backslashes."), false);
        return;
    }
    const QString typedBranch = branchText.trimmed();
    if (!typedBranch.isEmpty() && !github::isValidBranchName(typedBranch)) {
        done(false, {}, tr("“%1” cannot be the name of a branch.").arg(typedBranch), false);
        return;
    }
    m_checkingLink = true;
    Q_EMIT checkingChanged();
    const github::RepoId id{parsed->owner, parsed->name};
    QPointer<WorkspaceGitHub> self(this);
    const auto finish = [self, done = std::move(done)](bool ok, const RepoLink &link, const QString &message,
                                                       bool empty) {
        if (!self)
            return;
        self->m_checkingLink = false;
        Q_EMIT self->checkingChanged();
        done(ok, link, message, empty);
    };
    client()->repository(id, [self, parsed, typedBranch, typedFolder, finish](const GitHubError &error,
                                                                              const github::Repository &repo) {
        if (!self)
            return;
        if (error.isError()) {
            finish(false, {}, self->linkErrorText(error), false);
            return;
        }
        // The branch: as typed, else the address's (GitHub tells which segments name it), else the default.
        const auto withBranch = [self, repo, typedFolder, finish](const QString &branch, const QString &addressFolder) {
            RepoLink link;
            link.repo = repo.id; // as GitHub spells it (a renamed repository answers with its new name)
            link.branch = branch.isEmpty() ? repo.defaultBranch : branch;
            link.folder = typedFolder->isEmpty() ? addressFolder : *typedFolder;
            if (link.branch.isEmpty())
                link.branch = u"main"_s;
            self->client()->branch(
                    link.repo, link.branch, {},
                    [self, link, finish](const GitHubError &branchError, const github::BranchHead &, bool) {
                        if (!self)
                            return;
                        if (!branchError.isError()) {
                            finish(true, link, {}, false);
                            return;
                        }
                        if (branchError.code != GitHubError::Code::NotFound
                            && branchError.code != GitHubError::Code::EmptyRepository) {
                            finish(false, {}, self->linkErrorText(branchError), false);
                            return;
                        }
                        // No such branch — or an empty repository, which has none yet.
                        self->client()->branchNames(
                                link.repo, {},
                                [self, link, finish](const GitHubError &listError, const QStringList &names) {
                                    if (!self)
                                        return;
                                    if (!listError.isError() && names.isEmpty()) {
                                        finish(true, link, {}, true);
                                        return;
                                    }
                                    finish(false, {}, tr("The repository has no branch “%1”.").arg(link.branch), false);
                                });
                    });
        };
        if (!typedBranch.isEmpty() || parsed->refPath.isEmpty()) {
            const QString addressFolder = github::splitRefPath(parsed->refPath, {typedBranch}, parsed->file).second;
            withBranch(typedBranch, typedBranch.isEmpty() ? QString() : addressFolder);
            return;
        }
        self->client()->branchNames(
                repo.id, parsed->refPath.first(),
                [self, parsed, withBranch, finish](const GitHubError &listError, const QStringList &names) {
                    if (!self)
                        return;
                    if (listError.isError() && listError.isFatal()) {
                        finish(false, {}, self->linkErrorText(listError), false);
                        return;
                    }
                    const auto [branch, addressFolder] = github::splitRefPath(parsed->refPath, names, parsed->file);
                    withBranch(branch, addressFolder);
                });
    });
}

void WorkspaceGitHub::linkNotebook(const QString &address, const QString &branch, const QString &folder)
{
    if (!m_vault) {
        Q_EMIT linkFinished(false, tr("Open a notebook first."));
        return;
    }
    const QString notebook = m_vault->rootPath();
    QPointer<WorkspaceGitHub> self(this);
    resolveLink(address, branch, folder,
                [self, notebook](bool ok, const RepoLink &link, const QString &message, bool empty) {
                    if (!self)
                        return;
                    if (!ok) {
                        Q_EMIT self->linkFinished(false, message);
                        return;
                    }
                    if (!self->m_vault || self->m_vault->rootPath() != notebook) {
                        Q_EMIT self->linkFinished(false, tr("Another notebook was opened meanwhile."));
                        return;
                    }
                    self->storeLink(notebook, link);
                    qCInfo(logging::lcSync).noquote() << u"GitHub: %1 linked to %2@%3 (%4)"_s.arg(
                            QFileInfo(notebook).fileName(), link.repo.fullName(), link.branch,
                            link.folder.isEmpty() ? u"the whole repository"_s : link.folder);
                    self->setError({}, {});
                    self->rebuild();
                    Q_EMIT self->linkFinished(true,
                                              empty ? tr("Linked. The repository is empty: your first commit fills it.")
                                                    : tr("Linked. Brulion now gets the newest version."));
                });
}

void WorkspaceGitHub::unlink()
{
    if (!m_vault)
        return;
    storeLink(m_vault->rootPath(), std::nullopt);
    if (m_repo)
        m_repo->resetState();
    setError({}, {});
    m_lastReport.clear();
    rebuild();
}

QString WorkspaceGitHub::suggestedFolder(const QString &address, const QString &folder) const
{
    const std::optional<github::RepoAddress> parsed = github::parseRepoAddress(address);
    if (!parsed || m_notebooksRoot.isEmpty())
        return {};
    QString inner = github::normalizeFolder(folder).value_or(QString());
    if (inner.isEmpty() && !parsed->refPath.isEmpty())
        inner = github::splitRefPath(parsed->refPath, {}, parsed->file).second;
    QString name = parsed->name;
    if (!inner.isEmpty())
        name += u" – "_s + paths::basename(inner);
    name = paths::sanitizeFileName(name);
    if (name.isEmpty())
        name = parsed->name;
    return QDir::cleanPath(m_notebooksRoot + u'/' + parsed->owner + u'/' + name);
}

void WorkspaceGitHub::openRepository(const QString &address, const QString &branch, const QString &folder,
                                     const QString &localFolder)
{
    QPointer<WorkspaceGitHub> self(this);
    const QString chosen = QDir::cleanPath(localFolder.trimmed());
    resolveLink(address, branch, folder,
                [self, address, chosen](bool ok, const RepoLink &link, const QString &message, bool) {
                    if (!self)
                        return;
                    if (!ok) {
                        Q_EMIT self->prepareFailed(message);
                        return;
                    }
                    QString path =
                            chosen.isEmpty() || chosen == u"." ? self->suggestedFolder(address, link.folder) : chosen;
                    if (path.isEmpty()) {
                        Q_EMIT self->prepareFailed(tr("Choose a folder for the notebook."));
                        return;
                    }
                    // A folder of the app's storage taken by another link (or by notes of its
                    // own): the next free name. One the user chose is used as it is.
                    if (chosen.isEmpty() || chosen == u".") {
                        const QString base = path;
                        for (int n = 2; n < 100; ++n) {
                            const std::optional<RepoLink> stored = self->storedLink(path);
                            const bool empty =
                                    !QFileInfo::exists(path)
                                    || QDir(path).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
                            if ((stored && *stored == link) || (!stored && empty))
                                break;
                            path = base + u' ' + QString::number(n);
                        }
                    }
                    self->storeLink(path, link);
                    qCInfo(logging::lcSync).noquote() << u"GitHub: %1@%2 (%3) opens as the notebook %4"_s.arg(
                            link.repo.fullName(), link.branch,
                            link.folder.isEmpty() ? u"the whole repository"_s : link.folder,
                            QDir::toNativeSeparators(path));
                    Q_EMIT self->notebookPrepared(path);
                });
}

// ---- the token -----------------------------------------------------------------------------------------

void WorkspaceGitHub::saveToken(const QString &rawToken)
{
    const QString token = rawToken.trimmed();
    if (token.isEmpty()) {
        Q_EMIT tokenChecked(false, tr("Paste the token first."));
        return;
    }
    if (token.contains(u' ') || token.size() > 255) {
        Q_EMIT tokenChecked(false, tr("This does not look like a GitHub token."));
        return;
    }
    m_checkingToken = true;
    Q_EMIT checkingChanged();
    // A client of its own: the saved token stays in use until this one is known to work. Only the
    // newest check counts: after a later saveToken() or forgetToken() its answer saves nothing.
    const int check = ++m_tokenCheck;
    auto *probe = new github::GitHubClient(transport(), this);
    probe->setBrowser(m_browser);
    probe->setToken(token);
    QPointer<WorkspaceGitHub> self(this);
    probe->user([self, probe, check, token](const GitHubError &error, const github::User &user) {
        probe->deleteLater(); // (runs only while the probe, a child of `self`, is alive)
        if (!self || self->m_tokenCheck != check)
            return;
        self->m_checkingToken = false;
        Q_EMIT self->checkingChanged();
        if (error.isError()) {
            Q_EMIT self->tokenChecked(false, error.code == GitHubError::Code::Auth
                                                     ? WorkspaceGitHub::tr("GitHub did not accept this token.")
                                                     : self->linkErrorText(error));
            return;
        }
        if (!self->m_tokenFile.isEmpty() && !writeSecretFile(self->m_tokenFile, token)) {
            Q_EMIT self->tokenChecked(false, WorkspaceGitHub::tr("The token could not be saved on this device."));
            return;
        }
        self->m_token = token;
        self->client()->setToken(token);
        self->setSetting(kLoginKey, user.login);
        Q_EMIT self->accountChanged();
        Q_EMIT self->statusChanged();
        QString message = WorkspaceGitHub::tr("Signed in to GitHub as %1.").arg(u'@' + user.login);
        if (user.scopesKnown && !user.scopes.contains(u"repo"_s) && !user.scopes.contains(u"public_repo"_s))
            message += u' '
                       + WorkspaceGitHub::tr(
                               "This token has neither the “repo” nor the “public_repo” scope: it can only read.");
        Q_EMIT self->tokenChecked(true, message);
        if (self->m_repo && !self->m_repo->busy())
            self->pull();
    });
}

void WorkspaceGitHub::forgetToken()
{
    ++m_tokenCheck; // a token still being checked is not saved either
    if (std::exchange(m_checkingToken, false))
        Q_EMIT checkingChanged();
    if (!m_tokenFile.isEmpty() && !writeSecretFile(m_tokenFile, {})) {
        qCWarning(logging::lcSync).noquote()
                << u"GitHub: the token could not be removed from %1"_s.arg(QDir::toNativeSeparators(m_tokenFile));
    }
    m_token.clear();
    if (m_client)
        m_client->setToken({});
    setSetting(kLoginKey, QString());
    Q_EMIT accountChanged();
    Q_EMIT statusChanged();
}

} // namespace brulion::shell
