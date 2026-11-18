// Vault::writeTextIfUnchangedAsync(): an editor's save with its work on the writer (Zed's save).
// The vault's thread takes the text and what it knows of the file (WriteJob), the writer reads,
// compares, snapshots, writes, stats and hashes (WriteJob::run(), no vault state), and the vault's
// thread merges the result (mergeWrite()). Every other vault call waits for the writes in flight
// first (waitForWrites()), so nothing ever sees a write half-way or lands before or after it.
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault_p.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QMutexLocker>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion {

// ---------------------------------------------------------------------------------------------
// WriteJob: on the writer
// ---------------------------------------------------------------------------------------------

void WriteJob::run()
{
    // Anything but the plain case (the file the vault knows, as it knows it) is decided again on
    // the vault's thread by writeTextIfUnchanged(), which knows every rule.
    const auto fallback = [this] { result = Result::Fallback; };
    for (const QString &folder : std::as_const(ancestorsAbsolute)) {
        if (QFileInfo(folder).isSymLink())
            return fallback(); // (a followed folder link or one that is not the vault's)
    }
    const QFileInfo info(absolute);
    if (info.isSymLink() || !info.isFile() || !sameStat(statOf(info, path), known))
        return fallback();
    vaultutil::ContentRead read = vaultutil::readContent(absolute, kVaultMaxReadBytes);
    if (!read.ok())
        return fallback();
    // The exact bytes decide, never a hash (sync can bring content crafted to collide).
    if (vaultutil::decodeUtf8(read.bytes) != expected) {
        readHash = text::fnv1a64Hex(read.bytes);
        result = Result::Changed;
        return;
    }
    if (!knownHash.isEmpty() && text::fnv1a64Hex(read.bytes) != knownHash)
        return fallback(); // other bytes of the same text behind an unchanged stat (a BOM added)
    data = content.toUtf8();
    // Text written to a file that has a byte order mark keeps it.
    if (!data.startsWith(vaultutil::utf8Bom()) && read.bytes.startsWith(vaultutil::utf8Bom()))
        data.prepend(vaultutil::utf8Bom());
    if (data == read.bytes) {
        result = Result::Unchanged;
        return;
    }
    // The bytes as they were (what the vault's content cache holds for them): a BOM, CRLF or a
    // legacy encoding stays recoverable. Files too big for the cache are not kept, as before.
    if (history && read.bytes.size() <= ContentCache::kMaxItemBytes
        && !history->snapshotBytesAt(path, read.bytes, historyReason, at))
        historyError = history->errorString();
    const std::function<void()> hook = beforeRename ? std::function<void()>([this] { beforeRename(path); }) : nullptr;
    save = vaultutil::saveAtomicallyStamped(absolute, data, hook);
    if (!save.ok) {
        error = save.error;
        result = Result::Failed;
        return;
    }
    const QFileInfo after(absolute);
    if (!after.isFile() || after.isSymLink()) {
        error = Vault::tr("%1 disappeared right after it was written").arg(path);
        result = Result::Failed;
        return;
    }
    written = statOf(after, path);
    dataHash = text::fnv1a64Hex(data);
    if (afterWrite)
        afterWrite(vaultutil::decodeUtf8(data));
    result = Result::Written;
}

void WriteJob::markFinished()
{
    const QMutexLocker lock(&mutex);
    finished = true;
    finishedCondition.wakeAll();
}

void WriteJob::wait()
{
    QMutexLocker lock(&mutex);
    while (!finished)
        finishedCondition.wait(&mutex);
}

// ---------------------------------------------------------------------------------------------
// VaultPrivate: starting, waiting, merging
// ---------------------------------------------------------------------------------------------

quint64 VaultPrivate::writeAsync(AsyncTextWrite write)
{
    const quint64 ticket = ++nextTicket;
    const VaultPath path = paths::normalize(write.path);
    const VaultPath dir = paths::dirname(path);
    const auto known = files.constFind(path);
    // The plain case only: an open vault on its storage, a known file in a known folder whose
    // content the vault does not suspect. Anything else is written at once, on this thread.
    const bool plain = isOpen && !path.isEmpty() && vaultutil::isTracked(path) && known != files.cend() && !known->isDir
                       && !staleContent.contains(path) && (dir.isEmpty() || dirs.contains(dir)) && requireStorage();
    if (!plain) {
        const WriteOutcome outcome = q->writeTextIfUnchanged(path, write.content, write.expected, write.source);
        if (write.done)
            write.done(outcome);
        return ticket;
    }
    auto job = std::make_shared<WriteJob>();
    job->ticket = ticket;
    job->generation = generation;
    job->path = path;
    job->dir = dir;
    job->absolute = absPath(path);
    for (const VaultPath &folder : vaultutil::ancestors(path))
        job->ancestorsAbsolute.append(absPath(folder));
    job->content = std::move(write.content);
    job->expected = std::move(write.expected);
    job->source = write.source;
    job->historyReason = historyReason.isEmpty() ? changeSourceName(write.source) : historyReason;
    job->at = now();
    job->history = history.get();
    job->known = *known;
    job->knownHash = hashes.value(path);
    job->pendingBefore = foreignBatchPending(dir);
    job->afterWrite = std::move(write.afterWrite);
    job->beforeRename = beforeRenameHook;
    job->done = std::move(write.done);
    writes.append(job);
    if (!writeExecutor) {
        // WebAssembly, no threads: the same work, at once.
        job->run();
        job->markFinished();
        mergeWrite(job);
        return ticket;
    }
    writeExecutor([this, job] {
        job->run();
        // Posted before the job counts as finished: a vault that waits for it (its destructor
        // too) is still there, and a merge posted for a vault destroyed since is dropped.
        QMetaObject::invokeMethod(q, [this, job] { mergeWrite(job); }, Qt::QueuedConnection);
        job->markFinished();
    });
    return ticket;
}

void VaultPrivate::waitForWrites()
{
    // Oldest first; a merge may start (and wait for) others itself.
    while (!writes.isEmpty())
        mergeWrite(writes.constFirst());
}

void VaultPrivate::waitForWritesTo(const VaultPath &path)
{
    if (std::ranges::any_of(writes, [&](const std::shared_ptr<WriteJob> &job) { return job->path == path; }))
        waitForWrites();
}

void VaultPrivate::abandonWrites()
{
    for (const std::shared_ptr<WriteJob> &job : std::as_const(writes)) {
        job->wait();
        job->merged = true;
        job->done = {};
    }
    writes.clear();
}

void VaultPrivate::mergeWrite(const std::shared_ptr<WriteJob> &job)
{
    if (job->merged)
        return;
    job->wait();
    job->merged = true;
    writes.removeOne(job);
    const std::function<void(WriteOutcome)> done = std::exchange(job->done, {});
    job->afterWrite = {};
    if (job->generation != generation || !isOpen) {
        // The notebook was closed or opened again meanwhile (open() and an app's close wait: hardly
        // ever): the writer is told it failed, so it does not wait for an answer forever.
        if (done)
            done(WriteOutcome::Failed);
        return;
    }
    const VaultPath &path = job->path;
    QList<VaultChange> changes;
    WriteOutcome outcome = WriteOutcome::Written;
    switch (job->result) {
    case WriteJob::Result::Written:
        if (!job->historyError.isEmpty())
            qCWarning(lcVault).noquote() << "History snapshot failed:" << job->historyError;
        trackFile(*job->written);
        // Known hash = what was written: the watcher's echo of this write is recognised as such.
        rememberContent(path, job->data, true, job->dataHash);
        if (watcher)
            noteOwnWrite(job->dir, job->save, job->pendingBefore);
        watchFile(path);
        changes.append(VaultChange{ChangeType::Modified, path, {}, job->source});
        if (!job->content.startsWith(QChar(0xFEFF))) { // (else the file's text lacks it)
            ownTexts.removeIf([&](const OwnText &own) { return own.path == path; });
            ownTexts.append(OwnText{path, job->content, job->dataHash});
            if (ownTexts.size() > kMaxOwnTexts)
                ownTexts.removeFirst();
        }
        break;
    case WriteJob::Result::Unchanged: break;
    case WriteJob::Result::Changed:
        // Take the other program's change in now (External, its predecessor kept in History).
        if (const auto hash = hashes.constFind(path); hash != hashes.cend() && *hash != job->readHash)
            staleContent.insert(path); // other content behind an unchanged stat
        changes = reconcile(ScanScope{{}, {}, {path}}, ChangeSource::External);
        outcome = WriteOutcome::Changed;
        break;
    case WriteJob::Result::Fallback:
        outcome = q->writeTextIfUnchanged(path, job->content, job->expected, job->source);
        break;
    case WriteJob::Result::Failed:
        fail(Vault::tr("Cannot write %1: %2").arg(path, job->error));
        outcome = WriteOutcome::Failed;
        break;
    }
    if (done) {
        reporting.append(job->ticket); // (reportingWrite() in `done` too)
        done(outcome);
        reporting.removeLast();
    }
    finish(changes, job->ticket);
    // A watcher batch that waited for the write goes on.
    if (writes.isEmpty() && (!pendingDirs.isEmpty() || !pendingFiles.isEmpty()) && !batchTimer.isActive())
        batchTimer.start();
}

// ---------------------------------------------------------------------------------------------
// Vault
// ---------------------------------------------------------------------------------------------

quint64 Vault::writeTextIfUnchangedAsync(AsyncTextWrite write)
{
    return d->writeAsync(std::move(write));
}

void Vault::setWriteExecutor(std::function<void(std::function<void()>)> start)
{
    d->waitForWrites();
    d->writeExecutor = std::move(start);
}

bool Vault::hasWritesInFlight() const
{
    return !d->writes.isEmpty();
}

void Vault::waitForWrites()
{
    d->waitForWrites();
}

quint64 Vault::reportingWrite() const
{
    return d->reporting.isEmpty() ? 0 : d->reporting.constLast();
}

bool Vault::holdsText(const VaultPath &rawPath, const QString &text) const
{
    const VaultPath path = paths::normalize(rawPath);
    if (std::ranges::any_of(d->writes, [&](const auto &job) { return job->path == path; }))
        return false;
    const auto own = std::ranges::find_if(d->ownTexts, [&](const auto &o) { return o.path == path; });
    // The same string (not equal text: that would take a comparison of the whole note), and the
    // vault knows of no other content there since.
    return own != d->ownTexts.cend() && own->text.size() == text.size() && own->text.constData() == text.constData()
           && d->files.contains(path) && d->hashes.value(path) == own->hash && !d->staleContent.contains(path);
}

void Vault::setBeforeRenameHook(std::function<void(const VaultPath &path)> hook)
{
    d->waitForWrites();
    d->beforeRenameHook = std::move(hook);
}

} // namespace brulion
