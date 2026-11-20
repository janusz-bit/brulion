// brulion:// addresses (automation/uri.h): every spelling of the scheme and the action, the query
// read as a form ("+" a space, %XX UTF-8 — Polish text both encoded and typed as it is), the common
// parameters taken out, line breaks and control characters, the limits, malformed addresses
// refused rather than guessed at, building an address back (round trip), the key masked for logs,
// callback addresses.
#include "automation/uri.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::automation;

using Params = QList<std::pair<QString, QString>>;
Q_DECLARE_METATYPE(Params)

/// "p1=1&p2=1&…": `count` parameters of different names.
QString manyParameters(int count)
{
    QStringList pairs;
    for (int i = 1; i <= count; ++i)
        pairs.append(u"p%1=1"_s.arg(i));
    return pairs.join(u'&');
}

class TestAutomationUri : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void parses_data()
    {
        QTest::addColumn<QString>("uri");
        QTest::addColumn<QString>("action");
        QTest::addColumn<Params>("params");

        QTest::newRow("open") << u"brulion://open?page=Dom"_s << u"open"_s << Params{{u"page"_s, u"Dom"_s}};
        QTest::newRow("no slashes") << u"brulion:open?page=Dom"_s << u"open"_s << Params{{u"page"_s, u"Dom"_s}};
        QTest::newRow("scheme in capitals") << u"BRULION://Open?Page=Dom"_s << u"open"_s
                                            << Params{{u"page"_s, u"Dom"_s}};
        QTest::newRow("browser scheme") << u"web+brulion://daily?date=wczoraj"_s << u"daily"_s
                                        << Params{{u"date"_s, u"wczoraj"_s}};
        QTest::newRow("browser scheme, no slashes")
                << u"web+brulion:search?q=%C5%BC%C3%B3%C5%82w"_s << u"search"_s << Params{{u"q"_s, u"żółw"_s}};
        QTest::newRow("x-callback-url") << u"brulion://x-callback-url/append?text=a"_s << u"append"_s
                                        << Params{{u"text"_s, u"a"_s}};
        QTest::newRow("path after the action") << u"brulion://open/Projekty/Dom%20i%20ogr%C3%B3d"_s << u"open"_s
                                               << Params{{u"page"_s, u"Projekty/Dom i ogród"_s}};
        QTest::newRow("path names the new page's title")
                << u"brulion://new/Pomys%C5%82y?folder=Inbox"_s << u"new"_s
                << Params{{u"title"_s, u"Pomysły"_s}, {u"folder"_s, u"Inbox"_s}};
        QTest::newRow("Polish encoded") << u"brulion://append?page=Zakupy&text=Kupi%C4%87%20%C5%BC%C3%B3%C5%82ty%20ser"_s
                                        << u"append"_s
                                        << Params{{u"page"_s, u"Zakupy"_s}, {u"text"_s, u"Kupić żółty ser"_s}};
        QTest::newRow("Polish typed as it is") << u"brulion://append?page=Zakupy&text=Kupić żółty ser"_s
                                               << u"append"_s
                                               << Params{{u"page"_s, u"Zakupy"_s}, {u"text"_s, u"Kupić żółty ser"_s}};
        QTest::newRow("plus is a space") << u"brulion://capture?text=Zadzwo%C5%84+jutro+o+18"_s << u"capture"_s
                                         << Params{{u"text"_s, u"Zadzwoń jutro o 18"_s}};
        QTest::newRow("encoded plus") << u"brulion://capture?text=C%2B%2B+i+2%2B2"_s << u"capture"_s
                                      << Params{{u"text"_s, u"C++ i 2+2"_s}};
        QTest::newRow("hash is part of the value") << u"brulion://open?page=Dom#Zakupy"_s << u"open"_s
                                                   << Params{{u"page"_s, u"Dom#Zakupy"_s}};
        QTest::newRow("line breaks") << u"brulion://append?text=a%0D%0Ab%0Dc%0Ad"_s << u"append"_s
                                     << Params{{u"text"_s, u"a\nb\nc\nd"_s}};
        QTest::newRow("control characters dropped")
                << u"brulion://append?text=a%00b%1B%5B31mc%7F%09d"_s << u"append"_s
                << Params{{u"text"_s, u"ab[31mc\td"_s}};
        QTest::newRow("bare key") << u"brulion://task?text=x&alarm"_s << u"task"_s
                                  << Params{{u"text"_s, u"x"_s}, {u"alarm"_s, QString()}};
        QTest::newRow("empty pairs skipped") << u"brulion://open?&&page=Dom&"_s << u"open"_s
                                             << Params{{u"page"_s, u"Dom"_s}};
        QTest::newRow("leading BOM of a value") << u"brulion://append?text=%EF%BB%BFtekst"_s << u"append"_s
                                                << Params{{u"text"_s, u"tekst"_s}};
        QTest::newRow("emoji") << u"brulion://append?text=%F0%9F%8E%89%20urodziny"_s << u"append"_s
                               << Params{{u"text"_s, u"🎉 urodziny"_s}};
        QTest::newRow("alias journal") << u"brulion://journal"_s << u"daily"_s << Params{};
        QTest::newRow("alias today") << u"brulion://today"_s << u"daily"_s << Params{};
        QTest::newRow("alias create") << u"brulion://create?title=A"_s << u"new"_s << Params{{u"title"_s, u"A"_s}};
        QTest::newRow("alias command") << u"brulion://command?extension=silownia&command=rekord"_s << u"run"_s
                                       << Params{{u"extension"_s, u"silownia"_s}, {u"command"_s, u"rekord"_s}};
        QTest::newRow("spaces around") << u"  brulion://daily  "_s << u"daily"_s << Params{};
    }

    void parses()
    {
        QFETCH(QString, uri);
        QFETCH(QString, action);
        QFETCH(Params, params);
        const ParsedUri parsed = parseUri(uri);
        QVERIFY2(parsed.request, qPrintable(uriErrorCode(parsed.error) + u": "_s + parsed.detail));
        QCOMPARE(actionName(parsed.request->action), action);
        QCOMPARE(parsed.request->params, params);
    }

    void commonParameters()
    {
        const ParsedUri parsed = parseUri(
                u"brulion://append?text=a&notebook=Praca&key=ABC123&x-source=Tasker&id=42"
                u"&x-success=tasker%3A%2F%2Fok&x-error=https%3A%2F%2Fexample.org%2Ferr%3Fa%3D1&x-cancel=x%3Ac"_s);
        QVERIFY(parsed.request);
        const Request &r = *parsed.request;
        QCOMPARE(r.params, (Params{{u"text"_s, u"a"_s}}));
        QCOMPARE(r.vault, u"Praca"_s);
        QCOMPARE(r.key, u"ABC123"_s);
        QCOMPARE(r.source, u"Tasker"_s);
        QCOMPARE(r.id, u"42"_s);
        QCOMPARE(r.xSuccess, u"tasker://ok"_s);
        QCOMPARE(r.xError, u"https://example.org/err?a=1"_s);
        QCOMPARE(r.xCancel, u"x:c"_s);
        QCOMPARE(r.scheme, u"brulion"_s);
        QCOMPARE(parseUri(u"web+brulion://daily"_s).request->scheme, u"web+brulion"_s);
        // A caller's name is shown, so it is kept short.
        QCOMPARE(parseUri(QString(u"brulion://daily?x-source="_s + QString(200, u'a'))).request->source.size(), 80);
    }

    void refuses_data()
    {
        QTest::addColumn<QString>("uri");
        QTest::addColumn<QString>("error");

        QTest::newRow("other scheme") << u"https://example.org/open?page=Dom"_s << u"not_brulion"_s;
        QTest::newRow("look-alike scheme") << u"brulionx://open"_s << u"not_brulion"_s;
        QTest::newRow("empty") << QString() << u"not_brulion"_s;
        QTest::newRow("no action") << u"brulion://"_s << u"no_action"_s;
        QTest::newRow("only a query") << u"brulion://?page=Dom"_s << u"no_action"_s;
        QTest::newRow("unknown action") << u"brulion://delete?page=Dom"_s << u"unknown_action"_s;
        QTest::newRow("bad percent") << u"brulion://append?text=50%"_s << u"bad_encoding"_s;
        QTest::newRow("bad hex") << u"brulion://append?text=%G1"_s << u"bad_encoding"_s;
        QTest::newRow("invalid UTF-8") << u"brulion://append?text=%C5"_s << u"bad_encoding"_s;
        QTest::newRow("overlong UTF-8") << u"brulion://append?text=%C0%AF"_s << u"bad_encoding"_s;
        QTest::newRow("bad action encoding") << u"brulion://%E2%28open"_s << u"bad_encoding"_s;
        QTest::newRow("key without a name") << u"brulion://append?=x"_s << u"bad_encoding"_s;
        QTest::newRow("twice") << u"brulion://append?text=a&TEXT=b"_s << u"duplicate"_s;
        QTest::newRow("alias twice") << u"brulion://append?vault=a&notebook=b&text=c"_s << u"duplicate"_s;
        QTest::newRow("key twice") << u"brulion://append?key=a&key=b&text=c"_s << u"duplicate"_s;
        QTest::newRow("path and page") << u"brulion://open/Dom?page=Inny"_s << u"duplicate"_s;
        QTest::newRow("too many parameters") << u"brulion://open?"_s + manyParameters(MaxParameters + 1)
                                             << u"too_long"_s;
        QTest::newRow("value too long") << u"brulion://append?text="_s + QString(MaxValueLength + 1, u'a')
                                        << u"too_long"_s;
        QTest::newRow("address too long") << u"brulion://open?page="_s + QString(MaxUriLength, u'a')
                                          << u"too_long"_s;
    }

    void refuses()
    {
        QFETCH(QString, uri);
        QFETCH(QString, error);
        const ParsedUri parsed = parseUri(uri);
        QVERIFY(!parsed.request);
        QCOMPARE(uriErrorCode(parsed.error), error);
    }

    void limits()
    {
        // At the limit is fine (a value is counted once decoded).
        const ParsedUri atLimit = parseUri(QString(u"brulion://append?text="_s + QString(MaxValueLength, u'ż')));
        QVERIFY(atLimit.request);
        QCOMPARE(atLimit.request->value({u"text"}).size(), MaxValueLength);
        QVERIFY(parseUri(QString(u"brulion://open?"_s + manyParameters(MaxParameters))).error != UriError::TooLong);
    }

    void isAutomationUri_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("expected");
        QTest::newRow("brulion") << u"brulion://open"_s << true;
        QTest::newRow("capitals") << u"Brulion:daily"_s << true;
        QTest::newRow("web") << u"web+brulion://daily"_s << true;
        QTest::newRow("file") << u"/home/ala/Notatki/Dom.md"_s << false;
        QTest::newRow("file url") << u"file:///home/ala/Dom.md"_s << false;
        QTest::newRow("a file named like it") << u"brulion.md"_s << false;
        QTest::newRow("windows path") << u"C:\\Notatki\\brulion.md"_s << false;
    }

    void isAutomationUri()
    {
        QFETCH(QString, text);
        QFETCH(bool, expected);
        QCOMPARE(brulion::automation::isAutomationUri(text), expected);
    }

    void roundTrip_data()
    {
        QTest::addColumn<int>("action");
        QTest::addColumn<Params>("params");
        QTest::newRow("append") << int(Action::Append)
                                << Params{{u"page"_s, u"Projekty/Dom i ogród"_s},
                                          {u"heading"_s, u"## Do zrobienia"_s},
                                          {u"text"_s, u"Kupić farbę 50% taniej & szybko\nDruga linia"_s}};
        QTest::newRow("task") << int(Action::Task)
                              << Params{{u"text"_s, u"Zadzwonić do mamy"_s}, {u"remind"_s, u"jutro o 18"_s}};
        QTest::newRow("symbols") << int(Action::Capture) << Params{{u"text"_s, u"a+b=c #tag [[Link]] ?x/y 🎉"_s}};
        QTest::newRow("no params") << int(Action::Daily) << Params{};
    }

    void roundTrip()
    {
        QFETCH(int, action);
        QFETCH(Params, params);
        Request r;
        r.action = Action(action);
        r.params = params;
        r.vault = u"Mój notatnik"_s;
        r.key = u"abcdefghijklmnopqrstuvwxyz234567"_s;
        r.source = u"Skrypt"_s;
        r.xSuccess = u"tasker://ok?a=1"_s;
        const QString uri = buildUri(r);
        QVERIFY(uri.startsWith(u"brulion://"_s + actionName(r.action)));
        QVERIFY2(!uri.contains(u' ') && !uri.contains(u'+') && !uri.contains(u'\n'), qPrintable(uri));
        // The key comes last (easy to cut off before sharing an address).
        QVERIFY(uri.endsWith(u"&key=abcdefghijklmnopqrstuvwxyz234567"_s));
        const ParsedUri back = parseUri(uri);
        QVERIFY(back.request);
        QCOMPARE(back.request->action, r.action);
        QCOMPARE(back.request->params, r.params);
        QCOMPARE(back.request->vault, r.vault);
        QCOMPARE(back.request->key, r.key);
        QCOMPARE(back.request->source, r.source);
        QCOMPARE(back.request->xSuccess, r.xSuccess);
    }

    void encodeValue()
    {
        QCOMPARE(brulion::automation::encodeValue(u"Kupić mleko 2+2/3"_s), u"Kupi%C4%87%20mleko%202%2B2/3"_s);
        QCOMPARE(brulion::automation::encodeValue(u"a-b_c.d~e"_s), u"a-b_c.d~e"_s);
    }

    void redacts_data()
    {
        QTest::addColumn<QString>("uri");
        QTest::addColumn<QString>("expected");
        QTest::newRow("last") << u"brulion://append?text=a&key=sekret"_s << u"brulion://append?text=a&key=•••"_s;
        QTest::newRow("first") << u"brulion://append?key=sekret&text=a"_s << u"brulion://append?key=•••&text=a"_s;
        QTest::newRow("capitals") << u"brulion://append?KEY=sekret"_s << u"brulion://append?KEY=•••"_s;
        QTest::newRow("twice") << u"brulion://a?key=1&x=2&key=3"_s << u"brulion://a?key=•••&x=2&key=•••"_s;
        QTest::newRow("no key") << u"brulion://append?text=a"_s << u"brulion://append?text=a"_s;
        QTest::newRow("key-like name") << u"brulion://a?monkey=1"_s << u"brulion://a?monkey=1"_s;
        QTest::newRow("empty key") << u"brulion://a?key="_s << u"brulion://a?key="_s;
    }

    void redacts()
    {
        QFETCH(QString, uri);
        QFETCH(QString, expected);
        QCOMPARE(redactedUri(uri), expected);
    }

    void withQuery_data()
    {
        QTest::addColumn<QString>("base");
        QTest::addColumn<QString>("expected");
        QTest::newRow("none") << u"tasker://done"_s << u"tasker://done?path=Dziennik/2026-11-20.md&line=3"_s;
        QTest::newRow("query") << u"https://example.org/cb?a=1"_s
                               << u"https://example.org/cb?a=1&path=Dziennik/2026-11-20.md&line=3"_s;
        QTest::newRow("open query") << u"x://cb?"_s << u"x://cb?path=Dziennik/2026-11-20.md&line=3"_s;
        QTest::newRow("fragment") << u"x://cb#top"_s << u"x://cb?path=Dziennik/2026-11-20.md&line=3#top"_s;
    }

    void withQuery()
    {
        QFETCH(QString, base);
        QFETCH(QString, expected);
        QCOMPARE(brulion::automation::withQuery(base, {{u"path"_s, u"Dziennik/2026-11-20.md"_s}, {u"line"_s, u"3"_s}}),
                 expected);
    }

    void actions()
    {
        for (const Action a : allActions())
            QCOMPARE(actionFromName(actionName(a)), a);
        QCOMPARE(actionFromName(u"  APPEND "_s), Action::Append);
        QVERIFY(!actionFromName(u"remove"_s));
        QVERIFY(!actionFromName(QString()));
    }
};

QTEST_GUILESS_MAIN(TestAutomationUri)
#include "tst_automation_uri.moc"
