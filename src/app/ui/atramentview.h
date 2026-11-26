// AtramentView — an Atrament view inside the app's QML (docs/ATRAMENT.md D12, M2): the way views move
// from QML to Atrament one at a time. `AtramentView { view: "gallery" }` shows the named view of
// the process's one Atrament App (its fonts: the app's bundled ones, ":/fonts/ui" and ":/fonts").
// Built where Atrament is (BRULION_ATRAMENT: with the app on every platform); `brulion --atrament`
// opens qml/dev/AtramentDemo.qml (developer QML: desktop builds, and debuggable APKs — `adb shell am
// start … --es applicationArguments --atrament`).
#pragma once

#include "qthost.h"

#include <QQmlEngine>
#include <QString>

namespace brulion::atrament {
class App;
}

class AtramentView : public brulion::atrament::AtramentItem {
    Q_OBJECT
    QML_ELEMENT
    // The view to show: "gallery" (the framework's gallery). Set once.
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged FINAL)

public:
    explicit AtramentView(QQuickItem *parent = nullptr);

    QString view() const { return m_view; }
    void setView(const QString &view);

    // The process's Atrament App, made on first use.
    static brulion::atrament::App &atramentApp();

Q_SIGNALS:
    void viewChanged();

protected:
    void componentComplete() override;

private:
    void attachView();

    QString m_view;
    bool m_attached = false;
};
