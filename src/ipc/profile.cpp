#include "profile.h"

#include <QCoreApplication>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace brulion::ipc {

QString organizationName()
{
    const QString profile = qEnvironmentVariable("BRULION_PROFILE");
    static const QRegularExpression valid(u"^[A-Za-z0-9_-]{1,32}$"_s);
    if (profile.isEmpty() || !valid.match(profile).hasMatch())
        return u"Brulion"_s;
    return u"Brulion-"_s + profile;
}

void applyAppNames()
{
    QCoreApplication::setOrganizationName(organizationName());
    QCoreApplication::setApplicationName(u"Brulion"_s);
    QCoreApplication::setOrganizationDomain(u"januszbit.github.io"_s);
}

} // namespace brulion::ipc
