// WebDAV multistatus parsing (namespace prefixes, propstat variants, garbage) and HTTP dates.
#include "sync/davxml.h"

#include <QDateTime>
#include <QTest>
#include <QTimeZone>

#include <limits>

using namespace brulion;
using namespace brulion::sync;
using namespace Qt::StringLiterals;

namespace {

qint64 utc(int y, int m, int d, int h = 0, int min = 0, int s = 0)
{
    return QDateTime(QDate(y, m, d), QTime(h, min, s), QTimeZone::UTC).toMSecsSinceEpoch();
}

DavResource resource(const QString &href, bool collection, const QString &etag, qint64 mtime, qint64 size)
{
    return DavResource{href, collection, etag, mtime, size};
}

const QByteArray kNextcloud = "<?xml version=\"1.0\"?>\n"
        "<d:multistatus xmlns:d=\"DAV:\" xmlns:s=\"http://sabredav.org/ns\" xmlns:oc=\"http://owncloud.org/ns\" xmlns:nc=\"http://nextcloud.org/ns\">\n"
        " <d:response>\n"
        "  <d:href>/remote.php/dav/files/jan/Notatki/</d:href>\n"
        "  <d:propstat>\n"
        "   <d:prop>\n"
        "    <d:getlastmodified>Tue, 14 Oct 2026 08:00:00 GMT</d:getlastmodified>\n"
        "    <d:resourcetype><d:collection/></d:resourcetype>\n"
        "    <d:getetag>&quot;5f1a&quot;</d:getetag>\n"
        "   </d:prop>\n"
        "   <d:status>HTTP/1.1 200 OK</d:status>\n"
        "  </d:propstat>\n"
        "  <d:propstat>\n"
        "   <d:prop><d:getcontentlength/></d:prop>\n"
        "   <d:status>HTTP/1.1 404 Not Found</d:status>\n"
        "  </d:propstat>\n"
        " </d:response>\n"
        " <d:response>\n"
        "  <d:href>/remote.php/dav/files/jan/Notatki/Za%C5%BC%C3%B3%C5%82%C4%87%20g%C4%99%C5%9Bl%C4%85.md</d:href>\n"
        "  <d:propstat>\n"
        "   <d:prop>\n"
        "    <d:getlastmodified>Tue, 14 Oct 2026 09:30:00 GMT</d:getlastmodified>\n"
        "    <d:getcontentlength>1234</d:getcontentlength>\n"
        "    <d:resourcetype/>\n"
        "    <d:getetag>\"0012ab\"</d:getetag>\n"
        "    <oc:size>1234</oc:size>\n"
        "   </d:prop>\n"
        "   <d:status>HTTP/1.1 200 OK</d:status>\n"
        "  </d:propstat>\n"
        " </d:response>\n"
        "</d:multistatus>";

} // namespace

class TestSyncXml : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void nextcloudResponse()
    {
        const MultistatusResult r = parseMultistatus(kNextcloud);
        QVERIFY2(r.ok(), qPrintable(r.error));
        const QList<DavResource> expected = {
            resource(u"/remote.php/dav/files/jan/Notatki/"_s, true, u"5f1a"_s, utc(2026, 10, 14, 8), 0),
            resource(u"/remote.php/dav/files/jan/Notatki/Za%C5%BC%C3%B3%C5%82%C4%87%20g%C4%99%C5%9Bl%C4%85.md"_s, false,
                     u"0012ab"_s, utc(2026, 10, 14, 9, 30), 1234),
        };
        QCOMPARE(r.resources, expected);
    }

    void apachePrefixAndMixedPrefixes()
    {
        const QByteArray xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<D:multistatus xmlns:D=\"DAV:\" xmlns:lp1=\"DAV:\">\n"
        "<D:response><D:href>http://h/dav/a.md</D:href><D:propstat><D:prop>\n"
        "<lp1:getlastmodified>Wed, 15 Oct 2026 10:00:00 GMT</lp1:getlastmodified><lp1:getcontentlength>5</lp1:getcontentlength>\n"
        "<lp1:resourcetype/><lp1:getetag>W/\"77-abc\"</lp1:getetag></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response>\n"
        "</D:multistatus>";
        const MultistatusResult r = parseMultistatus(xml);
        QVERIFY(r.ok());
        QCOMPARE(r.resources, (QList<DavResource>{resource(u"http://h/dav/a.md"_s, false, u"77-abc"_s, utc(2026, 10, 15, 10), 5)}));
    }

    void defaultNamespace()
    {
        const QByteArray xml = "<multistatus xmlns=\"DAV:\"><response><href>/dav/x/</href><propstat><prop><resourcetype><collection></collection></resourcetype></prop><status>HTTP/1.1 200 OK</status></propstat></response></multistatus>";
        const MultistatusResult r = parseMultistatus(xml);
        QVERIFY(r.ok());
        QCOMPARE(r.resources, (QList<DavResource>{resource(u"/dav/x/"_s, true, QString(), 0, 0)}));
    }

    void undeclaredPrefixIsTolerated()
    {
        const QByteArray xml = "<d:multistatus><d:response><d:href>/a.md</d:href><d:propstat><d:prop><d:getetag>\"x\"</d:getetag></d:prop></d:propstat></d:response></d:multistatus>";
        const MultistatusResult r = parseMultistatus(xml);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.resources.size(), 1);
        QCOMPARE(r.resources.first().etag, u"x"_s);
    }

    void numericLookingEtagsStayStrings()
    {
        const QByteArray xml = "<d:multistatus xmlns:d=\"DAV:\"><d:response><d:href>/a</d:href><d:propstat><d:prop><d:getetag>00123</d:getetag><d:resourcetype/></d:prop></d:propstat></d:response></d:multistatus>";
        QCOMPARE(parseMultistatus(xml).resources.value(0).etag, u"00123"_s);
    }

    void errorStatusesAndFailedPropstats()
    {
        const QByteArray xml = "<d:multistatus xmlns:d=\"DAV:\">\n"
        "<d:response><d:href>/gone</d:href><d:status>HTTP/1.1 404 Not Found</d:status></d:response>\n"
        "<d:response><d:href>/ok</d:href><d:propstat><d:prop><d:getetag>\"x\"</d:getetag></d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>\n"
        "<d:propstat><d:prop><d:getcontentlength>99</d:getcontentlength></d:prop><d:status>HTTP/1.1 403 Forbidden</d:status></d:propstat></d:response>\n"
        "</d:multistatus>";
        QCOMPARE(parseMultistatus(xml).resources, (QList<DavResource>{resource(u"/ok"_s, false, u"x"_s, 0, 0)}));
    }

    void garbageValuesAndMissingHrefs()
    {
        const QByteArray xml = "<d:multistatus xmlns:d=\"DAV:\">\n"
        "<d:response><d:propstat><d:prop><d:getetag>\"x\"</d:getetag></d:prop></d:propstat></d:response>\n"
        "<d:response><d:href>/a</d:href><d:propstat><d:prop><d:getlastmodified>wczoraj</d:getlastmodified><d:getcontentlength>-5</d:getcontentlength></d:prop></d:propstat></d:response>\n"
        "<d:response><d:href>/b</d:href><d:propstat><d:prop><d:getcontentlength>12.7</d:getcontentlength></d:prop></d:propstat></d:response>\n"
        "<d:response><d:href>/c</d:href><d:propstat><d:prop><d:getcontentlength>dużo</d:getcontentlength></d:prop></d:propstat></d:response>\n"
        "</d:multistatus>";
        QCOMPARE(parseMultistatus(xml).resources,
                 (QList<DavResource>{resource(u"/a"_s, false, QString(), 0, 0), resource(u"/b"_s, false, QString(), 0, 12),
                                     resource(u"/c"_s, false, QString(), 0, 0)}));
    }

    void outOfRangeSizesCountAsInvalid_data()
    {
        QTest::addColumn<QString>("length");
        QTest::addColumn<qint64>("size");
        QTest::newRow("5 GB") << u"5000000000"_s << qint64(5'000'000'000);
        QTest::newRow("exact beyond 2^53") << u"9007199254740993"_s << qint64(9'007'199'254'740'993);
        QTest::newRow("largest qint64") << u"9223372036854775807"_s << std::numeric_limits<qint64>::max();
        QTest::newRow("2^63") << u"9223372036854775808"_s << qint64(0);
        QTest::newRow("20 digits") << u"99999999999999999999"_s << qint64(0);
        QTest::newRow("exponent") << u"1e30"_s << qint64(0);
        QTest::newRow("infinity") << u"inf"_s << qint64(0);
        QTest::newRow("not a number") << u"nan"_s << qint64(0);
        QTest::newRow("padded") << u"  42 "_s << qint64(42);
    }
    void outOfRangeSizesCountAsInvalid()
    {
        QFETCH(QString, length);
        QFETCH(qint64, size);
        const QByteArray xml = "<d:multistatus xmlns:d=\"DAV:\"><d:response><d:href>/a</d:href><d:propstat><d:prop>"
                               "<d:getcontentlength>" + length.toUtf8() + "</d:getcontentlength>"
                               "</d:prop></d:propstat></d:response></d:multistatus>";
        const MultistatusResult r = parseMultistatus(xml);
        QCOMPARE(r.resources.size(), 1);
        QCOMPARE(r.resources.first().size, size);
    }

    void emptyMultistatus()
    {
        QVERIFY(parseMultistatus("<d:multistatus xmlns:d=\"DAV:\"></d:multistatus>").ok());
        QVERIFY(parseMultistatus("<d:multistatus xmlns:d=\"DAV:\"></d:multistatus>").resources.isEmpty());
        QVERIFY(parseMultistatus("<d:multistatus xmlns:d=\"DAV:\"/>").ok());
        QVERIFY(parseMultistatus("<d:multistatus xmlns:d=\"DAV:\"/>").resources.isEmpty());
    }

    void unknownElementsAreSkipped()
    {
        const QByteArray xml = "<d:multistatus xmlns:d=\"DAV:\" xmlns:x=\"urn:x\"><x:sync-token>7</x:sync-token>\n"
        "<d:response><d:href>/n.md</d:href><x:extra><d:href>/nie-ten</d:href></x:extra>\n"
        "<d:propstat><d:prop><x:owner><d:getetag>\"zły\"</d:getetag></x:owner><d:getetag>\"dobry\"</d:getetag></d:prop></d:propstat></d:response>\n"
        "</d:multistatus>";
        const MultistatusResult r = parseMultistatus(xml);
        QVERIFY(r.ok());
        QCOMPARE(r.resources, (QList<DavResource>{resource(u"/n.md"_s, false, u"dobry"_s, 0, 0)}));
    }

    void notAMultistatus_data()
    {
        QTest::addColumn<QByteArray>("xml");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("html login page") << QByteArray("<html><body>Login</body></html>");
        QTest::newRow("not xml") << QByteArray("not xml at all");
        QTest::newRow("malformed") << QByteArray("<a><b></a>");
        QTest::newRow("truncated multistatus") << QByteArray("<d:multistatus xmlns:d=\"DAV:\"><d:response><d:href>/a");
        QTest::newRow("json") << QByteArray("{\"error\":\"Unauthorized\"}");
    }
    void notAMultistatus()
    {
        QFETCH(QByteArray, xml);
        const MultistatusResult r = parseMultistatus(xml);
        QVERIFY(!r.ok());
        QVERIFY(r.resources.isEmpty());
    }

    void parseStatusLine()
    {
        QCOMPARE(sync::parseStatusLine(u"HTTP/1.1 207 Multi-Status"), std::optional<int>(207));
        QCOMPARE(sync::parseStatusLine(u"HTTP/1.1 404 Not Found"), std::optional<int>(404));
        QVERIFY(!sync::parseStatusLine(u"garbage"));
        QVERIFY(!sync::parseStatusLine(u""));
    }

    void propfindBodyAsksForTheNeededProperties()
    {
        const QByteArray body = propfindBody();
        for (const char *p : {"getetag", "getlastmodified", "getcontentlength", "resourcetype"})
            QVERIFY(body.contains(QByteArray("<d:" + QByteArray(p) + "/>")));
        QVERIFY(body.contains("xmlns:d=\"DAV:\""));
    }

    void httpDates_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<qint64>("expected");
        QTest::newRow("IMF-fixdate") << u"Tue, 14 Oct 2026 08:00:00 GMT"_s << utc(2026, 10, 14, 8);
        QTest::newRow("single-digit day") << u"Sun, 6 Nov 1994 08:49:37 GMT"_s << utc(1994, 11, 6, 8, 49, 37);
        QTest::newRow("RFC 850") << u"Sunday, 06-Nov-94 08:49:37 GMT"_s << utc(1994, 11, 6, 8, 49, 37);
        QTest::newRow("asctime") << u"Sun Nov  6 08:49:37 1994"_s << utc(1994, 11, 6, 8, 49, 37);
        QTest::newRow("numeric offset") << u"Tue, 14 Oct 2026 10:00:00 +0200"_s << utc(2026, 10, 14, 8);
        QTest::newRow("UTC") << u"Tue, 14 Oct 2026 08:00:00 UTC"_s << utc(2026, 10, 14, 8);
        QTest::newRow("no zone") << u"14 Oct 2026 08:00:00"_s << utc(2026, 10, 14, 8);
        QTest::newRow("ISO with Z") << u"2026-10-14T08:00:00Z"_s << utc(2026, 10, 14, 8);
        QTest::newRow("ISO with offset") << u"2026-10-14T10:00:00+02:00"_s << utc(2026, 10, 14, 8);
        QTest::newRow("ISO without zone") << u"2026-10-14T08:00:00"_s << utc(2026, 10, 14, 8);
        QTest::newRow("Polish word") << u"wczoraj"_s << qint64(0);
        QTest::newRow("empty") << QString() << qint64(0);
        QTest::newRow("bad month") << u"Tue, 14 Paź 2026 08:00:00 GMT"_s << qint64(0);
        QTest::newRow("bad day") << u"Tue, 31 Feb 2026 08:00:00 GMT"_s << qint64(0);
        QTest::newRow("bad time") << u"Tue, 14 Oct 2026 25:00:00 GMT"_s << qint64(0);
    }
    void httpDates()
    {
        QFETCH(QString, text);
        QFETCH(qint64, expected);
        QCOMPARE(parseHttpDate(text), expected);
    }
};

QTEST_GUILESS_MAIN(TestSyncXml)
#include "tst_sync_xml.moc"
