// CodeProjects: a project cloned from GitHub or any git address (docs/CODE.md "Cloning a
// repository"), as T3 Code's "Clone a repository": what the user typed read by
// gitcli::parseCloneSource(), `git clone --progress` in the background through gitcli::Client
// (listed in `clones` while it runs, kept with its error when it fails), a second try over the
// other protocol when the first cannot sign in, the folder added as a project once it is there;
// the user's GitHub repositories from `gh repo list` for the picker.

#include "codeprojects.h"

#include "gitcli/client.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

/// What a clone is doing, in words: "Receiving objects · 45% · 12.4 MiB | 3.1 MiB/s".
QString cloneStatus(const gitcli::CloneProgress &p)
{
    QString stage;
    switch (p.stage) {
    case gitcli::CloneStage::Connecting: stage = CodeProjects::tr("Connecting…"); break;
    case gitcli::CloneStage::Counting: stage = CodeProjects::tr("Counting objects"); break;
    case gitcli::CloneStage::Receiving: stage = CodeProjects::tr("Receiving objects"); break;
    case gitcli::CloneStage::Resolving: stage = CodeProjects::tr("Resolving deltas"); break;
    case gitcli::CloneStage::Checkout: stage = CodeProjects::tr("Checking out files"); break;
    }
    QStringList parts{stage};
    if (p.percent >= 0)
        parts << CodeProjects::tr("%1%").arg(p.percent);
    if (!p.detail.isEmpty())
        parts << p.detail;
    return parts.join(u" · "_s);
}

/// A folder that a clone may go into: missing, or an empty folder.
bool freeForClone(const QString &folder)
{
    const QFileInfo info(folder);
    if (!info.exists() && !info.isSymLink())
        return true;
    return info.isDir() && !info.isSymLink()
           && QDir(folder).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
}

} // namespace

QString CodeProjects::clonesRoot() const
{
    return m_clonesRoot.isEmpty() ? QDir::home().filePath(u".brulion/projects"_s) : m_clonesRoot;
}

void CodeProjects::setClonesRoot(const QString &root)
{
    QString clean = root.trimmed();
    if (clean.startsWith(u"file:"_s))
        clean = QUrl(clean).toLocalFile();
    if (!clean.isEmpty())
        clean = QDir::cleanPath(clean);
    if (clean == m_clonesRoot)
        return;
    m_clonesRoot = clean;
    save();
}

QString CodeProjects::cleanFolder(const QString &folder) const
{
    QString path = folder.trimmed();
    if (path.startsWith(u"file:"_s))
        path = QUrl(path).toLocalFile();
    if (path.startsWith(u"~/"_s) || path == u"~")
        path = QDir::home().filePath(path.mid(2));
    if (path.isEmpty())
        return {};
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

int CodeProjects::cloneIndex(const QString &folder) const
{
    const QString key = cleanFolder(folder);
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity paths = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity paths = Qt::CaseSensitive;
#endif
    for (int i = 0; i < m_clones.size(); ++i) {
        if (m_clones.at(i).folder.compare(key, paths) == 0)
            return i;
    }
    return -1;
}

QVariantList CodeProjects::clones() const
{
    QVariantList out;
    for (const Clone &c : m_clones) {
        out << QVariantMap{{u"folder"_s, c.folder},
                           {u"source"_s, c.source},
                           {u"url"_s, gitcli::withoutUrlCredentials(c.url)},
                           {u"name"_s, c.name},
                           {u"phase"_s, c.running ? u"running"_s : u"failed"_s},
                           {u"stage"_s, gitcli::cloneStageName(c.progress.stage)},
                           {u"percent"_s, c.progress.percent},
                           {u"detail"_s, c.progress.detail},
                           {u"status"_s, c.running ? cloneStatus(c.progress) : c.error},
                           {u"error"_s, c.error}};
    }
    return out;
}

QVariantMap CodeProjects::cloneOf(const QString &folder) const
{
    const int i = cloneIndex(folder);
    return i < 0 ? QVariantMap() : clones().at(i).toMap();
}

QVariantMap CodeProjects::cloneSource(const QString &source) const
{
    const gitcli::CloneSource s = gitcli::parseCloneSource(source);
    return {{u"ok"_s, s.isValid()},
            {u"url"_s, gitcli::withoutUrlCredentials(s.url)},
            {u"name"_s, s.name},
            {u"title"_s, s.isValid() ? s.displayName() : QString()},
            {u"error"_s, s.error}};
}

QString CodeProjects::cloneFolder(const QString &source, const QString &parent) const
{
    const gitcli::CloneSource s = gitcli::parseCloneSource(source);
    if (!s.isValid())
        return {};
    const QString base = parent.trimmed().isEmpty() ? clonesRoot() : cleanFolder(parent);
    // A folder of the repository's own name chosen, empty, is the clone's folder itself (not
    // repo/repo).
    if (!parent.trimmed().isEmpty() && QFileInfo(base).fileName().compare(s.name, Qt::CaseInsensitive) == 0
        && QFileInfo(base).isDir() && freeForClone(base) && cloneIndex(base) < 0)
        return base;
    const QDir dir(base);
    QString folder = QDir::cleanPath(dir.filePath(s.name));
    for (int n = 2; (!freeForClone(folder) || cloneIndex(folder) >= 0) && n < 1000; ++n)
        folder = QDir::cleanPath(dir.filePath(s.name + u'-' + QString::number(n)));
    return folder;
}

QString CodeProjects::cloneRepository(const QString &source, const QString &folder)
{
    const gitcli::CloneSource s = gitcli::parseCloneSource(source);
    if (!s.isValid())
        return s.error.isEmpty() ? tr("Type owner/name, a git address or a repository’s page.") : s.error;
    const QString dest = cleanFolder(folder);
    if (dest.isEmpty())
        return tr("Choose the folder the repository goes into.");
    if (!gitAvailable())
        return tr("git is not on this computer.");
    if (const int i = cloneIndex(dest); i >= 0) {
        if (m_clones.at(i).running)
            return tr("A clone into this folder is running already.");
        m_clones.removeAt(i); // (one that failed: tried again, maybe from another address)
    }
    if (!freeForClone(dest))
        return tr("“%1” is there already and is not an empty folder.").arg(QDir::toNativeSeparators(dest));
    if (m_projects.size() >= MaxProjects)
        return tr("No more projects can be added: %1 is the most.").arg(MaxProjects);
    Clone c;
    c.folder = dest;
    c.source = source.trimmed();
    c.url = s.url;
    c.name = s.local ? s.name : s.displayName();
    c.existed = QFileInfo(dest).isDir();
    m_clones << c;
    startClone(int(m_clones.size()) - 1);
    Q_EMIT clonesChanged();
    return {};
}

void CodeProjects::startClone(int index)
{
    Clone &c = m_clones[index];
    c.running = true;
    c.cancelled = false;
    c.error.clear();
    c.progress = {};
    const QString folder = c.folder;
    git()->clone(
            c.url, folder, u"clone:"_s + folder,
            [this, folder](const gitcli::CloneProgress &p) { cloneProgressed(folder, p); },
            [this, folder](const gitcli::Outcome &o) { cloneEnded(folder, o); });
}

void CodeProjects::cloneProgressed(const QString &folder, const gitcli::CloneProgress &progress)
{
    const int i = cloneIndex(folder);
    if (i < 0 || !m_clones.at(i).running)
        return;
    Clone &c = m_clones[i];
    if (c.progress.stage == progress.stage && c.progress.percent == progress.percent
        && c.progress.detail == progress.detail)
        return;
    c.progress = progress;
    if (!m_cloneTimer.isActive())
        m_cloneTimer.start();
}

void CodeProjects::cloneEnded(const QString &folder, const gitcli::Outcome &outcome)
{
    const int i = cloneIndex(folder);
    if (i < 0)
        return;
    Clone &c = m_clones[i];
    c.running = false;
    if (c.cancelled) {
        m_clones.removeAt(i);
        Q_EMIT clonesChanged();
        Q_EMIT cloned(folder, QString(), QString(), true);
        return;
    }
    if (!outcome.ok) {
        // Signing in failed: once more over the other protocol (only ssh keys here, or only a
        // credential helper for https).
        if (!c.retried && gitcli::isAuthenticationFailure(outcome.message)) {
            const bool ssh = c.url.contains(u"://"_s) && !c.url.startsWith(u"ssh://"_s, Qt::CaseInsensitive);
            const QString other = gitcli::cloneUrlForProtocol(c.url, ssh);
            if (!other.isEmpty() && other != c.url) {
                c.retried = true;
                c.url = other;
                startClone(i);
                Q_EMIT clonesChanged();
                return;
            }
        }
        c.error = outcome.message.isEmpty() ? tr("git could not clone the repository.") : outcome.message;
        Q_EMIT clonesChanged();
        Q_EMIT cloned(folder, QString(), c.error, false);
        return;
    }
    m_clones.removeAt(i);
    const QString id = add(folder);
    Q_EMIT clonesChanged();
    Q_EMIT cloned(folder, id,
                  id.isEmpty() ? tr("The clone is in %1, but it could not be added as a project.")
                                         .arg(QDir::toNativeSeparators(folder))
                               : QString(),
                  false);
}

void CodeProjects::cancelClone(const QString &folder)
{
    const int i = cloneIndex(folder);
    if (i < 0)
        return;
    Clone &c = m_clones[i];
    if (!c.running) {
        dismissClone(folder);
        return;
    }
    c.cancelled = true;
    if (!git()->stop(u"clone:"_s + c.folder)) {
        // (Between two tries, or not started: nothing runs to stop.)
        m_clones.removeAt(i);
        Q_EMIT clonesChanged();
    }
}

bool CodeProjects::retryClone(const QString &folder)
{
    const int i = cloneIndex(folder);
    if (i < 0 || m_clones.at(i).running || !gitAvailable())
        return false;
    if (!freeForClone(m_clones.at(i).folder)) {
        m_clones[i].error = tr("“%1” is there already and is not an empty folder.")
                                    .arg(QDir::toNativeSeparators(m_clones.at(i).folder));
        Q_EMIT clonesChanged();
        return false;
    }
    // The address as typed again (the other protocol is tried after it, if need be).
    const gitcli::CloneSource s = gitcli::parseCloneSource(m_clones.at(i).source);
    if (s.isValid())
        m_clones[i].url = s.url;
    m_clones[i].retried = false;
    m_clones[i].existed = QFileInfo(m_clones.at(i).folder).isDir();
    startClone(i);
    Q_EMIT clonesChanged();
    return true;
}

void CodeProjects::dismissClone(const QString &folder)
{
    const int i = cloneIndex(folder);
    if (i < 0 || m_clones.at(i).running)
        return;
    m_clones.removeAt(i);
    Q_EMIT clonesChanged();
}

bool CodeProjects::listRepositories()
{
    if (!ghAvailable())
        return false;
    if (m_listing)
        return true;
    m_listing = true;
    git()->repositoryList(200, [this](const QList<gitcli::HostedRepository> &list, const QString &error) {
        m_listing = false;
        QVariantList out;
        for (const gitcli::HostedRepository &r : list) {
            out << QVariantMap{{u"nameWithOwner"_s, r.nameWithOwner},
                               {u"description"_s, r.description},
                               {u"url"_s, r.url},
                               {u"sshUrl"_s, r.sshUrl},
                               {u"isPrivate"_s, r.isPrivate},
                               {u"isFork"_s, r.isFork},
                               {u"isArchived"_s, r.isArchived},
                               {u"updatedAt"_s, r.updatedAt}};
        }
        if (error.isEmpty())
            m_repositories = out;
        Q_EMIT repositoriesListed(error.isEmpty() ? out : m_repositories, error);
    });
    return true;
}

QVariantList CodeProjects::repositoryItems(const QVariantList &repositories) const
{
    QVariantList out;
    for (const QVariant &v : repositories) {
        const QVariantMap r = v.toMap();
        const QString name = r.value(u"nameWithOwner"_s).toString();
        if (name.isEmpty())
            continue;
        QStringList badges;
        if (r.value(u"isPrivate"_s).toBool())
            badges << tr("private");
        if (r.value(u"isFork"_s).toBool())
            badges << tr("fork");
        if (r.value(u"isArchived"_s).toBool())
            badges << tr("archived");
        const QString description = r.value(u"description"_s).toString();
        if (!description.isEmpty())
            badges << description;
        out << QVariantMap{{u"title"_s, name},
                           {u"subtitle"_s, badges.join(u" · "_s)},
                           {u"icon"_s, r.value(u"isPrivate"_s).toBool() ? u"lock"_s : u"book-open"_s},
                           {u"source"_s, name},
                           {u"kind"_s, u"repo"_s}};
    }
    return out;
}

QVariantList CodeProjects::cloneOffers(const QString &query, const QVariantList &repositories) const
{
    const QString q = query.trimmed();
    if (q.isEmpty())
        return {};
    const gitcli::CloneSource s = gitcli::parseCloneSource(q);
    if (!s.isValid())
        return {};
    // One of the listed repositories is offered as itself.
    if (s.host == u"github.com") {
        for (const QVariant &v : repositories) {
            if (v.toMap().value(u"nameWithOwner"_s).toString().compare(s.path, Qt::CaseInsensitive) == 0)
                return {};
        }
    }
    return {QVariantMap{{u"title"_s, tr("Clone “%1”").arg(s.displayName())},
                        {u"subtitle"_s, gitcli::withoutUrlCredentials(s.url)},
                        {u"icon"_s, u"download"_s},
                        {u"source"_s, q},
                        {u"kind"_s, u"clone"_s}}};
}

QString CodeProjects::cloneProblem(const QString &query) const
{
    // A search word is no address: only what looks like one is told why it is none.
    const QString q = query.trimmed();
    if (!q.contains(u'/') && !q.contains(u':') && !q.contains(u'@'))
        return {};
    return gitcli::parseCloneSource(q).error;
}

} // namespace brulion::shell
