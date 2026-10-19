#include "logcontroller.h"

#include "logexport.h"
#include "logmodel.h"

#include "fileexchange.h"
#include "log/logger.h"
#include "platform.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QPointer>

#include <QStandardPaths>

#ifdef Q_OS_ANDROID
#include "android/androidbridge.h"
#endif
#ifdef Q_OS_WASM
#include <QFileDialog>
#endif

using namespace Qt::StringLiterals;

namespace brulion::diag {

using logging::Logger;
using logging::Verbosity;

namespace {

QPointer<LogController> s_instance;

constexpr qint64 kNormalFileBytes = 1024 * 1024;
constexpr qint64 kDetailedFileBytes = 3 * 1024 * 1024;

} // namespace

LogController::LogController(QObject *parent) : QObject(parent)
{
    if (!s_instance)
        s_instance = this;
    if (Logger *logger = Logger::instance()) {
        m_problems = logger->problemCount();
        m_hasEarlier = logger->hasEarlierEntries();
        connect(logger, &Logger::appended, this, &LogController::onAppended);
        connect(logger, &Logger::cleared, this, [this] {
            m_hasEarlier = false;
            onAppended();
            Q_EMIT countsChanged();
        });
        connect(logger, &Logger::verbosityChanged, this, &LogController::detailedChanged);
    }
}

LogController::~LogController()
{
    if (s_instance == this)
        s_instance = nullptr;
}

LogController *LogController::instance()
{
    return s_instance;
}

void LogController::setPersistVerbosity(std::function<void(const QString &)> persist)
{
    m_persist = std::move(persist);
}

void LogController::setRecentSyncs(std::function<QStringList()> provider)
{
    m_recentSyncs = std::move(provider);
}

void LogController::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
}

QDateTime LogController::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

void LogController::onAppended()
{
    const Logger *logger = Logger::instance();
    const int problems = logger ? logger->problemCount() : 0;
    if (problems != m_problems) {
        m_problems = problems;
        Q_EMIT countsChanged();
    }
}

bool LogController::available() const
{
    return Logger::instance() != nullptr;
}

int LogController::problemCount() const
{
    return m_problems;
}

bool LogController::hasEarlier() const
{
    return m_hasEarlier;
}

QString LogController::location() const
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_WASM)
    return {};
#else
    const Logger *logger = Logger::instance();
    return logger ? QDir::toNativeSeparators(logger->directory()) : QString();
#endif
}

QStringList LogController::actions() const
{
#if defined(Q_OS_ANDROID)
    return {u"copy"_s, u"share"_s, u"downloads"_s, u"saveAs"_s};
#elif defined(Q_OS_WASM)
    return {u"copy"_s, u"download"_s};
#else
    QStringList out{u"copy"_s, u"saveAs"_s};
    if (!location().isEmpty())
        out.append(u"openFolder"_s);
    return out;
#endif
}

bool LogController::detailed() const
{
    const Logger *logger = Logger::instance();
    return logger && logger->verbosity() == Verbosity::Detailed;
}

void LogController::setDetailed(bool on)
{
    Logger *logger = Logger::instance();
    if (!logger || on == detailed())
        return;
    logger->setMaxFileBytes(on ? kDetailedFileBytes : kNormalFileBytes);
    logger->setVerbosity(on ? Verbosity::Detailed : Verbosity::Normal);
    if (m_persist)
        m_persist(logging::verbosityName(logger->verbosity()));
}

QString LogController::suggestedFileName() const
{
    return diag::suggestedFileName(now());
}

QUrl LogController::suggestedSaveFile() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    return QUrl::fromLocalFile(QDir(dir).filePath(suggestedFileName()));
}

QStringList LogController::exportHeader() const
{
    ExportInfo info;
    info.version = QCoreApplication::applicationVersion();
    info.device = platform::deviceDescription();
    info.qtVersion = QString::fromLatin1(qVersion());
    info.created = now();
    info.detailed = detailed();
    if (m_recentSyncs)
        info.recentSyncs = m_recentSyncs();
    return diag::exportHeader(info);
}

QString LogController::exportText() const
{
    const Logger *logger = Logger::instance();
    if (!logger)
        return {};
    return logger->exportText(exportHeader());
}

bool LogController::copy(QObject *model)
{
    if (!available())
        return false;
    QString text;
    if (auto *logModel = qobject_cast<LogModel *>(model))
        text = exportHeader().join(u'\n') + u"\n----\n"_s + logModel->visibleText();
    else
        text = exportText();
    auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!app || !QGuiApplication::clipboard()) {
        Q_EMIT failed(tr("The clipboard is not available."));
        return false;
    }
    const QString capped = capForClipboard(text);
    QGuiApplication::clipboard()->setText(capped);
    Q_EMIT done(capped.size() < text.size() ? tr("Copied the newest part of the event log.")
                                            : tr("Copied the event log."));
    return true;
}

bool LogController::copyText(const QString &text)
{
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance()) || !QGuiApplication::clipboard())
        return false;
    QGuiApplication::clipboard()->setText(capForClipboard(text));
    return true;
}

bool LogController::share()
{
#ifdef Q_OS_ANDROID
    if (!available())
        return false;
    // Qt's FileProvider shares files below filesDir only: a fresh copy in files/share/.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/share"_s;
    QDir shareDir(dir);
    if (shareDir.exists())
        shareDir.removeRecursively();
    QDir().mkpath(dir);
    const QString path = dir + u'/' + suggestedFileName();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(exportText().toUtf8()) < 0) {
        Q_EMIT failed(tr("Could not prepare the event log for sharing."));
        return false;
    }
    file.close();
    if (!platform::AndroidBridge::instance()->shareFile(path, u"text/plain"_s, tr("Brulion — event log"),
                                                        tr("Send the event log"))) {
        Q_EMIT failed(tr("Could not open the share menu."));
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool LogController::saveToDownloads()
{
#ifdef Q_OS_ANDROID
    if (!available())
        return false;
    const QString name = suggestedFileName();
    const QByteArray data = exportText().toUtf8();
    const platform::DownloadResult result =
            platform::AndroidBridge::instance()->saveToDownloads(name, u"text/plain"_s, data);
    if (result.unsupported) {
        // Android 9: the Download folder is a plain folder there.
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        QString error;
        if (!dir.isEmpty() && ioviews::writeToUrl(QUrl::fromLocalFile(dir + u'/' + name), data, &error)) {
            Q_EMIT done(tr("Saved “%1” in Downloads.").arg(name));
            return true;
        }
        Q_EMIT failed(tr("Could not save the event log: %1").arg(error));
        return false;
    }
    if (!result.ok) {
        Q_EMIT failed(tr("Could not save the event log: %1").arg(result.error));
        return false;
    }
    Q_EMIT done(tr("Saved “%1” in Downloads.").arg(result.name));
    return true;
#else
    return false;
#endif
}

bool LogController::saveTo(const QUrl &url)
{
    if (!available() || url.isEmpty())
        return false;
    QString error;
    if (!ioviews::writeToUrl(url, exportText().toUtf8(), &error)) {
        Q_EMIT failed(tr("Could not save the event log: %1").arg(error));
        return false;
    }
    Q_EMIT done(tr("Saved the event log."));
    return true;
}

void LogController::download()
{
#ifdef Q_OS_WASM
    if (available())
        QFileDialog::saveFileContent(exportText().toUtf8(), suggestedFileName());
#endif
}

void LogController::openFolder()
{
    const QString folder = location();
    if (!folder.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void LogController::clear()
{
    if (Logger *logger = Logger::instance()) {
        logger->clear();
        Q_EMIT done(tr("The event log was cleared."));
    }
}

} // namespace brulion::diag
