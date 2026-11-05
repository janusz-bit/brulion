#include "deviceinfo.h"

#include "platform.h"
#include "themepalette.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QInputDevice>
#include <QInputMethod>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::ui {

DeviceInfo::DeviceInfo(QObject *parent)
    : QObject(parent)
    , m_accessible(QAccessible::isActive())
    , m_simulated(qEnvironmentVariableIntValue("BRULION_SIMULATE_SCREEN_READER") == 1)
{
    QAccessible::installActivationObserver(this);
    if (QInputMethod *im = QGuiApplication::inputMethod())
        connect(im, &QInputMethod::visibleChanged, this, &DeviceInfo::inputMethodVisibleChanged);
}

bool DeviceInfo::inputMethodVisible() const
{
    const QInputMethod *im = QGuiApplication::inputMethod();
    return im && im->isVisible();
}

void DeviceInfo::hideInputMethod() const
{
    if (QInputMethod *im = QGuiApplication::inputMethod())
        im->hide();
}

void DeviceInfo::showInputMethod() const
{
    if (QInputMethod *im = QGuiApplication::inputMethod())
        im->show();
}

DeviceInfo::~DeviceInfo()
{
    QAccessible::removeActivationObserver(this);
}

void DeviceInfo::accessibilityActiveChanged(bool active)
{
    const bool before = screenReader();
    m_accessible = active;
    if (screenReader() != before)
        Q_EMIT screenReaderChanged();
}

void DeviceInfo::setSimulateScreenReader(bool on)
{
    if (m_simulated == on)
        return;
    m_simulated = on;
    Q_EMIT screenReaderChanged();
}

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
