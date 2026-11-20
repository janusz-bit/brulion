// Text recognition (OCR) of pictures and scanned PDF pages for the whole-notebook search (docs/UI.md
// "Search", "Searching attachments"; docs/ARCHITECTURE.md "Platform layer"). One backend per build,
// chosen by src/platform/CMakeLists.txt (BRULION_OCR_BACKEND):
//
//   tesseract   the desktop (Linux, Windows): Tesseract 4 or 5 loaded at run time through its C API
//               (ocr_tesseract.cpp, QLibrary) — nothing to link, nothing bundled: the Nix package
//               names its own Tesseract (BRULION_TESSERACT_LIBRARY) and models
//               (BRULION_TESSDATA_DIRS); the AppImage and Windows use the one installed on the
//               computer (ocrdata.h: where it is looked for), and a computer without one has none
//   none        Android (Tesseract through the NDK would add some 25 MB per ABI plus the models to
//               the APK — docs/ANDROID.md), the browser: ocrAvailable() false
//
// An engine is used by one thread at a time (the app's AttachmentIndexer has one worker: one
// engine). Tesseract's own threads (OpenMP) are limited to one (OMP_THREAD_LIMIT, set before the
// library loads): OCR is background work and must not take the whole machine.
#pragma once

#include <QImage>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace brulion::platform {

/// "tesseract" or "none" (this build's backend).
QString ocrBackend();
/// The engine can run here: the backend's library was found and at least one language's model.
/// Cheap after the first call (the library is looked for once, on the first call — not the GUI
/// thread's job: the app asks first from its worker).
bool ocrAvailable();
/// Why OCR is not available: "" (it is), "unsupported" (no backend in this build), "noLibrary"
/// (Tesseract not found), "noLanguages" (no models found).
QString ocrUnavailableReason();
/// The engine's name and version for the event log and Settings ("Tesseract 5.5.4"; "" without).
QString ocrEngineName();
/// The languages whose models were found (Tesseract's codes: "eng", "pol" — "osd" and "equ" left
/// out), sorted.
QStringList ocrLanguages();
/// The folder the models are read from ("" without).
QString ocrDataDir();
/// The folder of the user's own models (looked in first: a model put there adds a language). ""
/// without a backend.
QString ocrUserDataDir();

/// A word found on a picture: its text, its box in the picture's pixels, its line and block (in
/// reading order; a new number starts a new one) and the engine's confidence (0–100).
struct OcrWord {
    QString text;
    QRect box;
    int line = 0;
    int block = 0;
    float confidence = 0;
};

struct OcrResult {
    bool ok = false;
    /// Stopped by `cancelled` (nothing to keep: the picture is read again later).
    bool cancelled = false;
    QString error;
    QList<OcrWord> words;
};

/// Words below this confidence are left out (pictures without text give single noisy "words").
inline constexpr float MinOcrConfidence = 40;
/// A picture is read at most this many pixels a side and in all (callers scale it down first).
inline constexpr int MaxOcrSide = 6000;
inline constexpr qint64 MaxOcrPixels = 24'000'000;

class OcrEngine {
public:
    virtual ~OcrEngine() = default;
    OcrEngine(const OcrEngine &) = delete;
    OcrEngine &operator=(const OcrEngine &) = delete;

    /// An engine reading `languages` (Tesseract's codes joined by '+': "pol+eng"; each one of
    /// ocrLanguages()), or null (`error` says why). Slow (the models load: a few hundred ms): on
    /// the worker.
    static std::unique_ptr<OcrEngine> create(const QString &languages, QString *error = nullptr);

    /// The words on `image` (any format; converted to 8-bit grey), `dpi` its resolution (0:
    /// unknown — a picture; 300 is assumed). `cancelled` is asked while it works (a pause, the
    /// notebook closed, the app quitting) and stops it within a fraction of a second. At most
    /// MaxOcrSide / MaxOcrPixels (larger pictures fail).
    virtual OcrResult recognize(const QImage &image, int dpi, const std::function<bool()> &cancelled = {}) = 0;
    virtual QString languages() const = 0;

protected:
    OcrEngine() = default;
};

} // namespace brulion::platform
