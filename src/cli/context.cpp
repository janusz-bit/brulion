// The run's context (the notebook, settings, clock, language) and the helpers of a command call
// (arguments, references, dates, writes).
#include "cli_p.h"

#include "dates/dates.h"
#include "index/vaultindex.h"
#include "markdown/journal.h"
#include "notebook/journals.h"
#include "notebook/pages.h"
#include "profile.h" // the app's settings file (BRULION_PROFILE)
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>

#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

/// "~/x" -> "$HOME/x".
QString expandHome(const QString &path)
{
    if (path == u"~" || path.startsWith(u"~/"))
        return QDir::homePath() + path.mid(1);
    return path;
}

/// A value of the app's settings file, read only: AppSettings' file (IniFormat, UserScope,
/// "Brulion"/"Brulion" — or "Brulion-<name>" for BRULION_PROFILE, src/ipc/profile.h).
QVariant appSettingsValue(const QString &key)
{
    const QSettings settings(QSettings::IniFormat, QSettings::UserScope, ipc::organizationName(), u"Brulion"_s);
    return settings.value(key);
}

QString foldName(const QString &name)
{
    return name.normalized(QString::NormalizationForm_C).toLower();
}

} // namespace

// ---- Context ------------------------------------------------------------------------------------

Context::Context() = default;
Context::~Context() = default;

QDateTime Context::now() const
{
    const QDateTime fixed = QDateTime::fromString(globals.now, u"yyyy-MM-dd HH:mm"_s);
    return fixed.isValid() ? fixed : QDateTime::currentDateTime();
}

QString Context::language() const
{
    if (!globals.lang.isEmpty())
        return globals.lang;
    // The app's language setting, also when it was never chosen (then the system's).
    return notebook::appLanguage(settingsValue(u"ui/language"_s));
}

dates::UiLocale Context::locale() const
{
    return notebook::contentLocale(language());
}

Qt::DayOfWeek Context::weekStart() const
{
    return m_notebook ? m_notebook->vault().config().weekStartsOn : Qt::Monday;
}

QString Context::settingsValue(const QString &key) const
{
    return appSettingsValue(key).toString();
}

QString Context::appDataDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

std::optional<Error> Context::resolveVault()
{
    if (!m_root.isEmpty())
        return std::nullopt;
    QString path;
    if (!globals.vault.trimmed().isEmpty()) {
        path = globals.vault;
        m_source = u"option"_s;
    } else if (const QString env = qEnvironmentVariable("BRULION_VAULT"); !env.trimmed().isEmpty()) {
        path = env;
        m_source = u"environment"_s;
    } else {
        path = settingsValue(u"vaults/current"_s);
        m_source = u"settings"_s;
        // The app's documentation ("Dokumentacja Brulionu") is for reading, never where quick
        // capture, the journal or reminders go: while it is open, or the app was quit in it, the
        // user's notebook is the last other one — where the app itself starts (AppController).
        const QString docs = QDir::cleanPath(appDataDir() + u"/Dokumentacja Brulionu"_s);
        if (!path.trimmed().isEmpty() && QDir::cleanPath(path.trimmed()) == docs) {
            path.clear();
            for (const QVariant &recent : appSettingsValue(u"vaults/recent"_s).toList()) {
                const QString candidate = recent.toMap().value(u"path"_s).toString();
                if (!candidate.trimmed().isEmpty() && QDir::cleanPath(candidate.trimmed()) != docs) {
                    path = candidate;
                    break;
                }
            }
            if (path.isEmpty())
                return Error{ExitUnavailable,
                             u"no_vault"_s,
                             tr("The app shows its documentation, not a notebook of yours: give --vault <folder> "
                                "or set BRULION_VAULT"),
                             {{u"source"_s, m_source}}};
        }
    }
    if (path.trimmed().isEmpty())
        return Error{ExitUnavailable, u"no_vault"_s,
                     tr("No notebook: give --vault <folder>, set BRULION_VAULT, or open a notebook in the app")};
    const QString root = QDir::cleanPath(QFileInfo(expandHome(path.trimmed())).absoluteFilePath());
    const QFileInfo info(root);
    if (!info.exists())
        return Error{ExitUnavailable,
                     u"vault_missing"_s,
                     tr("The notebook folder %1 does not exist (brulion-cli init <folder> creates one)").arg(root),
                     {{u"path"_s, root}, {u"source"_s, m_source}}};
    if (!info.isDir())
        return Error{ExitUnavailable,
                     u"vault_not_a_folder"_s,
                     tr("%1 is not a folder").arg(root),
                     {{u"path"_s, root}, {u"source"_s, m_source}}};
    m_root = root;
    return std::nullopt;
}

QString Context::stateDir() const
{
    if (globals.noHistory)
        return {};
    if (!globals.stateDir.trimmed().isEmpty())
        return QDir::cleanPath(QFileInfo(expandHome(globals.stateDir.trimmed())).absoluteFilePath());
    return notebook::deviceStatePath(appDataDir(), m_root);
}

notebook::Notebook *Context::notebook(std::optional<Error> *error)
{
    if (m_notebook)
        return m_notebook.get();
    if (auto e = resolveVault()) {
        *error = e;
        return nullptr;
    }
    auto nb = std::make_unique<notebook::Notebook>();
    notebook::Notebook::Options options;
    options.stateDir = stateDir();
    options.watch = serving;
    options.historyReason = u"cli"_s;
    // The index is read on worker threads (docs/PERFORMANCE.md, L1): as the app,
    // clamp(cores - 1, 1, 4) parse workers and one for the full-text index; BRULION_INDEX_THREADS
    // overrides (0 = on this thread).
    bool forced = false;
    const int threads = std::clamp(qEnvironmentVariableIsSet("BRULION_INDEX_THREADS")
                                           ? qEnvironmentVariableIntValue("BRULION_INDEX_THREADS", &forced)
                                           : QThread::idealThreadCount() - 1,
                                   forced ? 0 : 1, forced ? 64 : 4);
    if (threads > 0) {
        if (!m_indexPool)
            m_indexPool = std::make_unique<QThreadPool>();
        m_indexPool->setMaxThreadCount(threads + 1);
        QThreadPool *pool = m_indexPool.get();
        options.workers.threads = threads;
        options.workers.start = [pool](std::function<void()> job) { pool->start(std::move(job)); };
    }
    if (!globals.now.isEmpty()) {
        const QDateTime fixed = now();
        options.clock = [fixed] { return fixed; };
    }
    QString message;
    notebook::Notebook::OpenError kind = notebook::Notebook::OpenError::None;
    if (!nb->open(m_root, options, &message, &kind)) {
        const QString code = kind == notebook::Notebook::OpenError::Missing      ? u"vault_missing"_s
                             : kind == notebook::Notebook::OpenError::NotAFolder ? u"vault_not_a_folder"_s
                                                                                 : u"vault_unavailable"_s;
        *error = Error{ExitUnavailable, code, message, {{u"path"_s, m_root}}};
        return nullptr;
    }
    m_notebook = std::move(nb);
    return m_notebook.get();
}

void Context::closeNotebook()
{
    m_notebook.reset();
}

QString Context::readStdin()
{
    if (m_stdin)
        return *m_stdin;
    QFile in;
    QByteArray bytes;
    if (in.open(stdin, QIODevice::ReadOnly))
        bytes = in.readAll();
    QString text = QString::fromUtf8(bytes);
    stdinInvalidUtf8 = text.toUtf8() != bytes; // replaced by U+FFFD
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    m_stdin = text;
    return text;
}

// ---- Call ---------------------------------------------------------------------------------------

Call::Call(Context &context, const Command &command, const QJsonObject &arguments)
    : ctx(context), cmd(command), args(arguments)
{ }

Call::~Call() = default;

QString Call::str(const QString &name) const
{
    const QJsonValue v = args.value(name);
    return v.isDouble() ? QString::number(v.toInteger()) : v.toString();
}

bool Call::has(const QString &name) const
{
    return args.contains(name) && !(args.value(name).isString() && args.value(name).toString().isEmpty());
}

bool Call::flag(const QString &name) const
{
    return args.value(name).toBool();
}

int Call::integer(const QString &name, int fallback) const
{
    const QJsonValue v = args.value(name);
    if (v.isDouble())
        return int(v.toInteger());
    bool ok = false;
    const int n = v.toString().toInt(&ok);
    return ok ? n : fallback;
}

QString Call::text(const QString &name)
{
    if (ctx.globals.stdinText) {
        for (const Arg &a : cmd.args) {
            if (a.name == name && a.fromStdin) {
                const QString text = ctx.readStdin();
                if (ctx.stdinInvalidUtf8)
                    warn(u"stdin_invalid_utf8"_s,
                         tr("Standard input was not valid UTF-8: invalid bytes were replaced"));
                return text;
            }
        }
    }
    return str(name);
}

bool Call::dryRun() const
{
    return ctx.globals.dryRun;
}

Result Call::ok(const QJsonValue &value, const QString &text)
{
    Result r;
    r.value = value;
    r.text = text;
    return r;
}

Result Call::fail(int exit, const QString &code, const QString &message, const QJsonObject &details)
{
    Result r;
    r.error = Error{exit, code, message, details};
    return r;
}

Result Call::failed()
{
    Result r;
    r.error = error ? *error : Error{ExitInternal, u"internal"_s, tr("Something went wrong")};
    return r;
}

void Call::setError(int exit, const QString &code, const QString &message, const QJsonObject &details)
{
    error = Error{exit, code, message, details};
}

void Call::warn(const QString &code, const QString &message, const QJsonObject &details)
{
    QJsonObject w{{u"code"_s, code}, {u"message"_s, message}};
    if (!details.isEmpty())
        w.insert(u"details"_s, details);
    if (!warnings.contains(w))
        warnings.append(w);
}

notebook::Notebook *Call::nb()
{
    notebook::Notebook *n = ctx.notebook(&error);
    return n;
}

VaultIndex *Call::index()
{
    notebook::Notebook *n = nb();
    return n ? &n->index() : nullptr;
}

Vault *Call::vault()
{
    notebook::Notebook *n = nb();
    return n ? &n->vault() : nullptr;
}

std::optional<VaultPath> Call::vaultPath(const QString &raw)
{
    QString t = expandHome(raw.trimmed());
    if (t.isEmpty()) {
        setError(ExitUsage, u"path_required"_s, tr("A path is needed"));
        return std::nullopt;
    }
    if (t.contains(QChar(u'\0'))) {
        setError(ExitUsage, u"bad_path"_s, tr("The path contains a NUL character"));
        return std::nullopt;
    }
    if (QDir::isAbsolutePath(t)) {
        const QString abs = QDir::cleanPath(t);
        const QString root = ctx.vaultRoot();
        if (abs == root)
            return VaultPath();
        if (!abs.startsWith(root + u'/')) {
            setError(ExitUsage, u"outside_vault"_s, tr("%1 is outside the notebook").arg(abs), {{u"path"_s, abs}});
            return std::nullopt;
        }
        t = abs.mid(root.size() + 1);
    }
    const QString cleaned = QDir::cleanPath(t.replace(u'\\', u'/'));
    if (cleaned == u".." || cleaned.startsWith(u"../") || cleaned.startsWith(u'/')) {
        setError(ExitUsage, u"outside_vault"_s, tr("%1 is outside the notebook").arg(raw.trimmed()),
                 {{u"path"_s, raw.trimmed()}});
        return std::nullopt;
    }
    VaultPath p = paths::normalize(cleaned);
    if (p == u".")
        p.clear();
    const QString first = p.section(u'/', 0, 0);
    if (first == u".brulion" || first == vaultTrashFolder() || (!p.isEmpty() && paths::isIgnored(p))) {
        setError(ExitUsage, u"ignored_path"_s,
                 tr("%1 is not a note: .brulion/, .trash/ and other hidden folders are off limits").arg(p),
                 {{u"path"_s, p}});
        return std::nullopt;
    }
    return p;
}

std::optional<QDate> Call::date(const QString &phrase)
{
    const QString t = phrase.trimmed();
    if (t.isEmpty())
        return ctx.today();
    if (const auto iso = dates::parseIsoDate(t))
        return *iso;
    const auto found = dates::parseNaturalDate(t, ctx.now(), ctx.weekStart(), ctx.locale());
    if (found && found->start == 0 && found->length >= t.size())
        return found->spec.date;
    if (found && t.mid(found->end()).trimmed().isEmpty() && t.left(found->start).trimmed().isEmpty())
        return found->spec.date;
    setError(ExitParse, u"date_not_understood"_s, tr("Not a date: %1").arg(t),
             {{u"phrase"_s, t},
              {u"examples"_s, QJsonArray{u"dziś"_s, u"wczoraj"_s, u"w piątek"_s, u"2026-10-15"_s, u"next monday"_s}}});
    return std::nullopt;
}

std::optional<DateSpec> Call::dateSpec(const QString &phrase, const QString &argName)
{
    const QString t = phrase.trimmed();
    if (const auto spec = dates::parseDateSpec(t))
        return *spec;
    const auto found = dates::parseNaturalDate(t, ctx.now(), ctx.weekStart(), ctx.locale());
    if (!found) {
        setError(ExitParse, u"date_not_understood"_s, tr("Not a date: %1").arg(t),
                 {{u"argument"_s, argName},
                  {u"phrase"_s, t},
                  {u"examples"_s,
                   QJsonArray{u"jutro 9:00"_s, u"w piątek o 18"_s, u"2026-10-15 09:00"_s, u"tomorrow 6pm"_s}}});
        return std::nullopt;
    }
    const QString ignored = dates::stripNaturalDate(t, *found).trimmed();
    if (!ignored.isEmpty())
        warn(u"ignored_words"_s, tr("Words that are no date were ignored in %1: %2").arg(optionName(argName), ignored),
             {{u"argument"_s, argName}, {u"ignored"_s, ignored}});
    return found->spec;
}

std::optional<Call::PageRef> Call::page(const QString &raw, bool mustExist)
{
    notebook::Notebook *n = nb();
    if (!n)
        return std::nullopt;
    Vault &v = n->vault();
    const QString t = raw.trimmed();
    if (t.isEmpty()) {
        setError(ExitUsage, u"ref_required"_s, tr("A page is needed: a path, a page name, a date or an item ref"));
        return std::nullopt;
    }
    const auto item = notebook::parseItemRef(t);
    if (!item) {
        setError(ExitUsage, u"bad_ref"_s, tr("Not a valid reference: %1").arg(t.left(200)),
                 {{u"examples"_s, QJsonArray{u"Projekty/Dom.md"_s, u"Dom"_s, u"today"_s, u"Dom.md#L12@3f9a01bc"_s,
                                             u"Dom.md#^abc123"_s, u"((abc123))"_s}}});
        return std::nullopt;
    }
    PageRef out;
    if (item->hasLine() || item->hasBlock())
        out.item = *item;
    const auto finish = [&](const VaultPath &path) {
        out.path = path;
        out.exists = v.exists(path) || QFileInfo(v.absolutePath(path)).isFile();
        if (const auto d = markdown::journalDateFromPath(path, v.config()))
            out.journal = *d;
        return out;
    };
    if (item->page.isEmpty()) {
        // ^id / ((id)): the block anywhere in the notebook.
        const auto block = n->index().blockById(item->blockId);
        if (!block) {
            setError(ExitNotFound, u"block_not_found"_s, tr("No block with the id %1").arg(item->blockId),
                     {{u"blockId"_s, item->blockId}});
            return std::nullopt;
        }
        return finish(block->path);
    }
    const QString name = item->page;
    // 1. A path of a file (with or without ".md"; folders as spelled on any letter case).
    const auto path = vaultPath(name);
    if (!path)
        return std::nullopt;
    if (path->isEmpty()) {
        setError(ExitUsage, u"not_a_page"_s, tr("The notebook folder is not a page"));
        return std::nullopt;
    }
    QStringList candidates{*path};
    if (!paths::isMarkdown(*path))
        candidates.append(*path + u".md"_s);
    for (const QString &c : std::as_const(candidates)) {
        const VaultPath spelled = v.existingCase(c);
        if (const auto st = v.stat(spelled); st && !st->isDir) {
            if (!paths::isMarkdown(spelled)) {
                setError(ExitUsage, u"not_a_page"_s, tr("%1 is an attachment, not a page").arg(spelled),
                         {{u"path"_s, spelled}});
                return std::nullopt;
            }
            return finish(spelled);
        }
    }
    if (paths::isMarkdown(*path) && QFileInfo(v.absolutePath(*path)).isFile())
        return finish(*path); // just arrived on disk
    // 2. A day named unmistakably: "today" or "2026-10-15".
    if (!name.contains(u'/')) {
        if (const auto iso = dates::parseIsoDate(name.trimmed()))
            return finish(notebook::journalFileFor(v, *iso));
        if (name.trimmed().compare(u"today", Qt::CaseInsensitive) == 0)
            return finish(notebook::journalFileFor(v, ctx.today()));
    }
    // Another date phrase ("wczoraj", "w piątek", "Weekend") is a day only when no page has that
    // name: [[Poniedziałek]] opens the page Plan/Poniedziałek.md, so does the ref.
    std::optional<QDate> day;
    if (!name.contains(u'/')) {
        const auto found = dates::parseNaturalDate(name, ctx.now(), ctx.weekStart(), ctx.locale());
        if (found && name.left(found->start).trimmed().isEmpty() && name.mid(found->end()).trimmed().isEmpty())
            day = found->spec.date;
    }
    const auto named = [&](const VaultPath &found) {
        if (day) {
            const VaultPath journal = notebook::journalFileFor(v, *day);
            warn(u"ref_also_a_date"_s,
                 tr("%1 is the page %2; for the daily note of %3 give the date")
                         .arg(name, found, day->toString(Qt::ISODate)),
                 {{u"page"_s, found}, {u"date"_s, day->toString(Qt::ISODate)}, {u"journal"_s, journal}});
        }
        return finish(found);
    };
    // 3. A page name as [[links]] read it: a title, or a path suffix ("Projekty/Dom").
    const QString key = foldName(notebook::withoutMdExtension(name).trimmed());
    QStringList matches;
    for (const FileStat &f : v.markdownFiles()) {
        if (paths::isConflictCopy(f.path))
            continue;
        const QString stem = foldName(paths::stripMarkdownExtension(f.path));
        if (key.contains(u'/') ? (stem == key || stem.endsWith(u'/' + key)) : foldName(paths::pageTitle(f.path)) == key)
            matches.append(f.path);
    }
    if (matches.size() == 1)
        return named(matches.first());
    if (matches.size() > 1) {
        setError(ExitConflict, u"ambiguous_ref"_s, tr("Several pages are called %1: give the path").arg(name),
                 {{u"candidates"_s, QJsonArray::fromStringList(matches)}});
        return std::nullopt;
    }
    // 4. An alias (frontmatter `aliases:`), as links resolve it.
    VaultIndex &index = n->index();
    if (const auto resolved = index.resolveLink(name); resolved && paths::isMarkdown(*resolved))
        return named(*resolved);
    // 5. The day of a date phrase.
    if (day)
        return finish(notebook::journalFileFor(v, *day));
    if (!mustExist) {
        const VaultPath future = notebook::newPagePath(v, name);
        if (!future.isEmpty())
            return finish(future);
    }
    QJsonArray suggestions;
    for (const PageSuggestion &s : index.suggestPages(name, 5))
        suggestions.append(s.path);
    setError(ExitNotFound, u"page_not_found"_s, tr("No page %1").arg(name),
             {{u"ref"_s, t}, {u"suggestions"_s, suggestions}});
    return std::nullopt;
}

bool Call::lock()
{
    if (m_lock)
        return true;
    notebook::Notebook *n = nb();
    if (!n)
        return false;
    QString message;
    m_lock = n->lock(10000, &message);
    if (!m_lock) {
        setError(ExitUnavailable, u"lock_timeout"_s, tr("The notebook is busy: %1").arg(message));
        return false;
    }
    return true;
}

std::optional<notebook::Notebook::WriteResult> Call::edit(const VaultPath &path, const notebook::Notebook::Edit &edit,
                                                          notebook::Notebook::WriteOptions options)
{
    using W = notebook::Notebook::WriteResult;
    if (!lock())
        return std::nullopt;
    options.dryRun = options.dryRun || dryRun();
    const W r = nb()->edit(path, edit, options);
    switch (r.status) {
    case W::Written:
    case W::Unchanged:
    case W::DryRun: return r;
    case W::Refused:
        if (!error)
            setError(ExitConflict, u"refused"_s, tr("The change cannot be made"));
        return std::nullopt;
    case W::NotFound:
        setError(ExitNotFound, u"page_not_found"_s, tr("No page %1").arg(path), {{u"path"_s, path}});
        return std::nullopt;
    case W::HashMismatch:
        setError(ExitConflict, u"hash_mismatch"_s,
                 tr("%1 changed since it was read (its hash is not the one expected): read it again").arg(path),
                 {{u"path"_s, path}, {u"expected"_s, options.expectHash}, {u"actual"_s, r.hashBefore}});
        return std::nullopt;
    case W::Conflict:
        setError(ExitConflict, u"concurrent_change"_s,
                 tr("%1 kept changing while it was written (another program writes it): try again").arg(path),
                 {{u"path"_s, path}});
        return std::nullopt;
    case W::Encoding:
        setError(ExitConflict, u"unsupported_encoding"_s,
                 tr("%1 is not UTF-8: it is not changed, so no byte of it gets lost").arg(path), {{u"path"_s, path}});
        return std::nullopt;
    case W::Failed: break;
    }
    setError(ExitInternal, u"io_error"_s, tr("Could not write %1: %2").arg(path, r.error), {{u"path"_s, path}});
    return std::nullopt;
}

QJsonObject Call::writeJson(const notebook::Notebook::WriteResult &r) const
{
    using W = notebook::Notebook::WriteResult;
    QJsonObject o{{u"path"_s, r.path},
                  {u"changed"_s, r.status == W::Written},
                  {u"created"_s, r.created},
                  {u"hashBefore"_s, r.hashBefore},
                  {u"hashAfter"_s, r.status == W::DryRun ? QString() : r.hashAfter}};
    if (r.status == W::DryRun)
        o.insert(u"dryRun"_s, true);
    if (r.status == W::DryRun || (ctx.globals.diff && r.status == W::Written))
        o.insert(u"diff"_s, unifiedDiff(r.before, r.after, r.path));
    return o;
}

QString Call::lineRef(const VaultPath &path, const QString &content, int line)
{
    const QList<text::Line> lines = text::splitLines(content);
    if (line < 0 || line >= lines.size())
        return {};
    return notebook::formatLineRef(path, line, lines.at(line).text);
}

// ---- Running a command --------------------------------------------------------------------------

Result execute(Context &context, const Command &command, const QJsonObject &rawArgs)
{
    std::optional<Error> error;
    const QJsonObject args = checkArguments(command, rawArgs, &error);
    Result refused;
    if (error) {
        refused.error = error;
        return refused;
    }
    if (command.writes && context.globals.readOnly) {
        refused.error = Error{ExitRefused, u"read_only"_s,
                              tr("%1 writes notes: refused in read-only mode").arg(command.words().join(u' '))};
        return refused;
    }
    if (command.destructive && !context.globals.allowDestructive) {
        refused.error = Error{ExitRefused, u"destructive_refused"_s,
                              tr("%1 replaces, moves or removes existing notes: run it with --allow-destructive")
                                      .arg(command.words().join(u' '))};
        return refused;
    }
    if (command.needsVault) {
        if (auto e = context.resolveVault()) {
            refused.error = e;
            return refused;
        }
    }
    Call call(context, command, args);
    Result result = command.run(call);
    QJsonArray warnings = call.warnings;
    for (const QJsonValue &w : std::as_const(result.warnings)) {
        if (!warnings.contains(w))
            warnings.append(w);
    }
    result.warnings = warnings;
    return result;
}

int exitCode(const Result &result)
{
    return result.error ? result.error->exit : ExitOk;
}

} // namespace brulion::cli
