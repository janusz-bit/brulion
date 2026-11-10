#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

/// For comparing: forward slashes, resolved, lower case.
QString comparable(QString path)
{
    return QDir::cleanPath(path.replace(u'\\', u'/')).toCaseFolded();
}

/// A Run command: the program it starts and what follows it (trimmed).
struct RunCommand {
    QString program;
    QString arguments;
};

/// The program is the quoted text at the command's start or its first word; both "" when the quote
/// is not closed.
RunCommand splitCommand(const QString &command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.startsWith(u'"')) {
        const qsizetype end = trimmed.indexOf(u'"', 1);
        if (end < 0)
            return {};
        return {trimmed.mid(1, end - 1).trimmed(), trimmed.mid(end + 1).trimmed()};
    }
    const qsizetype space = trimmed.indexOf(u' ');
    if (space < 0)
        return {trimmed, QString()};
    return {trimmed.left(space), trimmed.mid(space + 1).trimmed()};
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
    return u'"' + QString(exePath).replace(u'/', u'\\') + u"\" --hidden"_s;
}

QString autostartProgram(const QString &command)
{
    return splitCommand(command).program;
}

bool autostartTargets(const QString &command, const QString &exePath)
{
    const QString program = autostartProgram(command);
    return !program.isEmpty() && !exePath.trimmed().isEmpty() && comparable(program) == comparable(exePath);
}

bool isBrulionAutostartCommand(const QString &command)
{
    const auto [program, arguments] = splitCommand(command);
    return !program.isEmpty() && comparable(program).section(u'/', -1) == u"brulion.exe" && arguments == u"--hidden";
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
    auto autostart =
            std::make_unique<Autostart>(run.get(), approved.get(), QCoreApplication::applicationFilePath(),
                                        separateProfile ? Availability::SeparateProfile : Availability::Available);
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
    return m_availability == Availability::Available && autostartTargets(command(), m_exePath)
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
    if (!isBrulionAutostartCommand(current) || autostartTargets(current, m_exePath))
        return false;
    const QString program = autostartProgram(current);
    if (exists ? exists(program) : QFileInfo::exists(program))
        return false;
    m_run->setValue(valueName(), autostartCommand(m_exePath));
    m_run->sync();
    return m_run->status() == QSettings::NoError;
}

} // namespace brulion::platform
