#include "ai/localrunner.h"

#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#if QT_CONFIG(library)
#include <QLibrary>
#endif

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::ai {

namespace {

#if QT_CONFIG(library)
/// The plugin's file in `dir`, as this system names libraries ("": none there).
QString pluginFileIn(const QString &dir)
{
    const QString name = QString::fromLatin1(LocalRunnerPlugin);
    const QStringList files{u"lib%1.so"_s.arg(name), u"%1.dll"_s.arg(name), u"lib%1.dylib"_s.arg(name)};
    for (const QString &file : files) {
        if (QFileInfo(dir + u'/' + file).isFile())
            return dir + u'/' + file;
    }
    return QString();
}
#endif

} // namespace

QString pluginName(const QString &file)
{
    QString name = QFileInfo(file).fileName();
    const qsizetype dot = name.lastIndexOf(u'.');
    if (dot > 0)
        name.truncate(dot);
    return name.size() > 3 && name.startsWith(u"lib") ? name.mid(3) : name;
}

Reply answerLocally(LocalRunner *runner, const LocalModel &model, Request request, const std::atomic<bool> *cancel,
                    LocalRunner::Result *run)
{
    if (run)
        *run = {};
    Reply failed;
    if (!runner) {
        failed.error = u"no model is open"_s;
        return failed;
    }
    if (!local::knowsFormat(model.format)) {
        failed.error = u"unknown model format \"%1\""_s.arg(model.format);
        return failed;
    }
    if (local::hasImages(request) && !runner->sees()) {
        failed.error = u"the model on this device (%1) does not see pictures: choose one that does "
                       "(Settings → Extensions → AI)"_s.arg(model.name);
        return failed;
    }

    LocalRunner::Params params;
    // The answer's length: what the request asks for, at most the model's own limit.
    params.maxTokens = request.maxTokens > 0 ? std::min(request.maxTokens, model.answerTokens) : model.answerTokens;
    // Room for the answer: what the request asks for, at most a quarter of the context.
    params.reserveTokens = std::clamp(request.maxTokens > 0 ? request.maxTokens : 1024, 256,
                                      std::max(256, runner->contextTokens() / 4));
    params.temperature = model.temperature;
    params.topK = model.topK;
    params.repeatPenalty = model.repeatPenalty;
    params.timeoutMs = TimeoutMs;
    params.cancel = cancel;

    LocalRunner::Result result;
    for (int attempt = 0; attempt < 12; ++attempt) {
        result = runner->generate(local::prompt(model.format, request), params);
        if (result.status != LocalRunner::TooLong)
            break;
        // Too long by so many tokens: as many characters as those are in this request, off its
        // longest texts.
        qint64 chars = request.system.size();
        for (const Message &m : std::as_const(request.messages)) {
            chars += m.content.size() + m.raw.size();
            for (const ToolResult &r : m.results)
                chars += r.content.size();
        }
        const double perToken = double(std::max<qint64>(chars, 1)) / std::max(result.promptTokens, 1);
        const int excess = std::max(result.promptTokens - result.promptLimit, 1);
        // Tokens are counted, characters cut: a little under the limit, and more with every try.
        const double cut = excess * perToken * (1.0 + 0.2 * attempt) + 0.03 * result.promptLimit * perToken + 64;
        if (local::shorten(&request, int(std::min(cut, 1.0e9))) == 0)
            break; // nothing left to cut: the error says how long it is
    }

    if (run) {
        *run = result;
        run->pieces.clear(); // the reply has them
    }
    switch (result.status) {
    case LocalRunner::Ok: break;
    case LocalRunner::TimedOut: failed.error = u"no answer in %1 minutes"_s.arg(TimeoutMs / 60000); return failed;
    case LocalRunner::Cancelled: failed.error = u"stopped"_s; return failed;
    case LocalRunner::TooLong:
    case LocalRunner::Failed:
        failed.error = result.error.isEmpty() ? u"the model on this device gave no answer"_s : result.error;
        return failed;
    }
    Reply reply = local::reply(model.format, result.pieces, request, result.complete);
    reply.model = model.id;
    reply.inputTokens = result.promptTokens;
    reply.outputTokens = result.outputTokens;
    return reply;
}

bool hasLocalRunner(const QStringList &pluginDirs)
{
#if QT_CONFIG(library)
    for (const QString &dir : pluginDirs) {
        if (dir.isEmpty() || !pluginFileIn(dir).isEmpty())
            return true;
    }
#else
    Q_UNUSED(pluginDirs) // a Qt that loads no libraries (the browser): no plugin, ever
#endif
    return false;
}

std::unique_ptr<LocalRunner> loadLocalRunner(const QStringList &pluginDirs, LocalRunner::Options options,
                                             QString *error)
{
#if !QT_CONFIG(library)
    Q_UNUSED(pluginDirs)
    Q_UNUSED(options)
    if (error)
        *error = u"this build of Brulion runs no model on the device"_s;
    return nullptr;
#else
    using Open = LocalRunner *(*)(const LocalRunner::Options *, QString *);
    static QMutex mutex;
    static Open open = nullptr;
    static QString pluginDir;

    QMutexLocker lock(&mutex);
    if (!open) {
        QStringList problems;
        for (const QString &dir : pluginDirs) {
            const QString file = dir.isEmpty() ? QString::fromLatin1(LocalRunnerPlugin) : pluginFileIn(dir);
            if (file.isEmpty())
                continue;
            // Never unloaded: llama.cpp keeps what it loaded for the life of the process.
            auto *library = new QLibrary(file);
            if (!library->load()) {
                problems << library->errorString();
                delete library;
                continue;
            }
            open = reinterpret_cast<Open>(library->resolve(LocalRunnerOpen));
            if (!open) {
                problems << u"%1 has no %2"_s.arg(file, QLatin1String(LocalRunnerOpen));
                continue;
            }
            pluginDir = dir.isEmpty() ? QString() : QFileInfo(library->fileName()).absolutePath();
            break;
        }
        if (!open) {
            if (error)
                *error = problems.isEmpty() ? u"this build of Brulion runs no model on the device"_s
                                            : u"the model runner cannot be loaded: "_s + problems.join(u"; "_s);
            return nullptr;
        }
    }
    if (options.backendDir.isEmpty())
        options.backendDir = pluginDir;
    const Open opener = open;
    lock.unlock();
    return std::unique_ptr<LocalRunner>(opener(&options, error));
#endif
}

} // namespace brulion::ai
