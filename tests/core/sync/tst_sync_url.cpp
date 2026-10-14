// URL, header and ETag helpers; error codes; conflict copy names; the job pool.
#include "sync/conflicts.h"
#include "sync/errors.h"
#include "sync/pool_p.h"
#include "sync/url.h"
#include "util/paths.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <memory>

using namespace brulion;
using namespace brulion::sync;
using namespace Qt::StringLiterals;

class TestSyncUrl : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // --- nextcloudDavUrl ---------------------------------------------------------------------
    void nextcloudDavUrl_data()
    {
        QTest::addColumn<QString>("server");
        QTest::addColumn<QString>("user");
        QTest::addColumn<QString>("folder");
        QTest::addColumn<QString>("expected");
        const QString nc = u"https://cloud.example.com/remote.php/dav/files/"_s;
        QTest::newRow("host, user, folder") << u"https://cloud.example.com"_s << u"jan"_s << u"Notatki"_s
                                            << nc + u"jan/Notatki"_s;
        QTest::newRow("default https, trailing slashes")
            << u"cloud.example.com/"_s << u"jan"_s << u"Notatki/"_s << nc + u"jan/Notatki"_s;
        QTest::newRow("spaces and slashes") << u"  cloud.example.com///  "_s << u"jan"_s << u"/Notatki"_s
                                            << nc + u"jan/Notatki"_s;
        QTest::newRow("http, port, sub-path")
            << u"http://192.168.1.10:8080/nextcloud/"_s << u"jan"_s << u"N"_s
            << u"http://192.168.1.10:8080/nextcloud/remote.php/dav/files/jan/N"_s;
        QTest::newRow("nested Polish folders")
            << u"cloud.example.com"_s << u"jan"_s << u"Dokumenty/Moje notatki/Zażółć"_s
            << nc + u"jan/Dokumenty/Moje%20notatki/Za%C5%BC%C3%B3%C5%82%C4%87"_s;
        QTest::newRow("e-mail login") << u"cloud.example.com"_s << u"jan.kowalski@poczta.pl"_s << u"N"_s
                                      << nc + u"jan.kowalski%40poczta.pl/N"_s;
        QTest::newRow("Polish user name") << u"cloud.example.com"_s << u"Łukasz Ż"_s << u"N"_s
                                          << nc + u"%C5%81ukasz%20%C5%BB/N"_s;
        QTest::newRow("no folder") << u"cloud.example.com"_s << u"jan"_s << QString() << nc + u"jan"_s;
        QTest::newRow("root folder") << u"cloud.example.com"_s << u"jan"_s << u"/"_s << nc + u"jan"_s;
        QTest::newRow("pasted DAV URL") << u"https://cloud.example.com/remote.php/dav/files/jan/"_s << u"jan"_s
                                        << u"N"_s << nc + u"jan/N"_s;
        QTest::newRow("pasted web UI URL")
            << u"https://cloud.example.com/nc/index.php/apps/files/?dir=/"_s << u"jan"_s << u"N"_s
            << u"https://cloud.example.com/nc/remote.php/dav/files/jan/N"_s;
        QTest::newRow("apps page") << u"https://cloud.example.com/apps/files/"_s << u"jan"_s << u"N"_s
                                   << nc + u"jan/N"_s;
        QTest::newRow("login page") << u"https://cloud.example.com/login"_s << u"jan"_s << u"N"_s
                                    << nc + u"jan/N"_s;
        QTest::newRow("dot segments") << u"h.pl"_s << u"jan"_s << u"./a/../b//c"_s
                                      << u"https://h.pl/remote.php/dav/files/jan/b/c"_s;
    }
    void nextcloudDavUrl()
    {
        QFETCH(QString, server);
        QFETCH(QString, user);
        QFETCH(QString, folder);
        QFETCH(QString, expected);
        QCOMPARE(sync::nextcloudDavUrl(server, user, folder), expected);
    }

    // --- path encoding -----------------------------------------------------------------------
    void encodePath()
    {
        QCOMPARE(sync::encodePath(u"a b/ż#?.md"_s), u"a%20b/%C5%BC%23%3F.md"_s);
        QCOMPARE(sync::encodePath(u"/a//b/"_s), u"a/b"_s);
        QCOMPARE(sync::encodePath(QString()), QString());
        QCOMPARE(sync::encodePath(u"100%.md"_s), u"100%25.md"_s);
    }

    void joinUrl()
    {
        QCOMPARE(sync::joinUrl(u"https://h/dav/"_s, u"Zeszyt/Notatka.md"_s), u"https://h/dav/Zeszyt/Notatka.md"_s);
        QCOMPARE(sync::joinUrl(u"https://h/dav"_s, QString(), true), u"https://h/dav/"_s);
        QCOMPARE(sync::joinUrl(u"https://h/dav"_s, u"folder"_s, true), u"https://h/dav/folder/"_s);
        QCOMPARE(sync::joinUrl(u"https://h/dav//"_s, u"ą ę.md"_s), u"https://h/dav/%C4%85%20%C4%99.md"_s);
    }

    void decodeSegment_data()
    {
        QTest::addColumn<QString>("segment");
        QTest::addColumn<QString>("expected");
        QTest::newRow("utf-8") << u"%C5%BC"_s << u"ż"_s;
        QTest::newRow("plain") << u"abc"_s << u"abc"_s;
        QTest::newRow("trailing %") << u"100%"_s << u"100%"_s;
        QTest::newRow("bad hex") << u"a%zzb"_s << u"a%zzb"_s;
        QTest::newRow("not utf-8") << u"%FF%FE"_s << u"%FF%FE"_s;
        QTest::newRow("space") << u"Moje%20notatki"_s << u"Moje notatki"_s;
        QTest::newRow("encoded slash") << u"a%2Fb"_s << u"a/b"_s;
    }
    void decodeSegment()
    {
        QFETCH(QString, segment);
        QFETCH(QString, expected);
        QCOMPARE(sync::decodeSegment(segment), expected);
    }

    void trimTrailingSlashes()
    {
        QCOMPARE(sync::trimTrailingSlashes(u"https://h/a//"_s), u"https://h/a"_s);
        QCOMPARE(sync::trimTrailingSlashes(u"///"_s), QString());
        QCOMPARE(sync::trimTrailingSlashes(u"a"_s), u"a"_s);
    }

    // --- hrefs ---------------------------------------------------------------------------------
    void urlPathname()
    {
        QCOMPARE(sync::urlPathname(u"https://h/a%20b/%C5%BC.md?x=1"_s), u"/a b/ż.md"_s);
        QCOMPARE(sync::urlPathname(u"/a%20b/"_s), u"/a b/"_s);
        QCOMPARE(sync::urlPathname(u"https://h/a/../b/x.md#frag"_s), u"/b/x.md"_s);
        QCOMPARE(sync::urlPathname(u"https://h"_s), QString());
    }

    void hrefPathname()
    {
        QCOMPARE(sync::hrefPathname(u"notatka.md"_s, u"https://h/dav/folder/"_s), u"/dav/folder/notatka.md"_s);
        QCOMPARE(sync::hrefPathname(u"https://other/dav/x.md"_s, u"https://h/dav/"_s), u"/dav/x.md"_s);
        QCOMPARE(sync::hrefPathname(u"/dav/%C5%BC/"_s, u"https://h/dav/"_s), u"/dav/ż/"_s);
        QCOMPARE(sync::hrefPathname(u"  sub/%C4%87.md  "_s, u"https://h/dav/"_s), u"/dav/sub/ć.md"_s);
    }

    void relativeTo_data()
    {
        QTest::addColumn<QString>("pathname");
        QTest::addColumn<QString>("base");
        QTest::addColumn<bool>("inside");
        QTest::addColumn<QString>("expected");
        QTest::newRow("file") << u"/dav/files/jan/N/a.md"_s << u"/dav/files/jan/N"_s << true << u"a.md"_s;
        QTest::newRow("the base itself") << u"/dav/files/jan/N/"_s << u"/dav/files/jan/N"_s << true << QString();
        QTest::newRow("double slashes") << u"/dav/files/jan/N//sub/"_s << u"/dav/files/jan/N/"_s << true << u"sub"_s;
        QTest::newRow("case of the prefix") << u"/DAV/files/jan/N/a.md"_s << u"/dav/files/jan/N"_s << true << u"a.md"_s;
        QTest::newRow("other folder") << u"/dav/files/jan/Other/a.md"_s << u"/dav/files/jan/N"_s << false << QString();
        QTest::newRow("dot dot") << u"/dav/files/jan/N/../x"_s << u"/dav/files/jan/N"_s << false << QString();
        QTest::newRow("shorter") << u"/dav"_s << u"/dav/files"_s << false << QString();
        QTest::newRow("keeps server spelling") << u"/dav/N/Łódź/Ćma.md"_s << u"/dav/N"_s << true << u"Łódź/Ćma.md"_s;
    }
    void relativeTo()
    {
        QFETCH(QString, pathname);
        QFETCH(QString, base);
        QFETCH(bool, inside);
        QFETCH(QString, expected);
        const std::optional<QString> rel = sync::relativeTo(pathname, base);
        QCOMPARE(rel.has_value(), inside);
        if (inside)
            QCOMPARE(*rel, expected);
    }

    void relativeToComparesNfcAndNfd()
    {
        const QString nfd = u"/dav/Zażół/a.md"_s.normalized(QString::NormalizationForm_D);
        QVERIFY(nfd != nfd.normalized(QString::NormalizationForm_C));
        QCOMPARE(sync::relativeTo(nfd, u"/dav/Zażół"_s), std::optional<QString>(u"a.md"_s));
        QCOMPARE(sync::relativeTo(u"/dav/Zażół/b.md"_s, u"/dav/Zażół"_s.normalized(QString::NormalizationForm_D)),
                 std::optional<QString>(u"b.md"_s));
    }

    // --- etags, auth ---------------------------------------------------------------------------
    void normalizeEtag_data()
    {
        QTest::addColumn<QString>("etag");
        QTest::addColumn<QString>("expected");
        QTest::newRow("quoted") << u"\"abc\""_s << u"abc"_s;
        QTest::newRow("weak") << u"W/\"abc\""_s << u"abc"_s;
        QTest::newRow("weak lower-case") << u"w/\"abc\""_s << u"abc"_s;
        QTest::newRow("entities") << u" &quot;abc&quot; "_s << u"abc"_s;
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("plain") << u"plain"_s << u"plain"_s;
        QTest::newRow("lone quote") << u"\""_s << u"\""_s;
    }
    void normalizeEtag()
    {
        QFETCH(QString, etag);
        QFETCH(QString, expected);
        QCOMPARE(sync::normalizeEtag(etag), expected);
    }

    void quoteEtag()
    {
        QCOMPARE(sync::quoteEtag(u"abc"_s), u"\"abc\""_s);
        QCOMPARE(sync::quoteEtag(u"\"abc\""_s), u"\"abc\""_s);
    }

    void basicAuthIsUtf8()
    {
        QCOMPARE(sync::basicAuth(u"jan"_s, u"hasło"_s), QByteArray("Basic " + u"jan:hasło"_s.toUtf8().toBase64()));
        QCOMPARE(sync::basicAuth(u"jan"_s, u"hasło"_s), QByteArray("Basic amFuOmhhc8WCbw=="));
        QCOMPARE(sync::basicAuth(QString(), QString()), QByteArray("Basic Og=="));
    }

    // --- errors --------------------------------------------------------------------------------
    void codeForStatus_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QString>("name");
        QTest::newRow("401") << 401 << u"auth"_s;
        QTest::newRow("403") << 403 << u"forbidden"_s;
        QTest::newRow("404") << 404 << u"not-found"_s;
        QTest::newRow("405") << 405 << u"not-allowed"_s;
        QTest::newRow("409") << 409 << u"conflict"_s;
        QTest::newRow("410") << 410 << u"not-found"_s;
        QTest::newRow("412") << 412 << u"precondition"_s;
        QTest::newRow("413") << 413 << u"too-large"_s;
        QTest::newRow("423") << 423 << u"locked"_s;
        QTest::newRow("507") << 507 << u"insufficient-storage"_s;
        QTest::newRow("500") << 500 << u"server"_s;
        QTest::newRow("502") << 502 << u"server"_s;
        QTest::newRow("400") << 400 << u"http"_s;
        QTest::newRow("418") << 418 << u"http"_s;
    }
    void codeForStatus()
    {
        QFETCH(int, status);
        QFETCH(QString, name);
        QCOMPARE(errorCodeName(sync::codeForStatus(status)), name);
    }

    void errorHelpers()
    {
        const WebDavError precondition = WebDavError::fromStatus(412, u"PUT failed"_s);
        QVERIFY(precondition.isError());
        QVERIFY(precondition.is(WebDavErrorCode::Precondition));
        QVERIFY(!precondition.isFatal());
        QVERIFY(WebDavError::make(WebDavErrorCode::Auth, u"x"_s).isFatal());
        QVERIFY(WebDavError::make(WebDavErrorCode::Network, u"x"_s).isFatal());
        QVERIFY(WebDavError::make(WebDavErrorCode::Timeout, u"x"_s).isFatal());
        QVERIFY(!WebDavError::make(WebDavErrorCode::Server, u"x"_s).isFatal());
        QVERIFY(!WebDavError().isError());
        QCOMPARE(errorCodeName(WebDavErrorCode::None), QString());
        QCOMPARE(errorCodeName(WebDavErrorCode::Timeout), u"timeout"_s);
        QCOMPARE(errorCodeName(WebDavErrorCode::Parse), u"parse"_s);
        QCOMPARE(errorCodeName(SyncErrorCode::RemoteEmpty), u"remote-empty"_s);
        QCOMPARE(errorCodeName(SyncErrorCode::LocalEmpty), u"local-empty"_s);
        QVERIFY((SyncError{SyncErrorCode::RemoteBusy, u"zajęty"_s}.is(SyncErrorCode::RemoteBusy)));
        QVERIFY(!SyncError().isError());
    }

    // --- conflict copies -----------------------------------------------------------------------
    void conflictStamp()
    {
        QCOMPARE(sync::conflictStamp(QDateTime(QDate(2026, 10, 14), QTime(9, 5, 7))), u"20261014-090507"_s);
    }

    void conflictCopyPath_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("device");
        QTest::addColumn<QString>("expected");
        QTest::newRow("note") << u"Notatka.md"_s << u"laptop"_s << u"Notatka.conflict-laptop-20261014-090507.md"_s;
        QTest::newRow("journal") << u"journals/2026-10-14.md"_s << u"Telefon"_s
                                 << u"journals/2026-10-14.conflict-Telefon-20261014-090507.md"_s;
        QTest::newRow("binary") << u"assets/zdjęcie.png"_s << u"pc"_s << u"assets/zdjęcie.conflict-pc-20261014-090507.png"_s;
        QTest::newRow("no extension") << u"README"_s << u"pc"_s << u"README.conflict-pc-20261014-090507"_s;
        QTest::newRow("hidden file") << u"a.b/.hidden"_s << u"pc"_s << u"a.b/.hidden.conflict-pc-20261014-090507"_s;
        QTest::newRow("device with space") << u"Plan.md"_s << u"Mój laptop"_s << u"Plan.conflict-Mój-laptop-20261014-090507.md"_s;
    }
    void conflictCopyPath()
    {
        QFETCH(QString, path);
        QFETCH(QString, device);
        QFETCH(QString, expected);
        const QDateTime t(QDate(2026, 10, 14), QTime(9, 5, 7));
        QCOMPARE(sync::conflictCopyPath(path, device, t), expected);
    }

    void conflictCopiesAreRecognisedByPaths()
    {
        const QDateTime t(QDate(2026, 10, 14), QTime(9, 5, 7));
        QVERIFY(paths::isConflictCopy(sync::conflictCopyPath(u"Strona.md"_s, u"Mój laptop"_s, t)));
    }

    void sanitizeDeviceName_data()
    {
        QTest::addColumn<QString>("device");
        QTest::addColumn<QString>("expected");
        QTest::newRow("space") << u"Mój laptop"_s << u"Mój-laptop"_s;
        QTest::newRow("slashes and padding") << u"  Pixel 8 / Łukasz  "_s << u"Pixel-8-Łukasz"_s;
        QTest::newRow("punctuation") << u"a.b:c"_s << u"a-b-c"_s;
        QTest::newRow("nothing usable") << u"///"_s << u"urzadzenie"_s;
        QTest::newRow("empty") << QString() << u"urzadzenie"_s;
        QTest::newRow("underscore kept") << u"moj_pc-2"_s << u"moj_pc-2"_s;
    }
    void sanitizeDeviceName()
    {
        QFETCH(QString, device);
        QFETCH(QString, expected);
        QCOMPARE(sync::sanitizeDeviceName(device), expected);
    }

    void sanitizeDeviceNameIsBounded()
    {
        QCOMPARE(sync::sanitizeDeviceName(QString(100, u'x')).size(), 40);
        // Cut at 40 characters, trailing dashes removed.
        QCOMPARE(sync::sanitizeDeviceName(QString(39, u'x') + u" y"_s), QString(39, u'x'));
        // Letters outside the BMP are surrogate pairs in UTF-16: the cut never splits one (a lone
        // surrogate is not a valid file name character).
        const QString cjk = u"𠀀"_s; // U+20000, a letter (Lo)
        QCOMPARE(cjk.size(), 2);
        const QString cut = sync::sanitizeDeviceName(QString(39, u'x') + cjk + cjk);
        QCOMPARE(cut, QString(39, u'x'));
        QVERIFY(!cut.back().isSurrogate());
        QCOMPARE(sync::sanitizeDeviceName(QString(38, u'x') + cjk + u"y"_s), QString(38, u'x') + cjk);
        QCOMPARE(sync::sanitizeDeviceName(cjk + u" telefon"_s), cjk + u"-telefon"_s);
    }

    void parseBrulionConflict()
    {
        const auto parsed = sync::parseBrulionConflict(u"dir/Zażółć.conflict-Mój-laptop-20261014-090507.md"_s);
        QVERIFY(parsed);
        QCOMPARE(parsed->original, u"dir/Zażółć.md"_s);
        QCOMPARE(parsed->device, u"Mój-laptop"_s);
        QCOMPARE(parsed->time, u"2026-10-14T09:05:07"_s);
        QCOMPARE(sync::parseBrulionConflict(u"a.conflict-pc-20261014-090507 2.md"_s)->original, u"a.md"_s);
        QCOMPARE(sync::parseBrulionConflict(u"img.conflict-pc-20261014-090507.png"_s)->original, u"img.png"_s);
        QCOMPARE(sync::parseBrulionConflict(u"README.conflict-pc-20261014-090507"_s)->original, u"README"_s);
    }

    void parseBrulionConflictRoundTrip()
    {
        const QDateTime t(QDate(2026, 10, 14), QTime(9, 5, 7));
        const QString copy = sync::conflictCopyPath(u"Projekty/Plan.md"_s, u"Telefon Ani"_s, t);
        const auto parsed = sync::parseBrulionConflict(copy);
        QVERIFY(parsed);
        QCOMPARE(*parsed, (BrulionConflict{u"Projekty/Plan.md"_s, u"2026-10-14T09:05:07"_s, u"Telefon-Ani"_s}));
    }

    void parseBrulionConflictRejectsOtherNames_data()
    {
        QTest::addColumn<QString>("path");
        QTest::newRow("plain note") << u"Notatka.md"_s;
        QTest::newRow("no device") << u"Notatka.conflict-.md"_s;
        QTest::newRow("no stem") << u".conflict-pc-20261014-090507.md"_s;
        QTest::newRow("short date") << u"Notatka.conflict-pc-2026101-090507.md"_s;
        QTest::newRow("syncthing") << u"a.sync-conflict-20261014-101010-ABCDEFG.md"_s;
    }
    void parseBrulionConflictRejectsOtherNames()
    {
        QFETCH(QString, path);
        QVERIFY(!sync::parseBrulionConflict(path));
    }

    void findConflicts()
    {
        const auto file = [](const QString &path) { return FileStat{path, false, 1, 0}; };
        const QList<FileStat> files = {
            file(u"b.md"_s),
            file(u"b.conflict-Telefon-20261015-080000.md"_s),
            file(u"a.sync-conflict-20261014-101010-ABCDEFG.md"_s),
            file(u"a.md"_s),
            file(u"a.conflict-pc-20261013-120000.md"_s),
            file(u"zdjęcia/kot.sync-conflict-20261014-101010-XYZ1234.jpg"_s),
            FileStat{u"x.sync-conflict-20261014-101010-ABCDEFG"_s, true, 0, 0},
        };
        using S = ConflictCopy::Source;
        const QList<ConflictCopy> expected = {
            {u"a.md"_s, u"a.conflict-pc-20261013-120000.md"_s, u"2026-10-13T12:00:00"_s, u"pc"_s, S::Brulion},
            {u"a.md"_s, u"a.sync-conflict-20261014-101010-ABCDEFG.md"_s, u"2026-10-14T10:10:10"_s, u"ABCDEFG"_s, S::Syncthing},
            {u"b.md"_s, u"b.conflict-Telefon-20261015-080000.md"_s, u"2026-10-15T08:00:00"_s, u"Telefon"_s, S::Brulion},
            {u"zdjęcia/kot.jpg"_s, u"zdjęcia/kot.sync-conflict-20261014-101010-XYZ1234.jpg"_s, u"2026-10-14T10:10:10"_s,
             u"XYZ1234"_s, S::Syncthing},
        };
        QCOMPARE(sync::findConflicts(files), expected);
        QVERIFY(sync::findConflicts({file(u"a.md"_s), file(u"b/c.md"_s)}).isEmpty());
        QVERIFY(sync::findConflicts({}).isEmpty());
    }

    // --- job pool ------------------------------------------------------------------------------
    void poolBoundsConcurrency()
    {
        QObject context;
        auto pool = std::make_shared<detail::JobPool>(3, &context);
        int active = 0;
        int peak = 0;
        QList<int> done;
        for (int n = 1; n <= 7; ++n) {
            pool->add([&, n](detail::JobPool::Done finish) {
                ++active;
                peak = qMax(peak, active);
                QTimer::singleShot(1, &context, [&, n, finish] {
                    --active;
                    done.append(n);
                    finish();
                });
            });
        }
        bool idle = false;
        pool->start([&] { idle = true; });
        QTRY_VERIFY(idle);
        std::sort(done.begin(), done.end());
        QCOMPARE(done, (QList<int>{1, 2, 3, 4, 5, 6, 7}));
        QCOMPARE(peak, 3);
    }

    void poolAcceptsJobsAddedByJobs()
    {
        QObject context;
        auto pool = std::make_shared<detail::JobPool>(2, &context);
        QStringList seen;
        std::function<void(const QString &)> add = [&](const QString &s) {
            pool->add([&, s](detail::JobPool::Done finish) {
                seen.append(s);
                if (s.size() < 3) {
                    add(s + u'x');
                    add(s + u'y');
                }
                finish(); // synchronously
            });
        };
        add(u"a"_s);
        bool idle = false;
        pool->start([&] { idle = true; });
        QTRY_VERIFY(idle);
        seen.sort();
        QCOMPARE(seen, (QStringList{u"a"_s, u"ax"_s, u"axx"_s, u"axy"_s, u"ay"_s, u"ayx"_s, u"ayy"_s}));
    }

    void poolIdleForEmptyQueue()
    {
        QObject context;
        auto pool = std::make_shared<detail::JobPool>(4, &context);
        bool idle = false;
        pool->start([&] { idle = true; });
        QVERIFY(idle);
    }

    void poolStopsTakingJobs()
    {
        QObject context;
        auto pool = std::make_shared<detail::JobPool>(1, &context);
        QList<int> seen;
        for (int n = 1; n <= 4; ++n) {
            pool->add([&, n](detail::JobPool::Done finish) {
                seen.append(n);
                if (n == 2)
                    pool->stop();
                QTimer::singleShot(1, &context, finish);
            });
        }
        bool idle = false;
        pool->start([&] { idle = true; });
        QTRY_VERIFY(idle);
        QCOMPARE(seen, (QList<int>{1, 2}));
        QVERIFY(pool->isStopped());
    }

    void poolIgnoresDoubleDone()
    {
        QObject context;
        auto pool = std::make_shared<detail::JobPool>(1, &context);
        int runs = 0;
        for (int n = 0; n < 3; ++n) {
            pool->add([&](detail::JobPool::Done finish) {
                ++runs;
                finish();
                finish();
            });
        }
        int idleCalls = 0;
        pool->start([&] { ++idleCalls; });
        QTRY_COMPARE(runs, 3);
        QTest::qWait(10);
        QCOMPARE(idleCalls, 1);
        QCOMPARE(pool->running(), 0);
    }

    void poolHandlesManySynchronousJobsWithoutRecursion()
    {
        QObject context;
        auto pool = std::make_shared<detail::JobPool>(4, &context);
        int runs = 0;
        for (int n = 0; n < 20000; ++n)
            pool->add([&](detail::JobPool::Done finish) {
                ++runs;
                finish();
            });
        bool idle = false;
        pool->start([&] { idle = true; });
        QTRY_VERIFY_WITH_TIMEOUT(idle, 20000);
        QCOMPARE(runs, 20000);
    }
};

QTEST_GUILESS_MAIN(TestSyncUrl)
#include "tst_sync_url.moc"
