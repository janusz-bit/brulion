#include "notebook/session.h"

#include "index/vaultindex.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>

using namespace Qt::StringLiterals;

namespace brulion::notebook {

QString deviceStatePath(const QString &appDataDir, const QString &vaultRoot)
{
    return QDir::cleanPath(appDataDir + u"/vaults/"_s + Vault::idForPath(vaultRoot));
}

QString contentHash(const QByteArray &bytes)
{
    return text::fnv1a64Hex(bytes);
}

QString decodeText(const QByteArray &bytes)
{
    if (bytes.startsWith("\xEF\xBB\xBF"))
        return QString::fromUtf8(QByteArrayView(bytes).sliced(3));
    return QString::fromUtf8(bytes);
}

Notebook::Notebook() = default;

Notebook::~Notebook()
{
    if (m_vault)
        m_vault->stopWatching();
}

bool Notebook::open(const QString &rawRoot, const Options &options, QString *error, OpenError *kind)
{
    const auto failed = [&](OpenError k, const QString &message) {
        if (error)
            *error = message;
        if (kind)
            *kind = k;
        return false;
    };
    const QString root = QDir::cleanPath(QFileInfo(rawRoot).absoluteFilePath());
    const QFileInfo info(root);
    // Vault::open() would create a missing folder: a mistyped path must not become a notebook.
    if (rawRoot.trimmed().isEmpty() || !info.exists())
        return failed(OpenError::Missing, u"No notebook folder at %1"_s.arg(root));
    if (!info.isDir())
        return failed(OpenError::NotAFolder, u"%1 is not a folder"_s.arg(root));
    auto vault = std::make_unique<Vault>(root, options.stateDir);
    if (options.clock)
        vault->setClock(options.clock);
    vault->setHistoryReason(options.historyReason);
    if (History *history = vault->history()) {
        // Every replaced text is kept: an agent's edits can be undone one by one.
        HistoryPolicy policy = history->policy();
        policy.throttleSeconds = 0;
        history->setPolicy(policy);
    }
    QString err;
    if (!vault->open(&err))
        return failed(OpenError::Failed, err);
    if (options.watch) {
        vault->startWatching();
        vault->setRescanInterval(120 * 1000);
    }
    m_index.reset();
    m_vault = std::move(vault);
    m_stateDir = options.stateDir;
    QObject::connect(m_vault.get(), &Vault::changed, m_vault.get(),
                     [this](const QList<VaultChange> &changes) { applyChanges(changes); });
    QObject::connect(m_vault.get(), &Vault::configChanged, m_vault.get(), [this] {
        if (m_index)
            m_index->setConfig(m_vault->config());
    });
    if (kind)
        *kind = OpenError::None;
    return true;
}

bool Notebook::isOpen() const
{
    return m_vault != nullptr;
}

Vault &Notebook::vault() const
{
    Q_ASSERT(m_vault);
    return *m_vault;
}

QString Notebook::root() const
{
    return m_vault ? m_vault->rootPath() : QString();
}

QString Notebook::stateDir() const
{
    return m_stateDir;
}

void Notebook::indexFile(const VaultPath &path)
{
    if (paths::isIgnored(path))
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

VaultIndex &Notebook::index()
{
    Q_ASSERT(m_vault);
    if (!m_index) {
        m_index = std::make_unique<VaultIndex>(m_vault->config());
        VaultIndex::Batch batch(*m_index);
        for (const FileStat &f : m_vault->files())
            indexFile(f.path);
    }
    return *m_index;
}

bool Notebook::hasIndex() const
{
    return m_index != nullptr;
}

void Notebook::applyChanges(const QList<VaultChange> &changes)
{
    if (!m_index)
        return;
    VaultIndex::Batch batch(*m_index);
    for (const VaultChange &c : changes) {
        switch (c.type) {
        case ChangeType::Created:
        case ChangeType::Modified: indexFile(c.path); break;
        case ChangeType::Deleted: m_index->remove(c.path); break;
        case ChangeType::Renamed: {
            QString content;
            if (paths::isMarkdown(c.path))
                content = m_vault->readText(c.path);
            if (paths::isIgnored(c.path))
                m_index->remove(c.oldPath);
            else
                m_index->rename(c.oldPath, c.path, content);
            break;
        }
        }
    }
}

std::unique_ptr<QLockFile> Notebook::lock(int timeoutMs, QString *error) const
{
    QString file;
    if (!m_stateDir.isEmpty()) {
        QDir().mkpath(m_stateDir);
        file = QDir(m_stateDir).filePath(u"cli.lock"_s);
    } else {
        file = QDir::temp().filePath(u"brulion-cli-"_s + (m_vault ? m_vault->id() : QString()) + u".lock"_s);
    }
    auto lock = std::make_unique<QLockFile>(file);
    // A lock is never taken over by age (a long command), only when its process is gone.
    lock->setStaleLockTime(0);
    if (!lock->tryLock(timeoutMs)) {
        if (error) {
            *error = lock->error() == QLockFile::LockFailedError ? u"Another brulion-cli process holds %1"_s.arg(file)
                                                                 : u"Cannot create the lock file %1"_s.arg(file);
        }
        return nullptr;
    }
    return lock;
}

Notebook::Current Notebook::read(const VaultPath &rawPath) const
{
    Current out;
    if (!m_vault)
        return out;
    const QString path = paths::normalize(rawPath);
    QFile file(m_vault->absolutePath(path));
    if (!QFileInfo(file.fileName()).isFile() || !file.open(QIODevice::ReadOnly))
        return out;
    out.exists = true;
    out.bytes = file.readAll();
    out.text = decodeText(out.bytes);
    out.hash = contentHash(out.bytes);
    // Invalid UTF-8 would come back as U+FFFD: the round trip tells.
    const QByteArrayView body =
            out.bytes.startsWith("\xEF\xBB\xBF") ? QByteArrayView(out.bytes).sliced(3) : QByteArrayView(out.bytes);
    out.validUtf8 = out.text.toUtf8() == body;
    return out;
}

void Notebook::setBeforeWrite(std::function<void(const VaultPath &, int)> hook)
{
    m_beforeWrite = std::move(hook);
}

Notebook::WriteResult Notebook::edit(const VaultPath &rawPath, const Edit &edit, const WriteOptions &options)
{
    WriteResult out;
    const QString path = paths::normalize(rawPath);
    out.path = path;
    if (!m_vault || path.isEmpty()) {
        out.error = u"No notebook is open"_s;
        return out;
    }
    const int attempts = std::max(1, options.attempts);
    for (int attempt = 0; attempt < attempts; ++attempt) {
        const Current current = read(path);
        if (!current.exists && !options.mayCreate) {
            out.status = WriteResult::NotFound;
            return out;
        }
        if (current.exists && !current.validUtf8) {
            out.status = WriteResult::Encoding;
            return out;
        }
        out.hashBefore = current.hash;
        if (!options.expectHash.isEmpty() && options.expectHash != current.hash) {
            out.status = WriteResult::HashMismatch;
            return out;
        }
        QString next;
        if (!edit(current.text, current.exists, &next)) {
            out.status = WriteResult::Refused;
            return out;
        }
        out.before = current.text;
        out.after = next;
        if (current.exists && next == current.text) {
            out.status = WriteResult::Unchanged;
            out.hashAfter = current.hash;
            return out;
        }
        if (options.dryRun) {
            out.status = WriteResult::DryRun;
            out.created = !current.exists;
            return out;
        }
        if (m_beforeWrite)
            m_beforeWrite(path, attempt);
        // A file that appeared meanwhile is "changed" too: its text is not the empty expected one.
        const QString expected = current.exists ? current.text : QString();
        if (!current.exists && QFileInfo::exists(m_vault->absolutePath(path)))
            continue;
        switch (m_vault->writeTextIfUnchanged(path, next, expected, ChangeSource::User)) {
        case WriteOutcome::Written: {
            out.status = WriteResult::Written;
            out.created = !current.exists;
            out.hashAfter = read(path).hash;
            return out;
        }
        case WriteOutcome::Changed: continue; // another program wrote it: apply the edit to its text
        case WriteOutcome::Failed:
            out.status = WriteResult::Failed;
            out.error = m_vault->errorString();
            return out;
        }
    }
    out.status = WriteResult::Conflict;
    return out;
}

} // namespace brulion::notebook
