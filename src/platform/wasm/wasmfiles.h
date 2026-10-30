#pragma once

// WebAssembly only (compiled when EMSCRIPTEN is set, see src/platform/CMakeLists.txt).
//
// Files from and to the user in the browser, without Qt Widgets (docs/WEB.md "Pliki"): the
// browser's own file picker and its save picker or a download, over Qt's private
// QWasmLocalFileAccess (what QFileDialog::getOpenFileContent / saveFileContent wrap — those need
// Qt Widgets in the build; Qt's QPA file dialog for the browser hands out weblocalfile: URLs that
// need synchronous reads, which the single-threaded build cannot do). Import, "Obraz…", the
// export and the event log use these.

#include "pickedfiles.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace brulion::platform::wasm {

/// The browser's file picker (showOpenFilePicker where the browser has it, else an
/// <input type=file>). Call from a click, tap or key handler: browsers open pickers only on a
/// user's gesture. `done` gets the chosen files in order once every one is read; an empty list
/// when the picker was cancelled — and never at all when the browser reports no cancel (an
/// <input> closed without a choice in an older browser): callers keep no state waiting for it.
/// Files above `maxBytes` (< 0: no limit) come back with tooLarge set and no data (never read).
/// `nameFilters`: Qt's filter syntax ("Notatki (*.md *.txt)").
void openFiles(const QStringList &nameFilters, bool multiple, qint64 maxBytes,
               std::function<void(const QList<PickedFile> &files)> done);

/// The browser's save picker where it has one (showSaveFilePicker: Chromium on https), else a
/// download named `suggestedName`.
void saveFile(const QByteArray &data, const QString &suggestedName);

} // namespace brulion::platform::wasm
