// DeviceInfo — facts about the device the UI adapts to (QML singleton `DeviceInfo`):
// touch screen presence, target platform and the system font families. Theme.qml derives
// `touchUi` and the font tokens from it.
#pragma once

#include <QObject>
#include <QString>
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
};

} // namespace brulion::ui
