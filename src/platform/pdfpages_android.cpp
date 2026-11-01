// PDF pages through Android's own PdfRenderer (pdfpages.h): JNI to
// android/src/io/github/januszbit/brulion/android/BrulionAndroid.kt (pdfOpen, pdfPageSizes,
// pdfRenderInto, pdfPageText, pdfSearch, pdfClose → PdfPages.kt). Nothing added to the APK (Qt PDF
// would add 4.5 MB). A reader is a session of the Kotlin side — a PdfRenderer kept open, at most six
// of them (the oldest closed: a leak here cannot exhaust file descriptors). Any thread: QJniObject
// attaches it and finds the class through the app's class loader.
//
// Pages are drawn straight into the QImage's pixels (a direct ByteBuffer over them: one copy, by
// Bitmap.copyPixelsToBuffer). Text needs Android 15 (API 35: PdfRenderer.Page.getTextContents(),
// searchText()); PdfRenderer's own search is case-insensitive only, so it is asked for each spelling
// our matcher (pdfsearch.h) finds in the page's text, and the rectangles are taken from its matches.
#include "pdfpages.h"
#include "pdfsearch.h"

#include <QHash>
#include <QJniEnvironment>
#include <QJniObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <mutex>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

constexpr char kClass[] = "io/github/januszbit/brulion/android/BrulionAndroid";
/// Page sizes are asked for in chunks (each page is opened for its size: ~1 ms).
constexpr int SizeChunk = 64;

/// A Java exception of the last call cleared; true when there was one.
bool threw()
{
    QJniEnvironment env;
    return env.checkAndClearExceptions();
}

class AndroidPdfReader final : public PdfReader {
public:
    AndroidPdfReader(int session, int pages, QSizeF first) : m_session(session), m_pages(pages)
    {
        m_sizes.insert(1, first);
    }

    ~AndroidPdfReader() override
    {
        QJniObject::callStaticMethod<void>(kClass, "pdfClose", "(I)V", jint(m_session));
        threw();
    }

    int pageCount() const override { return m_pages; }

    QSizeF pageSize(int page) override
    {
        if (page < 1 || page > m_pages)
            return {};
        if (const auto it = m_sizes.constFind(page); it != m_sizes.constEnd())
            return it.value();
        const int from = ((page - 1) / SizeChunk) * SizeChunk + 1;
        const int count = std::min(SizeChunk, m_pages - from + 1);
        const QJniObject answer = QJniObject::callStaticObjectMethod(kClass, "pdfPageSizes", "(III)Ljava/lang/String;",
                                                                     jint(m_session), jint(from), jint(count));
        if (threw() || !answer.isValid())
            return {};
        const QJsonArray sizes = QJsonDocument::fromJson(answer.toString().toUtf8()).array();
        for (qsizetype i = 0; i < sizes.size(); ++i) {
            const QJsonArray wh = sizes.at(i).toArray();
            m_sizes.insert(from + int(i), QSizeF(wh.at(0).toDouble(), wh.at(1).toDouble()));
        }
        return m_sizes.value(page);
    }

    QImage render(int page, const QSize &full, const QRect &clip) override
    {
        const QRect part = clipOf(full, clip);
        if (part.isEmpty() || page < 1 || page > m_pages)
            return {};
        QImage image(part.size(), QImage::Format_RGBA8888_Premultiplied);
        if (image.isNull() || image.bytesPerLine() != part.width() * 4)
            return {};
        QJniEnvironment env;
        jobject buffer = env->NewDirectByteBuffer(image.bits(), image.sizeInBytes());
        if (!buffer || threw())
            return {};
        const jboolean ok = QJniObject::callStaticMethod<jboolean>(
                kClass, "pdfRenderInto", "(IIIIIIIILjava/nio/ByteBuffer;)Z", jint(m_session), jint(page),
                jint(full.width()), jint(full.height()), jint(part.x()), jint(part.y()), jint(part.width()),
                jint(part.height()), buffer);
        env->DeleteLocalRef(buffer);
        if (threw() || !ok)
            return {};
        return image;
    }

    QString pageText(int page) override
    {
        if (!pdfTextAvailable() || page < 1 || page > m_pages)
            return {};
        if (page == m_textPage)
            return m_text;
        const QJniObject answer = QJniObject::callStaticObjectMethod(kClass, "pdfPageText", "(II)Ljava/lang/String;",
                                                                     jint(m_session), jint(page));
        m_textPage = page;
        m_text = threw() || !answer.isValid() ? QString() : answer.toString();
        return m_text;
    }

    QList<PdfTextHit> find(int page, QStringView query, int maxHits) override
    {
        QList<PdfTextHit> out;
        const QString text = pageText(page);
        const QList<PdfTextRange> ranges = findPdfText(text, query, std::min(maxHits, MaxPdfHitsPerPage));
        if (ranges.isEmpty())
            return out;
        QStringList spellings = pdfMatchSpellings(text, ranges);
        const QString typed = query.trimmed().toString();
        if (!typed.isEmpty() && !spellings.contains(typed))
            spellings.append(typed);
        QJniEnvironment env;
        jclass stringClass = env->FindClass("java/lang/String");
        jobjectArray array = env->NewObjectArray(jsize(spellings.size()), stringClass, nullptr);
        for (qsizetype i = 0; i < spellings.size(); ++i) {
            const QJniObject s = QJniObject::fromString(spellings.at(i));
            env->SetObjectArrayElement(array, jsize(i), s.object<jstring>());
        }
        const QJniObject answer = QJniObject::callStaticObjectMethod(
                kClass, "pdfSearch", "(II[Ljava/lang/String;)Ljava/lang/String;", jint(m_session), jint(page), array);
        env->DeleteLocalRef(array);
        env->DeleteLocalRef(stringClass);
        // PdfRenderer's matches by where they start in the page's text.
        QHash<int, QList<QRectF>> rects;
        if (!threw() && answer.isValid()) {
            for (const QJsonValue &match : QJsonDocument::fromJson(answer.toString().toUtf8()).array()) {
                const QJsonObject m = match.toObject();
                QList<QRectF> &list = rects[m.value(u"start"_s).toInt(-1)];
                for (const QJsonValue &r : m.value(u"rects"_s).toArray()) {
                    const QJsonArray ltrb = r.toArray();
                    list.append(QRectF(QPointF(ltrb.at(0).toDouble(), ltrb.at(1).toDouble()),
                                       QPointF(ltrb.at(2).toDouble(), ltrb.at(3).toDouble()))
                                        .normalized());
                }
            }
        }
        for (const PdfTextRange &range : ranges) {
            PdfTextHit hit;
            hit.start = int(range.start);
            hit.length = int(range.length);
            hit.rects = rects.value(hit.start);
            out.append(hit);
        }
        return out;
    }

private:
    int m_session = 0;
    int m_pages = 0;
    QHash<int, QSizeF> m_sizes;
    int m_textPage = 0;
    QString m_text;
};

} // namespace

bool pdfAvailable()
{
    return true;
}

QString pdfBackendName()
{
    return u"Android PdfRenderer"_s;
}

bool pdfTextAvailable()
{
    // Asked once: the text APIs of Android 15 (and whether this device's system has them at all).
    static const bool available = [] {
        const jboolean yes = QJniObject::callStaticMethod<jboolean>(kClass, "pdfTextAvailable", "()Z");
        return !threw() && yes;
    }();
    return available;
}

std::unique_ptr<PdfReader> PdfReader::open(const QString &file, PdfInfo *info)
{
    PdfInfo local;
    PdfInfo &out = info ? *info : local;
    out = PdfInfo();
    const QJniObject path = QJniObject::fromString(file);
    const QJniObject answer = QJniObject::callStaticObjectMethod(
            kClass, "pdfOpen", "(Ljava/lang/String;)Ljava/lang/String;", path.object<jstring>());
    if (threw() || !answer.isValid()) {
        out.error = u"BrulionAndroid.pdfOpen failed"_s;
        return nullptr;
    }
    const QJsonObject json = QJsonDocument::fromJson(answer.toString().toUtf8()).object();
    const int session = json.value(u"session"_s).toInt(0);
    const int pages = json.value(u"pages"_s).toInt(-1);
    const QJsonArray size = json.value(u"size"_s).toArray();
    out.locked = json.value(u"locked"_s).toBool();
    out.error = json.value(u"error"_s).toString();
    if (session <= 0 || pages <= 0) {
        if (session > 0) {
            QJniObject::callStaticMethod<void>(kClass, "pdfClose", "(I)V", jint(session));
            threw();
        }
        if (out.error.isEmpty())
            out.error = pages == 0 ? u"The PDF has no pages"_s : u"The PDF cannot be read"_s;
        return nullptr;
    }
    out.pages = pages;
    const QSizeF first(size.at(0).toDouble(), size.at(1).toDouble());
    out.pagePoints = {first};
    return std::make_unique<AndroidPdfReader>(session, pages, first);
}

} // namespace brulion::platform
