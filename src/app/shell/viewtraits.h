// ViewTraits — what the shell reads of the view a ShellRouteView shows (docs/UI.md "Navigation":
// the view contract). The views share no base type: each has some of the optional properties
// (`title`, `sidePanel`, `editing`, `toolbarDocked`, `canStepBack`, …) and functions (`viewState()`,
// `restoreViewState(state)`, `endEditing()`, `stepBack()`). QML looked them up dynamically, which qmlcachegen
// cannot compile; this object does it through the view's meta-object and gives typed results,
// following the properties' change signals:
//
//   ViewTraits { id: traits; view: loader.item }
//   readonly property string viewTitle: traits.title
//
// A property the view lacks, or of another type than the contract's, reads as "", null or false
// (as `typeof v.title === "string" ? v.title : ""` and `v.editing === true` did); a function it
// lacks does nothing (viewState() gives {}).
#pragma once

#include <QMetaMethod>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <QQmlComponent>

namespace brulion::shell {

class ViewTraits : public QObject {
    Q_OBJECT
    /// The view (a ShellRouteView's Loader.item); null for none.
    Q_PROPERTY(QObject *view READ view WRITE setView NOTIFY viewChanged FINAL)
    /// `title` when it is a string, else "".
    Q_PROPERTY(QString title READ title NOTIFY traitsChanged FINAL)
    /// `sidePanel` when it is a Component, else null.
    Q_PROPERTY(QQmlComponent *sidePanel READ sidePanel NOTIFY traitsChanged FINAL)
    /// `noteMode` when it is a string, else "".
    Q_PROPERTY(QString noteMode READ noteMode NOTIFY traitsChanged FINAL)
    /// The status bar's texts (ShellStatusBar, wide windows): `statusText` (PageView: the word
    /// count, the selection's while there is one) and `statusPosition` (PageView: "Block 3 of 12"
    /// while a block is edited) when they are strings, else "".
    Q_PROPERTY(QString statusText READ statusText NOTIFY traitsChanged FINAL)
    Q_PROPERTY(QString statusPosition READ statusPosition NOTIFY traitsChanged FINAL)
    // Each true only when the view's property of that name is the boolean true.
    Q_PROPERTY(bool editing READ editing NOTIFY traitsChanged FINAL)
    Q_PROPERTY(bool toolbarDocked READ toolbarDocked NOTIFY traitsChanged FINAL)
    Q_PROPERTY(bool replacesBottomNav READ replacesBottomNav NOTIFY traitsChanged FINAL)
    Q_PROPERTY(bool pictureRoute READ pictureRoute NOTIFY traitsChanged FINAL)
    Q_PROPERTY(bool pictureAsText READ pictureAsText NOTIFY traitsChanged FINAL)
    Q_PROPERTY(bool pdfRoute READ pdfRoute NOTIFY traitsChanged FINAL)
    Q_PROPERTY(bool fileRoute READ fileRoute NOTIFY traitsChanged FINAL)
    Q_PROPERTY(bool sourceMode READ sourceMode NOTIFY traitsChanged FINAL)
    Q_PROPERTY(bool edgeSwipe READ edgeSwipe NOTIFY traitsChanged FINAL)
    /// Back has a step of its own to take in the view before the history (stepBack()): a page or
    /// journal day zoomed into a block zooms out.
    Q_PROPERTY(bool canStepBack READ canStepBack NOTIFY traitsChanged FINAL)

public:
    explicit ViewTraits(QObject *parent = nullptr);

    QObject *view() const { return m_view; }
    void setView(QObject *view);

    QString title() const { return m_values.title; }
    QQmlComponent *sidePanel() const { return m_values.sidePanel; }
    QString noteMode() const { return m_values.noteMode; }
    QString statusText() const { return m_values.statusText; }
    QString statusPosition() const { return m_values.statusPosition; }
    bool editing() const { return m_values.flags & Editing; }
    bool toolbarDocked() const { return m_values.flags & ToolbarDocked; }
    bool replacesBottomNav() const { return m_values.flags & ReplacesBottomNav; }
    bool pictureRoute() const { return m_values.flags & PictureRoute; }
    bool pictureAsText() const { return m_values.flags & PictureAsText; }
    bool pdfRoute() const { return m_values.flags & PdfRoute; }
    bool fileRoute() const { return m_values.flags & FileRoute; }
    bool sourceMode() const { return m_values.flags & SourceMode; }
    bool edgeSwipe() const { return m_values.flags & EdgeSwipe; }
    bool canStepBack() const { return m_values.flags & CanStepBack; }

    /// The view's viewState() ({} when it has none, or it gives no map).
    Q_INVOKABLE QVariantMap viewState() const;
    /// Keep `state` for the view ({}: none) and hand it over at once when the view can take it
    /// (restoreViewState(state)); a view loaded later gets it from applyPendingState(). A view
    /// without restoreViewState() drops it.
    Q_INVOKABLE void restoreViewState(const QVariantMap &state);
    /// Hand the state kept by restoreViewState() to the view (a view just loaded).
    Q_INVOKABLE void applyPendingState();
    /// The view's endEditing(), when it has one.
    Q_INVOKABLE void endEditing() const;
    /// The view's stepBack(), when it has one (AppShell calls it while canStepBack holds).
    Q_INVOKABLE void stepBack() const;
    /// The view's takeFocus(), when it has one: a view built ahead, hidden, shown for the first
    /// time gives the keyboard to its own item (a list, a field) as it does when made shown.
    Q_INVOKABLE void takeFocus() const;

Q_SIGNALS:
    void viewChanged();
    void traitsChanged();

private Q_SLOTS:
    void refresh();
    void viewDestroyed();

private:
    enum Flag {
        Editing = 1 << 0,
        ToolbarDocked = 1 << 1,
        ReplacesBottomNav = 1 << 2,
        PictureRoute = 1 << 3,
        PictureAsText = 1 << 4,
        PdfRoute = 1 << 5,
        FileRoute = 1 << 6,
        SourceMode = 1 << 7,
        EdgeSwipe = 1 << 8,
        CanStepBack = 1 << 9,
    };
    /// What refresh() last read of the view (traitsChanged() when any of it changes).
    struct Values {
        QString title;
        QPointer<QQmlComponent> sidePanel;
        QString noteMode;
        QString statusText;
        QString statusPosition;
        int flags = 0; ///< Flag bits
        bool operator==(const Values &) const = default;
    };
    /// The view's method `name` with `parameters` parameters (invalid when it has none).
    QMetaMethod method(const char *name, int parameters) const;
    QVariant read(const char *name) const;

    QPointer<QObject> m_view;
    QList<QMetaObject::Connection> m_connections;
    Values m_values;
    QVariantMap m_pending; ///< restoreViewState()'s state until a view takes it ({}: none)
};

} // namespace brulion::shell
