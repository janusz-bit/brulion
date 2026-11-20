#include "speech/speechrunner.h"

#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#if QT_CONFIG(library)
#include <QLibrary>
#endif

using namespace Qt::StringLiterals;

namespace brulion::speech {

namespace {

#if QT_CONFIG(library)
/// The plugin's file in `dir`, as this system names libraries ("": none there).
QString pluginFileIn(const QString &dir)
{
    const QString name = QString::fromLatin1(SpeechRunnerPlugin);
    const QStringList files{u"lib%1.so"_s.arg(name), u"%1.dll"_s.arg(name), u"lib%1.dylib"_s.arg(name)};
    for (const QString &file : files) {
        if (QFileInfo(dir + u'/' + file).isFile())
            return dir + u'/' + file;
    }
    return QString();
}
#endif

} // namespace

bool hasSpeechRunner(const QStringList &pluginDirs)
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

std::unique_ptr<SpeechRunner> loadSpeechRunner(const QStringList &pluginDirs, SpeechRunner::Options options,
                                               QString *error)
{
#if !QT_CONFIG(library)
    Q_UNUSED(pluginDirs)
    Q_UNUSED(options)
    if (error)
        *error = u"this build of Brulion recognizes no speech on the device"_s;
    return nullptr;
#else
    using Open = SpeechRunner *(*)(const SpeechRunner::Options *, QString *);
    static QMutex mutex;
    static Open open = nullptr;
    static QString pluginDir;

    QMutexLocker lock(&mutex);
    if (!open) {
        QStringList problems;
        for (const QString &dir : pluginDirs) {
            const QString file = dir.isEmpty() ? QString::fromLatin1(SpeechRunnerPlugin) : pluginFileIn(dir);
            if (file.isEmpty())
                continue;
            // Never unloaded: ggml keeps what it loaded for the life of the process.
            auto *library = new QLibrary(file);
            if (!library->load()) {
                problems << library->errorString();
                delete library;
                continue;
            }
            open = reinterpret_cast<Open>(library->resolve(SpeechRunnerOpen));
            if (!open) {
                problems << u"%1 has no %2"_s.arg(file, QLatin1String(SpeechRunnerOpen));
                continue;
            }
            pluginDir = dir.isEmpty() ? QString() : QFileInfo(library->fileName()).absolutePath();
            break;
        }
        if (!open) {
            if (error)
                *error = problems.isEmpty() ? u"this build of Brulion recognizes no speech on the device"_s
                                            : u"the speech recognizer cannot be loaded: "_s + problems.join(u"; "_s);
            return nullptr;
        }
    }
    if (options.backendDir.isEmpty())
        options.backendDir = pluginDir;
    const Open opener = open;
    lock.unlock();
    return std::unique_ptr<SpeechRunner>(opener(&options, error));
#endif
}

} // namespace brulion::speech
