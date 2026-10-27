#include "deviceinfo.h"

#include "platform.h"
#include "themepalette.h"

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

QStringList DeviceInfo::fontFamilies(bool fixedOnly) const
{
    QStringList &cache = fixedOnly ? m_fixedFamilies : m_families;
    if (cache.isEmpty()) {
        QStringList all = QFontDatabase::families();
        if (fixedOnly) {
            all.erase(std::remove_if(all.begin(), all.end(),
                                     [](const QString &f) { return !QFontDatabase::isFixedPitch(f); }),
                      all.end());
            // Some systems report their fixed font without the fixed-pitch flag.
            const QString fixed = fixedFontFamily();
            if (!fixed.isEmpty() && !all.contains(fixed))
                all.append(fixed);
        }
        cache = filterFontFamilies(all);
    }
    return cache;
}

bool DeviceInfo::hasFontFamily(const QString &family) const
{
    return !family.trimmed().isEmpty() && QFontDatabase::hasFamily(family.trimmed());
}

bool DeviceInfo::hasTouchScreen() const
{
    const auto devices = QInputDevice::devices();
    return std::any_of(devices.cbegin(), devices.cend(), [](const QInputDevice *device) {
        return device->type() == QInputDevice::DeviceType::TouchScreen;
    });
}

} // namespace brulion::ui
