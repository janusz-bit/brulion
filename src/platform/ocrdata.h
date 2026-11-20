// Where OCR's engine and models are looked for, and which languages it reads (ocr.h; docs/UI.md
// "Searching attachments"). Pure — tested on every platform (tests/platform/tst_ocrdata.cpp);
// ocr_tesseract.cpp gives it the environment and the folders that exist.
#pragma once

#include <QString>
#include <QStringList>

namespace brulion::platform {

/// The Tesseract library files to try, in order: BRULION_TESSERACT_LIBRARY from the environment
/// (`envLibrary`; "none" turns OCR off — tests of the "not available" state), the build's
/// (`builtIn`: the Nix package's store path), then the system's — Linux: libtesseract.so.5,
/// libtesseract.so.4 (QLibrary's names "tesseract" with versions); Windows: libtesseract-5.dll and
/// tesseract55/54/53/41.dll, also in the folders of a Tesseract installation (`windowsDirs`:
/// "C:/Program Files/Tesseract-OCR", the user's Programs folder). Empty when "none".
QStringList tesseractLibraryCandidates(const QString &envLibrary, const QString &builtIn, bool windows,
                                       const QStringList &windowsDirs);

/// The folders that may hold the models (*.traineddata), in order: the user's own
/// (`userDir`), BRULION_TESSDATA_DIR and TESSDATA_PREFIX (`envDirs`), the build's (`builtIn`),
/// the library's own share/tessdata (`libraryDir`/../share/tessdata) and tessdata beside it
/// (Windows' installation), then the usual system folders (Linux: /usr/share/tesseract-ocr/5/tessdata,
/// /usr/share/tesseract-ocr/4.00/tessdata, /usr/share/tessdata, /usr/share/tesseract/tessdata,
/// /usr/local/share/tessdata). Duplicates and empty entries left out.
QStringList tessdataCandidates(const QString &userDir, const QStringList &envDirs, const QStringList &builtIn,
                               const QString &libraryDir, bool windows);

/// The languages of model files (`files`: names in a folder): "pol.traineddata" → "pol"; "osd",
/// "equ" and names that are no language code (letters, digits, '_' and '-' only, 2–32 long) left
/// out. Sorted, unique.
QStringList tessdataLanguages(const QStringList &files);

/// Tesseract's code for a UI or spelling language ("pl" → "pol", "en" → "eng", "de" → "deu", …;
/// a region ignored: "pt_BR" → "por"); "" for one it does not know.
QString tesseractCode(const QString &language);

/// The languages OCR reads, as Tesseract wants them ("pol+eng"): `setting` — "auto" or "" for the
/// UI language and English, else codes or UI languages separated by ',' / '+' / ' ' — kept to the
/// `installed` ones, in that order, without repeats; "auto" with neither installed takes the first
/// installed model; "" when none of them is installed.
QString ocrLanguagesFor(const QString &setting, const QString &uiLanguage, const QStringList &installed);

} // namespace brulion::platform
