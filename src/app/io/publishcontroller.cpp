#include "publishcontroller.h"

#include "iobackground.h"
#include "texthelper.h"

#include "appsettings.h"
#include "workspace.h"

#include "dates/dates.h"
#include "github/address.h"
#include "index/vaultindex.h"
#include "log/categories.h"
#include "sync/http.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::ioviews {

namespace {

using namespace brulion::publish;

constexpr int kMaxFileRows = 300;

QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString kindIcon(TargetKind kind)
{
    switch (kind) {
    case TargetKind::GitHubPages: return u"git-branch"_s;
    case TargetKind::WebDav: return u"server"_s;
    case TargetKind::Nextcloud: return u"cloud"_s;
    case TargetKind::Folder: return u"folder"_s;
    }
    return u"globe"_s;
}

QString itemIcon(PublishItem::Kind kind)
{
    switch (kind) {
    case PublishItem::Kind::Page: return u"file-text"_s;
    case PublishItem::Kind::Folder: return u"folder"_s;
    case PublishItem::Kind::Tag: return u"hash"_s;
    case PublishItem::Kind::Notebook: return u"notebook-pen"_s;
    }
    return u"file-text"_s;
}

QString actionIcon(FileAction action)
{
    switch (action) {
    case FileAction::Upload: return u"plus"_s;
    case FileAction::Update: return u"pencil"_s;
    case FileAction::Unchanged: return u"check"_s;
    case FileAction::Delete: return u"trash-2"_s;
    case FileAction::Conflict: return u"triangle-alert"_s;
    }
    return u"file"_s;
}

/// The order of a file row: what changes first.
int actionRank(FileAction action)
{
    switch (action) {
    case FileAction::Conflict: return 0;
    case FileAction::Upload: return 1;
    case FileAction::Update: return 2;
    case FileAction::Delete: return 3;
    case FileAction::Unchanged: return 4;
    }
    return 5;
}

} // namespace

PublishController::PublishController(QObject *parent) : QObject(parent)
{
    m_readTimer.setSingleShot(true);
    m_readTimer.setInterval(0);
    connect(&m_readTimer, &QTimer::timeout, this, &PublishController::readStep);
    if (QCoreApplication *app = QCoreApplication::instance())
        app->installEventFilter(this);
}

PublishController::~PublishController() = default;

bool PublishController::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::LanguageChange && watched == QCoreApplication::instance()) {
        Q_EMIT languageChanged();
        refreshTargets();
    }
    return QObject::eventFilter(watched, event);
}

void PublishController::setWorkspace(shell::Workspace *workspace)
{
    if (m_workspace == workspace)
        return;
    if (m_workspace)
        disconnect(m_workspace, nullptr, this, nullptr);
    m_workspace = workspace;
    if (m_workspace) {
        connect(m_workspace, &shell::Workspace::openChanged, this, [this] {
            cancel();
            m_current.clear();
            dismiss();
            reload();
        });
    }
    Q_EMIT workspaceChanged();
    reload();
}

void PublishController::setSettings(shell::AppSettings *settings)
{
    if (m_settings == settings)
        return;
    m_settings = settings;
    Q_EMIT workspaceChanged();
    refreshTargets();
}

void PublishController::setTransport(sync::HttpTransport *transport)
{
    m_transport = transport;
    m_publisher.reset();
}

sync::HttpTransport *PublishController::transport()
{
    if (m_transport)
        return m_transport;
    if (!m_ownTransport)
        m_ownTransport = std::make_unique<sync::NetworkHttpTransport>();
    return m_ownTransport.get();
}

QString PublishController::appDataDir() const
{
    if (!m_appDataDir.isEmpty())
        return m_appDataDir;
    // The secrets folder of the Nextcloud app password: <app data>/secrets/…
    if (m_settings)
        return QFileInfo(QFileInfo(m_settings->passwordFile()).absolutePath()).absolutePath();
    return {};
}

bool PublishController::folderTargets() const
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_WASM)
    return false;
#else
    return true;
#endif
}

PublishStore PublishController::store() const
{
    Vault *vault = m_workspace ? m_workspace->vault() : nullptr;
    return PublishStore(vault ? vault->deviceStatePath() : QString());
}

std::optional<PublishTarget> PublishController::target(const QString &id) const
{
    if (id.isEmpty())
        return std::nullopt;
    return store().target(id);
}

QDateTime PublishController::now() const
{
    if (m_clock)
        return m_clock();
    return m_workspace ? m_workspace->now() : QDateTime::currentDateTime();
}

QString PublishController::human(const QDateTime &time) const
{
    if (!time.isValid())
        return {};
    const bool english = m_settings && m_settings->language() == u"en";
    return dates::formatHuman(DateSpec{time.date(), QTime(time.time().hour(), time.time().minute())},
                              english ? dates::UiLocale::English : dates::UiLocale::Polish, now().date());
}

QString PublishController::statusText(const PublishStatus &s) const
{
    if (!s.isValid())
        return tr("Not published yet");
    if (s.operation == u"unpublish")
        return s.ok ? tr("Unpublished %1").arg(human(s.at)) : tr("Unpublishing failed %1").arg(human(s.at));
    if (!s.ok)
        return tr("Publishing failed %1").arg(human(s.at));
    return tr("Published %1").arg(human(s.at));
}

QVariantMap PublishController::targetRow(const PublishTarget &t) const
{
    QVariantList items;
    for (const PublishItem &i : t.scope.items) {
        QString label;
        switch (i.kind) {
        case PublishItem::Kind::Page: label = paths::pageTitle(i.value); break;
        case PublishItem::Kind::Folder: label = i.value; break;
        case PublishItem::Kind::Tag: label = u'#' + i.value; break;
        case PublishItem::Kind::Notebook: label = tr("The whole notebook"); break;
        }
        items.append(QVariantMap{{u"kind"_s, itemKindName(i.kind)},
                                 {u"value"_s, i.value},
                                 {u"label"_s, label},
                                 {u"icon"_s, itemIcon(i.kind)}});
    }
    const QString data = appDataDir();
    const bool dav = t.kind == TargetKind::WebDav || t.kind == TargetKind::Nextcloud;
    const bool published = t.last.isValid() && t.last.operation == u"publish" && t.last.ok;
    return QVariantMap{
            {u"id"_s, t.id},
            {u"name"_s, t.name},
            {u"kind"_s, kindName(t.kind)},
            {u"kindText"_s, kindText(t.kind)},
            {u"icon"_s, kindIcon(t.kind)},
            {u"address"_s, describe(t)},
            {u"siteUrl"_s, t.siteUrl},
            {u"url"_s, published ? t.last.url : QString()},
            {u"items"_s, items},
            {u"itemCount"_s, int(items.size())},
            {u"linkedPages"_s, t.scope.linkedPages},
            {u"onlyMarked"_s, t.scope.onlyMarked},
            {u"extensionPages"_s, t.scope.extensionPages},
            {u"aiConversations"_s, t.scope.aiConversations},
            {u"content"_s, contentName(t.content)},
            {u"published"_s, published},
            {u"lastText"_s, statusText(t.last)},
            {u"lastOk"_s, !t.last.isValid() || t.last.ok},
            {u"lastError"_s,
             !t.last.isValid() || t.last.ok ? QString() : publish::errorText(t.last.errorCode, t.last.error)},
            {u"needsPassword"_s,
             dav && readSecret(secretFile(data, t.id, u"password"_s)).isEmpty() && t.kind == TargetKind::Nextcloud},
            {u"needsSharePassword"_s, t.kind == TargetKind::Nextcloud && t.sharePassword
                                              && readSecret(secretFile(data, t.id, u"share"_s)).isEmpty()},
            {u"needsToken"_s, t.kind == TargetKind::GitHubPages && readSecret(githubTokenFile(data)).isEmpty()},
            {u"protect"_s, t.protect},
            {u"needsPagesPassword"_s, t.protect && readSecret(secretFile(data, t.id, u"pages"_s)).isEmpty()},
    };
}

void PublishController::refreshTargets()
{
    QVariantList rows;
    bool currentFound = false;
    for (const QList<PublishTarget> all = store().targets(); const PublishTarget &t : all) {
        rows.append(targetRow(t));
        currentFound = currentFound || t.id == m_current;
    }
    const bool currentLost = !m_current.isEmpty() && !currentFound;
    if (rows != m_targets) {
        m_targets = rows;
        Q_EMIT targetsChanged();
    }
    if (currentLost || (m_current.isEmpty() && !m_targets.isEmpty())) {
        m_current = m_targets.isEmpty() ? QString() : m_targets.first().toMap().value(u"id"_s).toString();
        Q_EMIT currentChanged();
        return;
    }
    Q_EMIT currentChanged(); // its row may have changed
}

void PublishController::reload()
{
    refreshTargets();
}

void PublishController::setCurrent(const QString &id)
{
    if (id == m_current)
        return;
    cancel();
    dismiss();
    m_current = id;
    Q_EMIT currentChanged();
}

QVariantMap PublishController::currentTarget() const
{
    for (const QVariant &row : m_targets) {
        const QVariantMap map = row.toMap();
        if (map.value(u"id"_s).toString() == m_current)
            return map;
    }
    return {};
}

QVariantList PublishController::kinds() const
{
    QVariantList out{
            QVariantMap{{u"value"_s, u"github"_s},
                        {u"text"_s, kindText(TargetKind::GitHubPages)},
                        {u"icon"_s, kindIcon(TargetKind::GitHubPages)},
                        {u"description"_s, tr("A free website at <you>.github.io from a repository of yours.")}},
            QVariantMap{{u"value"_s, u"nextcloud"_s},
                        {u"text"_s, kindText(TargetKind::Nextcloud)},
                        {u"icon"_s, kindIcon(TargetKind::Nextcloud)},
                        {u"description"_s,
                         tr("A folder of your Nextcloud and a public link to it, with a password if you like.")}},
            QVariantMap{{u"value"_s, u"webdav"_s},
                        {u"text"_s, kindText(TargetKind::WebDav)},
                        {u"icon"_s, kindIcon(TargetKind::WebDav)},
                        {u"description"_s, tr("A folder on your own web server that takes WebDAV.")}},
    };
    if (folderTargets())
        out.append(
                QVariantMap{{u"value"_s, u"folder"_s},
                            {u"text"_s, kindText(TargetKind::Folder)},
                            {u"icon"_s, kindIcon(TargetKind::Folder)},
                            {u"description"_s, tr("A folder a web server or a sync app takes from this computer.")}});
    return out;
}

QVariantMap PublishController::targetFields(const QString &id) const
{
    PublishTarget t;
    if (const auto found = target(id))
        t = *found;
    else
        t.kind = TargetKind::GitHubPages;
    const QString data = appDataDir();
    return QVariantMap{
            {u"id"_s, t.id},
            {u"name"_s, t.name},
            {u"kind"_s, kindName(t.kind)},
            {u"repository"_s, t.repository},
            {u"branch"_s, t.branch},
            {u"folder"_s, t.folder},
            {u"url"_s, t.url},
            {u"server"_s, t.server},
            {u"username"_s, t.username},
            {u"path"_s, t.path},
            {u"siteUrl"_s, t.siteUrl},
            {u"title"_s, t.title},
            {u"content"_s, contentName(t.content)},
            {u"share"_s, t.share},
            {u"sharePassword"_s, t.sharePassword},
            {u"shareExpires"_s, t.shareExpires.isValid() ? t.shareExpires.toString(Qt::ISODate) : QString()},
            {u"hasPassword"_s, !t.id.isEmpty() && !readSecret(secretFile(data, t.id, u"password"_s)).isEmpty()},
            {u"hasSharePassword"_s, !t.id.isEmpty() && !readSecret(secretFile(data, t.id, u"share"_s)).isEmpty()},
            {u"protect"_s, t.protect},
            {u"hasPagesPassword"_s, !t.id.isEmpty() && !readSecret(secretFile(data, t.id, u"pages"_s)).isEmpty()},
    };
}

namespace {

PublishTarget targetFromFields(const QVariantMap &f, const PublishTarget &base)
{
    PublishTarget t = base;
    const auto text = [&f](const QString &key) { return f.value(key).toString().trimmed(); };
    t.name = text(u"name"_s);
    t.kind = kindFromName(text(u"kind"_s)).value_or(TargetKind::GitHubPages);
    t.repository = text(u"repository"_s);
    if (const auto parsed = github::parseRepoAddress(t.repository))
        t.repository = parsed->owner + u'/' + parsed->name; // a pasted github.com address
    t.branch = text(u"branch"_s).isEmpty() ? u"gh-pages"_s : text(u"branch"_s);
    t.folder = text(u"folder"_s);
    t.url = text(u"url"_s);
    t.server = text(u"server"_s);
    t.username = text(u"username"_s);
    t.path = text(u"path"_s);
    if (t.path.startsWith(u"file:"_s))
        t.path = QUrl(t.path).toLocalFile();
    t.siteUrl = text(u"siteUrl"_s);
    t.title = text(u"title"_s);
    t.content = contentFromName(text(u"content"_s));
    t.share = f.value(u"share"_s, true).toBool();
    t.sharePassword = f.value(u"sharePassword"_s).toBool();
    t.shareExpires = QDate::fromString(text(u"shareExpires"_s), Qt::ISODate);
    if (f.contains(u"protect"_s))
        t.protect = f.value(u"protect"_s).toBool();
    return t;
}

} // namespace

QString PublishController::checkFields(const QVariantMap &fields) const
{
    PublishTarget base;
    base.id = u"t000000000000"_s;
    const PublishTarget t = targetFromFields(fields, base);
    if (const QString problem = checkTarget(t); !problem.isEmpty())
        return publish::errorText(problem);
    if (!fields.value(u"shareExpires"_s).toString().trimmed().isEmpty() && !t.shareExpires.isValid())
        return tr("Write the expiry date as YYYY-MM-DD.");
    const QString id = fields.value(u"id"_s).toString();
    if (t.protect && fields.contains(u"pagesPasswordGiven"_s) && !fields.value(u"pagesPasswordGiven"_s).toBool()
        && (id.isEmpty() || readSecret(secretFile(appDataDir(), id, u"pages"_s)).isEmpty()))
        return tr("Type the pages' password.");
    for (const QVariant &row : m_targets) {
        const QVariantMap map = row.toMap();
        if (map.value(u"id"_s).toString() != id
            && map.value(u"name"_s).toString().compare(t.name, Qt::CaseInsensitive) == 0)
            return tr("Another target has this name.");
    }
    return {};
}

QString PublishController::saveTarget(const QVariantMap &fields, const QString &password, const QString &sharePassword,
                                      const QString &pagesPassword)
{
    if (!m_workspace || !m_workspace->vault() || !checkFields(fields).isEmpty())
        return {};
    PublishStore s = store();
    PublishTarget base;
    const QString id = fields.value(u"id"_s).toString();
    if (const auto existing = s.target(id))
        base = *existing;
    else
        base.id = newTargetId();
    const PublishTarget t = targetFromFields(fields, base);
    if (!s.saveTarget(t))
        return {};
    const QString data = appDataDir();
    if (!password.isEmpty() || fields.value(u"forgetPassword"_s).toBool())
        writeSecret(secretFile(data, t.id, u"password"_s), password);
    if (!sharePassword.isEmpty() || fields.value(u"forgetSharePassword"_s).toBool() || !t.sharePassword)
        writeSecret(secretFile(data, t.id, u"share"_s), t.sharePassword ? sharePassword : QString());
    if (!pagesPassword.isEmpty() || fields.value(u"forgetPagesPassword"_s).toBool() || !t.protect)
        writeSecret(secretFile(data, t.id, u"pages"_s), t.protect ? pagesPassword : QString());
    m_current = t.id;
    refreshTargets();
    dismiss();
    return t.id;
}

void PublishController::removeTarget(const QString &id)
{
    if (busy())
        return;
    PublishStore s = store();
    s.removeTarget(id);
    const QString data = appDataDir();
    writeSecret(secretFile(data, id, u"password"_s), {});
    writeSecret(secretFile(data, id, u"share"_s), {});
    writeSecret(secretFile(data, id, u"pages"_s), {});
    if (m_current == id)
        m_current.clear();
    dismiss();
    refreshTargets();
}

void PublishController::saveScope(const PublishTarget &t)
{
    PublishStore s = store();
    s.saveTarget(t);
    refreshTargets();
}

void PublishController::addItem(const QString &kind, const QString &value)
{
    auto t = target(m_current);
    const auto k = itemKindFromName(kind);
    if (!t || !k || busy())
        return;
    if (!t->scope.add(PublishItem{*k, value})) {
        preview();
        return;
    }
    saveScope(*t);
    preview();
}

bool PublishController::addFromRoute(const QVariantMap &route)
{
    const QString kind = route.value(u"add"_s).toString();
    if (!itemKindFromName(kind))
        return false;
    QString value = route.value(u"path"_s).toString();
    if (kind == u"folder")
        value = route.value(u"folder"_s).toString();
    else if (kind == u"tag")
        value = route.value(u"tag"_s).toString();
    else if (kind == u"notebook")
        value.clear();
    if (!m_current.isEmpty()) {
        addItem(kind, value);
        return false;
    }
    m_pendingKind = kind;
    m_pendingValue = value;
    return true;
}

void PublishController::addPending()
{
    if (m_pendingKind.isEmpty())
        return;
    const QString kind = std::exchange(m_pendingKind, {});
    addItem(kind, std::exchange(m_pendingValue, {}));
}

void PublishController::clearPending()
{
    m_pendingKind.clear();
    m_pendingValue.clear();
}

void PublishController::removeItem(int index)
{
    auto t = target(m_current);
    if (!t || index < 0 || index >= t->scope.items.size() || busy())
        return;
    t->scope.items.removeAt(index);
    saveScope(*t);
    dismiss();
}

void PublishController::setOption(const QString &name, bool on)
{
    auto t = target(m_current);
    if (!t || busy())
        return;
    bool *field = name == u"linkedPages"       ? &t->scope.linkedPages
                  : name == u"onlyMarked"      ? &t->scope.onlyMarked
                  : name == u"extensionPages"  ? &t->scope.extensionPages
                  : name == u"aiConversations" ? &t->scope.aiConversations
                                               : nullptr;
    if (!field || *field == on)
        return;
    *field = on;
    saveScope(*t);
    dismiss();
}

void PublishController::setContent(const QString &content)
{
    auto t = target(m_current);
    if (!t || busy() || contentName(t->content) == content)
        return;
    t->content = contentFromName(content);
    saveScope(*t);
    dismiss();
}

QVariantList PublishController::tagRows(const QString &query) const
{
    QVariantList out;
    VaultIndex *index = m_workspace ? m_workspace->index() : nullptr;
    if (!index)
        return out;
    QStringList tags;
    QList<int> counts;
    for (const QList<TagCount> all = index->tags(); const TagCount &t : all) {
        tags.append(t.display.isEmpty() ? t.tag : t.display);
        counts.append(int(t.paths.size()));
    }
    const QList<ui::FuzzyRanked> ranked = ui::fuzzyRankList(query, tags, 100, {});
    for (const ui::FuzzyRanked &r : ranked) {
        if (r.index < 0 || r.index >= tags.size())
            continue;
        out.append(QVariantMap{{u"title"_s, QString(u'#' + tags.at(r.index))},
                               {u"subtitle"_s, tr("%n page(s)", nullptr, counts.at(r.index))},
                               {u"icon"_s, u"hash"_s},
                               {u"path"_s, tags.at(r.index)}});
    }
    return out;
}

QStringList PublishController::folders() const
{
    QStringList out;
    if (Vault *vault = m_workspace ? m_workspace->vault() : nullptr) {
        for (const QList<FileStat> all = vault->folders(); const FileStat &f : all)
            out.append(f.path);
    }
    return out;
}

bool PublishController::copyUrl() const
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard || m_url.isEmpty())
        return false;
    clipboard->setText(m_url);
    return true;
}

// ---- Operations -----------------------------------------------------------------------------------

bool PublishController::busy() const
{
    return m_state == u"preparing" || m_state == u"checking" || m_state == u"publishing" || m_state == u"unpublishing";
}

void PublishController::setState(const QString &state, const QString &error)
{
    if (state == m_state && error == m_errorText)
        return;
    m_state = state;
    m_errorText = error;
    Q_EMIT stateChanged();
}

void PublishController::setProgress(qreal value, const QString &text)
{
    m_progress = value;
    m_progressText = text;
    Q_EMIT progressChanged();
}

void PublishController::preview()
{
    start(Operation::Preview, false);
}

void PublishController::publish(bool force)
{
    start(Operation::Publish, force);
}

void PublishController::unpublish(bool force)
{
    start(Operation::Unpublish, force);
}

void PublishController::cancel()
{
    if (!busy())
        return;
    ++m_generation;
    m_readTimer.stop();
    m_readQueue.clear();
    m_readFiles.clear();
    if (m_publisher)
        m_publisher->cancel();
    setState(u"idle"_s);
}

void PublishController::dismiss()
{
    if (busy())
        return;
    m_plan.clear();
    m_resultText.clear();
    m_url.clear();
    setState(u"idle"_s);
    Q_EMIT stateChanged();
}

void PublishController::start(Operation operation, bool force)
{
    if (busy())
        return;
    Vault *vault = m_workspace ? m_workspace->vault() : nullptr;
    VaultIndex *index = m_workspace ? m_workspace->index() : nullptr;
    const auto t = target(m_current);
    if (!vault || !index || !t) {
        setState(u"error"_s, tr("Choose a target first."));
        return;
    }
    ++m_generation;
    m_operation = operation;
    m_force = force;
    m_target = *t;
    m_plan.clear();
    m_resultText.clear();
    m_url.clear();
    m_readFiles.clear();
    m_unreadable.clear();
    m_readIndex = 0;
    if (operation == Operation::Unpublish) {
        m_selection = {};
        m_readQueue.clear();
        setState(u"unpublishing"_s);
        setProgress(-1, tr("Removing what was published…"));
        runPublisher(SiteBuild{});
        return;
    }
    if (m_target.protect && readSecret(secretFile(appDataDir(), m_target.id, u"pages"_s)).isEmpty()) {
        setState(u"error"_s, publish::errorText(u"no-page-password"_s));
        return;
    }
    m_selection = selectFiles(*index, m_target.scope);
    if (m_selection.pages.isEmpty()) {
        m_plan = planMap(PublishReport{}, SiteBuild{});
        setState(u"error"_s, publish::errorText(u"nothing"_s));
        return;
    }
    m_readQueue = filesToRead(m_selection);
    setState(u"preparing"_s);
    setProgress(0, tr("Reading the pages…"));
    m_readTimer.start();
}

void PublishController::readStep()
{
    Vault *vault = m_workspace ? m_workspace->vault() : nullptr;
    if (!vault || m_state != u"preparing")
        return;
    int read = 0;
    while (m_readIndex < m_readQueue.size() && read < m_readChunk) {
        const VaultPath path = m_readQueue.at(m_readIndex++);
        ++read;
        if (!vault->exists(path))
            continue;
        bool ok = false;
        QByteArray data = vault->readBinary(path, &ok);
        if (!ok) {
            m_unreadable.append(path);
            continue;
        }
        m_readFiles.append(io::FileEntry{path, std::move(data)});
    }
    const qsizetype total = m_readQueue.size();
    setProgress(total > 0 ? 0.5 * qreal(m_readIndex) / qreal(total) : 0.5,
                tr("Reading %1 of %2 files…").arg(m_readIndex).arg(total));
    if (m_readIndex < total) {
        m_readTimer.start();
        return;
    }
    buildAndRun();
}

void PublishController::buildAndRun()
{
    const QString notebook = m_workspace ? m_workspace->name() : QString();
    SiteInput input =
            siteInputFor(m_target, std::exchange(m_readFiles, {}), m_workspace->vault()->config(), notebook, now());
    if (input.protect)
        input.protection.password = readSecret(secretFile(appDataDir(), m_target.id, u"pages"_s));
    input.calcLocale = m_settings && m_settings->language() == u"en" ? calc::CalcLocale::English
                                                                      : calc::CalcLocale::Polish;
    m_readQueue.clear();
    setProgress(-1, tr("Making the pages…"));
    const int generation = m_generation;
    runInBackground<SiteBuild>(
            this, [input] { return buildSite(input); },
            [this, generation](SiteBuild site) {
                if (generation != m_generation || m_state != u"preparing")
                    return;
                if (!site.errorCode.isEmpty()) {
                    setState(u"error"_s, publish::errorText(site.errorCode));
                    return;
                }
                setState(m_operation == Operation::Preview ? u"checking"_s : u"publishing"_s);
                runPublisher(site);
            });
}

PublishCredentials PublishController::credentials(const PublishTarget &t) const
{
    const QString data = appDataDir();
    PublishCredentials c;
    c.token = readSecret(githubTokenFile(data));
    c.password = readSecret(secretFile(data, t.id, u"password"_s));
    c.sharePassword = readSecret(secretFile(data, t.id, u"share"_s));
    return c;
}

void PublishController::runPublisher(const SiteBuild &site)
{
    if (!m_publisher) {
        m_publisher = std::make_unique<Publisher>(transport());
#ifdef Q_OS_WASM
        m_publisher->setBrowser(true);
#endif
        connect(m_publisher.get(), &Publisher::progress, this, [this](const QString &phase, int done, int total) {
            if (!busy())
                return;
            QString text;
            if (phase == u"uploading")
                text = tr("Uploading %1 of %2…").arg(done).arg(total);
            else if (phase == u"deleting")
                text = tr("Removing %1 of %2…").arg(done).arg(total);
            else if (phase == u"committing")
                text = tr("Making the commit…");
            else if (phase == u"sharing")
                text = tr("Making the public link…");
            else if (phase == u"pages")
                text = tr("Checking GitHub Pages…");
            else
                text = tr("Looking at the target…");
            setProgress(total > 0 ? 0.5 + 0.5 * qreal(done) / qreal(total) : -1, text);
        });
    }
    m_publisher->setClock([this] { return now(); });
    const PublishManifest manifest = store().manifest(m_target.id);
    const int generation = m_generation;
    const auto done = [this, generation, site](const PublishReport &report) {
        if (generation != m_generation)
            return;
        onReport(report, site);
    };
    if (m_state == u"checking" || m_operation == Operation::Preview)
        setProgress(-1, tr("Looking at the target…"));
    switch (m_operation) {
    case Operation::Preview: m_publisher->preview(m_target, site, manifest, credentials(m_target), done); break;
    case Operation::Publish:
        setProgress(-1, tr("Looking at the target…"));
        m_publisher->publish(m_target, site, manifest, credentials(m_target), m_force, done);
        break;
    case Operation::Unpublish: m_publisher->unpublish(m_target, manifest, credentials(m_target), m_force, done); break;
    }
}

QVariantMap PublishController::planMap(const PublishReport &r, const SiteBuild &site) const
{
    const PublishPlan &plan = r.plan;
    QList<PlannedFile> files = plan.files;
    std::ranges::stable_sort(files, {}, [](const PlannedFile &f) { return actionRank(f.action); });
    QVariantList rows;
    for (const PlannedFile &f : std::as_const(files)) {
        if (rows.size() >= kMaxFileRows)
            break;
        rows.append(QVariantMap{{u"path"_s, f.path},
                                {u"action"_s, actionName(f.action)},
                                {u"actionText"_s, actionText(f.action, f.deletion)},
                                {u"icon"_s, actionIcon(f.action)},
                                {u"sizeText"_s, f.action == FileAction::Delete ? QString() : sizeText(f.size)}});
    }
    QVariantList leftOut;
    for (const LeftOut &l : m_selection.leftOut)
        leftOut.append(QVariantMap{
                {u"path"_s, l.path},
                {u"title"_s, paths::isMarkdown(l.path) ? paths::pageTitle(l.path) : paths::basename(l.path)},
                {u"reasonText"_s, reasonText(l.reason)}});
    for (const VaultPath &p : site.encryptedPages)
        leftOut.append(QVariantMap{{u"path"_s, p},
                                   {u"title"_s, paths::pageTitle(p)},
                                   {u"reasonText"_s, reasonText(LeftOutReason::Encrypted)}});
    for (const VaultPath &p : m_unreadable)
        leftOut.append(QVariantMap{
                {u"path"_s, p}, {u"title"_s, paths::basename(p)}, {u"reasonText"_s, tr("Could not be read")}});
    QVariantList conflicts;
    for (const PlannedFile &c : r.conflicts)
        conflicts.append(
                QVariantMap{{u"path"_s, c.path}, {u"actionText"_s, actionText(FileAction::Conflict, c.deletion)}});
    QStringList warnings;
    for (qsizetype i = 0; i < r.warningCodes.size(); ++i)
        warnings.append(warningText(r.warningCodes.at(i), r.warningDetails.value(i)));
    for (const PublishItem &i : m_selection.missingItems)
        warnings.append(tr("“%1” is not in the notebook any more.")
                                .arg(i.kind == PublishItem::Kind::Tag ? u'#' + i.value : i.value));
    if (site.fragmentsRemoved > 0)
        warnings.append(tr("%n encrypted fragment(s) are left out of the pages.", nullptr, site.fragmentsRemoved));
    const bool preview = r.operation == u"preview";
    const int up = preview ? plan.count(FileAction::Upload) : r.uploaded;
    const int changed = preview ? plan.count(FileAction::Update) : r.updated;
    const int removed = preview ? plan.count(FileAction::Delete) : r.deleted;
    QStringList parts;
    if (up > 0)
        parts.append(tr("%n new", nullptr, up));
    if (changed > 0)
        parts.append(tr("%n changed", nullptr, changed));
    if (removed > 0)
        parts.append(tr("%n to remove", nullptr, removed));
    if (const int conflict = plan.count(FileAction::Conflict); conflict > 0)
        parts.append(tr("%n changed there", nullptr, conflict));
    if (const int same = plan.count(FileAction::Unchanged); same > 0)
        parts.append(tr("%n unchanged", nullptr, same));
    QString remote;
    if (!plan.remoteChecked && preview)
        remote = tr("The target could not be checked (%1) – this is what the last publish recorded.")
                         .arg(publish::errorText(r.remoteErrorCode, r.remoteError));
    return QVariantMap{
            {u"counts"_s, QVariantMap{{u"upload"_s, plan.count(FileAction::Upload)},
                                      {u"update"_s, plan.count(FileAction::Update)},
                                      {u"unchanged"_s, plan.count(FileAction::Unchanged)},
                                      {u"delete"_s, plan.count(FileAction::Delete)},
                                      {u"conflict"_s, plan.count(FileAction::Conflict)}}},
            {u"summary"_s, parts.isEmpty() ? tr("Nothing to send") : parts.join(u" · "_s)},
            {u"pagesText"_s,
             QString(tr("%n page(s)", nullptr, site.pages) + u", "_s + tr("%n attachment(s)", nullptr, site.attachments)
                     + u" · "_s + sizeText(site.bytes))},
            {u"pages"_s, site.pages},
            {u"attachments"_s, site.attachments},
            {u"sizeText"_s, sizeText(plan.bytes(FileAction::Upload) + plan.bytes(FileAction::Update))},
            {u"remoteChecked"_s, plan.remoteChecked},
            {u"remoteText"_s, remote},
            {u"url"_s, r.url},
            {u"files"_s, rows},
            {u"moreFiles"_s, int(std::max<qsizetype>(0, files.size() - rows.size()))},
            {u"leftOut"_s, leftOut},
            {u"leftOutCount"_s, int(leftOut.size())},
            {u"conflicts"_s, conflicts},
            {u"warnings"_s, warnings},
            {u"fragmentsRemoved"_s, site.fragmentsRemoved},
            {u"protectedPages"_s, site.protectedPages},
            {u"hasChanges"_s, plan.hasChanges()},
            {u"conflictCount"_s, plan.count(FileAction::Conflict)},
    };
}

void PublishController::onReport(const PublishReport &r, const SiteBuild &site)
{
    PublishStore s = store();
    if (r.operation != u"preview") {
        if (r.manifestChanged)
            s.saveManifest(m_target.id, r.manifest);
        PublishStatus status;
        status.at = now();
        status.operation = r.operation;
        status.ok = r.ok;
        status.url = r.url;
        status.pages = site.pages;
        status.uploaded = r.uploaded + r.updated;
        status.deleted = r.deleted;
        status.unchanged = r.unchanged;
        status.conflicts = int(r.conflicts.size());
        status.commit = r.commit;
        status.errorCode = r.errorCode;
        status.error = r.error;
        s.setStatus(m_target.id, status);
        refreshTargets();
    }
    m_plan = planMap(r, site);
    m_url = r.url;
    setProgress(1, {});
    if (r.errorCode == u"cancelled") {
        setState(u"idle"_s);
        return;
    }
    if (r.operation == u"preview") {
        setState(u"previewed"_s);
        return;
    }
    if (!r.ok && r.errorCode == u"conflicts" && r.operation == u"publish" && !r.manifestChanged) {
        setState(u"conflicts"_s, publish::errorText(r.errorCode));
        return;
    }
    if (!r.ok) {
        const QString message = publish::errorText(r.errorCode, r.error);
        setState(u"error"_s, message);
        Q_EMIT finished(message, u"error"_s);
        return;
    }
    if (r.operation == u"unpublish") {
        m_resultText = tr("“%1” is no longer published.").arg(m_target.name);
        setState(u"unpublished"_s);
        Q_EMIT finished(m_resultText, u"success"_s);
        return;
    }
    m_resultText = r.uploaded + r.updated + r.deleted == 0
                           ? tr("“%1” was up to date already.").arg(m_target.name)
                           : tr("Published “%1”: %2.").arg(m_target.name, m_plan.value(u"summary"_s).toString());
    qCInfo(logging::lcIo).noquote() << u"Published %1 (%2): %3 new, %4 changed, %5 removed"_s.arg(
            m_target.id, kindName(m_target.kind), QString::number(r.uploaded), QString::number(r.updated),
            QString::number(r.deleted));
    setState(u"published"_s);
    Q_EMIT finished(m_resultText, u"success"_s);
}

} // namespace brulion::ioviews
