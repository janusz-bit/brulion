// DeviceInfo — facts about the device the UI adapts to (QML singleton `DeviceInfo`):
// touch screen presence, target platform, the system font families and the installed ones (the
// font pickers of Ustawienia → Wygląd / Edytor). Theme.qml derives `touchUi` and the font tokens
// from it.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace brulion::ui {

class DeviceInfo : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// "linux", "windows", "android" or "wasm" (brulion::platform::targetName()).
    Q_PROPERTY(QString platform READ platform CONSTANT FINAL)
    /// Android (or another phone/tablet OS): touch-first UI everywhere.
    Q_PROPERTY(bool mobile READ mobile CONSTANT FINAL)
    /// A touch screen was attached at start-up.
    Q_PROPERTY(bool hasTouchScreen READ hasTouchScreen CONSTANT FINAL)
    /// The system's general UI font family.
    Q_PROPERTY(QString systemFontFamily READ systemFontFamily CONSTANT FINAL)
    /// The system's fixed-width font family (code, diffs, keyboard hints).
    Q_PROPERTY(QString fixedFontFamily READ fixedFontFamily CONSTANT FINAL)

public:
    explicit DeviceInfo(QObject *parent = nullptr);

    QString platform() const;
    bool mobile() const;
    bool hasTouchScreen() const;
    QString systemFontFamily() const;
    QString fixedFontFamily() const;

    /// The installed families a font picker offers (filterFontFamilies(): no emoji, symbol or
    /// private families; sorted for Polish readers); `fixedOnly`: monospaced ones only.
    Q_INVOKABLE QStringList fontFamilies(bool fixedOnly) const;
    /// `family` is installed (a family chosen on another device or since removed is not:
    /// Theme.qml falls back to the default font then).
    Q_INVOKABLE bool hasFontFamily(const QString &family) const;

private:
    mutable QStringList m_families;
    mutable QStringList m_fixedFamilies;
};

} // namespace brulion::ui
