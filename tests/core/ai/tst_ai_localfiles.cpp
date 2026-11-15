// The files of the models on the device (src/core/ai/localfiles.h) against a small HTTP server on
// 127.0.0.1: a download file after file, checked by SHA-256; one that broke off goes on from where
// it stopped (an HTTP range) or from the start (a server without ranges); a damaged file is thrown
// away; cancel and remove.

#include "ai/localfiles.h"

#include "fileserver.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <functional>
#include <utility>

using namespace Qt::StringLiterals;
using namespace brulion;

namespace {

QByteArray bytesOf(char seed, int size)
{
    QByteArray out(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i)
        out[i] = char(seed + i * 31 + i / 251);
    return out;
}

QByteArray sha256(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QByteArray contentOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

struct Fixture {
    FileServer server;
    QTemporaryDir dir;
    QByteArray modelBytes = bytesOf('m', 300 * 1000);
    QByteArray projectorBytes = bytesOf('p', 70 * 1000);
    ai::LocalModel model;

    Fixture()
    {
        server.files.insert("/files/model.gguf", modelBytes);
        server.files.insert("/files/projector.gguf", projectorBytes);
        model.id = u"test"_s;
        model.name = u"Test"_s;
        model.format = u"lfm3"_s;
        model.file = {u"model.gguf"_s, server.url(u"/files/model.gguf"_s), sha256(modelBytes), modelBytes.size()};
        model.projector = {u"projector.gguf"_s, server.url(u"/files/projector.gguf"_s), sha256(projectorBytes),
                           projectorBytes.size()};
    }
    QString folder() const { return dir.path() + u"/models"_s; }
};

/// Runs a download to its end; the arguments of finished(): id, ok, error.
QVariantList run(ai::LocalModelFiles &files, const ai::LocalModel &model)
{
    QSignalSpy finished(&files, &ai::LocalModelFiles::finished);
    files.download(model);
    if (finished.isEmpty() && !finished.wait(20000))
        return {};
    return finished.takeFirst();
}

} // namespace

class TestAiLocalFiles : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void downloadsAndChecks()
    {
        Fixture f;
        ai::LocalModelFiles files(f.folder());
        QCOMPARE(files.folder(), f.folder());
        QCOMPARE(files.pathOf(f.model.file), f.folder() + u"/model.gguf"_s);
        QVERIFY(!files.has(f.model));
        QCOMPARE(files.bytesHere(f.model), 0);
        QCOMPARE(files.downloading(), QString());

        QSignalSpy progress(&files, &ai::LocalModelFiles::progress);
        QSignalSpy changed(&files, &ai::LocalModelFiles::changed);
        QSignalSpy finished(&files, &ai::LocalModelFiles::finished);
        files.download(f.model);
        QCOMPARE(files.downloading(), u"test"_s);
        QCOMPARE(files.total(), 370 * 1000);
        files.download(f.model); // one at a time: ignored
        QVERIFY(finished.wait(20000));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished[0][0].toString(), u"test"_s);
        QVERIFY2(finished[0][1].toBool(), qPrintable(finished[0][2].toString()));
        QCOMPARE(finished[0][2].toString(), QString());
        QVERIFY(progress.size() >= 2);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(files.downloading(), QString());

        // Both files, whole, under their names; nothing else left.
        QVERIFY(files.has(f.model));
        QCOMPARE(files.bytesHere(f.model), 370 * 1000);
        QCOMPARE(contentOf(files.pathOf(f.model.file)), f.modelBytes);
        QCOMPARE(contentOf(files.pathOf(f.model.projector)), f.projectorBytes);
        QCOMPARE(QDir(f.folder()).entryList(QDir::Files, QDir::Name),
                 (QStringList{u"model.gguf"_s, u"projector.gguf"_s}));
        QCOMPARE(f.server.requests.size(), 2);
        QCOMPARE(f.server.requests[0].path, QByteArray("/files/model.gguf"));
        QCOMPARE(f.server.requests[0].range, QByteArray());
        QCOMPARE(f.server.requests[1].path, QByteArray("/files/projector.gguf"));

        // Again: nothing to download.
        const QVariantList again = run(files, f.model);
        QVERIFY(again.value(1).toBool());
        QCOMPARE(f.server.requests.size(), 2);

        // A model of one file (it reads text only) shares nothing with the other's files.
        ai::LocalModel textOnly = f.model;
        textOnly.id = u"text"_s;
        textOnly.projector = {};
        QVERIFY(files.has(textOnly));
        ai::LocalModel none;
        QVERIFY(!files.has(none));
    }

    void goesOnWhereItBrokeOff()
    {
        Fixture f;
        ai::LocalModelFiles files(f.folder());
        // The connection breaks after 100 000 bytes of the model.
        f.server.breakAfter = 100 * 1000;
        QVariantList ended = run(files, f.model);
        QCOMPARE(ended.value(0).toString(), u"test"_s);
        QVERIFY(!ended.value(1).toBool());
        QVERIFY(!ended.value(2).toString().isEmpty());
        QVERIFY(!files.has(f.model));
        QCOMPARE(files.bytesHere(f.model), 100 * 1000);
        QCOMPARE(contentOf(f.folder() + u"/model.gguf.part"_s), f.modelBytes.left(100 * 1000));
        QVERIFY(!QFile::exists(f.folder() + u"/model.gguf"_s));

        // The next download asks for the rest only. Qt may still send the first download's request
        // again after the connection broke (its own retry), and on a slow machine that one reaches
        // the server only now: the requests are told apart by path and range, not by order.
        f.server.breakAfter = -1;
        f.server.requests.clear();
        ended = run(files, f.model);
        QVERIFY2(ended.value(1).toBool(), qPrintable(ended.value(2).toString()));
        QStringList asked;
        QList<QByteArray> projectorRanges;
        bool resumed = false;
        for (const FileServer::Request &request : std::as_const(f.server.requests)) {
            asked.append(QString::fromLatin1(request.path + " " + request.range));
            resumed = resumed || (request.path == "/files/model.gguf" && request.range == "bytes=100000-");
            if (request.path == "/files/projector.gguf")
                projectorRanges.append(request.range);
        }
        QVERIFY2(resumed, qPrintable(asked.join(u", "_s)));
        QCOMPARE(projectorRanges, QList<QByteArray>{QByteArray()});
        QCOMPARE(contentOf(files.pathOf(f.model.file)), f.modelBytes);
        QCOMPARE(contentOf(files.pathOf(f.model.projector)), f.projectorBytes);
        QVERIFY(files.has(f.model));
    }

    void aServerWithoutRangesStartsAgain()
    {
        Fixture f;
        ai::LocalModelFiles files(f.folder());
        f.server.breakAfter = 50 * 1000;
        QVERIFY(!run(files, f.model).value(1).toBool());
        QCOMPARE(files.bytesHere(f.model), 50 * 1000);
        // Asked for the rest, it sends the whole file: taken from the start, not appended.
        f.server.breakAfter = -1;
        f.server.ranges = false;
        const QVariantList ended = run(files, f.model);
        QVERIFY2(ended.value(1).toBool(), qPrintable(ended.value(2).toString()));
        QCOMPARE(contentOf(files.pathOf(f.model.file)), f.modelBytes);
    }

    void aDamagedFileIsThrownAway()
    {
        Fixture f;
        ai::LocalModelFiles files(f.folder());
        QByteArray damaged = f.modelBytes;
        damaged[1234] = char(damaged[1234] + 1);
        f.server.files.insert("/files/model.gguf", damaged);
        QSignalSpy changed(&files, &ai::LocalModelFiles::changed);
        QVariantList ended = run(files, f.model);
        QVERIFY(!ended.value(1).toBool());
        QVERIFY2(ended.value(2).toString().contains(u"damaged"), qPrintable(ended.value(2).toString()));
        QVERIFY(!files.has(f.model));
        QCOMPARE(files.bytesHere(f.model), 0);
        QVERIFY(QDir(f.folder()).entryList(QDir::Files).isEmpty());
        QVERIFY(changed.isEmpty());
        // The right file after all.
        f.server.files.insert("/files/model.gguf", f.modelBytes);
        ended = run(files, f.model);
        QVERIFY2(ended.value(1).toBool(), qPrintable(ended.value(2).toString()));
        QVERIFY(files.has(f.model));
    }

    void aLongerAnswerIsCutOff()
    {
        // A server that sends more than the file's size (another file, an endless answer): the
        // download stops at once and keeps nothing — the storage is not filled first.
        Fixture f;
        ai::LocalModelFiles files(f.folder());
        f.server.files.insert("/files/model.gguf", f.modelBytes + bytesOf('x', 4 * 1000 * 1000));
        f.server.slow = true; // in small pieces: the rest is never sent
        QSignalSpy changed(&files, &ai::LocalModelFiles::changed);
        QVariantList ended = run(files, f.model);
        QVERIFY(!ended.value(1).toBool());
        QVERIFY2(ended.value(2).toString().contains(u"more than the file's"), qPrintable(ended.value(2).toString()));
        QVERIFY(!files.has(f.model));
        QVERIFY(QDir(f.folder()).entryList(QDir::Files).isEmpty());
        QVERIFY(changed.isEmpty());
        // The same when the download goes on after a part: the rest is longer than what is missing.
        f.server.files.insert("/files/model.gguf", f.modelBytes);
        f.server.slow = false;
        f.server.breakAfter = 100 * 1000;
        ended = run(files, f.model);
        QVERIFY(!ended.value(1).toBool());
        QCOMPARE(QFileInfo(files.pathOf(f.model.file) + u".part"_s).size(), 100 * 1000);
        f.server.breakAfter = -1;
        f.server.files.insert("/files/model.gguf", f.modelBytes + bytesOf('x', 1000 * 1000));
        ended = run(files, f.model);
        QVERIFY(!ended.value(1).toBool());
        QVERIFY2(ended.value(2).toString().contains(u"more than the file's"), qPrintable(ended.value(2).toString()));
        QVERIFY(QDir(f.folder()).entryList(QDir::Files).isEmpty());
        // The right file after all.
        f.server.files.insert("/files/model.gguf", f.modelBytes);
        ended = run(files, f.model);
        QVERIFY2(ended.value(1).toBool(), qPrintable(ended.value(2).toString()));
        QVERIFY(files.has(f.model));
        QCOMPARE(contentOf(files.pathOf(f.model.file)), f.modelBytes);
    }

    void serverErrors()
    {
        Fixture f;
        ai::LocalModelFiles files(f.folder());
        f.server.files.remove("/files/model.gguf");
        QVariantList ended = run(files, f.model);
        QVERIFY(!ended.value(1).toBool());
        QVERIFY2(ended.value(2).toString().contains(u"404"), qPrintable(ended.value(2).toString()));
        QCOMPARE(files.bytesHere(f.model), 0); // the error page is no part of the file
        // Nothing listens there at all.
        ai::LocalModel elsewhere = f.model;
        elsewhere.file.url = QUrl(u"http://127.0.0.1:9/model.gguf"_s);
        ended = run(files, elsewhere);
        QVERIFY(!ended.value(1).toBool());
        QVERIFY(!ended.value(2).toString().isEmpty());
        QCOMPARE(files.downloading(), QString());
    }

    void whileTheAppStandsStillTheNetworkWaits()
    {
        // Android pauses the app's event loop while the app is off the screen. What the server
        // still has to send then waits there: it must not pile up in the app's memory (a model
        // is a gigabyte, and a fast network brings it within the minute the system leaves a
        // background app). The server in a thread of its own, this one standing still a second.
#ifdef Q_OS_WIN
        QSKIP("Windows takes a whole 64 MB answer over loopback into the system's buffers, a fixed "
              "send buffer at the server or not: nothing is left waiting there to measure");
#else
        QThread thread;
        thread.start();
        QObject inThread;
        inThread.moveToThread(&thread);
        const auto there = [&](const std::function<void()> &call) {
            QMetaObject::invokeMethod(&inThread, call, Qt::BlockingQueuedConnection);
        };
        const QByteArray bytes(64 * 1024 * 1024, 'b');
        FileServer *server = nullptr;
        const auto stop = qScopeGuard([&] {
            there([&] { delete server; });
            thread.quit();
            thread.wait();
        });
        QUrl url;
        there([&] {
            server = new FileServer;
            server->files.insert("/big.gguf", bytes);
            url = server->url(u"/big.gguf"_s);
        });
        QTemporaryDir dir;
        ai::LocalModel model;
        model.id = u"big"_s;
        model.name = u"Big"_s;
        model.format = u"lfm3"_s;
        model.file = {u"big.gguf"_s, url, sha256(bytes), bytes.size()};
        ai::LocalModelFiles files(dir.path() + u"/models"_s);
        QSignalSpy finished(&files, &ai::LocalModelFiles::finished);
        files.download(model);
        // Events only until the server was asked (the request leaves from this thread's loop) …
        qsizetype asked = 0;
        QElapsedTimer waiting;
        waiting.start();
        while (asked == 0 && waiting.elapsed() < 20000) {
            QCoreApplication::processEvents();
            there([&] { asked = server->requests.size(); });
        }
        QCOMPARE(asked, 1);
        // … then none for a second: most of the file is still the server's to send.
        QThread::msleep(1000);
        qint64 unsent = 0;
        there([&] { unsent = server->unsent(); });
        QVERIFY2(unsent > bytes.size() / 3,
                 qPrintable(u"the server has %1 MB left of %2 MB to send"_s.arg(unsent >> 20).arg(bytes.size() >> 20)));
        // The download goes on from there and ends whole.
        QVERIFY(finished.wait(60000));
        QVERIFY2(finished[0][1].toBool(), qPrintable(finished[0][2].toString()));
        QVERIFY(contentOf(dir.path() + u"/models/big.gguf"_s) == bytes);
#endif
    }

    void cancelAndGoOn()
    {
        Fixture f;
        ai::LocalModelFiles files(f.folder());
        f.server.slow = true;
        QSignalSpy finished(&files, &ai::LocalModelFiles::finished);
        QSignalSpy progress(&files, &ai::LocalModelFiles::progress);
        files.download(f.model);
        QTRY_VERIFY_WITH_TIMEOUT(files.received() > 20 * 1000, 20000);
        QVERIFY(files.received() < 300 * 1000);
        files.cancel();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished[0][0].toString(), u"test"_s);
        QVERIFY(!finished[0][1].toBool());
        QCOMPARE(finished[0][2].toString(), QString()); // cancelled: no error to show
        QCOMPARE(files.downloading(), QString());
        const qint64 here = files.bytesHere(f.model);
        QVERIFY2(here > 20 * 1000 && here < 300 * 1000, qPrintable(QString::number(here)));
        files.cancel(); // nothing runs: nothing happens
        QCOMPARE(finished.size(), 1);

        f.server.slow = false;
        f.server.requests.clear();
        const QVariantList ended = run(files, f.model);
        QVERIFY2(ended.value(1).toBool(), qPrintable(ended.value(2).toString()));
        QCOMPARE(f.server.requests.value(0).range, "bytes=" + QByteArray::number(here) + "-");
        QCOMPARE(contentOf(files.pathOf(f.model.file)), f.modelBytes);
    }

    void aWholePartIsOnlyChecked()
    {
        // The app closed after the last byte, before the file was checked and named.
        Fixture f;
        QDir().mkpath(f.folder());
        QFile part(f.folder() + u"/model.gguf.part"_s);
        QVERIFY(part.open(QIODevice::WriteOnly));
        part.write(f.modelBytes);
        part.close();
        ai::LocalModelFiles files(f.folder());
        QCOMPARE(files.bytesHere(f.model), 300 * 1000);
        const QVariantList ended = run(files, f.model);
        QVERIFY2(ended.value(1).toBool(), qPrintable(ended.value(2).toString()));
        QCOMPARE(f.server.requests.size(), 1); // the projector only
        QCOMPARE(f.server.requests[0].path, QByteArray("/files/projector.gguf"));
        QVERIFY(files.has(f.model));
    }

    void remove()
    {
        Fixture f;
        ai::LocalModelFiles files(f.folder());
        QVERIFY(run(files, f.model).value(1).toBool());
        QSignalSpy changed(&files, &ai::LocalModelFiles::changed);
        QVERIFY(files.remove(f.model));
        QCOMPARE(changed.size(), 1);
        QVERIFY(!files.has(f.model));
        QCOMPARE(files.bytesHere(f.model), 0);
        QVERIFY(QDir(f.folder()).entryList(QDir::Files).isEmpty());
        QVERIFY(files.remove(f.model)); // nothing there: fine

        // Removing while it downloads stops the download and takes its part too.
        f.server.slow = true;
        QSignalSpy finished(&files, &ai::LocalModelFiles::finished);
        files.download(f.model);
        QTRY_VERIFY_WITH_TIMEOUT(files.received() > 8 * 1000, 20000);
        QVERIFY(files.remove(f.model));
        QCOMPARE(finished.size(), 1);
        QVERIFY(!finished[0][1].toBool());
        QCOMPARE(files.downloading(), QString());
        QVERIFY(QDir(f.folder()).entryList(QDir::Files).isEmpty());
    }

    void aFolderThatCannotBeMade()
    {
        Fixture f;
        // A file where the folder should be.
        QFile blocker(f.dir.path() + u"/models"_s);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        ai::LocalModelFiles files(f.folder());
        const QVariantList ended = run(files, f.model);
        QVERIFY(!ended.value(1).toBool());
        QVERIFY(!ended.value(2).toString().isEmpty());
        QVERIFY(f.server.requests.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestAiLocalFiles)
#include "tst_ai_localfiles.moc"
