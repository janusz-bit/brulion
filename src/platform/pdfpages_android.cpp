// PDF pages through Android's own PdfRenderer (pdfpages.h): JNI to
// android/src/io/github/januszbit/brulion/android/BrulionAndroid.kt (pdfInfo, renderPdfPage →
// PdfPages.kt). Nothing added to the APK (Qt PDF would add 4.5 MB). Any thread: QJniObject attaches
// it and finds the class through the app's class loader; the Kotlin side keeps no state.
#include "pdfpages.h"

#include <QJniEnvironment>
#include <QJniObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

constexpr char kClass[] = "io/github/januszbit/brulion/android/BrulionAndroid";

/// A Java exception of the last call cleared; true when there was one.
bool threw()
{
    QJniEnvironment env;
    return env.checkAndClearExceptions();
}

} // namespace

bool pdfAvailable()
{
    return true;
}

QString pdfBackendName()
{
    return u"Android PdfRenderer"_s;
}

PdfInfo pdfInfo(const QString &file, bool pageSizes)
{
    PdfInfo info;
    const QJniObject path = QJniObject::fromString(file);
    const QJniObject answer = QJniObject::callStaticObjectMethod(
            kClass, "pdfInfo", "(Ljava/lang/String;Z)Ljava/lang/String;", path.object<jstring>(), jboolean(pageSizes));
    if (threw() || !answer.isValid()) {
        info.error = u"BrulionAndroid.pdfInfo failed"_s;
        return info;
    }
    const QJsonObject json = QJsonDocument::fromJson(answer.toString().toUtf8()).object();
    info.pages = json.value(u"pages"_s).toInt(-1);
    info.locked = json.value(u"locked"_s).toBool();
    info.error = json.value(u"error"_s).toString();
    for (const QJsonValue &size : json.value(u"sizes"_s).toArray()) {
        const QJsonArray wh = size.toArray();
        info.pagePoints.append(QSizeF(wh.at(0).toDouble(), wh.at(1).toDouble()));
    }
    if (info.pages <= 0 || info.pagePoints.size() != (pageSizes ? info.pages : 1)) {
        info.pages = -1;
        info.pagePoints.clear();
        if (info.error.isEmpty())
            info.error = u"The PDF has no pages"_s;
    }
    return info;
}

QImage renderPdfPage(const QString &file, int page, int maxSide, QString *error)
{
    const QJniObject path = QJniObject::fromString(file);
    const QJniObject answer = QJniObject::callStaticObjectMethod(kClass, "renderPdfPage", "(Ljava/lang/String;II)[B",
                                                                 path.object<jstring>(), jint(page),
                                                                 jint(std::clamp(maxSide, 1, MaxPdfPageSide)));
    if (threw() || !answer.isValid()) {
        if (error)
            *error = u"Page %1 cannot be rendered"_s.arg(page);
        return {};
    }
    // 8 bytes of header — the width and the height, big-endian — then the RGBA_8888 premultiplied
    // pixels (Bitmap.copyPixelsToBuffer), row by row.
    QJniEnvironment env;
    const jbyteArray array = answer.object<jbyteArray>();
    const jsize length = env->GetArrayLength(array);
    QByteArray bytes(length, Qt::Uninitialized);
    env->GetByteArrayRegion(array, 0, length, reinterpret_cast<jbyte *>(bytes.data()));
    const auto be32 = [&bytes](int at) {
        return (quint32(quint8(bytes[at])) << 24) | (quint32(quint8(bytes[at + 1])) << 16)
               | (quint32(quint8(bytes[at + 2])) << 8) | quint32(quint8(bytes[at + 3]));
    };
    const qint64 w = length >= 8 ? be32(0) : 0;
    const qint64 h = length >= 8 ? be32(4) : 0;
    if (w <= 0 || h <= 0 || w > MaxPdfPageSide || h > MaxPdfPageSide || length != 8 + w * h * 4) {
        if (error)
            *error = u"Page %1: a malformed picture from PdfRenderer"_s.arg(page);
        return {};
    }
    const QImage view(reinterpret_cast<const uchar *>(bytes.constData() + 8), int(w), int(h), int(w * 4),
                      QImage::Format_RGBA8888_Premultiplied);
    return view.convertToFormat(QImage::Format_RGB32); // (a copy: `bytes` goes)
}

} // namespace brulion::platform
