#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

QString windowsPath(QString path)
{
    return path.replace(u'/', u'\\');
}

/// For comparing: forward slashes, resolved, lower case.
QString comparable(const QString &path)
{
    QString p = path;
    p.replace(u'\\', u'/');
    return QDir::cleanPath(p).toCaseFolded();
}

/// What follows the program in a command (trimmed).
QString autostartArguments(const QString &command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.startsWith(u'"')) {
        const qsizetype end = trimmed.indexOf(u'"', 1);
        return end < 0 ? QString() : trimmed.mid(end + 1).trimmed();
    }
    const qsizetype space = trimmed.indexOf(u' ');
    return space < 0 ? QString() : trimmed.mid(space + 1).trimmed();
}

/// A StartupApproved value as QSettings reads it: bytes (INI), or the registry's REG_BINARY data
/// read as UTF-16 text (QSettings on Windows).
QByteArray approvedBytes(const QVariant &value)
{
    if (value.typeId() == QMetaType::QByteArray)
        return value.toByteArray();
    if (value.typeId() == QMetaType::QString) {
        const QString text = value.toString();
        QByteArray bytes;
        bytes.reserve(text.size() * 2);
        for (const QChar c : text) {
            bytes.append(char(c.unicode() & 0xff));
            bytes.append(char(c.unicode() >> 8));
        }
        return bytes;
    }
    return {};
}

} // namespace

QString autostartCommand(const QString &exePath)
{
    return u'"' + windowsPath(exePath) + u"\" --hidden"_s;
}

QString autostartProgram(const QString &command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.startsWith(u'"')) {
        const qsizetype end = trimmed.indexOf(u'"', 1);
        return end < 0 ? QString() : trimmed.mid(1, end - 1).trimmed();
    }
    const qsizetype space = trimmed.indexOf(u' ');
    return space < 0 ? trimmed : trimmed.left(space);
}

bool autostartTargets(const QString &command, const QString &exePath)
{
    const QString program = autostartProgram(command);
    if (program.isEmpty() || exePath.trimmed().isEmpty())
        return false;
    return comparable(program) == comparable(exePath);
}

bool isBrulionAutostartCommand(const QString &command)
{
    const QString program = autostartProgram(command);
    if (program.isEmpty())
        return false;
    const QString name = comparable(program).section(u'/', -1);
    return name == u"brulion.exe" && autostartArguments(command) == u"--hidden";
}

bool startupApprovedDisabled(const QByteArray &value)
{
    return !value.isEmpty() && (uchar(value.at(0)) & 1) != 0;
}

QString Autostart::valueName()
{
    return u"Brulion"_s;
}

Autostart::Autostart(QSettings *run, QSettings *approved, const QString &exePath, Availability availability)
    : m_run(run), m_approved(approved), m_exePath(exePath), m_availability(availability)
{
    if (!m_run || !m_approved)
        m_availability = Availability::Unsupported;
}

Autostart::~Autostart() = default;

std::unique_ptr<Autostart> Autostart::forThisProcess(bool separateProfile)
{
#ifdef Q_OS_WIN
    auto run = std::make_unique<QSettings>(u"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"_s,
                                           QSettings::NativeFormat);
    auto approved = std::make_unique<QSettings>(
            u"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"_s,
            QSettings::NativeFormat);
    auto autostart = std::make_unique<Autostart>(run.get(), approved.get(), QCoreApplication::applicationFilePath(),
                                                 separateProfile ? Availability::SeparateProfile
                                                                 : Availability::Available);
    autostart->m_ownedRun = std::move(run);
    autostart->m_ownedApproved = std::move(approved);
    return autostart;
#else
    Q_UNUSED(separateProfile);
    return std::make_unique<Autostart>(nullptr, nullptr, QString(), Availability::Unsupported);
#endif
}

QString Autostart::command() const
{
    return m_run ? m_run->value(valueName()).toString() : QString();
}

bool Autostart::enabled() const
{
    if (m_availability != Availability::Available)
        return false;
    return autostartTargets(command(), m_exePath)
           && !startupApprovedDisabled(approvedBytes(m_approved->value(valueName())));
}

bool Autostart::setEnabled(bool on, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (m_availability != Availability::Available)
        return fail(u"not available"_s);
    if (on) {
        m_run->setValue(valueName(), autostartCommand(m_exePath));
        // Asked for explicitly: Task Manager's "off" goes too.
        m_approved->remove(valueName());
    } else {
        m_run->remove(valueName());
    }
    m_run->sync();
    m_approved->sync();
    if (m_run->status() != QSettings::NoError)
        return fail(u"the Run key could not be written"_s);
    if (enabled() != on)
        return fail(u"the Run key did not change"_s);
    return true;
}

bool Autostart::refresh(const std::function<bool(const QString &path)> &exists)
{
    if (m_availability != Availability::Available)
        return false;
    const QString current = command();
    if (current.isEmpty() || !isBrulionAutostartCommand(current) || autostartTargets(current, m_exePath))
        return false;
    const QString program = autostartProgram(current);
    const bool there = exists ? exists(program) : QFileInfo::exists(program);
    if (there)
        return false;
    m_run->setValue(valueName(), autostartCommand(m_exePath));
    m_run->sync();
    return m_run->status() == QSettings::NoError;
}

} // namespace brulion::platform
