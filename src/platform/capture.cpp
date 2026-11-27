#include "capture.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QPointer>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace detail {
/// The system's recognizer of this build (capture_android.cpp / capture_none.cpp), or nullptr.
SpeechRecognizer *systemSpeechRecognizer();
/// The system's camera app of this build, or nullptr.
PhotoCamera *systemPhotoCamera();
} // namespace detail

namespace {

template<typename T>
QPointer<T> &registered()
{
    static QPointer<T> object;
    return object;
}

/// `object` registered in place of the one before (deleted), owned by the application.
template<typename T>
void registerObject(T *object)
{
    QPointer<T> &current = registered<T>();
    if (current == object)
        return;
    if (current)
        current->deleteLater();
    current = object;
    if (object && QCoreApplication::instance() && !object->parent())
        object->setParent(QCoreApplication::instance());
}

} // namespace

SpeechRecognizer *speechRecognizer()
{
    if (SpeechRecognizer *own = registered<SpeechRecognizer>())
        return own;
    return detail::systemSpeechRecognizer();
}

void setSpeechRecognizer(SpeechRecognizer *recognizer)
{
    registerObject(recognizer);
}

PhotoCamera *photoCamera()
{
    if (PhotoCamera *own = registered<PhotoCamera>())
        return own;
    return detail::systemPhotoCamera();
}

void setPhotoCamera(PhotoCamera *camera)
{
    registerObject(camera);
}

void PhotoCamera::answered(const QJsonObject &answer)
{
    if (answer.value(u"taken"_s).toBool())
        Q_EMIT photosWaiting();
    else
        Q_EMIT failed(answer.value(u"purpose"_s).toString(), answer.value(u"error"_s).toString());
}

QList<CapturedPhoto> capturedPhotos(const QJsonArray &array)
{
    QList<CapturedPhoto> photos;
    for (const QJsonValue &value : array) {
        const QJsonObject o = value.toObject();
        CapturedPhoto photo{o.value(u"file"_s).toString(), o.value(u"notebook"_s).toString(),
                            o.value(u"purpose"_s).toString()};
        if (!photo.file.isEmpty())
            photos.append(photo);
    }
    return photos;
}

} // namespace brulion::platform
