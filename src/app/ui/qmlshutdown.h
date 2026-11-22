#pragma once

class QQmlApplicationEngine;

namespace brulion::ui {

/// The app's QML gone while the engine and the application object still exist (main(), right
/// after exec()): the engine's windows deleted, then whatever their item views handed to the
/// application object on the way.
///
/// Why: once the application is quitting (aboutToQuit was emitted), a ListView or GridView that
/// gives up a delegate — released, or cancelled while it is still being created — makes the
/// delegate a child of the application object and deletes it later
/// (QQmlDelegateModelItem::destroyObjectLater, QTBUG-87228). No event loop runs after exec(), so
/// such delegates were deleted only by the application object's destructor, when
/// `QCoreApplication::instance()` is already null. A delegate holding a view of its own whose next
/// delegate was still being created (asynchronous incubation: a view built ahead) cancels that one
/// then, and Qt reads the null instance: a crash at quit (SIGSEGV in destroyObjectLater, from
/// QQmlDelegateModel::cancel in the inner view's destructor).
///
/// Deleting the windows here hands those delegates over while the application exists; the
/// deferred deletions are then delivered at once, round after round while they change the
/// application object's children (deleting a delegate can hand over more: a view inside it
/// giving up its own).
void releaseQmlWindows(QQmlApplicationEngine *engine);

/// Deletes the QML items left as children of the application object (what the engine's own
/// destruction handed over: objects it owned, with views in them), again while that hands over
/// more. For the last moment before the application object goes (main()'s guard). Does nothing
/// without an application object.
void deleteQmlLeftovers();

/// The direct children of the application object that are QML items: what its destructor would
/// still delete (tests).
int qmlItemsLeftOnApplication();

} // namespace brulion::ui
