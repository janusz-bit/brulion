// OCR with Tesseract (ocr.h): the library loaded at run time and driven through its C API
// (tesseract/capi.h — stable since Tesseract 3.02; the few functions used are declared here, so the
// build needs no headers and no library). Where it is looked for: ocrdata.h. The models are the
// *.traineddata files of the first folder that has any (the user's own folder first).
#include "ocr.h"
#include "ocrdata.h"

#include "log/categories.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QStandardPaths>

#include <algorithm>
#include <mutex>

using namespace Qt::StringLiterals;

namespace brulion::platform {

using logging::lcPlatform;

namespace {

// ---- The C API (capi.h) ----------------------------------------------------------------------

using TessBaseAPI = void;
using TessResultIterator = void;
using TessPageIterator = void;
using ETEXT_DESC = void;
using TessCancelFunc = bool (*)(void *cancelThis, int words);

constexpr int PSM_AUTO = 3;
constexpr int RIL_BLOCK = 0;
constexpr int RIL_TEXTLINE = 2;
constexpr int RIL_WORD = 3;

struct Api {
    const char *(*version)() = nullptr;
    TessBaseAPI *(*create)() = nullptr;
    void (*destroy)(TessBaseAPI *) = nullptr;
    int (*init3)(TessBaseAPI *, const char *, const char *) = nullptr;
    void (*end)(TessBaseAPI *) = nullptr;
    int (*setVariable)(TessBaseAPI *, const char *, const char *) = nullptr;
    void (*setPageSegMode)(TessBaseAPI *, int) = nullptr;
    void (*setImage)(TessBaseAPI *, const unsigned char *, int, int, int, int) = nullptr;
    void (*setSourceResolution)(TessBaseAPI *, int) = nullptr;
    int (*recognize)(TessBaseAPI *, ETEXT_DESC *) = nullptr;
    void (*clear)(TessBaseAPI *) = nullptr;
    TessResultIterator *(*getIterator)(TessBaseAPI *) = nullptr;
    void (*iteratorDelete)(TessResultIterator *) = nullptr;
    int (*iteratorNext)(TessResultIterator *, int) = nullptr;
    char *(*iteratorText)(const TessResultIterator *, int) = nullptr;
    float (*iteratorConfidence)(const TessResultIterator *, int) = nullptr;
    const TessPageIterator *(*pageIterator)(const TessResultIterator *) = nullptr;
    int (*boundingBox)(const TessPageIterator *, int, int *, int *, int *, int *) = nullptr;
    int (*atBeginningOf)(const TessPageIterator *, int) = nullptr;
    void (*deleteText)(const char *) = nullptr;
    ETEXT_DESC *(*monitorCreate)() = nullptr;
    void (*monitorDelete)(ETEXT_DESC *) = nullptr;
    void (*monitorSetCancelFunc)(ETEXT_DESC *, TessCancelFunc) = nullptr;
    void (*monitorSetCancelThis)(ETEXT_DESC *, void *) = nullptr;
    void (*monitorSetDeadline)(ETEXT_DESC *, int) = nullptr;
};

template<typename F>
bool resolve(QLibrary &library, const char *name, F &target)
{
    target = reinterpret_cast<F>(library.resolve(name));
    return target != nullptr;
}

bool resolveAll(QLibrary &l, Api &a)
{
    return resolve(l, "TessVersion", a.version) && resolve(l, "TessBaseAPICreate", a.create)
           && resolve(l, "TessBaseAPIDelete", a.destroy) && resolve(l, "TessBaseAPIInit3", a.init3)
           && resolve(l, "TessBaseAPIEnd", a.end) && resolve(l, "TessBaseAPISetVariable", a.setVariable)
           && resolve(l, "TessBaseAPISetPageSegMode", a.setPageSegMode) && resolve(l, "TessBaseAPISetImage", a.setImage)
           && resolve(l, "TessBaseAPISetSourceResolution", a.setSourceResolution)
           && resolve(l, "TessBaseAPIRecognize", a.recognize) && resolve(l, "TessBaseAPIClear", a.clear)
           && resolve(l, "TessBaseAPIGetIterator", a.getIterator)
           && resolve(l, "TessResultIteratorDelete", a.iteratorDelete)
           && resolve(l, "TessResultIteratorNext", a.iteratorNext)
           && resolve(l, "TessResultIteratorGetUTF8Text", a.iteratorText)
           && resolve(l, "TessResultIteratorConfidence", a.iteratorConfidence)
           && resolve(l, "TessResultIteratorGetPageIteratorConst", a.pageIterator)
           && resolve(l, "TessPageIteratorBoundingBox", a.boundingBox)
           && resolve(l, "TessPageIteratorIsAtBeginningOf", a.atBeginningOf)
           && resolve(l, "TessDeleteText", a.deleteText) && resolve(l, "TessMonitorCreate", a.monitorCreate)
           && resolve(l, "TessMonitorDelete", a.monitorDelete)
           && resolve(l, "TessMonitorSetCancelFunc", a.monitorSetCancelFunc)
           && resolve(l, "TessMonitorSetCancelThis", a.monitorSetCancelThis)
           && resolve(l, "TessMonitorSetDeadlineMSecs", a.monitorSetDeadline);
}

QString builtInLibrary()
{
#ifdef BRULION_TESSERACT_LIBRARY
    return QString::fromUtf8(BRULION_TESSERACT_LIBRARY);
#else
    return {};
#endif
}

QStringList builtInDataDirs()
{
#ifdef BRULION_TESSDATA_DIRS
    return QString::fromUtf8(BRULION_TESSDATA_DIRS).split(u'|', Qt::SkipEmptyParts);
#else
    return {};
#endif
}

/// The library and the models, looked for once.
struct Engine {
    QLibrary library;
    Api api;
    bool loaded = false;
    QString name;
    QString reason;
    QString dataDir;
    QStringList languages;
};

QString userDataDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : QDir(base).filePath(u"tessdata"_s);
}

Engine &engine()
{
    static Engine e;
    static std::once_flag once;
    std::call_once(once, [] {
#ifdef Q_OS_WIN
        constexpr bool windows = true;
        QStringList installs;
        for (const char *var : {"ProgramFiles", "ProgramW6432", "LOCALAPPDATA"}) {
            const QString base = qEnvironmentVariable(var);
            if (base.isEmpty())
                continue;
            installs.append(QDir(base).filePath(u"Tesseract-OCR"_s));
            installs.append(QDir(base).filePath(u"Programs/Tesseract-OCR"_s));
        }
#else
        constexpr bool windows = false;
        const QStringList installs;
#endif
        // One thread for Tesseract's OpenMP loops (read when its runtime loads): background work.
        if (!qEnvironmentVariableIsSet("OMP_THREAD_LIMIT"))
            qputenv("OMP_THREAD_LIMIT", "1");
        QString libraryDir;
        for (const QString &candidate : tesseractLibraryCandidates(qEnvironmentVariable("BRULION_TESSERACT_LIBRARY"),
                                                                   builtInLibrary(), windows, installs)) {
            e.library.setFileName(candidate);
            if (!e.library.load())
                continue;
            if (!resolveAll(e.library, e.api)) {
                qCWarning(lcPlatform) << "Tesseract library without the C API:" << candidate;
                e.library.unload();
                continue;
            }
            e.loaded = true;
            libraryDir = QFileInfo(e.library.fileName()).absolutePath();
            e.name = u"Tesseract "_s + QString::fromUtf8(e.api.version());
            break;
        }
        if (!e.loaded) {
            e.reason = u"noLibrary"_s;
            qCInfo(lcPlatform) << "OCR: no Tesseract library found";
            return;
        }
        QStringList envDirs;
        for (const char *var : {"BRULION_TESSDATA_DIR", "TESSDATA_PREFIX"}) {
            const QString dir = qEnvironmentVariable(var);
            if (!dir.isEmpty()) {
                envDirs.append(dir);
                envDirs.append(QDir(dir).filePath(u"tessdata"_s));
            }
        }
        for (const QString &dir : tessdataCandidates(userDataDir(), envDirs, builtInDataDirs(), libraryDir, windows)) {
            const QStringList found = tessdataLanguages(QDir(dir).entryList({u"*.traineddata"_s}, QDir::Files));
            if (found.isEmpty())
                continue;
            e.dataDir = dir;
            e.languages = found;
            break;
        }
        if (e.languages.isEmpty())
            e.reason = u"noLanguages"_s;
        qCInfo(lcPlatform).noquote() << "OCR:" << e.name << "from" << e.library.fileName() << "models"
                                     << e.languages.join(u',') << "in" << e.dataDir;
    });
    return e;
}

bool cancelThunk(void *cancelThis, int)
{
    const auto *cancelled = static_cast<const std::function<bool()> *>(cancelThis);
    return cancelled && *cancelled && (*cancelled)();
}

class TesseractEngine final : public OcrEngine {
public:
    TesseractEngine(const Api &api, TessBaseAPI *handle, QString languages)
        : m_api(api), m_handle(handle), m_languages(std::move(languages))
    { }
    ~TesseractEngine() override
    {
        m_api.end(m_handle);
        m_api.destroy(m_handle);
    }

    QString languages() const override { return m_languages; }

    OcrResult recognize(const QImage &image, int dpi, const std::function<bool()> &cancelled) override
    {
        OcrResult out;
        if (image.isNull() || image.width() > MaxOcrSide || image.height() > MaxOcrSide
            || qint64(image.width()) * image.height() > MaxOcrPixels) {
            out.error = u"The picture is too large to read"_s;
            return out;
        }
        const QImage grey = image.convertToFormat(QImage::Format_Grayscale8);
        m_api.setImage(m_handle, grey.constBits(), grey.width(), grey.height(), 1, int(grey.bytesPerLine()));
        m_api.setSourceResolution(m_handle, dpi > 0 ? dpi : 300);
        ETEXT_DESC *monitor = m_api.monitorCreate();
        m_api.monitorSetCancelFunc(monitor, &cancelThunk);
        m_api.monitorSetCancelThis(monitor, const_cast<std::function<bool()> *>(&cancelled));
        // A hostile or huge picture never takes more than this.
        m_api.monitorSetDeadline(monitor, 120'000);
        const int status = m_api.recognize(m_handle, monitor);
        m_api.monitorDelete(monitor);
        if (cancelled && cancelled()) {
            out.cancelled = true;
            m_api.clear(m_handle);
            return out;
        }
        if (status != 0) {
            out.error = u"Tesseract could not read the picture"_s;
            m_api.clear(m_handle);
            return out;
        }
        TessResultIterator *it = m_api.getIterator(m_handle);
        int line = -1;
        int block = -1;
        if (it) {
            do {
                const TessPageIterator *page = m_api.pageIterator(it);
                if (m_api.atBeginningOf(page, RIL_BLOCK))
                    ++block;
                if (m_api.atBeginningOf(page, RIL_TEXTLINE))
                    ++line;
                char *text = m_api.iteratorText(it, RIL_WORD);
                if (!text)
                    continue;
                OcrWord word;
                word.text = QString::fromUtf8(text).trimmed();
                m_api.deleteText(text);
                word.confidence = m_api.iteratorConfidence(it, RIL_WORD);
                int left = 0, top = 0, right = 0, bottom = 0;
                if (!m_api.boundingBox(page, RIL_WORD, &left, &top, &right, &bottom))
                    continue;
                word.box = QRect(QPoint(left, top), QPoint(right - 1, bottom - 1));
                word.line = std::max(0, line);
                word.block = std::max(0, block);
                const bool letters = std::ranges::any_of(word.text, [](QChar c) { return c.isLetterOrNumber(); });
                if (word.text.isEmpty() || !letters || word.confidence < MinOcrConfidence)
                    continue;
                out.words.append(std::move(word));
            } while (m_api.iteratorNext(it, RIL_WORD));
            m_api.iteratorDelete(it);
        }
        m_api.clear(m_handle);
        out.ok = true;
        return out;
    }

private:
    const Api &m_api;
    TessBaseAPI *m_handle;
    QString m_languages;
};

} // namespace

QString ocrBackend()
{
    return u"tesseract"_s;
}

bool ocrAvailable()
{
    return engine().loaded && !engine().languages.isEmpty();
}

QString ocrUnavailableReason()
{
    return engine().reason;
}

QString ocrEngineName()
{
    return engine().name;
}

QStringList ocrLanguages()
{
    return engine().languages;
}

QString ocrDataDir()
{
    return engine().dataDir;
}

QString ocrUserDataDir()
{
    return userDataDir();
}

std::unique_ptr<OcrEngine> OcrEngine::create(const QString &languages, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return nullptr;
    };
    Engine &e = engine();
    if (!ocrAvailable())
        return fail(e.reason);
    const QStringList wanted = languages.split(u'+', Qt::SkipEmptyParts);
    if (wanted.isEmpty() || std::ranges::any_of(wanted, [&e](const QString &l) { return !e.languages.contains(l); }))
        return fail(u"No model for %1"_s.arg(languages));
    TessBaseAPI *handle = e.api.create();
    if (!handle)
        return fail(u"Tesseract could not start"_s);
    // Its messages ("Estimating resolution…") are no business of the event log.
#ifdef Q_OS_WIN
    e.api.setVariable(handle, "debug_file", "NUL");
#else
    e.api.setVariable(handle, "debug_file", "/dev/null");
#endif
    const QByteArray dir = QFile::encodeName(QDir::toNativeSeparators(e.dataDir));
    if (e.api.init3(handle, dir.constData(), languages.toUtf8().constData()) != 0) {
        e.api.destroy(handle);
        return fail(u"Tesseract could not load %1"_s.arg(languages));
    }
    e.api.setPageSegMode(handle, PSM_AUTO);
    return std::make_unique<TesseractEngine>(e.api, handle, languages);
}

} // namespace brulion::platform
