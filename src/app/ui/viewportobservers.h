// ViewportObservers — empty texts stop making every scroll step walk the page
// (docs/PERFORMANCE.md "Scrolling: empty texts no longer walked").
//
// Qt Quick tells the items with the flag QQuickItem::ItemObservesViewport whenever anything above
// them moves: every scroll step walks the item tree from the moved item (a list's content) down
// to each such item, along the paths that lead to one (QQuickItemPrivate::transformChanged; a
// path without an observer below is switched off on the first walk and on again when an observer
// is added). A `Text` starts with the flag, "until its size is known", and drops it when its text
// is set (QQuickText::setText) — but setting the text it already has returns early, so a `Text`
// whose text stays empty observes the viewport for good. A block row has at least one, a
// picture's frame three: on a page of forty photos 60 % of the GUI thread's instructions while it
// scrolled went into that walk.
//
// So once a frame (QQuickWindow::afterAnimating, before the frame's polish, on the GUI thread)
// the paths Qt would walk are walked once: an empty `Text` on them lets go of the flag (it has no
// lines whose drawing would depend on the viewport) and the paths with no observer left below are
// switched off as Qt itself does. After that a scroll step walks only to the items that use the
// viewport — a `Text` or `TextEdit` with a text of more than 10 000 characters, which draws only
// its lines in view. A frame with nothing new walks only those paths.
//
// BRULION_VIEWPORT_OBSERVERS=0 in the environment: windows are not watched (A/B measurements).
#pragma once

#include <QObject>

class QQuickItem;
class QQuickWindow;

namespace brulion::ui {

class ViewportObservers : public QObject {
    Q_OBJECT

public:
    /// The instance, installed on the application on first use: every QQuickWindow shown from
    /// then on (and every one shown already) is watched. Null without an application.
    static ViewportObservers *instance();
    /// BRULION_VIEWPORT_OBSERVERS=0 in the environment.
    static bool switchedOff();

    /// `window`'s items are looked at once a frame from now on (once, however often it is asked).
    static void watch(QQuickWindow *window);
    /// One look at `root`'s items now: the empty texts on the paths Qt walks let go of the flag,
    /// the paths with no observer below are switched off. Returns how many texts let go.
    static int sweep(QQuickItem *root);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    ViewportObservers();
};

} // namespace brulion::ui
