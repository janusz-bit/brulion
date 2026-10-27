#include "appidentity.h"

#include "log/categories.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#endif

using namespace Qt::StringLiterals;

namespace brulion::platform {

QString appUserModelId(const QString &organizationName)
{
    const QString base = u"JanuszBit.Brulion"_s;
    const QString prefix = u"Brulion-"_s;
    if (organizationName.startsWith(prefix) && organizationName.size() > prefix.size())
        return base + u'.' + organizationName.mid(prefix.size());
    return base;
}

QString appUserModelId()
{
    return appUserModelId(QCoreApplication::organizationName());
}

bool registerAppUserModelId(QSettings &classes, const QString &id, const QString &displayName, const QString &iconPath)
{
    classes.beginGroup(id);
    if (classes.value(u"DisplayName"_s).toString() != displayName)
        classes.setValue(u"DisplayName"_s, displayName);
    if (!iconPath.isEmpty() && classes.value(u"IconUri"_s).toString() != iconPath)
        classes.setValue(u"IconUri"_s, iconPath);
    classes.endGroup();
    classes.sync();
    return classes.status() == QSettings::NoError;
}

void applyAppIdentity()
{
#ifdef Q_OS_WIN
    const QString id = appUserModelId();
    const HRESULT result = SetCurrentProcessExplicitAppUserModelID(reinterpret_cast<const wchar_t *>(id.utf16()));
    if (FAILED(result))
        qCWarning(brulion::logging::lcApp).noquote()
                << u"AppUserModelID %1 not set (0x%2)"_s.arg(id).arg(quint32(result), 8, 16, QLatin1Char('0'));
    const QString icon = QCoreApplication::applicationDirPath() + u"/brulion.ico"_s;
    QSettings classes(u"HKEY_CURRENT_USER\\Software\\Classes\\AppUserModelId"_s, QSettings::NativeFormat);
    const QString name = id == appUserModelId(u"Brulion"_s) ? u"Brulion"_s : u"Brulion (%1)"_s.arg(id.section(u'.', 2));
    if (!registerAppUserModelId(classes, id, name,
                                QFileInfo::exists(icon) ? QDir::toNativeSeparators(icon) : QString()))
        qCWarning(brulion::logging::lcApp).noquote()
                << u"AppUserModelID %1 not registered: notifications may not show"_s.arg(id);
#endif
}

} // namespace brulion::platform
