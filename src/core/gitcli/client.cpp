#include "gitcli/client.h"

#include "gitcli/messages_p.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#if QT_CONFIG(process)
#include <QProcess>
#include <QProcessEnvironment>
#endif

using namespace Qt::StringLiterals;

namespace brulion::gitcli {

using detail::GitMessages;
using detail::inTree;
using detail::shortOid;

namespace {

constexpr qsizetype DefaultMaxOutput = qsizetype(256) * 1024 * 1024;
constexpr qsizetype MaxErrorBytes = 256 * 1024;

/// The variables that would make git work on another repository, index or work tree than the
/// folder's (inherited from a hook, a terminal inside a repository …).
constexpr const char *RepositoryVariables[] = {
        "GIT_DIR",
        "GIT_WORK_TREE",
        "GIT_INDEX_FILE",
        "GIT_OBJECT_DIRECTORY",
        "GIT_ALTERNATE_OBJECT_DIRECTORIES",
        "GIT_COMMON_DIR",
        "GIT_NAMESPACE",
        "GIT_PREFIX",
        "GIT_LITERAL_PATHSPECS",
        "GIT_GLOB_PATHSPECS",
        "GIT_NOGLOB_PATHSPECS",
        "GIT_ICASE_PATHSPECS",
};

/// The lines worth showing of a program's output: those git starts with "fatal:" / "error:"
/// (the prefix taken off), else the last few.
QString lastLines(const QString &text)
{
    QStringList lines;
    QStringList errors;
    for (const QString &raw : text.split(u'\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(u"fatal: "_s))
            errors.append(line.sliced(7));
        else if (line.startsWith(u"error: "_s))
            errors.append(line.sliced(7));
        lines.append(line);
    }
    const QStringList &pick = errors.isEmpty() ? lines : errors;
    return pick.mid(qMax<qsizetype>(0, pick.size() - 4)).join(u'\n');
}

/// A program's output as lines, empty ones left out: '\n'-separated, a '\r' before it dropped (a
/// wrapper on Windows may write CRLF where git writes LF).
QStringList outputLines(const QByteArray &out)
{
    QStringList lines = QString::fromUtf8(out).split(u'\n', Qt::SkipEmptyParts);
    for (QString &line : lines) {
        if (line.endsWith(u'\r'))
            line.chop(1);
    }
    lines.removeAll(QString());
    return lines;
}

} // namespace

QString RunResult::message() const
{
    if (!failure.isEmpty())
        return failure;
    const QString e = lastLines(err);
    if (!e.isEmpty())
        return e;
    const QString o = lastLines(QString::fromUtf8(out.left(64 * 1024)));
    if (!o.isEmpty())
        return o;
    return GitMessages::tr("The command ended with code %1").arg(exitCode);
}

// ---- Runs --------------------------------------------------------------------------------------

struct Client::Run {
#if QT_CONFIG(process)
    QProcess *process = nullptr;
#endif
    QTimer *timer = nullptr;
    RunResult result;
    Done done;
    qsizetype maxOutput = DefaultMaxOutput;
    std::function<void(const QByteArray &)> onError;
    QString tag;
    /// stop() asked for it (killed as soon as it runs).
    bool stopRequested = false;
    bool ended = false;
};

Client::Client(QObject *parent)
    : QObject(parent), m_git(QStandardPaths::findExecutable(u"git"_s)), m_gh(QStandardPaths::findExecutable(u"gh"_s))
{ }

Client::~Client()
{
    // No callback after this: every run still going is stopped without one.
    const QList<std::shared_ptr<Run>> runs = m_runs;
    m_runs.clear();
    for (const std::shared_ptr<Run> &run : runs) {
        run->ended = true;
        run->done = nullptr;
#if QT_CONFIG(process)
        if (run->process) {
            run->process->disconnect();
            if (run->process->state() != QProcess::NotRunning) {
                run->process->kill();
                run->process->waitForFinished(1000);
            }
            delete run->process;
            run->process = nullptr;
        }
#endif
    }
}

bool Client::processesAvailable()
{
#if QT_CONFIG(process)
    return true;
#else
    return false;
#endif
}

int Client::running() const
{
    return int(m_runs.size());
}

bool Client::stop(const QString &tag)
{
    if (tag.isEmpty())
        return false;
    bool any = false;
#if QT_CONFIG(process)
    const QList<std::shared_ptr<Run>> runs = m_runs;
    for (const std::shared_ptr<Run> &run : runs) {
        if (run->tag != tag || run->ended || !run->process)
            continue;
        any = true;
        if (run->result.failure.isEmpty())
            run->result.failure = GitMessages::tr("Stopped");
        // finished() follows, and with it the callback; a process still starting is killed once it
        // runs (a kill before that would be lost).
        run->stopRequested = true;
        if (run->process->state() == QProcess::Running)
            run->process->kill();
    }
#endif
    return any;
}

void Client::later(std::function<void()> done)
{
    QMetaObject::invokeMethod(this, [done = std::move(done)] { done(); }, Qt::QueuedConnection);
}

void Client::git(const QString &folder, const QStringList &arguments, const RunOptions &options, Done done)
{
    QStringList args{u"-C"_s, folder, u"--no-pager"_s, u"-c"_s, u"color.ui=false"_s};
    if (options.readOnly || options.noHooks)
        args << u"-c"_s << u"core.fsmonitor=false"_s;
    args += arguments;
    start(m_git, folder, args, options, false, std::move(done));
}

void Client::gh(const QString &folder, const QStringList &arguments, const RunOptions &options, Done done)
{
    RunOptions o = options;
    if (o.timeoutMs <= 0)
        o.timeoutMs = m_networkTimeoutMs;
    start(m_gh, folder, arguments, o, true, std::move(done));
}

void Client::start(const QString &program, const QString &folder, const QStringList &arguments,
                   const RunOptions &options, bool isGh, Done done)
{
#if QT_CONFIG(process)
    if (program.isEmpty()) {
        RunResult r;
        r.failure = isGh ? GitMessages::tr("The GitHub CLI (gh) is not installed")
                         : GitMessages::tr("git is not installed (it was not found on PATH)");
        later([done = std::move(done), r] { done(r); });
        return;
    }
    auto run = std::make_shared<Run>();
    run->done = std::move(done);
    run->maxOutput = options.maxOutput > 0 ? options.maxOutput : DefaultMaxOutput;
    run->onError = options.onError;
    run->tag = options.tag;
    auto *process = new QProcess(this);
    run->process = process;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char *name : RepositoryVariables)
        env.remove(QString::fromLatin1(name));
    env.remove(u"LANGUAGE"_s);
    env.insert(u"LC_ALL"_s, u"C"_s);
    env.insert(u"GIT_TERMINAL_PROMPT"_s, u"0"_s);
    env.insert(u"GIT_PAGER"_s, u"cat"_s);
    env.insert(u"PAGER"_s, u"cat"_s);
    if (options.readOnly)
        env.insert(u"GIT_OPTIONAL_LOCKS"_s, u"0"_s);
    if (isGh) {
        env.insert(u"GH_PROMPT_DISABLED"_s, u"1"_s);
        env.insert(u"GH_NO_UPDATE_NOTIFIER"_s, u"1"_s);
        env.insert(u"GH_SPINNER_DISABLED"_s, u"1"_s);
        env.insert(u"GH_PAGER"_s, u"cat"_s);
        env.insert(u"NO_COLOR"_s, u"1"_s);
        env.insert(u"CLICOLOR"_s, u"0"_s);
    }
    for (const auto &[name, value] : options.env)
        env.insert(name, value);
    process->setProcessEnvironment(env);
    if (isGh || QFileInfo(folder).isDir())
        process->setWorkingDirectory(folder);

    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    run->timer = timer;
    const int timeout = options.timeoutMs > 0 ? options.timeoutMs : m_localTimeoutMs;
    const QString name = isGh ? u"gh"_s : u"git"_s;

    const std::weak_ptr<Run> weak = run;
    const auto finish = [this, weak] {
        const std::shared_ptr<Run> r = weak.lock();
        if (!r || r->ended)
            return;
        r->ended = true;
        m_runs.removeOne(r);
        if (r->process) {
            r->process->disconnect();
            if (r->process->state() != QProcess::NotRunning)
                r->process->kill();
            r->process->deleteLater();
            r->process = nullptr;
        }
        if (r->done) {
            const Done d = std::move(r->done);
            d(r->result);
        }
    };
    connect(timer, &QTimer::timeout, process, [weak, name, timeout] {
        const std::shared_ptr<Run> r = weak.lock();
        if (!r || r->ended || !r->process)
            return;
        r->result.failure = GitMessages::tr("%1 did not finish within %2 s").arg(name).arg(timeout / 1000);
        r->process->kill(); // finished() follows
    });
    connect(process, &QProcess::readyReadStandardOutput, process, [weak] {
        const std::shared_ptr<Run> r = weak.lock();
        if (!r || !r->process)
            return;
        const QByteArray chunk = r->process->readAllStandardOutput();
        if (r->result.outputCut)
            return;
        if (r->result.out.size() + chunk.size() > r->maxOutput) {
            r->result.out.append(chunk.left(r->maxOutput - r->result.out.size()));
            r->result.outputCut = true;
            r->result.failure = GitMessages::tr("The output was too long");
            r->process->kill();
            return;
        }
        r->result.out.append(chunk);
    });
    connect(process, &QProcess::readyReadStandardError, process, [weak] {
        const std::shared_ptr<Run> r = weak.lock();
        if (!r || !r->process)
            return;
        const QByteArray chunk = r->process->readAllStandardError();
        if (r->result.err.size() < MaxErrorBytes)
            r->result.err.append(QString::fromUtf8(chunk.left(MaxErrorBytes - r->result.err.size())));
        if (r->onError && !chunk.isEmpty())
            r->onError(chunk);
    });
    connect(process, &QProcess::finished, process, [weak, finish, name](int code, QProcess::ExitStatus status) {
        const std::shared_ptr<Run> r = weak.lock();
        if (!r || r->ended || !r->process)
            return;
        if (!r->result.outputCut) {
            r->result.out.append(r->process->readAllStandardOutput());
            if (r->result.out.size() > r->maxOutput) {
                r->result.out.truncate(r->maxOutput);
                r->result.outputCut = true;
                if (r->result.failure.isEmpty())
                    r->result.failure = GitMessages::tr("The output was too long");
            }
        }
        const QByteArray rest = r->process->readAllStandardError();
        if (r->result.err.size() < MaxErrorBytes)
            r->result.err.append(QString::fromUtf8(rest.left(MaxErrorBytes - r->result.err.size())));
        if (r->onError && !rest.isEmpty())
            r->onError(rest);
        r->result.exitCode = code;
        if (status == QProcess::CrashExit && r->result.failure.isEmpty())
            r->result.failure = GitMessages::tr("%1 stopped unexpectedly").arg(name);
        finish();
    });
    connect(process, &QProcess::started, process, [weak] {
        const std::shared_ptr<Run> r = weak.lock();
        if (r && !r->ended && r->process && r->stopRequested)
            r->process->kill();
    });
    connect(process, &QProcess::errorOccurred, process, [weak, finish, name](QProcess::ProcessError error) {
        const std::shared_ptr<Run> r = weak.lock();
        if (!r || r->ended || !r->process || error != QProcess::FailedToStart)
            return;
        r->result.failure =
                name == u"gh"_s
                        ? GitMessages::tr("The GitHub CLI (gh) could not be started: %1").arg(r->process->errorString())
                        : GitMessages::tr("git could not be started: %1").arg(r->process->errorString());
        finish(); // (no finished() after a failed start)
    });

    m_runs.append(run);
    process->start(program, arguments);
    if (!options.input.isEmpty())
        process->write(options.input);
    process->closeWriteChannel();
    timer->start(timeout);
#else
    Q_UNUSED(program)
    Q_UNUSED(folder)
    Q_UNUSED(arguments)
    Q_UNUSED(options)
    Q_UNUSED(isGh)
    RunResult r;
    r.failure = GitMessages::tr("This version of Brulion cannot run git");
    later([done = std::move(done), r] { done(r); });
#endif
}

// ---- Reading -----------------------------------------------------------------------------------

void Client::repository(const QString &folder, std::function<void(const RepoInfo &)> done)
{
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        RepoInfo info;
        info.error = folder.isEmpty() ? GitMessages::tr("No folder") : GitMessages::tr("The folder does not exist");
        later([done = std::move(done), info] { done(info); });
        return;
    }
    RunOptions o;
    o.readOnly = true;
    git(folder,
        {u"rev-parse"_s, u"--is-bare-repository"_s, u"--path-format=absolute"_s, u"--absolute-git-dir"_s,
         u"--git-common-dir"_s, u"--show-toplevel"_s},
        o, [done = std::move(done)](const RunResult &r) {
            RepoInfo info;
            const QStringList lines = outputLines(r.out);
            if (!lines.isEmpty() && lines.first() == u"true"_s) {
                info.bare = true;
                info.gitDir = lines.value(1);
                info.commonDir = lines.value(2);
                info.error = GitMessages::tr("A bare repository has no working tree");
                done(info);
                return;
            }
            if (!r.ok() || lines.size() < 4) {
                info.error = r.message();
                done(info);
                return;
            }
            info.isRepo = true;
            info.gitDir = lines.at(1);
            info.commonDir = lines.at(2);
            info.topLevel = lines.at(3);
            done(info);
        });
}

namespace {

FileChange changeFromStatus(const StatusEntry &e)
{
    FileChange c;
    c.path = e.path;
    c.oldPath = e.oldPath;
    c.change = changeOf(e);
    const bool untracked = e.kind == EntryKind::Untracked;
    c.staged = !untracked && e.staged != '.';
    c.unstaged = untracked || e.unstaged != '.';
    c.oldMode = e.headMode;
    c.newMode = untracked ? 0 : e.worktreeMode;
    return c;
}

void applyNumstat(QList<FileChange> &changes, const QList<NumstatEntry> &numstat)
{
    QHash<QString, const NumstatEntry *> byPath;
    for (const NumstatEntry &n : numstat)
        byPath.insert(n.path, &n);
    for (FileChange &c : changes) {
        const NumstatEntry *n = byPath.value(c.path);
        if (!n && c.change == Change::Renamed)
            n = byPath.value(c.oldPath);
        if (!n)
            continue;
        c.binary = n->binary;
        c.additions = n->binary ? -1 : n->additions;
        c.deletions = n->binary ? -1 : n->deletions;
    }
}

} // namespace

void Client::status(const QString &folder, std::function<void(const StatusReport &)> done)
{
    RunOptions ro;
    ro.readOnly = true;
    git(folder, {u"status"_s, u"--porcelain=v2"_s, u"--branch"_s, u"-z"_s, u"--untracked-files=all"_s}, ro,
        [this, folder, done = std::move(done), ro](const RunResult &r) {
            StatusReport report;
            if (!r.ok()) {
                report.error = r.message();
                done(report);
                return;
            }
            report.ok = true;
            report.status = parseStatus(r.out);
            QStringList untracked;
            for (const StatusEntry &e : std::as_const(report.status.entries)) {
                if (e.kind == EntryKind::Ignored)
                    continue;
                report.changes.append(changeFromStatus(e));
                // (An untracked folder — a repository inside — ends with '/': no lines to count.)
                if (e.kind == EntryKind::Untracked && !e.path.endsWith(u'/') && untracked.size() < MaxCountedUntracked)
                    untracked.append(e.path);
            }
            if (report.changes.isEmpty()) {
                done(report);
                return;
            }
            auto shared = std::make_shared<StatusReport>(std::move(report));
            const auto counted = [shared, done](const QList<NumstatEntry> &numstat) {
                applyNumstat(shared->changes, numstat);
                done(*shared);
            };
            // Against HEAD — or, before the first commit, against the empty tree.
            const auto withBase = [this, folder, untracked, counted, ro](const QString &base) {
                const QStringList diff{u"diff"_s,         base, u"--numstat"_s, u"-z"_s, u"-M"_s, u"--no-ext-diff"_s,
                                       u"--no-textconv"_s};
                if (untracked.isEmpty()) {
                    git(folder, diff, ro, [counted](const RunResult &d) {
                        counted(d.ok() ? parseNumstat(d.out) : QList<NumstatEntry>());
                    });
                    return;
                }
                // The untracked files as intent-to-add in a copy of the index: `git diff` counts
                // their lines with the rest, and nothing of the user's index changes.
                git(folder, {u"rev-parse"_s, u"--path-format=absolute"_s, u"--git-path"_s, u"index"_s}, ro,
                    [this, folder, untracked, counted, diff, ro](const RunResult &p) {
                        auto temp = std::make_shared<QTemporaryDir>();
                        if (!p.ok() || !temp->isValid()) {
                            git(folder, diff, ro, [counted](const RunResult &d) {
                                counted(d.ok() ? parseNumstat(d.out) : QList<NumstatEntry>());
                            });
                            return;
                        }
                        const QString indexPath = QString::fromUtf8(p.out).trimmed();
                        const QString tempIndex = temp->filePath(u"index"_s);
                        if (QFile::exists(indexPath) && !QFile::copy(indexPath, tempIndex))
                            QFile::remove(tempIndex);
                        RunOptions add;
                        add.noHooks = true;
                        add.env = {{u"GIT_INDEX_FILE"_s, tempIndex}, {u"GIT_LITERAL_PATHSPECS"_s, u"1"_s}};
                        add.input = untracked.join(QChar(0)).toUtf8() + '\0';
                        git(folder,
                            {u"-c"_s, u"core.splitIndex=false"_s, u"add"_s, u"--intent-to-add"_s,
                             u"--pathspec-from-file=-"_s, u"--pathspec-file-nul"_s},
                            add, [this, folder, counted, diff, ro, temp, tempIndex](const RunResult &a) {
                                RunOptions o = ro;
                                if (a.ok())
                                    o.env = {{u"GIT_INDEX_FILE"_s, tempIndex}};
                                git(folder, diff, o, [counted, temp](const RunResult &d) {
                                    counted(d.ok() ? parseNumstat(d.out) : QList<NumstatEntry>());
                                });
                            });
                    });
            };
            if (!shared->status.branch.initial) {
                withBase(u"HEAD"_s);
                return;
            }
            RunOptions h = ro;
            git(folder, {u"hash-object"_s, u"-t"_s, u"tree"_s, u"--stdin"_s}, h,
                [withBase, counted](const RunResult &e) {
                    const QString empty = QString::fromUtf8(e.out).trimmed();
                    if (!e.ok() || empty.isEmpty()) {
                        counted({});
                        return;
                    }
                    withBase(empty);
                });
        });
}

void Client::fileTexts(const QString &folder, const FileTextsRequest &request,
                       std::function<void(const FileTexts &)> done)
{
    FileTexts texts;
    texts.path = request.path;
    const QString basePath = request.oldPath.isEmpty() ? request.path : request.oldPath;
    if (!isSafeRelativePath(request.path) || !isSafeRelativePath(basePath)) {
        texts.error = GitMessages::tr("Not a path inside the repository: %1").arg(request.path);
        later([done = std::move(done), texts] { done(texts); });
        return;
    }
    if ((!request.base.isEmpty() && !isSafeRevision(request.base))
        || (!request.target.isEmpty() && !isSafeRevision(request.target))) {
        texts.error = GitMessages::tr("Not a revision: %1").arg(request.base + u' ' + request.target);
        later([done = std::move(done), texts] { done(texts); });
        return;
    }
    // The work tree's side, from disk (bounded).
    const auto readNew = [folder, request](FileTexts &t, QByteArray &bytes) {
        const QString full = inTree(folder, request.path);
        const QFileInfo info(full);
        if (info.isSymbolicLink()) {
            t.newExists = true;
            bytes = info.readSymLink().toUtf8();
            return;
        }
        if (!info.exists())
            return;
        t.newExists = true;
        if (info.isDir()) { // a submodule
            t.binary = true;
            return;
        }
        if (info.size() > MaxTextBytes) {
            t.tooLarge = true;
            return;
        }
        QFile file(full);
        if (!file.open(QIODevice::ReadOnly)) {
            t.error = file.errorString();
            return;
        }
        bytes = file.read(MaxTextBytes + 1);
        if (bytes.size() > MaxTextBytes) {
            t.tooLarge = true;
            bytes.clear();
        }
    };
    const auto finish = [readNew](FileTexts t, const QByteArray &oldBytes) -> FileTexts {
        QByteArray newBytes;
        readNew(t, newBytes);
        if (!t.error.isEmpty())
            return t;
        t.ok = true;
        if (t.tooLarge)
            return t;
        t.binary = t.binary || looksBinary(oldBytes) || looksBinary(newBytes);
        if (!t.binary) {
            t.oldText = QString::fromUtf8(oldBytes);
            t.newText = QString::fromUtf8(newBytes);
        }
        return t;
    };
    RunOptions o;
    o.readOnly = true;
    o.maxOutput = MaxTextBytes;
    if (!request.target.isEmpty()) {
        // Both sides in git: the base's and the target's blobs.
        const auto withOld = [this, folder, request, o, done](FileTexts t, const QByteArray &oldBytes) {
            git(folder, {u"cat-file"_s, u"blob"_s, request.target + u':' + request.path}, o,
                [t, oldBytes, done](const RunResult &r) mutable {
                    t.ok = true;
                    if (r.outputCut) {
                        t.tooLarge = true;
                        t.newExists = true;
                        done(t);
                        return;
                    }
                    t.newExists = r.ok();
                    const QByteArray newBytes = r.ok() ? r.out : QByteArray();
                    t.binary = looksBinary(oldBytes) || looksBinary(newBytes);
                    if (!t.binary) {
                        t.oldText = QString::fromUtf8(oldBytes);
                        t.newText = QString::fromUtf8(newBytes);
                    }
                    done(t);
                });
        };
        if (request.base.isEmpty()) {
            later([withOld, texts] { withOld(texts, {}); });
            return;
        }
        git(folder, {u"cat-file"_s, u"blob"_s, request.base + u':' + basePath}, o,
            [withOld, done, texts](const RunResult &r) mutable {
                if (r.outputCut) {
                    texts.tooLarge = true;
                    texts.oldExists = true;
                    texts.ok = true;
                    done(texts);
                    return;
                }
                texts.oldExists = r.ok();
                withOld(texts, r.ok() ? r.out : QByteArray());
            });
        return;
    }
    if (request.base.isEmpty()) {
        later([done = std::move(done), texts, finish] { done(finish(texts, {})); });
        return;
    }
    git(folder, {u"cat-file"_s, u"blob"_s, request.base + u':' + basePath}, o,
        [done = std::move(done), texts, finish](const RunResult &r) mutable {
            if (r.outputCut) {
                texts.tooLarge = true;
                texts.oldExists = true;
                texts.ok = true;
                done(texts);
                return;
            }
            // (Not in the base — a new file, a submodule's commit —: no old text.)
            texts.oldExists = r.ok();
            done(finish(texts, r.ok() ? r.out : QByteArray()));
        });
}

void Client::branches(const QString &folder, std::function<void(const BranchList &)> done)
{
    RunOptions o;
    o.readOnly = true;
    git(folder, {u"branch"_s, u"-a"_s, u"--sort=-committerdate"_s, u"--format="_s + branchFormat()}, o,
        [done = std::move(done)](const RunResult &r) {
            BranchList list;
            list.ok = r.ok();
            if (!r.ok())
                list.error = r.message();
            else
                list.branches = parseBranches(r.out);
            done(list);
        });
}

void Client::worktrees(const QString &folder, std::function<void(const WorktreeList &)> done)
{
    RunOptions o;
    o.readOnly = true;
    auto finish = [done = std::move(done)](const RunResult &r) {
        WorktreeList list;
        list.ok = r.ok();
        if (!r.ok())
            list.error = r.message();
        else
            list.worktrees = parseWorktrees(r.out);
        done(list);
    };
    // `-z` came with git 2.36 (Ubuntu 22.04 has 2.34): without it the porcelain is a line a field,
    // which parseWorktrees() reads too.
    git(folder, {u"worktree"_s, u"list"_s, u"--porcelain"_s, u"-z"_s}, o,
        [this, folder, o, finish = std::move(finish)](const RunResult &r) mutable {
            if (!r.ok() && r.err.contains(u"unknown switch"_s)) {
                git(folder, {u"worktree"_s, u"list"_s, u"--porcelain"_s}, o, std::move(finish));
                return;
            }
            finish(r);
        });
}

// ---- Actions -----------------------------------------------------------------------------------

namespace {

Outcome failed(const QString &message)
{
    Outcome o;
    o.message = message;
    return o;
}

Outcome succeeded(const QString &message)
{
    Outcome o;
    o.ok = true;
    o.message = message;
    return o;
}

} // namespace

void Client::commitAll(const QString &folder, const QString &message, std::function<void(const Outcome &)> done)
{
    if (subjectOf(message).isEmpty()) {
        later([done = std::move(done)] { done(failed(GitMessages::tr("Write a commit message first"))); });
        return;
    }
    git(folder, {u"add"_s, u"-A"_s}, {}, [this, folder, message, done = std::move(done)](const RunResult &a) {
        if (!a.ok()) {
            done(failed(a.message()));
            return;
        }
        RunOptions c;
        c.input = message.toUtf8();
        c.timeoutMs = m_commitTimeoutMs;
        git(folder, {u"commit"_s, u"--file=-"_s}, c, [this, folder, done](const RunResult &r) {
            if (!r.ok()) {
                if (r.failure.isEmpty() && QString::fromUtf8(r.out).contains(u"nothing to commit"_s))
                    done(failed(GitMessages::tr("Nothing to commit")));
                else
                    done(failed(r.message()));
                return;
            }
            RunOptions ro;
            ro.readOnly = true;
            git(folder, {u"rev-parse"_s, u"HEAD"_s}, ro, [done](const RunResult &h) {
                Outcome o = succeeded(QString());
                o.value = QString::fromUtf8(h.out).trimmed();
                o.message = o.value.isEmpty() ? GitMessages::tr("Committed")
                                              : GitMessages::tr("Committed %1").arg(shortOid(o.value));
                done(o);
            });
        });
    });
}

void Client::push(const QString &folder, std::function<void(const Outcome &)> done)
{
    RunOptions ro;
    ro.readOnly = true;
    git(folder, {u"symbolic-ref"_s, u"-q"_s, u"--short"_s, u"HEAD"_s}, ro,
        [this, folder, ro, done = std::move(done)](const RunResult &h) {
            const QString branch = QString::fromUtf8(h.out).trimmed();
            if (!h.ok() || branch.isEmpty()) {
                done(failed(h.failure.isEmpty() ? GitMessages::tr("HEAD is detached: switch to a branch to push it")
                                                : h.failure));
                return;
            }
            RunOptions net;
            net.timeoutMs = m_networkTimeoutMs;
            git(folder, {u"rev-parse"_s, u"--abbrev-ref"_s, u"--symbolic-full-name"_s, u"@{upstream}"_s}, ro,
                [this, folder, ro, net, branch, done](const RunResult &u) {
                    const QString upstream = QString::fromUtf8(u.out).trimmed();
                    if (u.ok() && !upstream.isEmpty()) {
                        git(folder, {u"push"_s}, net, [branch, upstream, done](const RunResult &p) {
                            done(p.ok() ? succeeded(GitMessages::tr("Pushed %1 to %2").arg(branch, upstream))
                                        : failed(p.message()));
                        });
                        return;
                    }
                    git(folder, {u"remote"_s}, ro, [this, folder, net, branch, done](const RunResult &rm) {
                        const QStringList remotes = outputLines(rm.out);
                        if (!rm.ok() || remotes.isEmpty()) {
                            done(failed(rm.ok() ? GitMessages::tr("The repository has no remote to push to")
                                                : rm.message()));
                            return;
                        }
                        const QString remote = remotes.contains(u"origin"_s) ? u"origin"_s : remotes.first().trimmed();
                        git(folder, {u"push"_s, u"-u"_s, remote, branch}, net,
                            [branch, remote, done](const RunResult &p) {
                                done(p.ok() ? succeeded(GitMessages::tr("Pushed %1 to %2").arg(branch, remote))
                                            : failed(p.message()));
                            });
                    });
                });
        });
}

void Client::pull(const QString &folder, std::function<void(const Outcome &)> done)
{
    RunOptions net;
    net.timeoutMs = m_networkTimeoutMs;
    git(folder, {u"pull"_s, u"--ff-only"_s}, net, [done = std::move(done)](const RunResult &r) {
        if (!r.ok()) {
            done(failed(r.message()));
            return;
        }
        done(succeeded(QString::fromUtf8(r.out).contains(u"Already up to date"_s)
                               ? GitMessages::tr("Already up to date")
                               : GitMessages::tr("Pulled")));
    });
}

void Client::createBranch(const QString &folder, const QString &name, std::function<void(const Outcome &)> done)
{
    if (!isValidBranchName(name)) {
        later([done = std::move(done), name] {
            done(failed(GitMessages::tr("“%1” cannot be a branch's name").arg(name)));
        });
        return;
    }
    git(folder, {u"switch"_s, u"-c"_s, name}, {}, [name, done = std::move(done)](const RunResult &r) {
        done(r.ok() ? succeeded(GitMessages::tr("Created the branch %1").arg(name)) : failed(r.message()));
    });
}

void Client::checkout(const QString &folder, const QString &name, std::function<void(const Outcome &)> done)
{
    if (!isValidBranchName(name)) {
        later([done = std::move(done), name] {
            done(failed(GitMessages::tr("“%1” cannot be a branch's name").arg(name)));
        });
        return;
    }
    RunOptions ro;
    ro.readOnly = true;
    const auto switchTo = [this, folder, name, done](const QStringList &args) {
        git(folder, args, {}, [name, done](const RunResult &r) {
            done(r.ok() ? succeeded(GitMessages::tr("Switched to %1").arg(name)) : failed(r.message()));
        });
    };
    git(folder, {u"show-ref"_s, u"--verify"_s, u"--quiet"_s, u"refs/heads/"_s + name}, ro,
        [this, folder, name, ro, switchTo](const RunResult &local) {
            if (local.ok()) {
                switchTo({u"switch"_s, name});
                return;
            }
            git(folder, {u"show-ref"_s, u"--verify"_s, u"--quiet"_s, u"refs/remotes/"_s + name}, ro,
                [name, switchTo](const RunResult &remote) {
                    if (remote.ok())
                        switchTo({u"switch"_s, u"--track"_s, name});
                    else
                        switchTo({u"switch"_s, name});
                });
        });
}

void Client::addWorktree(const QString &folder, const QString &path, const QString &branch, const QString &base,
                         std::function<void(const Outcome &)> done)
{
    const QString target = QDir::cleanPath(QDir::isAbsolutePath(path) ? path : QDir(folder).absoluteFilePath(path));
    if (path.trimmed().isEmpty() || (!branch.isEmpty() && !isValidBranchName(branch)) || base.startsWith(u'-')) {
        later([done = std::move(done), branch] {
            done(failed(!branch.isEmpty() && !isValidBranchName(branch)
                                ? GitMessages::tr("“%1” cannot be a branch's name").arg(branch)
                                : GitMessages::tr("No folder for the worktree")));
        });
        return;
    }
    QStringList args{u"worktree"_s, u"add"_s};
    if (!branch.isEmpty())
        args << u"-b"_s << branch;
    args << u"--"_s << target;
    if (!base.isEmpty())
        args << base;
    git(folder, args, {}, [target, done = std::move(done)](const RunResult &r) {
        Outcome o = r.ok() ? succeeded(GitMessages::tr("Added a worktree at %1").arg(target)) : failed(r.message());
        o.value = target;
        done(o);
    });
}

void Client::removeWorktree(const QString &folder, const QString &path, bool force,
                            std::function<void(const Outcome &)> done)
{
    const QString target = QDir::cleanPath(QDir::isAbsolutePath(path) ? path : QDir(folder).absoluteFilePath(path));
    QStringList args{u"worktree"_s, u"remove"_s};
    if (force)
        args << u"--force"_s;
    args << u"--"_s << target;
    git(folder, args, {}, [target, done = std::move(done)](const RunResult &r) {
        done(r.ok() ? succeeded(GitMessages::tr("Removed the worktree %1").arg(target)) : failed(r.message()));
    });
}

void Client::createPullRequest(const QString &folder, const PullRequest &request,
                               std::function<void(const Outcome &)> done)
{
    if (request.title.trimmed().isEmpty()) {
        later([done = std::move(done)] { done(failed(GitMessages::tr("A pull request needs a title"))); });
        return;
    }
    if (m_gh.isEmpty()) {
        later([done = std::move(done)] {
            done(failed(GitMessages::tr("The GitHub CLI (gh) is not installed: install it to create pull requests")));
        });
        return;
    }
    QStringList args{u"pr"_s, u"create"_s, u"--title"_s, request.title.trimmed(), u"--body"_s, request.body};
    if (request.draft)
        args << u"--draft"_s;
    if (!request.head.isEmpty())
        args << u"--head"_s << request.head;
    if (!request.base.isEmpty())
        args << u"--base"_s << request.base;
    if (!request.repo.isEmpty())
        args << u"--repo"_s << request.repo;
    gh(folder, args, {}, [done = std::move(done)](const RunResult &r) {
        const QString all = QString::fromUtf8(r.out) + u'\n' + r.err;
        const QString url = parsePullRequestUrl(all);
        Outcome o;
        o.url = url;
        if (r.ok()) {
            o.ok = true;
            o.message = GitMessages::tr("Created the pull request");
        } else if (r.failure.isEmpty() && r.err.contains(u"already exists"_s) && !url.isEmpty()) {
            o.ok = true;
            o.message = GitMessages::tr("A pull request for this branch exists already");
        } else if (r.failure.isEmpty()
                   && (r.err.contains(u"gh auth login"_s) || r.err.contains(u"not logged in"_s, Qt::CaseInsensitive)
                       || r.err.contains(u"GH_TOKEN"_s))) {
            o.message = GitMessages::tr("Sign in to GitHub first: run “gh auth login” in a terminal");
        } else {
            o.message = r.message();
        }
        done(o);
    });
}

} // namespace brulion::gitcli
