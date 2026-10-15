#include "workspace.h"

#include "workspacecapture.h"
#include "workspacereminders.h"
#include "workspacesync.h"

#include "dates/dates.h"
#include "index/vaultindex.h"
#include "markdown/markdown.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDesktopServices>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QMimeDatabase>
#include <QMimeType>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

Q_LOGGING_CATEGORY(lcWorkspace, "brulion.workspace")

namespace {

constexpr auto kFavorites = "ui/favorites";
constexpr auto kRecentPages = "ui/recentPages";

/// LocalStore key of a page's folded blocks.
QString foldStateKey(const QString &path)
{
    return u"ui/folds/"_s + path;
}

/// Files indexed per event-loop turn while building the index in the background.
constexpr qint64 kIndexBudgetMs = 12;

QVariantMap pageEntry(const QString &path)
{
    return { { u"path"_s, path }, { u"title"_s, paths::pageTitle(path) } };
}

/// A page title typed with its extension ("Notatka.md") names the page "Notatka".
QString withoutMdExtension(const QString &title)
{
    const QString t = title.trimmed();
    if (t.size() > 3 && t.endsWith(u".md"_s, Qt::CaseInsensitive) && t.at(t.size() - 4) != u'/')
        return t.chopped(3);
    return title;
}

QString lineOf(const QString &text)
{
    QString out = text.simplified();
    if (out.size() > 300)
        out = out.left(299) + u'…';
    return out;
}

} // namespace

Workspace::Workspace(QObject *parent) : EditorHost(parent)
{
#ifdef Q_OS_WASM
    m_dataUrlAssets = true;
#endif
    m_indexTimer.setInterval(0);
    connect(&m_indexTimer, &QTimer::timeout, this, &Workspace::indexChunk);

    m_reminders = new WorkspaceReminders([this] { return now(); }, this);
    connect(m_reminders, &WorkspaceReminders::navigationRequested, this,
            [this](const QVariantMap &route) { navigate(route); });
    connect(m_reminders, &WorkspaceReminders::errorOccurred, this, &Workspace::errorOccurred);
    m_sync = new WorkspaceSync(this);
    m_sync->setClock([this] { return now(); });
    connect(m_sync, &WorkspaceSync::errorOccurred, this, &Workspace::errorOccurred);
}

Workspace::~Workspace()
{
    // Nobody may react to a workspace that is going away (the app's controller is half
    // destroyed by now): close quietly.
    m_sync->blockSignals(true);
    m_reminders->blockSignals(true);
    blockSignals(true);
    close();
}

bool Workspace::open(const QString &rootPath, const QString &deviceStatePath, QString *error)
{
    close();
    auto vault = std::make_unique<Vault>(rootPath, deviceStatePath);
    QString err;
    if (!vault->open(&err)) {
        if (error)
            *error = err;
        return false;
    }
    m_vault = std::move(vault);
    if (m_clock)
        m_vault->setClock(m_clock);
    m_index = std::make_unique<VaultIndex>(m_vault->config());
    connect(m_vault.get(), &Vault::changed, this, &Workspace::onVaultChanged);
    connect(m_vault.get(), &Vault::configChanged, this, [this] {
        m_index->setConfig(m_vault->config());
        Q_EMIT configChanged();
    });
    connect(m_index.get(), &VaultIndex::changed, this, &EditorHost::indexChanged);
#ifndef Q_OS_WASM
    // Other programs (Syncthing, editors) change files: watch the folder, and rescan slowly
    // because watchers miss events now and then (Android, network folders). In the browser
    // nothing else writes to the vault (the HTML shell allows one tab), so neither is needed.
    m_vault->startWatching();
    m_vault->setRescanInterval(120 * 1000);
#endif

    for (const FileStat &f : m_vault->files())
        m_indexQueue.append(f.path);
    m_indexTotal = m_indexQueue.size();
    m_trashCount = -1;
    m_reminders->attach(m_vault.get(), m_index.get());
    m_sync->attach(m_vault.get());
    if (!m_indexQueue.isEmpty())
        m_indexTimer.start();

    ++m_revision;
    Q_EMIT openChanged();
    Q_EMIT configChanged();
    Q_EMIT revisionChanged();
    Q_EMIT favoritesChanged();
    Q_EMIT recentPagesChanged();
    Q_EMIT indexingChanged();
    Q_EMIT indexProgressChanged();
    Q_EMIT trashChanged();
    if (m_indexQueue.isEmpty())
        indexingFinished();
    return true;
}

void Workspace::close()
{
    if (!m_vault)
        return;
    const bool wasReady = ready();
    // Reminders and sync let go of the vault and the index first (a running sync is abandoned:
    // what it finished stays synced, the rest is synced next time).
    m_reminders->detach();
    m_sync->detach();
    m_indexTimer.stop();
    const bool wasIndexing = !m_indexQueue.isEmpty();
    m_indexQueue.clear();
    m_indexTotal = 0;
    m_trashCount = -1;
    m_dataUrls.clear();
    m_vault->stopWatching();
    disconnect(m_vault.get(), nullptr, this, nullptr);
    disconnect(m_index.get(), nullptr, this, nullptr);
    m_index.reset();
    m_vault.reset();
    ++m_revision;
    Q_EMIT openChanged();
    Q_EMIT configChanged();
    Q_EMIT revisionChanged();
    Q_EMIT favoritesChanged();
    Q_EMIT recentPagesChanged();
    Q_EMIT trashChanged();
    if (wasIndexing) {
        Q_EMIT indexingChanged();
        Q_EMIT indexProgressChanged();
    }
    setReady(wasReady);
}

bool Workspace::isOpen() const
{
    return m_vault != nullptr;
}

QString Workspace::rootPath() const
{
    return m_vault ? m_vault->rootPath() : QString();
}

QString Workspace::name() const
{
    return m_vault ? QFileInfo(m_vault->rootPath()).fileName() : QString();
}

bool Workspace::isSyncthingFolder() const
{
    return m_vault && QFileInfo::exists(m_vault->rootPath() + u"/.stfolder"_s);
}

int Workspace::pageCount() const
{
    return m_vault ? int(m_vault->markdownFiles().size()) : 0;
}

void Workspace::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
    // Trash stamps and version history follow the same clock (tests, screenshots).
    if (m_vault)
        m_vault->setClock(m_clock);
}

QDateTime Workspace::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

void Workspace::setUiLanguage(const QString &language)
{
    m_language = language == u"en" ? u"en"_s : u"pl"_s;
    m_reminders->setLanguage(m_language);
    m_sync->setLanguage(m_language);
}

void Workspace::setDataUrlAssets(bool on)
{
    m_dataUrlAssets = on;
    m_dataUrls.clear();
}

// ---- Index ------------------------------------------------------------------------------------

void Workspace::indexFile(const QString &path)
{
    if (!m_vault || !m_index || paths::isIgnored(path))
        return;
    if (paths::isMarkdown(path)) {
        bool ok = false;
        const QString content = m_vault->readText(path, &ok);
        if (ok)
            m_index->update(path, content);
    } else {
        m_index->update(path, QString()); // attachments are remembered by path
    }
}

void Workspace::indexChunk()
{
    if (!m_index || m_indexQueue.isEmpty()) {
        m_indexTimer.stop();
        return;
    }
    QElapsedTimer timer;
    timer.start();
    {
        VaultIndex::Batch batch(*m_index);
        while (!m_indexQueue.isEmpty() && timer.elapsed() < kIndexBudgetMs)
            indexFile(m_indexQueue.takeFirst());
    }
    Q_EMIT indexProgressChanged();
    if (m_indexQueue.isEmpty()) {
        m_indexTimer.stop();
        Q_EMIT indexingChanged();
        indexingFinished();
    }
}

void Workspace::finishIndexing()
{
    if (!m_index || m_indexQueue.isEmpty())
        return;
    {
        VaultIndex::Batch batch(*m_index);
        while (!m_indexQueue.isEmpty())
            indexFile(m_indexQueue.takeFirst());
    }
    m_indexTimer.stop();
    Q_EMIT indexingChanged();
    Q_EMIT indexProgressChanged();
    indexingFinished();
}

void Workspace::indexingFinished()
{
    // Every note is known now: reminders may fire and missed ones be recorded (a half-built
    // index would make reminders look deleted).
    m_reminders->start();
    setReady(false);
}

void Workspace::setReady(bool wasReady)
{
    if (wasReady != ready())
        Q_EMIT readyChanged();
}

qreal Workspace::indexProgress() const
{
    if (!m_vault || m_indexTotal <= 0 || m_indexQueue.isEmpty())
        return 1.0;
    return std::clamp(1.0 - qreal(m_indexQueue.size()) / qreal(m_indexTotal), 0.0, 1.0);
}

void Workspace::onVaultChanged(const QList<VaultChange> &changes)
{
    QStringList changed;
    {
        VaultIndex::Batch batch(*m_index);
        for (const VaultChange &c : changes) {
            m_dataUrls.remove(c.path);
            if (!c.oldPath.isEmpty())
                m_dataUrls.remove(c.oldPath);
            switch (c.type) {
            case ChangeType::Created:
            case ChangeType::Modified:
                indexFile(c.path);
                break;
            case ChangeType::Deleted:
                m_index->remove(c.path);
                m_indexQueue.removeAll(c.path);
                m_vault->store()->remove(foldStateKey(c.path));
                break;
            case ChangeType::Renamed: {
                QString content;
                if (paths::isMarkdown(c.path))
                    content = m_vault->readText(c.path);
                m_indexQueue.removeAll(c.oldPath);
                if (paths::isIgnored(c.path))
                    m_index->remove(c.oldPath);
                else
                    m_index->rename(c.oldPath, c.path, content);
                renamePathInLists(c.oldPath, c.path);
                break;
            }
            }
            if (c.type == ChangeType::Renamed && !paths::isIgnored(c.oldPath))
                changed.append(c.oldPath);
            if (!paths::isIgnored(c.path))
                changed.append(c.path);
        }
    }
    ++m_revision;
    Q_EMIT revisionChanged();
    // Deletions and restores move files through the trash (.trash is not tracked itself).
    if (std::any_of(changes.cbegin(), changes.cend(), [](const VaultChange &c) {
            return c.type == ChangeType::Deleted || c.type == ChangeType::Created || c.type == ChangeType::Renamed;
        })) {
        m_trashCount = -1;
        Q_EMIT trashChanged();
    }
    for (const VaultChange &c : changes) {
        if (c.type == ChangeType::Renamed)
            Q_EMIT pathRenamed(c.oldPath, c.path);
        else if (c.type == ChangeType::Deleted)
            Q_EMIT pathRemoved(c.path);
    }
    for (const QString &path : std::as_const(changed))
        Q_EMIT fileChanged(path);
    if (!changed.isEmpty())
        Q_EMIT filesChanged(changed);
}

// ---- EditorHost: files ----------------------------------------------------------------------

QString Workspace::read(const QString &path, bool *ok)
{
    if (!m_vault) {
        if (ok)
            *ok = false;
        return {};
    }
    return m_vault->readText(path, ok);
}

bool Workspace::write(const QString &path, const QString &content)
{
    // fileChanged(path) follows, also for the editor that saved: the same page may be open in a
    // second pane (side panel) that has to reload. The saving editor recognises its own text
    // (PageController compares with what it wrote).
    if (!m_vault)
        return false;
    const bool ok = m_vault->writeText(path, content, ChangeSource::User);
    if (!ok)
        Q_EMIT errorOccurred(tr("Could not save %1: %2").arg(path, m_vault->errorString()));
    return ok;
}

QString Workspace::readFile(const QString &path)
{
    return read(path);
}

bool Workspace::writeFile(const QString &path, const QString &content)
{
    return write(path, content);
}

bool Workspace::exists(const QString &path) const
{
    return m_vault && m_vault->exists(path);
}

QString Workspace::pageTitle(const QString &path) const
{
    return paths::pageTitle(path);
}

QString Workspace::folderOf(const QString &path) const
{
    return paths::dirname(path);
}

QString Workspace::journalDate(const QString &path) const
{
    if (!m_vault)
        return {};
    const auto date = markdown::journalDateFromPath(path, m_vault->config());
    return date ? date->toString(Qt::ISODate) : QString();
}

// ---- EditorHost: index-backed helpers --------------------------------------------------------

QVariantList Workspace::suggestPages(const QString &prefix, int limit) const
{
    QVariantList out;
    if (!m_index)
        return out;
    for (const PageSuggestion &s : m_index->suggestPages(prefix, limit)) {
        QVariantMap m{ { u"path"_s, s.path }, { u"title"_s, s.title }, { u"folder"_s, paths::dirname(s.path) } };
        if (!s.alias.isEmpty())
            m.insert(u"alias"_s, s.alias);
        out.append(m);
    }
    return out;
}

QVariantList Workspace::suggestBlocks(const QString &text, int limit) const
{
    QVariantList out;
    if (!m_index)
        return out;
    for (const BlockRef &ref : m_index->suggestBlocks(text, limit)) {
        const ParsedNote *note = m_index->note(ref.path);
        if (!note || ref.blockIndex < 0 || ref.blockIndex >= note->blocks.size())
            continue;
        const Block &block = note->blocks.at(ref.blockIndex);
        QVariantMap m{ { u"path"_s, ref.path },
                       { u"line"_s, block.line },
                       { u"text"_s, lineOf(block.text) },
                       { u"title"_s, note->title } };
        if (!block.id.isEmpty())
            m.insert(u"blockId"_s, block.id);
        out.append(m);
    }
    return out;
}

QVariantList Workspace::suggestTags(const QString &prefix, int limit) const
{
    QVariantList out;
    if (!m_index || limit <= 0)
        return out;
    const QString key = text::foldKey(prefix.startsWith(u'#') ? QStringView(prefix).mid(1) : QStringView(prefix));
    const QList<TagCount> tags = m_index->tags(); // most used first
    QVariantList contains;
    for (const TagCount &t : tags) {
        const QString folded = text::foldKey(t.tag);
        const QVariantMap m{ { u"tag"_s, t.display.isEmpty() ? t.tag : t.display }, { u"count"_s, t.count } };
        if (key.isEmpty() || folded.startsWith(key))
            out.append(m);
        else if (folded.contains(key))
            contains.append(m);
        if (out.size() >= limit)
            return out;
    }
    for (const QVariant &m : std::as_const(contains)) {
        if (out.size() >= limit)
            break;
        out.append(m);
    }
    return out;
}

QString Workspace::resolveLink(const QString &target, const QString &fromPath) const
{
    if (!m_index)
        return {};
    return m_index->resolveLink(target, fromPath).value_or(QString());
}

QString Workspace::blockText(const QString &blockId) const
{
    if (!m_index)
        return {};
    const auto ref = m_index->blockById(blockId);
    if (!ref)
        return {};
    const ParsedNote *note = m_index->note(ref->path);
    if (!note || ref->blockIndex < 0 || ref->blockIndex >= note->blocks.size())
        return {};
    return note->blocks.at(ref->blockIndex).text;
}

QString Workspace::ensureBlockId(const QString &path, int line)
{
    if (!m_vault)
        return {};
    bool ok = false;
    const QString content = m_vault->readText(path, &ok);
    if (!ok)
        return {};
    const auto taken = [this](const QString &id) { return m_index && m_index->blockById(id).has_value(); };
    const markdown::EnsuredBlockId result = markdown::ensureBlockId(content, line, taken);
    if (result.id.isEmpty())
        return {};
    // Written as an ordinary change: an editor showing that page reloads it (fileChanged).
    if (result.content != content && !m_vault->writeText(path, result.content, ChangeSource::User))
        return {};
    return result.id;
}

QVariantList Workspace::templates() const
{
    QVariantList out;
    if (!m_vault)
        return out;
    const QString folder = paths::normalize(m_vault->config().templatesFolder);
    if (folder.isEmpty())
        return out;
    const QString prefix = folder + u'/';
    QStringList found;
    for (const FileStat &f : m_vault->markdownFiles()) {
        if (f.path.startsWith(prefix, Qt::CaseInsensitive))
            found.append(f.path);
    }
    // By title as people read them, not by code units ("journal" before "Spotkanie").
    std::sort(found.begin(), found.end(), [](const QString &a, const QString &b) {
        return QString::compare(paths::pageTitle(a), paths::pageTitle(b), Qt::CaseInsensitive) < 0;
    });
    for (const QString &path : std::as_const(found))
        out.append(pageEntry(path));
    return out;
}

// ---- EditorHost: assets ----------------------------------------------------------------------

QUrl Workspace::assetUrl(const QString &path) const
{
    if (!m_vault || path.isEmpty())
        return {};
    if (!m_dataUrlAssets)
        return QUrl::fromLocalFile(m_vault->absolutePath(paths::normalize(path)));
    const QString key = paths::normalize(path);
    const auto cached = m_dataUrls.constFind(key);
    if (cached != m_dataUrls.constEnd())
        return cached.value();
    bool ok = false;
    const QByteArray bytes = m_vault->readBinary(key, &ok);
    if (!ok)
        return {};
    const QString mime = QMimeDatabase().mimeTypeForFile(key, QMimeDatabase::MatchExtension).name();
    QUrl url(u"data:"_s + mime + u";base64,"_s + QString::fromLatin1(bytes.toBase64()));
    m_dataUrls.insert(key, url);
    return url;
}

QString Workspace::saveAttachment(const QString &fileName, const QByteArray &data)
{
    if (!m_vault)
        return {};
    QString name = paths::basename(QString(fileName).replace(u'\\', u'/')).trimmed();
    if (name.isEmpty())
        name = u"attachment-"_s + now().toString(u"yyyyMMdd-HHmmss"_s);
    // sanitizeFileName keeps the extension (dots are allowed) and drops separators/reserved chars.
    name = paths::sanitizeFileName(name);
    if (paths::extension(name).isEmpty()) {
        // Pasted images often come without a name: take the type from the bytes, so the embed
        // shows the image and other programs recognise the file. Plain text stays as it is.
        const QMimeType type = QMimeDatabase().mimeTypeForData(data);
        const QString suffix = type.preferredSuffix();
        if (!suffix.isEmpty() && !type.inherits(u"text/plain"_s) && !type.isDefault())
            name += u'.' + suffix;
    }
    const QString folder = paths::normalize(m_vault->config().attachmentsFolder);
    const VaultPath path = m_vault->createBinary(folder.isEmpty() ? name : paths::join(folder, name), data,
                                                 ChangeSource::User);
    if (path.isEmpty())
        Q_EMIT errorOccurred(tr("Could not save the attachment %1: %2").arg(name, m_vault->errorString()));
    return path;
}

// ---- EditorHost: context and navigation ------------------------------------------------------

QVariantMap Workspace::vaultConfig() const
{
    const VaultConfig c = m_vault ? m_vault->config() : VaultConfig();
    return {
        { u"journalsFolder"_s, c.journalsFolder },
        { u"journalFileFormat"_s, c.journalFileFormat },
        { u"newPagesFolder"_s, c.newPagesFolder },
        { u"attachmentsFolder"_s, c.attachmentsFolder },
        { u"templatesFolder"_s, c.templatesFolder },
        { u"journalTemplate"_s, c.journalTemplate },
        { u"defaultReminderTime"_s, c.defaultReminderTime.toString(u"HH:mm"_s) },
        { u"weekStartsOn"_s, int(c.weekStartsOn) },
    };
}

bool Workspace::setConfig(const QVariantMap &map)
{
    if (!m_vault)
        return false;
    VaultConfig c = m_vault->config();
    const auto folder = [&](const char *key, QString &field) {
        const QString k = QString::fromLatin1(key);
        if (map.contains(k))
            field = paths::normalize(map.value(k).toString());
    };
    folder("journalsFolder", c.journalsFolder);
    folder("newPagesFolder", c.newPagesFolder);
    folder("attachmentsFolder", c.attachmentsFolder);
    folder("templatesFolder", c.templatesFolder);
    folder("journalTemplate", c.journalTemplate);
    if (map.contains(u"journalFileFormat"_s)) {
        const QString format = map.value(u"journalFileFormat"_s).toString().trimmed();
        if (!isUsableJournalFormat(format))
            return false;
        c.journalFileFormat = format;
    }
    if (map.contains(u"defaultReminderTime"_s)) {
        const QTime t = QTime::fromString(map.value(u"defaultReminderTime"_s).toString().trimmed(), u"H:mm"_s);
        if (!t.isValid())
            return false;
        c.defaultReminderTime = t;
    }
    if (map.contains(u"weekStartsOn"_s)) {
        const int day = map.value(u"weekStartsOn"_s).toInt();
        if (day < 1 || day > 7)
            return false;
        c.weekStartsOn = Qt::DayOfWeek(day);
    }
    if (sameVaultConfig(c, m_vault->config()))
        return true;
    if (!m_vault->saveConfig(c)) {
        Q_EMIT errorOccurred(tr("Could not save the notebook settings: %1").arg(m_vault->errorString()));
        return false;
    }
    return true;
}

QStringList Workspace::configWarnings() const
{
    return m_vault ? m_vault->configWarnings() : QStringList();
}

void Workspace::navigate(const QVariantMap &route, bool newPane)
{
    Q_EMIT navigationRequested(resolveRoute(route), newPane);
}

QVariantMap Workspace::resolveRoute(const QVariantMap &route)
{
    // Only page routes the editor could not resolve itself: {view: "page", blockId} for a
    // ((block)) of another page, {view: "page", target} for a link to a page that does not exist.
    if (route.value(u"view"_s).toString() != u"page" || !route.value(u"path"_s).toString().isEmpty() || !m_vault)
        return route;
    QVariantMap out = route;
    // Right after opening the index is still being built: what looks unresolved may exist.
    if (indexing())
        finishIndexing();
    const QString blockId = route.value(u"blockId"_s).toString();
    if (!blockId.isEmpty() && m_index) {
        if (const auto ref = m_index->blockById(blockId)) {
            out.insert(u"path"_s, ref->path);
            return out;
        }
    }
    const QString target = route.value(u"target"_s).toString().trimmed();
    if (target.isEmpty())
        return out;
    QString path = resolveLink(target, QString());
    if (path.isEmpty())
        path = newPagePath(target);
    if (!path.isEmpty())
        out.insert(u"path"_s, path);
    return out;
}

QString Workspace::newPagePath(const QString &target) const
{
    if (!m_vault)
        return {};
    const VaultConfig config = m_vault->config();
    // [[2026-10-20]]: a day without a note yet opens that day's journal page.
    const QString name = withoutMdExtension(target).trimmed();
    QDate day = QDate::fromString(name, config.journalFileFormat);
    if (!day.isValid())
        day = QDate::fromString(name, Qt::ISODate);
    if (day.isValid())
        return markdown::journalPathForDate(day, config);
    // SYNTAX.md §2: an unresolved link points to a page in newPagesFolder ("Projekty/Dom" keeps
    // its folders). The page is not created here: the editor creates it with its first content.
    QStringList parts;
    for (const QString &part : name.split(u'/', Qt::SkipEmptyParts)) {
        if (!part.trimmed().isEmpty())
            parts.append(paths::sanitizeFileName(part.trimmed()));
    }
    if (parts.isEmpty())
        return {};
    const QString base = paths::normalize(config.newPagesFolder);
    const QString relative = parts.join(u'/') + u".md"_s;
    return base.isEmpty() ? relative : paths::join(base, relative);
}

void Workspace::openUrl(const QUrl &url)
{
    // Only links meant for the browser / mail client; never file:, javascript: or app schemes
    // from note content.
    static const QStringList allowed{ u"http"_s, u"https"_s, u"mailto"_s };
    if (!url.isValid() || !allowed.contains(url.scheme().toLower())) {
        qCWarning(lcWorkspace) << "Refusing to open" << url.scheme() << "link";
        return;
    }
    QDesktopServices::openUrl(url);
}

// ---- Pages ------------------------------------------------------------------------------------

QString Workspace::createPage(const QString &title, const QString &folder)
{
    if (!m_vault)
        return {};
    // "Projekty/Dom" creates the page "Dom" in the folder "Projekty".
    QStringList parts;
    for (const QString &part : withoutMdExtension(title).split(u'/', Qt::SkipEmptyParts)) {
        if (!part.trimmed().isEmpty())
            parts.append(paths::sanitizeFileName(part));
    }
    if (parts.isEmpty())
        return {};
    QString base = folder.isNull() ? m_vault->config().newPagesFolder : folder;
    base = paths::normalize(base);
    const QString relative = parts.join(u'/') + u".md"_s;
    const VaultPath path = m_vault->create(base.isEmpty() ? relative : paths::join(base, relative), QString(),
                                           ChangeSource::User);
    if (path.isEmpty())
        Q_EMIT errorOccurred(tr("Could not create the page: %1").arg(m_vault->errorString()));
    return path;
}

bool Workspace::createFolder(const QString &path)
{
    const QString folder = paths::normalize(path);
    if (!m_vault || folder.isEmpty() || !m_vault->createFolder(folder))
        return false;
    // Folders produce no Vault change of their own (only files do): tell the lists.
    ++m_revision;
    Q_EMIT revisionChanged();
    Q_EMIT filesChanged({ folder });
    return true;
}

QString Workspace::renamePage(const QString &rawPath, const QString &newTitle)
{
    if (!m_vault)
        return {};
    const QString path = paths::normalize(rawPath);
    const QString ext = paths::extension(path).isEmpty() ? u".md"_s : paths::extension(path);
    const QString title = paths::isMarkdown(path) ? withoutMdExtension(newTitle).trimmed() : newTitle.trimmed();
    if (!m_vault->exists(path) || title.isEmpty())
        return {};
    // Every file that links to the page must be in the index before it is asked (right after
    // opening, the index is still being built in the background).
    finishIndexing();
    const QString oldTitle = paths::pageTitle(path);
    const QString newName = paths::sanitizeFileName(title);
    const QString folder = paths::dirname(path);
    const QString newPath = folder.isEmpty() ? newName + ext : paths::join(folder, newName + ext);
    if (newPath == path)
        return path;
    const bool caseOnly = newPath.compare(path, Qt::CaseInsensitive) == 0;
    if (!caseOnly && m_vault->exists(newPath)) {
        Q_EMIT errorOccurred(tr("A page named “%1” already exists here.").arg(newName));
        return {};
    }
    // Files to rewrite, asked before the index learns about the rename.
    const QList<VaultPath> linking = m_index ? m_index->linksTo(path) : QList<VaultPath>();
    if (!m_vault->rename(path, newPath, ChangeSource::User)) {
        Q_EMIT errorOccurred(tr("Could not rename the page: %1").arg(m_vault->errorString()));
        return {};
    }
    if (paths::isMarkdown(newPath)) {
        for (const VaultPath &file : linking) {
            const VaultPath target = file == path ? newPath : file;
            bool ok = false;
            const QString content = m_vault->readText(target, &ok);
            if (!ok)
                continue;
            const QString rewritten = markdown::renameLinks(content, oldTitle, newName, path, newPath);
            if (rewritten != content)
                m_vault->writeText(target, rewritten, ChangeSource::User);
        }
    }
    return newPath;
}

QString Workspace::renameFolder(const QString &rawPath, const QString &newName)
{
    if (!m_vault)
        return {};
    const QString path = paths::normalize(rawPath);
    const QString name = paths::sanitizeFileName(newName.trimmed());
    if (path.isEmpty() || newName.trimmed().isEmpty())
        return {};
    const QString parent = paths::dirname(path);
    const QString newPath = parent.isEmpty() ? name : paths::join(parent, name);
    if (newPath == path)
        return path;
    finishIndexing(); // every linking file must be known (see renamePage)
    // Path-qualified links ([[Projekty/Dom]]) to pages inside the folder must follow the move.
    struct Rewrite {
        VaultPath oldPath;
        QList<VaultPath> linking;
    };
    QList<Rewrite> rewrites;
    const QString prefix = path + u'/';
    if (m_index) {
        for (const FileStat &f : m_vault->markdownFiles()) {
            if (f.path.startsWith(prefix))
                rewrites.append({ f.path, m_index->linksTo(f.path) });
        }
    }
    if (!m_vault->rename(path, newPath, ChangeSource::User)) {
        Q_EMIT errorOccurred(tr("Could not rename the folder: %1").arg(m_vault->errorString()));
        return {};
    }
    const auto moved = [&](const VaultPath &p) { return p.startsWith(prefix) ? newPath + u'/' + p.mid(prefix.size()) : p; };
    for (const Rewrite &r : std::as_const(rewrites)) {
        const QString title = paths::pageTitle(r.oldPath);
        for (const VaultPath &file : r.linking) {
            const VaultPath target = moved(file);
            bool ok = false;
            const QString content = m_vault->readText(target, &ok);
            if (!ok)
                continue;
            const QString rewritten = markdown::renameLinks(content, title, title, r.oldPath, moved(r.oldPath));
            if (rewritten != content)
                m_vault->writeText(target, rewritten, ChangeSource::User);
        }
    }
    return newPath;
}

QString Workspace::deletePage(const QString &rawPath)
{
    if (!m_vault)
        return {};
    const QString path = paths::normalize(rawPath);
    if (path.isEmpty() || !m_vault->exists(path))
        return {};
    QSet<QString> before;
    for (const FileStat &f : m_vault->trash())
        before.insert(f.path);
    if (!m_vault->remove(path, ChangeSource::User)) {
        Q_EMIT errorOccurred(tr("Could not move “%1” to the trash: %2").arg(paths::pageTitle(path), m_vault->errorString()));
        return {};
    }
    forgetPath(path);
    // The new entry in the trash: ".trash/<path with a time stamp>" (a folder: its top entry).
    const int depth = int(path.count(u'/')) + 2; // ".trash" + the path's segments
    for (const FileStat &f : m_vault->trash()) {
        if (before.contains(f.path))
            continue;
        return f.path.split(u'/').mid(0, depth).join(u'/');
    }
    return vaultTrashFolder() + u'/' + path;
}

QString Workspace::restore(const QString &trashPath)
{
    if (!m_vault)
        return {};
    const VaultPath path = m_vault->restore(trashPath, ChangeSource::User);
    if (path.isEmpty())
        Q_EMIT errorOccurred(tr("Could not restore the file: %1").arg(m_vault->errorString()));
    return path;
}

// ---- Journals ---------------------------------------------------------------------------------

QString Workspace::journalPath(const QString &date) const
{
    const QDate d = QDate::fromString(date, Qt::ISODate);
    if (!d.isValid())
        return {};
    return markdown::journalPathForDate(d, m_vault ? m_vault->config() : VaultConfig());
}

QString Workspace::templateText(const QString &templatePath, const QDateTime &when, const QString &title) const
{
    bool ok = false;
    const QString tpl = m_vault->readText(paths::normalize(templatePath), &ok);
    if (!ok)
        return {};
    markdown::TemplateVars vars;
    vars.now = when;
    vars.title = title;
    vars.locale = m_language == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish;
    return markdown::applyTemplate(tpl, vars).text;
}

QString Workspace::ensureJournal(const QString &date)
{
    if (!m_vault)
        return {};
    const QDate d = QDate::fromString(date, Qt::ISODate);
    if (!d.isValid())
        return {};
    const QString path = markdown::journalPathForDate(d, m_vault->config());
    // Also check the disk: a note that just arrived (Syncthing) may not be known yet.
    if (m_vault->exists(path) || QFileInfo::exists(m_vault->absolutePath(path)))
        return path;
    // The day may have a note under another name the journal folder accepts: Logseq's
    // "2026_10_14.md", "Journals/" capitalised on another device, the previous file name format.
    // Never create a second note for the same day next to it.
    const QString existing = existingJournal(d);
    if (!existing.isEmpty())
        return existing;
    QString content;
    const QString tpl = paths::normalize(m_vault->config().journalTemplate);
    if (!tpl.isEmpty() && m_vault->exists(tpl))
        content = templateText(tpl, QDateTime(d, now().time()), paths::pageTitle(path));
    if (!m_vault->writeText(path, content, ChangeSource::User)) {
        Q_EMIT errorOccurred(tr("Could not create the journal page: %1").arg(m_vault->errorString()));
        return {};
    }
    return path;
}

QString Workspace::existingJournal(QDate date) const
{
    if (!m_vault)
        return {};
    const VaultConfig config = m_vault->config();
    const QString preferred = config.journalsFolder.isEmpty() ? QString() : config.journalsFolder + u'/';
    const auto inPreferred = [&](const QString &path) { return !preferred.isEmpty() && path.startsWith(preferred); };
    QString found;
    for (const FileStat &f : m_vault->markdownFiles()) {
        if (markdown::journalDateFromPath(f.path, config) != date)
            continue;
        // Several (e.g. "journals/" and "Journals/"): the configured spelling, else the first.
        if (found.isEmpty() || (inPreferred(f.path) && !inPreferred(found)))
            found = f.path;
    }
    return found;
}

QString Workspace::captureToJournal(const QString &text)
{
    static const QRegularExpression listMarker(u"^([-*+]|\\d+[.)])\\s"_s);
    QStringList lines = QString(text).remove(u'\r').split(u'\n');
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty())
        lines.removeFirst();
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    if (lines.isEmpty() || !m_vault)
        return {};
    QString first = lines.first().trimmed();
    if (!listMarker.match(first).hasMatch())
        first.prepend(u"- "_s);
    QString block = first;
    for (qsizetype i = 1; i < lines.size(); ++i)
        block += u"\n  "_s + lines.at(i).trimmed();
    return appendToTodaysJournal(block);
}

QString Workspace::quickCapture(const QString &text)
{
    if (!m_vault)
        return {};
    const CaptureLine line = buildCaptureLine(text, now(), m_vault->config().weekStartsOn,
                                              m_language == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish);
    if (line.kind == CaptureLine::Kind::Empty)
        return {};
    return appendToTodaysJournal(line.block);
}

QVariantMap Workspace::quickCapturePreview(const QString &text) const
{
    const dates::UiLocale locale = m_language == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish;
    const QDateTime current = now();
    const CaptureLine line = buildCaptureLine(text, current, m_vault ? m_vault->config().weekStartsOn : Qt::Monday, locale);
    const bool dated = line.when.isValid();
    return {
        { u"kind"_s, captureKindName(line.kind) },
        { u"text"_s, line.text },
        { u"date"_s, dated ? line.when.date.toString(Qt::ISODate) : QString() },
        { u"time"_s, dated && line.when.time ? line.when.time->toString(u"HH:mm"_s) : QString() },
        { u"when"_s, dated ? dates::formatHuman(line.when, locale, current.date()) : QString() },
        { u"line"_s, line.block.section(u'\n', 0, 0) },
    };
}

QString Workspace::appendToTodaysJournal(const QString &block)
{
    static const QRegularExpression emptyBullet(u"^\\s*[-*+]\\s*$"_s);
    if (!m_vault || block.trimmed().isEmpty())
        return {};
    const QString path = ensureJournal(now().date().toString(Qt::ISODate));
    if (path.isEmpty())
        return {};
    QString content = m_vault->readText(path);
    const QString eol = text::detectEol(content);

    // A template often ends with an empty bullet ("- "): fill that instead of adding below it.
    QList<text::Line> existing = text::splitLines(content);
    while (!existing.isEmpty() && existing.last().text.trimmed().isEmpty())
        existing.removeLast();
    if (!existing.isEmpty() && emptyBullet.match(existing.last().text).hasMatch())
        existing.removeLast();
    content = text::joinLines(existing);
    if (!content.isEmpty() && !content.endsWith(u'\n'))
        content += eol;
    content += QString(block).replace(u'\n', eol) + eol;
    return writeFile(path, content) ? path : QString();
}

// ---- Sidebar lists ---------------------------------------------------------------------------

QStringList Workspace::storedList(const QString &key) const
{
    QStringList out;
    if (!m_vault)
        return out;
    const QJsonArray array = m_vault->store()->get(key).toArray();
    for (const QJsonValue &v : array) {
        if (v.isString())
            out.append(v.toString());
    }
    return out;
}

void Workspace::storeList(const QString &key, const QStringList &list)
{
    if (!m_vault)
        return;
    m_vault->store()->set(key, QJsonArray::fromStringList(list));
}

QVariantList Workspace::favorites() const
{
    QVariantList out;
    for (const QString &path : storedList(QString::fromLatin1(kFavorites))) {
        if (m_vault && m_vault->exists(path))
            out.append(pageEntry(path));
    }
    return out;
}

QVariantList Workspace::recentPages() const
{
    QVariantList out;
    for (const QString &path : storedList(QString::fromLatin1(kRecentPages))) {
        if (m_vault && m_vault->exists(path))
            out.append(pageEntry(path));
    }
    return out;
}

bool Workspace::isFavorite(const QString &path) const
{
    return storedList(QString::fromLatin1(kFavorites)).contains(paths::normalize(path));
}

void Workspace::setFavorite(const QString &rawPath, bool favorite)
{
    const QString path = paths::normalize(rawPath);
    QStringList list = storedList(QString::fromLatin1(kFavorites));
    if (list.contains(path) == favorite || path.isEmpty())
        return;
    if (favorite)
        list.append(path);
    else
        list.removeAll(path);
    storeList(QString::fromLatin1(kFavorites), list);
    Q_EMIT favoritesChanged();
}

void Workspace::notePageOpened(const QString &rawPath)
{
    const QString path = paths::normalize(rawPath);
    if (!m_vault || path.isEmpty() || !m_vault->exists(path))
        return;
    QStringList list = storedList(QString::fromLatin1(kRecentPages));
    if (!list.isEmpty() && list.first() == path)
        return;
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > MaxRecentPages)
        list.removeLast();
    storeList(QString::fromLatin1(kRecentPages), list);
    Q_EMIT recentPagesChanged();
}

QVariantList Workspace::foldState(const QString &path) const
{
    QVariantList out;
    if (!m_vault)
        return out;
    for (const QJsonValue &v : m_vault->store()->get(foldStateKey(paths::normalize(path))).toArray()) {
        if (v.isDouble() && v.toInt() >= 0)
            out.append(v.toInt());
    }
    return out;
}

void Workspace::setFoldState(const QString &rawPath, const QVariantList &lines)
{
    const QString path = paths::normalize(rawPath);
    if (!m_vault || path.isEmpty())
        return;
    QList<int> sorted;
    for (const QVariant &v : lines) {
        bool ok = false;
        const int line = v.toInt(&ok);
        if (ok && line >= 0 && !sorted.contains(line))
            sorted.append(line);
    }
    std::sort(sorted.begin(), sorted.end());
    if (sorted.isEmpty()) {
        m_vault->store()->remove(foldStateKey(path));
        return;
    }
    QJsonArray array;
    for (int line : std::as_const(sorted))
        array.append(line);
    m_vault->store()->set(foldStateKey(path), array);
}

void Workspace::forgetPath(const QString &path)
{
    const QString prefix = path + u'/';
    if (m_vault) {
        m_vault->store()->remove(foldStateKey(path));
        m_vault->store()->removePrefix(foldStateKey(prefix));
    }
    for (const char *key : { kFavorites, kRecentPages }) {
        const QString k = QString::fromLatin1(key);
        QStringList list = storedList(k);
        const auto before = list.size();
        list.removeIf([&](const QString &p) { return p == path || p.startsWith(prefix); });
        if (list.size() != before) {
            storeList(k, list);
            if (key == kFavorites)
                Q_EMIT favoritesChanged();
            else
                Q_EMIT recentPagesChanged();
        }
    }
}

void Workspace::renamePathInLists(const QString &oldPath, const QString &newPath)
{
    if (m_vault) {
        LocalStore *store = m_vault->store();
        const QJsonValue folds = store->get(foldStateKey(oldPath));
        if (!folds.isUndefined()) {
            store->remove(foldStateKey(oldPath));
            store->set(foldStateKey(newPath), folds);
        }
    }
    for (const char *key : { kFavorites, kRecentPages }) {
        const QString k = QString::fromLatin1(key);
        QStringList list = storedList(k);
        const qsizetype i = list.indexOf(oldPath);
        if (i < 0)
            continue;
        list[i] = newPath;
        list.removeDuplicates();
        storeList(k, list);
        if (key == kFavorites)
            Q_EMIT favoritesChanged();
        else
            Q_EMIT recentPagesChanged();
    }
}

QVariantList Workspace::switcherPages(const QString &query, int limit) const
{
    QVariantList out;
    if (!m_vault || limit <= 0)
        return out;
    QSet<QString> seen;
    const auto add = [&](const QString &path, const QString &alias) {
        if (seen.contains(path) || out.size() >= limit || paths::isIgnored(path))
            return;
        seen.insert(path);
        const QString folder = paths::dirname(path);
        QString subtitle = folder;
        if (!alias.isEmpty())
            subtitle = folder.isEmpty() ? alias : alias + u" · "_s + folder;
        const bool journal = !journalDate(path).isEmpty();
        out.append(QVariantMap{ { u"title"_s, paths::pageTitle(path) },
                                { u"subtitle"_s, subtitle },
                                { u"icon"_s, journal ? u"calendar"_s : u"file-text"_s },
                                { u"path"_s, path } });
    };
    const QString q = query.trimmed();
    if (q.isEmpty()) {
        for (const QString &path : storedList(QString::fromLatin1(kRecentPages))) {
            if (m_vault->exists(path))
                add(path, {});
        }
    }
    if (m_index) {
        for (const PageSuggestion &s : m_index->suggestPages(q, limit))
            add(s.path, s.alias);
    }
    return out;
}

} // namespace brulion::shell
