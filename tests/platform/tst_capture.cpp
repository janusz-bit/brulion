// The camera of capture.h: the photos the Android camera hands out (capturedPhotos(): the JSON of
// MediaCapture.kt's takePhotos, a photo's file, notebook and purpose — Polish names, entries
// without a file, broken entries left out), its answers (PhotoCamera::answered: a photo waiting,
// none taken, an error, malformed ones) and the camera registered by the app (setPhotoCamera:
// used instead of the system's, the one it replaces deleted, nullptr the system's again — none on
// the desktop). Pure or desktop-only, so tested on every platform.
#include "capture.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::platform::CapturedPhoto;
using brulion::platform::PhotoCamera;

Q_DECLARE_METATYPE(QList<CapturedPhoto>)

namespace {

class Camera : public PhotoCamera {
public:
    using PhotoCamera::answered;
    bool take(const QString &, const QString &) override { return true; }
    QList<CapturedPhoto> takePhotos(const QString &) override { return {}; }
};

} // namespace

class TestCapture : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void capturedPhotos_data()
    {
        QTest::addColumn<QJsonArray>("json");
        QTest::addColumn<QList<CapturedPhoto>>("photos");
        const QJsonObject photo{{u"file"_s, u"/data/user/0/pl.brulion/cache/camera/photo-1.jpg"_s},
                                {u"notebook"_s, u"notatnik-ąę"_s},
                                {u"purpose"_s, u"AI/2026-11-27 Żółw.md"_s},
                                {u"started"_s, 1795000000000.0}};
        QTest::newRow("none") << QJsonArray() << QList<CapturedPhoto>();
        QTest::newRow("a photo, Polish names")
                << QJsonArray{photo}
                << QList<CapturedPhoto>{{u"/data/user/0/pl.brulion/cache/camera/photo-1.jpg"_s, u"notatnik-ąę"_s,
                                         u"AI/2026-11-27 Żółw.md"_s}};
        QTest::newRow("a new conversation's draft, no notebook")
                << QJsonArray{QJsonObject{{u"file"_s, u"/c/photo-2.jpg"_s}}}
                << QList<CapturedPhoto>{{u"/c/photo-2.jpg"_s, QString(), QString()}};
        QTest::newRow("broken entries left out, the order kept")
                << QJsonArray{QJsonObject{{u"notebook"_s, u"bez pliku"_s}},
                              u"photo-3.jpg"_s,
                              7,
                              QJsonObject{{u"file"_s, 5}},
                              QJsonObject{{u"file"_s, u"/c/b.jpg"_s}},
                              QJsonObject{{u"file"_s, u"/c/a.jpg"_s}, {u"purpose"_s, QJsonArray{1}}}}
                << QList<CapturedPhoto>{{u"/c/b.jpg"_s, QString(), QString()}, {u"/c/a.jpg"_s, QString(), QString()}};
    }
    void capturedPhotos()
    {
        QFETCH(QJsonArray, json);
        QFETCH(QList<CapturedPhoto>, photos);
        QCOMPARE(brulion::platform::capturedPhotos(json), photos);
    }

    void answers_data()
    {
        QTest::addColumn<QJsonObject>("answer");
        QTest::addColumn<bool>("waiting");
        QTest::addColumn<QString>("purpose");
        QTest::addColumn<QString>("why");
        QTest::newRow("a photo taken") << QJsonObject{{u"taken"_s, true}} << true << QString() << QString();
        QTest::newRow("none taken") << QJsonObject{{u"cancelled"_s, true}, {u"purpose"_s, u"AI/Żółw.md"_s}} << false
                                    << u"AI/Żółw.md"_s << QString();
        QTest::newRow("the camera could not start")
                << QJsonObject{{u"error"_s, u"no camera app"_s}, {u"purpose"_s, u"AI/Łąka.md"_s}} << false
                << u"AI/Łąka.md"_s << u"no camera app"_s;
        QTest::newRow("taken, not a bool") << QJsonObject{{u"taken"_s, u"tak"_s}} << false << QString() << QString();
        QTest::newRow("nothing") << QJsonObject() << false << QString() << QString();
    }
    void answers()
    {
        QFETCH(QJsonObject, answer);
        QFETCH(bool, waiting);
        QFETCH(QString, purpose);
        QFETCH(QString, why);
        Camera camera;
        QSignalSpy waits(&camera, &PhotoCamera::photosWaiting);
        QSignalSpy fails(&camera, &PhotoCamera::failed);
        camera.answered(answer);
        QCOMPARE(waits.size(), waiting ? 1 : 0);
        QCOMPARE(fails.size(), waiting ? 0 : 1);
        if (!waiting) {
            QCOMPARE(fails.first().at(0).toString(), purpose);
            QCOMPARE(fails.first().at(1).toString(), why);
        }
    }

    void aCameraOfTheAppsOwn()
    {
        QCOMPARE(brulion::platform::photoCamera(), nullptr); // (the desktop has no camera app)
        QPointer<PhotoCamera> first = new Camera;
        brulion::platform::setPhotoCamera(first);
        QCOMPARE(brulion::platform::photoCamera(), first.data());
        QCOMPARE(first->parent(), QCoreApplication::instance()); // (deleted with the application)
        brulion::platform::setPhotoCamera(first);                // (the same again: kept)
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(first);
        // Another replaces it: the first goes.
        QPointer<PhotoCamera> second = new Camera;
        brulion::platform::setPhotoCamera(second);
        QCOMPARE(brulion::platform::photoCamera(), second.data());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!first);
        // None: the system's again.
        brulion::platform::setPhotoCamera(nullptr);
        QCOMPARE(brulion::platform::photoCamera(), nullptr);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!second);
    }
};

QTEST_GUILESS_MAIN(TestCapture)
#include "tst_capture.moc"
