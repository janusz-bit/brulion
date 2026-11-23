// A report of an AI response (aireport/aireport.h): the reasons and their ids, the report's title
// and text — the reason, where, the model, the app and the device listed; the comment, the response
// and the question fenced literally (Markdown, HTML, mentions and backticks in them), the question
// only when it is added, an empty response said so —, the link of a new GitHub issue and of an
// email (every character percent-encoded but the unreserved ones: Polish letters, "+", "&", "#",
// line breaks; the decoded link gives the texts back), long texts cut so the link fits (each to
// the same length, short ones whole, never inside a surrogate pair, said under each), addresses that
// are no plain email (the issue instead), where the response was shown and the model the settings
// name (never a server's address).
#include "aireport/aireport.h"

#include "ai/ai.h"
#include "ai/local.h"

#include <QRegularExpression>
#include <QTest>
#include <QUrlQuery>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::aireport;

namespace {

Report sample()
{
    Report r;
    r.reason = Reason::Offensive;
    r.comment = u"Obraźliwa odpowiedź — żółć!"_s;
    r.response = u"Zażółć gęślą jaźń: **to** <b>HTML</b> i @ktoś"_s;
    r.where = place(u"chat");
    r.model = u"Brulion's AI assistant · Anthropic · claude-opus-5-5"_s;
    r.version = u"0.28.1"_s;
    r.device = u"Android 15, arm64"_s;
    return r;
}

QString queryValue(const QUrl &url, const QString &key)
{
    return QUrlQuery(url).queryItemValue(key, QUrl::FullyDecoded);
}

/// No half of a surrogate pair alone.
bool wholePairs(const QString &s)
{
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s.at(i).isHighSurrogate()) {
            if (i + 1 >= s.size() || !s.at(i + 1).isLowSurrogate())
                return false;
            ++i;
        } else if (s.at(i).isLowSurrogate()) {
            return false;
        }
    }
    return true;
}

} // namespace

class TestAiReport : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void reasons_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("text");
        QTest::newRow("offensive") << u"offensive"_s << u"Offensive or hateful"_s;
        QTest::newRow("sexual") << u"sexual"_s << u"Sexual content"_s;
        QTest::newRow("violent") << u"violent"_s << u"Violent or dangerous"_s;
        QTest::newRow("harassment") << u"harassment"_s << u"Harassment"_s;
        QTest::newRow("misleading") << u"misleading"_s << u"False or misleading"_s;
        QTest::newRow("other") << u"other"_s << u"Other"_s;
    }
    void reasons()
    {
        QFETCH(QString, id);
        QFETCH(QString, text);
        const std::optional<Reason> reason = reasonFromId(id);
        QVERIFY(reason.has_value());
        QCOMPARE(reasonId(*reason), id);
        QCOMPARE(reasonText(*reason), text);
        QCOMPARE(reasonFromId(QString(u"  "_s + id.toUpper() + u" "_s)), reason);
        QVERIFY(aireport::reasons().contains(*reason));
    }

    void unknownReasons()
    {
        QCOMPARE(aireport::reasons().size(), 6);
        QCOMPARE(aireport::reasons().first(), Reason::Offensive);
        QCOMPARE(aireport::reasons().last(), Reason::Other);
        QVERIFY(!reasonFromId(u""_s).has_value());
        QVERIFY(!reasonFromId(u"obraźliwe"_s).has_value());
        QVERIFY(!reasonFromId(u"offensive2"_s).has_value());
    }

    void theReport()
    {
        const Report r = sample();
        QCOMPARE(title(r), u"AI response report: Offensive or hateful"_s);
        QCOMPARE(body(r), u"An AI response reported from Brulion (Report response).\n"
                          "\n"
                          "- Reason: Offensive or hateful\n"
                          "- Where: AI chat\n"
                          "- Model: Brulion's AI assistant · Anthropic · claude-opus-5-5\n"
                          "- App: Brulion 0.28.1\n"
                          "- Device: Android 15, arm64\n"
                          "\n"
                          "### Comment\n"
                          "\n"
                          "```\n"
                          "Obraźliwa odpowiedź — żółć!\n"
                          "```\n"
                          "\n"
                          "### The response\n"
                          "\n"
                          "```\n"
                          "Zażółć gęślą jaźń: **to** <b>HTML</b> i @ktoś\n"
                          "```\n"_s);
    }

    void theQuestionOnlyWhenAdded()
    {
        Report r = sample();
        QVERIFY(!body(r).contains(u"question"_s));
        r.question = u"Co mam jutro do zrobienia?\n([[Dom]])"_s;
        QVERIFY2(body(r).endsWith(u"### The question before it\n\n```\nCo mam jutro do zrobienia?\n([[Dom]])\n```\n"_s),
                 qPrintable(body(r)));
        r.question = u"  \n "_s; // (only whitespace: none)
        QVERIFY(!body(r).contains(u"question"_s));
    }

    void emptyAndUnknown()
    {
        Report r;
        r.reason = Reason::Other;
        r.comment = u"   \n"_s;
        r.model = u"  Claude\n  Code  "_s; // (one line)
        const QString text = body(r);
        QVERIFY(!text.contains(u"### Comment"_s));
        QVERIFY(text.contains(u"- Where: not known\n"_s));
        QVERIFY(text.contains(u"- Model: Claude Code\n"_s));
        QVERIFY(text.contains(u"- App: Brulion not known\n"_s));
        QVERIFY(text.contains(u"- Device: not known\n"_s));
        QVERIFY2(text.endsWith(u"### The response\n\n(The response is empty.)\n"_s), qPrintable(text));
        QCOMPARE(title(r), u"AI response report: Other"_s);
    }

    void fences_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected");
        QTest::newRow("plain") << u"Zażółć"_s << u"```\nZażółć\n```"_s;
        QTest::newRow("a fence inside") << u"a\n```luau\nprint(1)\n```\nb"_s
                                        << u"````\na\n```luau\nprint(1)\n```\nb\n````"_s;
        QTest::newRow("a longer run") << u"x ````` y"_s << u"``````\nx ````` y\n``````"_s;
        QTest::newRow("inline code") << u"`kod`"_s << u"```\n`kod`\n```"_s;
        QTest::newRow("tildes") << u"~~~\n- 🤖 nie wiadomość\n~~~"_s << u"```\n~~~\n- 🤖 nie wiadomość\n~~~\n```"_s;
        QTest::newRow("CRLF and trailing breaks") << u"ą\r\nę\r\n\r\n"_s << u"```\ną\nę\n```"_s;
        QTest::newRow("empty") << QString() << u"```\n\n```"_s;
    }
    void fences()
    {
        QFETCH(QString, text);
        QFETCH(QString, expected);
        QCOMPARE(fenced(text), expected);
    }

    void anIssue()
    {
        Report r = sample();
        r.comment = u"C++ & Qt #1 + 50% ą"_s;
        const Link l = issueLink(r);
        QVERIFY(!l.email);
        QVERIFY(!l.cut);
        QVERIFY(l.url.isValid());
        QCOMPARE(l.title, title(r));
        QCOMPARE(l.body, body(r));
        const QByteArray encoded = l.url.toEncoded();
        QVERIFY2(encoded.startsWith("https://github.com/janusz-bit/brulion/issues/new?title=AI%20response%20report%3A%20"),
                 encoded.constData());
        QVERIFY(encoded.contains("&body="));
        // Every character but the unreserved ones encoded: no "+" that GitHub would read as a
        // space, no "&" or "#" that would end the text.
        const qsizetype bodyAt = encoded.indexOf("&body=") + 6;
        const QByteArray bodyPart = encoded.mid(bodyAt);
        QVERIFY(!bodyPart.contains('+'));
        QVERIFY(!bodyPart.contains('&'));
        QVERIFY(!bodyPart.contains('#'));
        QVERIFY(!bodyPart.contains(' '));
        QVERIFY(bodyPart.contains("C%2B%2B%20%26%20Qt%20%231%20%2B%2050%25%20%C4%85"));
        QVERIFY(bodyPart.contains("%0A"));
        for (const char c : encoded)
            QVERIFY(uchar(c) < 0x80);
        // Decoded, the texts are as they were.
        QCOMPARE(queryValue(l.url, u"title"_s), l.title);
        QCOMPARE(queryValue(l.url, u"body"_s), l.body);
        QVERIFY(encoded.size() <= MaxLinkLength);
    }

    void anEmail()
    {
        const Report r = sample();
        const Link l = emailLink(r, u" raporty+ai@example.com "_s);
        QVERIFY(l.email);
        QVERIFY(!l.cut);
        QCOMPARE(l.url.scheme(), u"mailto"_s);
        QCOMPARE(l.url.path(), u"raporty+ai@example.com"_s);
        QCOMPARE(queryValue(l.url, u"subject"_s), title(r));
        // (RFC 6068: CRLF between the body's lines.)
        QString crlf = body(r);
        crlf.replace(u"\n"_s, u"\r\n"_s);
        QCOMPARE(queryValue(l.url, u"body"_s), crlf);
        QCOMPARE(l.body, body(r));
        QVERIFY(l.url.toEncoded().contains("%0D%0A"));
        QVERIFY(l.url.toEncoded().startsWith("mailto:raporty+ai@example.com?subject="));
        // link(): the email when there is an address, else the issue.
        QVERIFY(link(r, u"raporty@example.com"_s).email);
        QVERIFY(!link(r, QString()).email);
    }

    void addresses_data()
    {
        QTest::addColumn<QString>("address");
        QTest::addColumn<bool>("plain");
        QTest::newRow("plain") << u"ai-reports@example.com"_s << true;
        QTest::newRow("plus and dots") << u"jan.kowalski+brulion@mail.example.pl"_s << true;
        QTest::newRow("empty") << QString() << false;
        QTest::newRow("no domain dot") << u"x@localhost"_s << false;
        QTest::newRow("a space") << u"a b@example.com"_s << false;
        QTest::newRow("a query") << u"a@example.com?cc=evil@example.org"_s << false;
        QTest::newRow("an ampersand") << u"a@example.com&body=x"_s << false;
        QTest::newRow("angle brackets") << u"<a@example.com>"_s << false;
        QTest::newRow("Polish letters") << u"zgłoszenia@przykład.pl"_s << false;
        QTest::newRow("two @") << u"a@b@example.com"_s << false;
        QTest::newRow("percent") << u"a%40b@example.com"_s << false;
        QTest::newRow("a label ending in -") << u"a@ex-.com"_s << false;
    }
    void addresses()
    {
        QFETCH(QString, address);
        QFETCH(bool, plain);
        QCOMPARE(isEmailAddress(address), plain);
        // Not plain: the issue instead.
        QCOMPARE(emailLink(sample(), address).email, plain);
    }

    void longTextsAreCut()
    {
        Report r = sample();
        QString answer;
        for (int i = 0; answer.size() < 20000; ++i)
            answer += u"Zdanie %1: zażółć gęślą jaźń. "_s.arg(i);
        r.response = answer;
        r.question = QString(9000, u'ś');
        const Link l = issueLink(r);
        QVERIFY(l.cut);
        QVERIFY2(l.url.toEncoded().size() <= MaxLinkLength, QByteArray::number(l.url.toEncoded().size()).constData());
        // Not much less either: the longest texts that fit.
        QVERIFY(l.url.toEncoded().size() > MaxLinkLength - 300);
        QCOMPARE(queryValue(l.url, u"body"_s), l.body);
        // The list and the short comment stay whole; each long text says how much of it is there.
        QVERIFY(l.body.contains(u"- Model: Brulion's AI assistant · Anthropic · claude-opus-5-5\n"_s));
        QVERIFY(l.body.contains(u"```\nObraźliwa odpowiedź — żółć!\n```"_s));
        static const QRegularExpression note(u"\\(Cut to fit the link: the first (\\d+) of (\\d+) characters\\.\\)"_s);
        QList<int> kept;
        QList<int> of;
        for (auto it = note.globalMatch(l.body); it.hasNext();) {
            const auto m = it.next();
            kept << m.captured(1).toInt();
            of << m.captured(2).toInt();
        }
        QCOMPARE(of, (QList<int>{int(answer.size()), 9000}));
        QCOMPARE(kept.size(), 2);
        QCOMPARE(kept.at(0), kept.at(1)); // (the same length for both)
        QVERIFY(kept.at(0) > 100);
        QVERIFY(l.body.contains(u"```\n"_s + answer.left(kept.at(0)) + u"…\n```"_s));
        QVERIFY(l.body.contains(u"```\n"_s + QString(kept.at(0), u'ś') + u"…\n```"_s));
        // A whole one says nothing of the kind.
        QVERIFY(!issueLink(sample()).body.contains(u"Cut to fit"_s));
    }

    void cutsAWholeCharacter()
    {
        Report r = sample();
        r.response = QString();
        for (int i = 0; i < 3000; ++i)
            r.response += u"😀"_s;
        for (const int max : {2000, 2001, 2002, 2003, 3000}) {
            const Link l = issueLink(r, max);
            QVERIFY(l.cut);
            QVERIFY(l.url.toEncoded().size() <= max);
            QVERIFY2(wholePairs(l.body), qPrintable(QString::number(max)));
            QVERIFY(wholePairs(queryValue(l.url, u"body"_s)));
        }
    }

    void aLinkTooShortForAnything()
    {
        // Even the list does not fit: the texts are cut to nothing, the link is made all the same.
        Report r = sample();
        r.response = QString(500, u'a');
        const Link l = issueLink(r, 100);
        QVERIFY(l.cut);
        QVERIFY(l.url.isValid());
        QVERIFY(l.body.contains(u"```\n…\n```"_s));
        // An email cut as the issue is.
        r.response = QString(2000, u'a');
        const Link mail = emailLink(r, u"a@example.com"_s, 1200);
        QVERIFY(mail.email);
        QVERIFY(mail.cut);
        QVERIFY(mail.url.toEncoded().size() <= 1200);
        QVERIFY(mail.body.contains(u"(Cut to fit the link: the first "_s));
    }

    void places_data()
    {
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("text");
        QTest::newRow("chat") << u"chat"_s << u"AI chat"_s;
        QTest::newRow("a rewrite") << u"writing/rewrite/shorter"_s << u"Writing assistant: rewrite (shorter)"_s;
        QTest::newRow("a summary") << u"writing/summarize"_s << u"Writing assistant: summarize"_s;
        QTest::newRow("own words") << u"writing/rewrite/"_s << u"Writing assistant: rewrite"_s;
        QTest::newRow("writing alone") << u"writing"_s << u"Writing assistant"_s;
        QTest::newRow("odd characters") << u"writing/re<b>write/ż ółw"_s << u"Writing assistant: rebwrite (w)"_s;
        QTest::newRow("unknown") << u"coś"_s << u"AI response"_s;
        QTest::newRow("empty") << QString() << u"AI response"_s;
    }
    void places()
    {
        QFETCH(QString, code);
        QFETCH(QString, text);
        QCOMPARE(place(code), text);
    }

    void models()
    {
        ai::Settings s;
        s.provider = u"anthropic"_s;
        QCOMPARE(modelDescription(s), u"Anthropic · "_s + ai::defaultModel(u"anthropic"_s));
        s.model = u"claude-sonnet-5-5"_s;
        QCOMPARE(modelDescription(s), u"Anthropic · claude-sonnet-5-5"_s);
        // A server of the user's own: never its address.
        s.provider = u"openai"_s;
        s.model = u"qwen3:8b"_s;
        s.baseUrl = u"http://192.168.1.10:11434/v1"_s;
        QCOMPARE(modelDescription(s), u"OpenAI-compatible API · qwen3:8b"_s);
        s.model = QString();
        QCOMPARE(modelDescription(s), u"OpenAI-compatible API · no model chosen"_s);
        s.model = u"a\nb"_s;
        QCOMPARE(modelDescription(s), u"OpenAI-compatible API · a b"_s);
        // The model on the device by its name.
        s.provider = u"local"_s;
        s.baseUrl = QString();
        QVERIFY(!ai::localModels().isEmpty());
        s.model = ai::localModels().first().id;
        QCOMPARE(modelDescription(s), u"On this device · "_s + ai::localModels().first().name);
        s.model = QString();
        QCOMPARE(modelDescription(s), u"On this device · "_s + ai::localModels().first().name);
        s.model = u"nieznany-model"_s;
        QCOMPARE(modelDescription(s), u"On this device · nieznany-model"_s);
        s.provider = QString();
        s.model = u"x"_s;
        QCOMPARE(modelDescription(s), u"Unknown provider · x"_s);
    }
};

QTEST_GUILESS_MAIN(TestAiReport)
#include "tst_aireport.moc"
