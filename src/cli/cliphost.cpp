// The Web Clipper's native messaging host (docs/CLIPPER.md "The host"): a browser starts brulion-cli
// (`clip host`, or with the arguments a browser passes: run.cpp) and talks to it over standard input
// and output in frames (clipper/nativemessaging.h). Every request is a JSON object with a `type` and
// an `id` the answer repeats:
//
//   hello     → {version, protocol, notebooks, notebook {path, name, folders…}, templates, language}
//   folders   → {folders}               the notebook's folders (for a new page)
//   pages     → {pages: [{path, title}]} pages whose title has `query` (for a heading's section)
//   headings  → {headings: [{level, text}]} of `page`
//   preview   → what `clip` would write (Markdown; nothing downloaded or written)
//   clip      → {result}: the clip written (commands_clip.cpp); with `progress: true` first
//               {type: "progress", done, total} while its pictures download (a port, not
//               runtime.sendNativeMessage, which takes the first answer as the reply)
//   open      → the page shown in the app
//
// Each answer has `type` (the request's, "error" on failure: {code, message, details}). A request
// names its notebook with `vault`: one of the notebooks `hello` listed (the app's current and recent
// ones, BRULION_VAULT, --vault), else the default one. Nothing here runs code from a page; a frame
// over 64 MiB ends the session, a malformed one is answered with an error.
#include "clip_p.h"
#include "commands_p.h"

#include "clipper/nativehost.h"
#include "clipper/nativemessaging.h"
#include "markdown/blockdocument.h"
#include "notebook/session.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <map>
#include <memory>

#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

using namespace Qt::StringLiterals;

namespace brulion::cli {

using namespace brulion::clipper;

namespace {

/// The version of these messages (`hello`'s `protocol`); the extension refuses a host it does not know.
constexpr int Protocol = 1;

/// Read what standard input has (blocking until something arrives): 0 at its end.
qint64 readStdin(char *buffer, qint64 size)
{
#ifdef Q_OS_WIN
    return _read(_fileno(stdin), buffer, unsigned(size));
#else
    for (;;) {
        const ssize_t n = ::read(STDIN_FILENO, buffer, size_t(size));
        if (n < 0 && errno == EINTR)
            continue;
        return n;
    }
#endif
}

void writeFrame(const QJsonObject &message)
{
    QByteArray frame = encodeFrame(message);
    if (frame.isEmpty()) {
        // Too large for a browser: say so instead (it never happens with the answers below).
        frame = encodeFrame(QJsonObject{{u"type"_s, u"error"_s},
                                        {u"id"_s, message.value(u"id"_s)},
                                        {u"code"_s, u"answer_too_large"_s},
                                        {u"message"_s, u"The answer is larger than a browser takes"_s}});
    }
    std::fwrite(frame.constData(), 1, size_t(frame.size()), stdout);
    std::fflush(stdout);
}

QJsonObject errorMessage(const QJsonValue &id, const QString &code, const QString &message,
                         const QJsonObject &details = {})
{
    QJsonObject o{{u"type"_s, u"error"_s}, {u"id"_s, id}, {u"code"_s, code}, {u"message"_s, message}};
    if (!details.isEmpty())
        o.insert(u"details"_s, details);
    return o;
}

/// The program of the app (to start it for "Open in Brulion"): the AppImage, `brulion` next to this
/// program or on PATH; "" when there is none.
QString appProgram()
{
    if (const QString appImage = qEnvironmentVariable("APPIMAGE"); !appImage.isEmpty())
        return appImage;
    const QString self = QCoreApplication::applicationFilePath();
    const QString name = QFileInfo(self).fileName();
    // `brulion cli …`: this is the app.
    if (name == u"brulion"_s || name == u"brulion.exe"_s)
        return self;
    const QString beside =
            QFileInfo(self).absolutePath() + u'/' + (name.endsWith(u".exe"_s) ? u"brulion.exe"_s : u"brulion"_s);
    if (QFileInfo(beside).isExecutable())
        return beside;
    return QStandardPaths::findExecutable(u"brulion"_s);
}

/// The host: one Context per notebook used in the session.
class Host {
public:
    explicit Host(Context &base) : m_base(base) { }

    void handle(const QJsonObject &message)
    {
        const QJsonValue id = message.value(u"id"_s);
        const QString type = message.value(u"type"_s).toString();
        if (type == u"hello")
            return writeFrame(hello(id, message));
        if (type == u"folders" || type == u"pages" || type == u"headings" || type == u"templates" || type == u"preview"
            || type == u"clip" || type == u"open")
            return writeFrame(withNotebook(type, id, message));
        writeFrame(errorMessage(id, u"unknown_type"_s, u"Unknown request: %1"_s.arg(type.left(40))));
    }

private:
    /// The context of `vault` ("" = the default notebook); nullptr with `error`.
    Context *contextFor(const QString &vault, QJsonObject *error, const QJsonValue &id)
    {
        QString key = vault.trimmed();
        if (!key.isEmpty()) {
            const QString wanted = Vault::idForPath(QDir::cleanPath(key));
            bool known = false;
            for (const QJsonValue &n : knownNotebooks(m_base)) {
                if (Vault::idForPath(n.toObject().value(u"path"_s).toString()) == wanted)
                    known = true;
            }
            if (!known) {
                *error = errorMessage(id, u"unknown_notebook"_s,
                                      u"%1 is not one of the app's notebooks"_s.arg(key.left(300)));
                return nullptr;
            }
            key = wanted;
        }
        auto it = m_contexts.find(key);
        if (it == m_contexts.end()) {
            auto ctx = std::make_unique<Context>();
            ctx->globals = m_base.globals;
            ctx->program = m_base.program;
            if (!vault.trimmed().isEmpty())
                ctx->globals.vault = QDir::cleanPath(vault.trimmed());
            it = m_contexts.emplace(key, std::move(ctx)).first;
        }
        if (auto e = it->second->resolveVault()) {
            *error = errorMessage(id, e->code, e->message, e->details);
            return nullptr;
        }
        return it->second.get();
    }

    QJsonObject notebookJson(Call &call)
    {
        Vault *v = call.vault();
        if (!v)
            return {};
        const VaultConfig config = v->config();
        return {{u"path"_s, QDir::toNativeSeparators(call.ctx.vaultRoot())},
                {u"name"_s, QFileInfo(call.ctx.vaultRoot()).fileName()},
                {u"newPagesFolder"_s, config.newPagesFolder},
                {u"journalsFolder"_s, config.journalsFolder},
                {u"attachmentsFolder"_s, config.attachmentsFolder},
                {u"templatesFolder"_s, config.templatesFolder}};
    }

    QJsonObject hello(const QJsonValue &id, const QJsonObject &message)
    {
        QJsonObject out{{u"type"_s, u"hello"_s},
                        {u"id"_s, id},
                        {u"version"_s, QCoreApplication::applicationVersion()},
                        {u"protocol"_s, Protocol},
                        {u"notebooks"_s, knownNotebooks(m_base)}};
        QJsonObject error;
        Context *ctx = contextFor(message.value(u"vault"_s).toString(), &error, id);
        if (!ctx) {
            // No notebook yet (the app was never opened): the extension says how to get one.
            out.insert(u"notebookError"_s, error);
            return out;
        }
        Call call(*ctx, *findCommand(u"clip.save"_s), {});
        out.insert(u"notebook"_s, notebookJson(call));
        out.insert(u"templates"_s, clipTemplates(call));
        if (Vault *v = call.vault())
            out.insert(u"folders"_s, QJsonArray::fromStringList(notebook::folderPaths(*v).mid(0, 2000)));
        out.insert(u"language"_s, ctx->language());
        if (call.error)
            out.insert(u"notebookError"_s, errorMessage(id, call.error->code, call.error->message));
        return out;
    }

    QJsonObject withNotebook(const QString &type, const QJsonValue &id, const QJsonObject &message)
    {
        QJsonObject error;
        Context *ctx = contextFor(message.value(u"vault"_s).toString(), &error, id);
        if (!ctx)
            return error;
        Call call(*ctx, *findCommand(u"clip.save"_s), {});
        Vault *v = call.vault();
        if (!v)
            return errorMessage(id, call.error ? call.error->code : u"no_vault"_s,
                                call.error ? call.error->message : QString());
        QJsonObject out{{u"type"_s, type}, {u"id"_s, id}};
        if (type == u"folders") {
            out.insert(u"folders"_s, QJsonArray::fromStringList(notebook::folderPaths(*v).mid(0, 5000)));
            return out;
        }
        if (type == u"templates") {
            out.insert(u"templates"_s, clipTemplates(call));
            return out;
        }
        if (type == u"pages")
            return pages(out, *v, message);
        if (type == u"headings")
            return headings(call, out, message);
        if (type == u"open") {
            const auto path = call.vaultPath(message.value(u"path"_s).toString());
            if (!path)
                return errorMessage(id, call.error->code, call.error->message);
            const Result r = openInApp(call, *path, message.value(u"line"_s).toInt(0));
            if (r.error)
                return errorMessage(id, r.error->code, r.error->message, r.error->details);
            out.insert(u"result"_s, r.value);
            return out;
        }
        // preview, clip
        const QJsonValue clip = message.value(u"clip"_s);
        if (!clip.isObject())
            return errorMessage(id, u"bad_request"_s, u"clip is not an object"_s);
        RequestProblem problem;
        const auto request = parseClipRequest(clip.toObject(), &problem);
        if (!request)
            return errorMessage(id, problem.code, problem.message, {{u"field"_s, problem.field}});
        const bool progress = type == u"clip" && message.value(u"progress"_s).toBool();
        const Result r =
                runClip(call, *request, type == u"preview",
                        progress ? ClipProgress([&id](int done, int total) {
                            writeFrame(QJsonObject{
                                    {u"type"_s, u"progress"_s}, {u"id"_s, id}, {u"done"_s, done}, {u"total"_s, total}});
                        })
                                 : ClipProgress());
        if (r.error)
            return errorMessage(id, r.error->code, r.error->message, r.error->details);
        out.insert(u"result"_s, r.value);
        QJsonArray warnings = call.warnings;
        for (const QJsonValue &w : r.warnings) {
            if (!warnings.contains(w))
                warnings.append(w);
        }
        out.insert(u"warnings"_s, warnings);
        return out;
    }

    static QJsonObject pages(QJsonObject out, Vault &v, const QJsonObject &message)
    {
        const QString query = text::foldKey(message.value(u"query"_s).toString().left(200).trimmed());
        const int limit = std::clamp(message.value(u"limit"_s).toInt(30), 1, 200);
        QList<FileStat> files = v.markdownFiles();
        // Recently changed pages first: the page a reading list grows in.
        std::ranges::sort(files, [](const FileStat &a, const FileStat &b) { return a.mtime > b.mtime; });
        QJsonArray starts;
        QJsonArray contains;
        for (const FileStat &f : std::as_const(files)) {
            if (paths::isConflictCopy(f.path))
                continue;
            const QString title = paths::pageTitle(f.path);
            const QString key = text::foldKey(title);
            const QJsonObject page{{u"path"_s, f.path}, {u"title"_s, title}};
            if (query.isEmpty() || key.startsWith(query))
                starts.append(page);
            else if (key.contains(query) || text::foldKey(f.path).contains(query))
                contains.append(page);
            if (starts.size() >= limit)
                break;
        }
        for (const QJsonValue &p : std::as_const(contains)) {
            if (starts.size() >= limit)
                break;
            starts.append(p);
        }
        out.insert(u"pages"_s, starts);
        return out;
    }

    static QJsonObject headings(Call &call, QJsonObject out, const QJsonObject &message)
    {
        const auto path = call.vaultPath(message.value(u"page"_s).toString());
        if (!path)
            return errorMessage(out.value(u"id"_s), call.error->code, call.error->message);
        const notebook::Notebook::Current current = call.nb()->read(*path);
        if (!current.exists)
            return errorMessage(out.value(u"id"_s), u"page_not_found"_s, u"No page %1"_s.arg(*path));
        const markdown::BlockDocument doc(current.text);
        QJsonArray list;
        for (int i = 0; i < doc.count() && list.size() < 500; ++i) {
            const markdown::BlockEntry &e = doc.at(i);
            if (e.headingLevel > 0 && e.prefix.isEmpty()) {
                QString text = e.body.section(u'\n', 0, 0);
                while (text.startsWith(u'#'))
                    text.remove(0, 1);
                list.append(QJsonObject{{u"level"_s, e.headingLevel}, {u"text"_s, text.trimmed()}});
            }
        }
        out.insert(u"headings"_s, list);
        out.insert(u"page"_s, *path);
        return out;
    }

    Context &m_base;
    std::map<QString, std::unique_ptr<Context>> m_contexts;
};

} // namespace

QJsonArray knownNotebooks(Context &context)
{
    QJsonArray out;
    QStringList seen;
    const QString data = context.appDataDir();
    const QString current = context.settingsValue(u"vaults/current"_s);
    const auto add = [&](const QString &raw, bool isCurrent) {
        const QString path = QDir::cleanPath(raw.trimmed());
        if (raw.trimmed().isEmpty() || !notebook::docsNotebookLanguage(data, path).isEmpty())
            return;
        const QString id = Vault::idForPath(path);
        if (seen.contains(id))
            return;
        seen.append(id);
        out.append(QJsonObject{{u"path"_s, QDir::toNativeSeparators(path)},
                               {u"name"_s, QFileInfo(path).fileName()},
                               {u"current"_s, isCurrent},
                               {u"exists"_s, QFileInfo(path).isDir()}});
    };
    const QString env = qEnvironmentVariable("BRULION_VAULT");
    if (!context.globals.vault.trimmed().isEmpty())
        add(context.globals.vault, true);
    if (!env.trimmed().isEmpty())
        add(env, context.globals.vault.trimmed().isEmpty());
    add(current, context.globals.vault.trimmed().isEmpty() && env.trimmed().isEmpty());
    const QVariantList recents = context.settingsVariant(u"vaults/recent"_s).toList();
    for (const QVariant &recent : recents) {
        add(recent.toMap().value(u"path"_s).toString(), false);
        if (out.size() >= 50)
            break;
    }
    return out;
}

Result openInApp(Call &call, const QString &path, int line)
{
    const Command *open = findCommand(u"app.open"_s);
    QJsonObject args{{u"target"_s, path}};
    if (line > 0)
        args.insert(u"line"_s, line);
    Result r = execute(call.ctx, *open, args);
    if (!r.error || r.error->code != u"app_not_running")
        return r;
    // Not running: start it there.
    const QString program = appProgram();
    if (program.isEmpty())
        return call.fail(ExitUnavailable, u"no_app"_s, tr("Brulion's app is not installed next to brulion-cli"));
    QJsonObject route{{u"view"_s, u"page"_s}, {u"path"_s, path}};
    if (line > 0)
        route.insert(u"line"_s, line);
    QStringList arguments;
    const QString current = call.ctx.settingsValue(u"vaults/current"_s);
    if (current.isEmpty() || Vault::idForPath(current) != Vault::idForPath(call.ctx.vaultRoot()))
        arguments << u"--vault"_s << call.ctx.vaultRoot();
    arguments << u"--route"_s << QString::fromUtf8(QJsonDocument(route).toJson(QJsonDocument::Compact));
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_UNIX
    // Its own session: the browser ends the host's process group when the clip is done.
    process.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
#endif
    if (call.dryRun())
        return call.ok(QJsonObject{
                {u"dryRun"_s, true}, {u"program"_s, program}, {u"arguments"_s, QJsonArray::fromStringList(arguments)}});
    if (!process.startDetached())
        return call.fail(ExitUnavailable, u"app_not_started"_s, tr("Could not start %1").arg(program));
    return call.ok(QJsonObject{{u"opened"_s, true}, {u"via"_s, u"launch"_s}, {u"program"_s, program}});
}

int serveClipHost(Context &context)
{
    Host host(context);
    FrameDecoder decoder;
    QByteArray buffer(64 * 1024, Qt::Uninitialized);
    for (;;) {
        while (auto frame = decoder.next()) {
            if (frame->error == FrameError::TooLarge) {
                writeFrame(errorMessage(QJsonValue(), frameErrorCode(frame->error),
                                        u"A message of %1 bytes is larger than the host reads (%2)"_s.arg(frame->length)
                                                .arg(MaxIncomingBytes)));
                return ExitUsage;
            }
            if (frame->error != FrameError::None) {
                writeFrame(
                        errorMessage(QJsonValue(), frameErrorCode(frame->error), u"The message is no JSON object"_s));
                continue;
            }
            host.handle(frame->message);
        }
        const qint64 n = readStdin(buffer.data(), buffer.size());
        if (n <= 0)
            break;
        decoder.feed(QByteArrayView(buffer.constData(), n));
    }
    if (decoder.finish() == FrameError::Truncated) {
        writeFrame(errorMessage(QJsonValue(), frameErrorCode(FrameError::Truncated),
                                u"The input ended inside a message"_s));
        return ExitUsage;
    }
    return ExitOk;
}

} // namespace brulion::cli
