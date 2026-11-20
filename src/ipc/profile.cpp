#include "profile.h"

#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::ipc {

bool isValidProfileName(const QString &name)
{
    static const QRegularExpression valid(u"^[A-Za-z0-9_-]{1,32}$"_s);
    return valid.match(name).hasMatch();
}

QString organizationName()
{
    const QString profile = qEnvironmentVariable("BRULION_PROFILE");
    if (!isValidProfileName(profile))
        return u"Brulion"_s;
    return u"Brulion-"_s + profile;
}

QString profileArgument(const QStringList &arguments)
{
    QString profile;
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        const QString &a = arguments.at(i);
        if (a == u"--open")
            break; // files from here on
        if (a == u"--profile")
            profile = i + 1 < arguments.size() ? arguments.at(++i) : QString();
        else if (a.startsWith(u"--profile="))
            profile = a.mid(10);
    }
    return profile;
}

ProfileChoice chooseProfile(const QStringList &arguments, const QString &environmentProfile,
                            const QString &xdgConfigHome, const QString &home)
{
    ProfileChoice choice;
    // (The options only: the files after a lone "--open" can have any name.)
    const qsizetype open = arguments.indexOf(u"--open"_s);
    const QStringList options = open < 0 ? arguments : arguments.first(open);
    const bool asked = options.contains(u"--profile"_s)
                       || std::ranges::any_of(options, [](const QString &a) { return a.startsWith(u"--profile="); });
    if (asked) {
        const QString named = profileArgument(arguments);
        if (!isValidProfileName(named))
            choice.error = u"--profile takes a name of letters, digits, \"-\" and \"_\" (at most 32): \"%1\""_s.arg(named);
        else
            choice.profile = named;
        return choice;
    }
    if (isValidProfileName(environmentProfile)) {
        choice.profile = environmentProfile;
        return choice;
    }
    // Only tests and screenshots: never the user's own settings and recent notebooks.
    const bool testRun = std::ranges::any_of(options, [](const QString &a) {
        return a.startsWith(u"--selftest") || a == u"--journey" || a == u"--screenshot";
    });
    const QString config = QDir::cleanPath(xdgConfigHome);
    const bool ownHome = xdgConfigHome.isEmpty()
                         || (!home.isEmpty() && config == QDir::cleanPath(home + u"/.config"_s));
    if (testRun && ownHome) {
        choice.profile = u"test"_s;
        choice.implied = true;
    }
    return choice;
}

ProfileChoice applyProfile(const QStringList &arguments)
{
    const ProfileChoice choice = chooseProfile(arguments, qEnvironmentVariable("BRULION_PROFILE"),
                                               qEnvironmentVariable("XDG_CONFIG_HOME"), QDir::homePath());
    if (choice.error.isEmpty() && !choice.profile.isEmpty())
        qputenv("BRULION_PROFILE", choice.profile.toUtf8());
    return choice;
}

void applyAppNames()
{
    QCoreApplication::setOrganizationName(organizationName());
    QCoreApplication::setApplicationName(u"Brulion"_s);
    QCoreApplication::setOrganizationDomain(u"januszbit.github.io"_s);
}

} // namespace brulion::ipc
