#include "deviceinfo.h"

#include "platform.h"

#include <QFontDatabase>
#include <QInputDevice>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::ui {

DeviceInfo::DeviceInfo(QObject *parent) : QObject(parent) { }

QString DeviceInfo::platform() const
{
    return brulion::platform::targetName();
}

bool DeviceInfo::mobile() const
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    return true;
#else
    return false;
#endif
}

QString DeviceInfo::systemFontFamily() const
{
    return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
}

QString DeviceInfo::fixedFontFamily() const
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

bool DeviceInfo::hasTouchScreen() const
{
    const auto devices = QInputDevice::devices();
    return std::any_of(devices.cbegin(), devices.cend(), [](const QInputDevice *device) {
        return device->type() == QInputDevice::DeviceType::TouchScreen;
    });
}

} // namespace brulion::ui
