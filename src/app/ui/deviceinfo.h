// DeviceInfo — facts about the device the UI adapts to (QML singleton `DeviceInfo`):
// touch screen presence, target platform, the system font families and the installed ones (the
// font pickers of Ustawienia → Wygląd / Edytor), whether a screen reader is on. Theme.qml derives
// `touchUi` and the font tokens from it.
#pragma once

#include <QAccessible>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace brulion::ui {

class DeviceInfo : public QObject, public QAccessible::ActivationObserver {
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
    /// A screen reader or another accessibility client (TalkBack, Orca, NVDA, Narrator, Android's
    /// UiAutomator) is active — QAccessible::isActive(), followed through an activation observer
    /// — or simulated (simulateScreenReader; BRULION_SIMULATE_SCREEN_READER=1 for manual runs): the
    /// accessible link items of rendered text exist only then (BLinkTargets.qml).
    Q_PROPERTY(bool screenReader READ screenReader NOTIFY screenReaderChanged FINAL)
    /// Behave as if a screen reader were on (self-tests; the environment variable sets it at start).
    Q_PROPERTY(bool simulateScreenReader READ simulateScreenReader WRITE setSimulateScreenReader NOTIFY
                       screenReaderChanged FINAL)
    /// The on-screen keyboard is shown (Qt.inputMethod.visible, typed for compiled QML).
    Q_PROPERTY(bool inputMethodVisible READ inputMethodVisible NOTIFY inputMethodVisibleChanged FINAL)

public:
    explicit DeviceInfo(QObject *parent = nullptr);
    ~DeviceInfo() override;

    QString platform() const;
    bool mobile() const;
    bool hasTouchScreen() const;
    QString systemFontFamily() const;
    QString fixedFontFamily() const;
    bool screenReader() const { return m_simulated || m_accessible; }
    bool simulateScreenReader() const { return m_simulated; }
    void setSimulateScreenReader(bool on);
    bool inputMethodVisible() const;

    /// Qt.inputMethod.hide() / show(), callable from compiled QML.
    Q_INVOKABLE void hideInputMethod() const;
    Q_INVOKABLE void showInputMethod() const;

    /// The installed families a font picker offers (filterFontFamilies(): no emoji, symbol or
    /// private families; sorted for Polish readers); `fixedOnly`: monospaced ones only.
    Q_INVOKABLE QStringList fontFamilies(bool fixedOnly) const;
    /// `family` is installed (a family chosen on another device or since removed is not:
    /// Theme.qml falls back to the default font then).
    Q_INVOKABLE bool hasFontFamily(const QString &family) const;

Q_SIGNALS:
    void screenReaderChanged();
    void inputMethodVisibleChanged();

private:
    void accessibilityActiveChanged(bool active) override;

    bool m_accessible = false;
    bool m_simulated = false;
    mutable QStringList m_families;
    mutable QStringList m_fixedFamilies;
};

} // namespace brulion::ui
