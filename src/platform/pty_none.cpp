// platform::Pty where there is none (pty.h): Windows — a ConPTY backend (CreatePseudoConsole with
// overlapped pipes) would replace this file there —, Android and the browser. available() is false
// and nothing starts.
#include "pty.h"

namespace brulion::platform {

struct Pty::Private { };

Pty::Pty(QObject *parent) : QObject(parent), d(std::make_unique<Private>()) { }

Pty::~Pty() = default;

bool Pty::available()
{
    return false;
}

QStringList Pty::defaultEnvironment()
{
    return {};
}

QString Pty::userShell()
{
    return {};
}

bool Pty::start(const QString &, const QStringList &, const QString &, const QStringList &, int, int, QString *error)
{
    if (error)
        *error = tr("There is no terminal on this device.");
    return false;
}

bool Pty::isRunning() const
{
    return false;
}

qint64 Pty::processId() const
{
    return 0;
}

bool Pty::hasForegroundJob() const
{
    return false;
}

QString Pty::currentDirectory() const
{
    return {};
}

bool Pty::write(const QByteArray &)
{
    return false;
}

bool Pty::resize(int, int)
{
    return false;
}

void Pty::terminate(int) { }

} // namespace brulion::platform
