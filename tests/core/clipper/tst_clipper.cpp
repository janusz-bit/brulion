// Tests of the clipper module (src/core/clipper, docs/CLIPPER.md): native messaging frames (any
// pieces, limits, malformed input), clip requests (hostile values refused or cleaned), templates
// (YAML-safe values, lines left out, the built-ins), pages' HTML as Markdown (a real article's
// fixture, figures, code, relative addresses, malicious HTML, Polish text), the pictures' download
// against a server on 127.0.0.1 (limits, private hosts, data: pictures) and the host manifests with
// their places for every browser and system.
#include "clipper/clipper.h"

#include "fileserver.h"
#include "testtiming.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::clipper;

namespace {

QByteArray frame(const QByteArray &json)
{
    const quint32 length = quint32(json.size());
    QByteArray out(4, Qt::Uninitialized);
    std::memcpy(out.data(), &length, 4);
    return out + json;
}

QByteArray header(quint32 length)
{
    QByteArray out(4, Qt::Uninitialized);
    std::memcpy(out.data(), &length, 4);
    return out;
}

QString fixture(const QString &name)
{
    QFile f(QStringLiteral(BRULION_CLIPPER_FIXTURES) + u'/' + name);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

QByteArray pngBytes()
{
    return QByteArray::fromBase64(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mNgAAIAAAUAAen63NgAAAAASUVORK5CYII=");
}

QByteArray jpegBytes()
{
    return QByteArray("\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00", 20);
}

/// Markdown with the picture tokens as Markdown pictures from the internet (or the data: kept).
QString withRemotePictures(const QString &markdown, const PictureList &pictures)
{
    return replacePictureTokens(markdown, [&](int index, bool inLink) -> QString {
        if (inLink || index >= pictures.pictures().size())
            return {};
        const ClipPicture &p = pictures.pictures().at(index);
        return u"!["_s + p.alt + u"]("_s + p.source + u')';
    });
}

QString toMarkdown(const QString &html, const QString &base = u"https://przyroda.example/artykuly/zolw.html"_s)
{
    PictureList pictures;
    const ConvertedHtml c = convertHtml(html, QUrl(base), pictures);
    return withRemotePictures(c.markdown, pictures);
}

TemplateFill fillAt(const char *when = "2026-11-20 18:30",
                    brulion::dates::UiLocale locale = brulion::dates::UiLocale::Polish)
{
    return TemplateFill{QDateTime::fromString(QString::fromLatin1(when), u"yyyy-MM-dd HH:mm"_s), locale};
}

ClipValues sampleValues()
{
    ClipValues v;
    v.title = u"Żółw stepowy w ogrodzie"_s;
    v.url = u"https://przyroda.example/artykuly/zolw.html?a=1#x"_s;
    v.domain = u"przyroda.example"_s;
    v.site = u"Przyroda Dziś"_s;
    v.author = u"Zofia Łęcka"_s;
    v.published = u"2026-11-19T08:15:00+01:00"_s;
    v.description = u"Jak zimuje żółw: poradnik"_s;
    v.content = u"Żółw stepowy to **gad**.\n\n- liście\n- kwiaty"_s;
    v.contentText = u"Żółw stepowy to gad."_s;
    v.selection = u"gad, który **źle znosi** chłód"_s;
    v.selectionText = u"gad, który źle znosi chłód"_s;
    v.tags = {u"gady"_s, u"ogród zimą"_s};
    return v;
}

} // namespace

class TestClipper : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ---- frames
    void frames_data()
    {
        QTest::addColumn<QList<QByteArray>>("chunks");
        QTest::addColumn<QStringList>("expected"); ///< per frame: the message's "t", or the error code
        QTest::addColumn<bool>("truncated");
        const QByteArray hello = frame(R"({"t":"hello"})");
        const QByteArray polish = frame(QString(u"{\"t\":\"Zażółć gęślą jaźń\"}"_s).toUtf8());
        QTest::newRow("one") << QList<QByteArray>{hello} << QStringList{u"hello"_s} << false;
        QTest::newRow("two in one piece")
                << QList<QByteArray>{hello + polish} << QStringList{u"hello"_s, u"Zażółć gęślą jaźń"_s} << false;
        QList<QByteArray> bytes;
        const QByteArray both = hello + polish;
        for (const char c : both)
            bytes.append(QByteArray(1, c));
        QTest::newRow("byte by byte") << bytes << QStringList{u"hello"_s, u"Zażółć gęślą jaźń"_s} << false;
        QTest::newRow("split in the header")
                << QList<QByteArray>{hello.left(2), hello.mid(2)} << QStringList{u"hello"_s} << false;
        QTest::newRow("empty frame") << QList<QByteArray>{header(0), hello}
                                     << QStringList{u"invalid_json"_s, u"hello"_s} << false;
        QTest::newRow("no JSON, then one")
                << QList<QByteArray>{frame("{\"t\":"), hello} << QStringList{u"invalid_json"_s, u"hello"_s} << false;
        QTest::newRow("not an object") << QList<QByteArray>{frame("[1,2]"), frame("\"x\""), hello}
                                       << QStringList{u"not_object"_s, u"invalid_json"_s, u"hello"_s} << false;
        QTest::newRow("invalid UTF-8") << QList<QByteArray>{frame("{\"t\":\"\xff\xfe\"}")}
                                       << QStringList{u"invalid_json"_s} << false;
        QTest::newRow("over the limit") << QList<QByteArray>{header(0xffffffffu) + hello, hello}
                                        << QStringList{u"too_large"_s} << false;
        QTest::newRow("text typed at a terminal")
                << QList<QByteArray>{"abcdefgh\n"} << QStringList{u"too_large"_s} << false;
        QTest::newRow("ends inside a frame")
                << QList<QByteArray>{hello, header(10) + "{\"t\""} << QStringList{u"hello"_s} << true;
        QTest::newRow("ends inside a header") << QList<QByteArray>{QByteArray("\x05\x00", 2)} << QStringList{} << true;
        QTest::newRow("nothing") << QList<QByteArray>{} << QStringList{} << false;
    }
    void frames()
    {
        QFETCH(QList<QByteArray>, chunks);
        QFETCH(QStringList, expected);
        QFETCH(bool, truncated);
        FrameDecoder decoder;
        QStringList got;
        for (const QByteArray &chunk : std::as_const(chunks)) {
            decoder.feed(chunk);
            while (auto f = decoder.next()) {
                got.append(f->error == FrameError::None ? f->message.value(u"t"_s).toString()
                                                        : frameErrorCode(f->error));
            }
        }
        QCOMPARE(got, expected);
        QCOMPARE(decoder.finish() == FrameError::Truncated, truncated);
    }

    void tooLargeNeverAllocates()
    {
        FrameDecoder decoder(1024);
        decoder.feed(header(1025));
        const auto f = decoder.next();
        QVERIFY(f);
        QCOMPARE(f->error, FrameError::TooLarge);
        QCOMPARE(f->length, 1025u);
        QVERIFY(decoder.broken());
        decoder.feed(QByteArray(4096, 'x'));
        QCOMPARE(decoder.buffered(), 0);
        QVERIFY(!decoder.next());
    }

    void encodeRoundTrip()
    {
        const QJsonObject message{{u"type"_s, u"result"_s}, {u"path"_s, u"Wycinki/Żółw stepowy.md"_s}, {u"line"_s, 3}};
        const QByteArray bytes = encodeFrame(message);
        QCOMPARE(qsizetype(frameLength(bytes)), bytes.size() - 4);
        FrameDecoder decoder;
        decoder.feed(bytes);
        const auto f = decoder.next();
        QVERIFY(f && f->error == FrameError::None);
        QCOMPARE(f->message, message);
        // An answer a browser would drop is not encoded.
        QVERIFY(encodeFrame(QJsonObject{{u"x"_s, QString(2000, u'a')}}, 1000).isEmpty());
        QVERIFY(encodeFrame(QJsonObject{{u"x"_s, QString(MaxOutgoingBytes, u'a')}}).isEmpty());
    }

    // ---- requests
    void requests_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("code"); ///< "" = accepted
        QTest::addColumn<QString>("field");
        QTest::newRow("minimal") << QByteArray("{}") << QString() << QString();
        QTest::newRow("article") << QByteArray(
                "{\"kind\":\"article\",\"url\":\"https://a.pl/x\",\"html\":\"<p>x</p>\"}")
                                 << QString() << QString();
        QTest::newRow("unknown kind") << QByteArray(R"({"kind":"video"})") << u"bad_kind"_s << u"kind"_s;
        QTest::newRow("kind not a string") << QByteArray(R"({"kind":3})") << u"bad_kind"_s << u"kind"_s;
        QTest::newRow("title not a string") << QByteArray(R"({"title":["x"]})") << u"bad_value"_s << u"title"_s;
        QTest::newRow("html too large") << (QByteArray("{\"html\":\"") + QByteArray(MaxHtmlChars + 1, 'a')
                                            + QByteArray("\"}"))
                                        << u"too_large"_s << u"html"_s;
        QTest::newRow("folder with ..") << QByteArray(R"({"place":{"kind":"page","folder":"Wycinki/../../etc"}})")
                                        << u"bad_folder"_s << u"place.folder"_s;
        QTest::newRow("absolute folder") << QByteArray(R"({"place":{"folder":"/home/x"}})") << u"bad_folder"_s
                                         << u"place.folder"_s;
        QTest::newRow("drive") << QByteArray(R"({"place":{"folder":"C:\\Windows"}})") << u"bad_folder"_s
                               << u"place.folder"_s;
        QTest::newRow("hidden folder") << QByteArray(R"({"place":{"folder":".brulion"}})") << u"bad_folder"_s
                                       << u"place.folder"_s;
        QTest::newRow("git by another name")
                << QByteArray(R"({"place":{"folder":"a/GIT~1"}})") << u"bad_folder"_s << u"place.folder"_s;
        QTest::newRow("folder not a string")
                << QByteArray(R"({"place":{"folder":7}})") << u"bad_folder"_s << u"place.folder"_s;
        QTest::newRow("unknown place") << QByteArray(R"({"place":{"kind":"desktop"}})") << u"bad_place"_s
                                       << u"place.kind"_s;
        QTest::newRow("heading without page")
                << QByteArray(R"({"place":{"kind":"heading","heading":"A"}})") << u"bad_page"_s << u"place.page"_s;
        QTest::newRow("heading with ..") << QByteArray(R"({"place":{"kind":"heading","page":"../x.md","heading":"A"}})")
                                         << u"bad_page"_s << u"place.page"_s;
        QTest::newRow("heading without heading") << QByteArray(R"({"place":{"kind":"heading","page":"Czytelnia.md"}})")
                                                 << u"no_heading"_s << u"place.heading"_s;
        QTest::newRow("screenshot without picture")
                << QByteArray(R"({"kind":"screenshot"})") << u"no_screenshot"_s << u"screenshot"_s;
        QTest::newRow("screenshot not a picture")
                << QByteArray(R"({"kind":"screenshot","screenshot":"data:text/html;base64,PHNjcmlwdD4="})")
                << u"bad_screenshot"_s << u"screenshot"_s;
        QTest::newRow("picture of javascript")
                << QByteArray("{\"kind\":\"picture\",\"pictureUrl\":\"javascript:alert(1)\"}") << u"no_picture"_s
                << u"pictureUrl"_s;
        QTest::newRow("link without address")
                << QByteArray(R"({"kind":"link","linkUrl":"javascript:x"})") << u"no_link"_s << u"linkUrl"_s;
        QTest::newRow("template page outside") << QByteArray(R"({"template":{"page":"../../.ssh/id_rsa"}})")
                                               << u"bad_template"_s << u"template.page"_s;
        QTest::newRow("template too large") << (QByteArray("{\"template\":{\"text\":\"")
                                                + QByteArray(MaxTemplateChars + 1, 'a') + QByteArray("\"}}"))
                                            << u"too_large"_s << u"template.text"_s;
        QTest::newRow("tags of the wrong type") << QByteArray(R"({"tags":[1]})") << u"bad_value"_s << u"tags"_s;
        QTest::newRow("pictures not a bool")
                << QByteArray(R"({"downloadPictures":"yes"})") << u"bad_value"_s << u"downloadPictures"_s;
    }
    void requests()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, code);
        QFETCH(QString, field);
        RequestProblem problem;
        const auto r = parseClipRequest(QJsonDocument::fromJson(json).object(), &problem);
        QCOMPARE(r.has_value(), code.isEmpty());
        QCOMPARE(problem.code, code);
        QCOMPARE(problem.field, field);
    }

    void requestValuesAreCleaned()
    {
        const QJsonObject o{
                {u"kind"_s, u"Selection"_s},
                {u"url"_s, u"javascript:alert(document.cookie)"_s},
                {u"baseUrl"_s, u"https://przyroda.example/artykuly/"_s},
                {u"title"_s, u"  Żółw\nstepowy\u202e w\togrodzie\u0007 "_s},
                {u"author"_s, QString(5000, u'a')},
                {u"image"_s, u"file:///etc/passwd"_s},
                {u"tags"_s, u"#gady, [[ogród zimą]];  #Gady, a,b"_s},
                {u"place"_s, QJsonObject{{u"kind"_s, u"page"_s}, {u"folder"_s, u" Wycinki / Gady "_s}}},
                {u"template"_s, u"Obsidian"_s},
                {u"screenshot"_s, u"data:image/png;base64,iVBORw0KGgo="_s},
        };
        RequestProblem problem;
        const auto r = parseClipRequest(o, &problem);
        QVERIFY2(r, qPrintable(problem.message));
        QCOMPARE(r->kind, ClipKind::Selection);
        QCOMPARE(r->url, QString());
        QCOMPARE(r->title, u"Żółw stepowy w ogrodzie"_s);
        QCOMPARE(r->author.size(), MaxMetaChars);
        QCOMPARE(r->image, QString());
        QCOMPARE(r->tags, (QStringList{u"gady"_s, u"ogród zimą"_s, u"a"_s, u"b"_s}));
        QCOMPARE(r->place.folder, u"Wycinki/Gady"_s);
        QVERIFY(!r->place.defaultFolder);
        QCOMPARE(r->templ.builtin, u"obsidian"_s);
        QVERIFY(r->screenshot.startsWith(u"data:image/png"_s));
        QVERIFY(r->downloadPictures);

        // No folder: the notebook's; an empty one: the root.
        const auto none = parseClipRequest(QJsonObject{}, &problem);
        QVERIFY(none && none->place.defaultFolder);
        const auto root = parseClipRequest(QJsonObject{{u"place"_s, QJsonObject{{u"folder"_s, u""_s}}}}, &problem);
        QVERIFY(root && !root->place.defaultFolder && root->place.folder.isEmpty());
    }

    void tagTexts()
    {
        QCOMPARE(tagText(u"gady"_s), u"#gady"_s);
        QCOMPARE(tagText(u"Projekty/Dom"_s), u"#Projekty/Dom"_s);
        QCOMPARE(tagText(u"ogród zimą"_s), u"#[[ogród zimą]]"_s);
        QCOMPARE(tagText(u"c++"_s), u"#[[c++]]"_s);
        QCOMPARE(cleanTags({u"#[[a b]]"_s, u"A B"_s, u"   "_s, u"x]]y"_s}), (QStringList{u"a b"_s, u"xy"_s}));
    }

    void links_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("link");
        QTest::addColumn<bool>("web");
        QTest::newRow("https") << u"https://pl.wikipedia.org/wiki/Żółw"_s << true << true;
        QTest::newRow("http") << u"http://a.example/"_s << true << true;
        QTest::newRow("file") << u"file:///home/ja/strona.html"_s << true << false;
        QTest::newRow("mailto") << u"mailto:ja@example.com"_s << true << false;
        QTest::newRow("javascript") << u"javascript:alert(1)"_s << false << false;
        QTest::newRow("data") << u"data:text/html,<script>"_s << false << false;
        QTest::newRow("with a space") << u"https://a.example/a b"_s << false << false;
        QTest::newRow("no host") << u"https:///x"_s << false << false;
        QTest::newRow("control") << u"https://a.example/\u0001"_s << false << false;
        QTest::newRow("too long") << (u"https://a.example/"_s + QString(MaxUrlChars, u'a')) << false << false;
    }
    void links()
    {
        QFETCH(QString, url);
        QFETCH(bool, link);
        QFETCH(bool, web);
        QCOMPARE(!checkedLinkUrl(url).isEmpty(), link);
        QCOMPARE(!checkedWebUrl(url).isEmpty(), web);
    }

    // ---- templates
    void templates_data()
    {
        QTest::addColumn<QString>("tpl");
        QTest::addColumn<QString>("expected");
        QTest::newRow("text")
                << u"# {{title}}\n\nŹródło: {{url}} ({{site}})"_s
                << u"# Żółw stepowy w ogrodzie\n\nŹródło: https://przyroda.example/artykuly/zolw.html?a=1#x (Przyroda Dziś)\n"_s;
        QTest::newRow("dates of every template") << u"{{date}} {{time}} {{weekday}} {{date:d MMMM yyyy}} {{week}}"_s
                                                 << u"2026-11-20 18:30 piątek 20 listopada 2026 47\n"_s;
        QTest::newRow("published") << u"{{published}} / {{published:dd.MM.yyyy HH:mm}}"_s
                                   << u"2026-11-19 / 19.11.2026 08:15\n"_s;
        QTest::newRow("tags in text") << u"Tagi: {{tags}}"_s << u"Tagi: #gady #[[ogród zimą]]\n"_s;
        QTest::newRow("content and quote")
                << u"{{selection:quote}}\n\n{{content}}"_s
                << u"> gad, który **źle znosi** chłód\n\nŻółw stepowy to **gad**.\n\n- liście\n- kwiaty\n"_s;
        QTest::newRow("text forms") << u"{{content:text}} | {{selection:text}}"_s
                                    << u"Żółw stepowy to gad. | gad, który źle znosi chłód\n"_s;
        QTest::newRow("multi-line value indented")
                << u"- Wycinek\n  {{content}}"_s
                << u"- Wycinek\n  Żółw stepowy to **gad**.\n\n  - liście\n  - kwiaty\n"_s;
        QTest::newRow("unknown stays") << u"{{project}} {{title:x}}"_s << u"{{project}} Żółw stepowy w ogrodzie\n"_s;
        QTest::newRow("cursor removed") << u"{{title}}{{cursor}}"_s << u"Żółw stepowy w ogrodzie\n"_s;
        QTest::newRow("link of the page")
                << u"[{{title}}]({{url}})"_s
                << u"[Żółw stepowy w ogrodzie](https://przyroda.example/artykuly/zolw.html?a=1#x)\n"_s;
        QTest::newRow("yaml frontmatter")
                << u"---\ntitle: {{title}}\nsource: {{url}}\nauthor: \"{{author}}\"\ntags: {{tags}}\nclipped: {{date}} {{time}}\n---\n{{content}}"_s
                << u"---\ntitle: Żółw stepowy w ogrodzie\nsource: https://przyroda.example/artykuly/zolw.html?a=1#x\nauthor: \"Zofia Łęcka\"\ntags: [gady, ogród zimą]\nclipped: 2026-11-20 18:30\n---\nŻółw stepowy to **gad**.\n\n- liście\n- kwiaty\n"_s;
        QTest::newRow("yaml list of tags")
                << u"---\ntags:\n  - clippings\n  - \"{{tags}}\"\n---\nx"_s
                << u"---\ntags:\n  - clippings\n  - \"gady\"\n  - \"ogród zimą\"\n---\nx\n"_s;
        QTest::newRow("key:: properties")
                << u"source:: {{url}}\ntags:: {{tags}}\nautor:: {{author}}\n\n{{selection}}"_s
                << u"source:: https://przyroda.example/artykuly/zolw.html?a=1#x\ntags:: #gady, #[[ogród zimą]]\nautor:: Zofia Łęcka\n\ngad, który **źle znosi** chłód\n"_s;
    }
    void templates()
    {
        QFETCH(QString, tpl);
        QFETCH(QString, expected);
        QCOMPARE(fillClipTemplate(tpl, sampleValues(), fillAt()), expected);
    }

    void templateValuesAreData_data()
    {
        QTest::addColumn<QString>("tpl");
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("expected");
        // A value is never filled again, and the text around it cannot be broken by it.
        QTest::newRow("variables in a value")
                << u"{{title}} {{date}}"_s << u"{{date}} {{content}}"_s << u"{{date}} {{content}} 2026-11-20\n"_s;
        QTest::newRow("markdown in a title") << u"# {{title}}"_s << u"*Nowość* [x](javascript:alert(1)) <b>"_s
                                             << u"# \\*Nowość\\* \\[x\\](javascript:alert(1)) \\<b>\n"_s;
        QTest::newRow("title at a line's start") << u"{{title}}"_s << u"# nie nagłówek"_s << u"\\# nie nagłówek\n"_s;
        QTest::newRow("quotes in yaml") << u"---\ntitle: \"{{title}}\"\n---\n"_s << u"Ala \"ma\" \\ kota"_s
                                        << u"---\ntitle: \"Ala \\\"ma\\\" \\\\ kota\"\n---\n"_s;
        QTest::newRow("single quotes in yaml")
                << u"---\ntitle: '{{title}}'\n---\n"_s << u"It's"_s << u"---\ntitle: 'It''s'\n---\n"_s;
        QTest::newRow("bare yaml quoted when needed")
                << u"---\ntitle: {{title}}\n---\n"_s << u"Cel: zysk #1"_s << u"---\ntitle: \"Cel: zysk #1\"\n---\n"_s;
        QTest::newRow("yaml keyword") << u"---\ntitle: {{title}}\n---\n"_s << u"yes"_s
                                      << u"---\ntitle: \"yes\"\n---\n"_s;
        QTest::newRow("yaml number") << u"---\ntitle: {{title}}\n---\n"_s << u"1984"_s
                                     << u"---\ntitle: \"1984\"\n---\n"_s;
        QTest::newRow("yaml sexagesimal")
                << u"---\ntitle: {{title}}\n---\n"_s << u"18:30"_s << u"---\ntitle: \"18:30\"\n---\n"_s;
        QTest::newRow("yaml line break") << u"---\ntitle: {{title}}\n---\n"_s << u"a\nb: c"_s
                                         << u"---\ntitle: \"a b: c\"\n---\n"_s;
        QTest::newRow("yaml delimiter in a value")
                << u"---\ntitle: {{title}}\n---\n"_s << u"---"_s << u"---\ntitle: \"---\"\n---\n"_s;
    }
    void templateValuesAreData()
    {
        QFETCH(QString, tpl);
        QFETCH(QString, title);
        QFETCH(QString, expected);
        ClipValues v = sampleValues();
        v.title = title;
        QCOMPARE(fillClipTemplate(tpl, v, fillAt()), expected);
    }

    void emptyValuesLeaveNoLines()
    {
        ClipValues v;
        v.title = u"Bez autora"_s;
        v.url = u"https://a.example/"_s;
        const QString obsidian = builtinTemplate(u"obsidian"_s)->text;
        QCOMPARE(
                fillClipTemplate(obsidian, v, fillAt()),
                u"---\ntitle: \"Bez autora\"\nsource: \"https://a.example/\"\ncreated: 2026-11-20\ntags:\n  - \"clippings\"\n---\n"_s);
        QCOMPARE(fillClipTemplate(builtinTemplate(u"link"_s)->text, v, fillAt()),
                 u"[Bez autora](https://a.example/)\n"_s);
        QCOMPARE(fillClipTemplate(u"Przed\n\n{{selection}}\n\nPo"_s, v, fillAt()), u"Przed\n\nPo\n"_s);
        QCOMPARE(fillClipTemplate(u"{{selection}}\n\nTekst"_s, v, fillAt()), u"Tekst\n"_s);
        QCOMPARE(fillClipTemplate(u"source:: {{url}}\nautor:: {{author}}\n\nx"_s, v, fillAt()),
                 u"source:: https://a.example/\n\nx\n"_s);
        // Text of its own on the line keeps it.
        QCOMPARE(fillClipTemplate(u"Autor: {{author}}"_s, v, fillAt()), u"Autor:\n"_s);
    }

    void builtinTemplatesFill()
    {
        const ClipValues v = sampleValues();
        QCOMPARE(builtinTemplates().size(), 4);
        QCOMPARE(builtinTemplates().first().id, u"brulion"_s);
        QCOMPARE(defaultTemplateFor(true), u"brulion"_s);
        QCOMPARE(defaultTemplateFor(false), u"link"_s);
        QVERIFY(builtinTemplate(u"JOPLIN"_s));
        QVERIFY(!builtinTemplate(u"evernote"_s));
        QCOMPARE(
                fillClipTemplate(builtinTemplate(u"brulion"_s)->text, v, fillAt()),
                u"---\nsource: https://przyroda.example/artykuly/zolw.html?a=1#x\nauthor: Zofia Łęcka\npublished: "
                u"2026-11-19\n"
                "clipped: 2026-11-20 18:30\ntags: [gady, ogród zimą]\n---\n\nŻółw stepowy to **gad**.\n\n- liście\n- kwiaty\n"_s);
        QCOMPARE(fillClipTemplate(builtinTemplate(u"obsidian"_s)->text, v, fillAt()),
                 u"---\ntitle: \"Żółw stepowy w ogrodzie\"\nsource: "
                 u"\"https://przyroda.example/artykuly/zolw.html?a=1#x\"\n"
                 "author:\n  - \"[[Zofia Łęcka]]\"\npublished: 2026-11-19\ncreated: 2026-11-20\n"
                 "description: \"Jak zimuje żółw: poradnik\"\ntags:\n  - \"clippings\"\n  - \"gady\"\n  - \"ogród "
                 "zimą\"\n---\n"
                 "Żółw stepowy to **gad**.\n\n- liście\n- kwiaty\n"_s);
        QCOMPARE(fillClipTemplate(builtinTemplate(u"joplin"_s)->text, v, fillAt()),
                 u"---\ntitle: Żółw stepowy w ogrodzie\nsource: https://przyroda.example/artykuly/zolw.html?a=1#x\n"
                 "author: Zofia Łęcka\ncreated: 2026-11-20 18:30\ntags: [gady, ogród zimą]\n---\n\n"
                 "Żółw stepowy to **gad**.\n\n- liście\n- kwiaty\n"_s);
        QCOMPARE(fillClipTemplate(builtinTemplate(u"link"_s)->text, v, fillAt()),
                 u"[Żółw stepowy w ogrodzie](https://przyroda.example/artykuly/zolw.html?a=1#x) #gady #[[ogród zimą]]\n"
                 "> gad, który **źle znosi** chłód\n"_s);
        // English names of days and months.
        QCOMPARE(fillClipTemplate(u"{{weekday}} {{published:d MMMM}}"_s, v,
                                  fillAt("2026-11-20 18:30", brulion::dates::UiLocale::English)),
                 u"Friday 19 November\n"_s);
    }

    void frontmatterAsProperties_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<QString>("expected");
        QTest::newRow("brulion")
                << u"---\nsource: https://a.example/\ntags: [gady, ogród zimą]\n---\n\nPierwszy akapit.\n\nDrugi.\n"_s
                << u"Pierwszy akapit.\nsource:: https://a.example/\ntags:: #gady, #[[ogród zimą]]\n\nDrugi.\n"_s;
        QTest::newRow("obsidian lists")
                << u"---\ntitle: \"Żółw\"\nauthor:\n  - \"[[Zofia Łęcka]]\"\ntags:\n  - \"clippings\"\n---\nTreść"_s
                << u"Treść\ntitle:: Żółw\nauthor:: [[Zofia Łęcka]]\ntags:: #clippings"_s;
        QTest::newRow("no body") << u"---\nsource: https://a.example/\n---\n"_s << u"source:: https://a.example/\n"_s;
        QTest::newRow("no frontmatter") << u"[Tytuł](https://a.example/)\n> cytat\n"_s
                                        << u"[Tytuł](https://a.example/)\n> cytat\n"_s;
        QTest::newRow("unclosed") << u"---\nsource: x\n"_s << u"---\nsource: x\n"_s;
        QTest::newRow("keys that are no property")
                << u"---\n\"źródło\": x\nźródło: y\nok: z\n---\nT"_s << u"T\nok:: z"_s;
    }
    void frontmatterAsProperties()
    {
        QFETCH(QString, markdown);
        QFETCH(QString, expected);
        QCOMPARE(frontmatterAsBlockProperties(markdown), expected);
    }

    void published_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected"); ///< "yyyy-MM-dd HH:mm", "" = not a date
        QTest::newRow("ISO with offset") << u"2026-11-19T08:15:00+01:00"_s << u"2026-11-19 08:15"_s;
        QTest::newRow("ISO UTC late") << u"2026-11-19T23:30:00Z"_s << u"2026-11-19 23:30"_s;
        QTest::newRow("milliseconds") << u"2026-11-19T08:15:00.123+0100"_s << u"2026-11-19 08:15"_s;
        QTest::newRow("date") << u"2026-11-19"_s << u"2026-11-19 00:00"_s;
        QTest::newRow("space") << u"2026-11-19 08:15"_s << u"2026-11-19 08:15"_s;
        QTest::newRow("RFC 2822") << u"Thu, 19 Nov 2026 08:15:00 +0100"_s << u"2026-11-19 08:15"_s;
        QTest::newRow("Polish words") << u"19 listopada 2026"_s << QString();
        QTest::newRow("invalid date") << u"2026-13-45"_s << QString();
        QTest::newRow("empty") << QString() << QString();
    }
    void published()
    {
        QFETCH(QString, text);
        QFETCH(QString, expected);
        const QDateTime dt = parsePublished(text);
        QCOMPARE(dt.isValid() ? dt.toString(u"yyyy-MM-dd HH:mm"_s) : QString(), expected);
    }

    // ---- HTML
    void articleFixture()
    {
        PictureList pictures;
        const ConvertedHtml c =
                convertHtml(fixture(u"artykul.html"_s), QUrl(u"https://przyroda.example/artykuly/zolw-stepowy.html"_s),
                            pictures, true);
        const QString markdown = withRemotePictures(c.markdown, pictures);
        // BRULION_CLIPPER_UPDATE_FIXTURES=1 writes what the converter gives now (look at the diff).
        if (qEnvironmentVariableIsSet("BRULION_CLIPPER_UPDATE_FIXTURES")) {
            QFile out(QStringLiteral(BRULION_CLIPPER_FIXTURES "/artykul.md"));
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(markdown.toUtf8());
        }
        const QString expected = fixture(u"artykul.md"_s);
        QCOMPARE(markdown, expected);
        QCOMPARE(pictures.pictures().size(), 2);
        QVERIFY(c.plainText.contains(u"Żółw stepowy (Testudo horsfieldii) to gad"_s));
        QVERIFY(!c.plainText.contains(u"tracker"_s));
    }

    void convert_data()
    {
        QTest::addColumn<QString>("html");
        QTest::addColumn<QString>("expected");
        QTest::newRow("figure with a caption")
                << u"<figure><img src=\"/img/a.png\" alt=\"Wykres\"><figcaption>Rys. 1: <b>wzrost</b></figcaption></figure><p>Dalej</p>"_s
                << u"![Wykres](https://przyroda.example/img/a.png)\n\n*Rys. 1:* ***wzrost***\n\nDalej\n"_s;
        QTest::newRow("sections do not run together")
                << u"<header>Nagłówek strony</header><section>Pierwsza</section><aside>Na marginesie</aside><footer>Stopka</footer>"_s
                << u"Nagłówek strony\nPierwsza\nNa marginesie\nStopka\n"_s;
        QTest::newRow("details and summary")
                << u"<details><summary>Więcej</summary><p>Ukryte</p></details>"_s << u"**Więcej**\n\nUkryte\n"_s;
        QTest::newRow("lazy picture")
                << u"<img src=\"data:image/gif;base64,R0lGODlhAQABAAAAACw=\" data-src=\"zdj/zolw.jpg\">"_s
                << u"![](https://przyroda.example/artykuly/zdj/zolw.jpg)\n"_s;
        QTest::newRow("srcset only") << u"<img srcset=\"a.jpg 320w, b.jpg 1024w\" alt=\"Żółw\">"_s
                                     << u"![Żółw](https://przyroda.example/artykuly/b.jpg)\n"_s;
        QTest::newRow("protocol-relative picture")
                << u"<img src=\"//cdn.example/x.png\">"_s << u"![](https://cdn.example/x.png)\n"_s;
        QTest::newRow("picture of another scheme dropped")
                << u"<p>a<img src=\"file:///etc/passwd\">b</p>"_s << u"ab\n"_s;
        QTest::newRow("relative links")
                << u"<p><a href=\"../inne/strona.html\">inna</a> <a href=\"/\">start</a></p>"_s
                << u"[inna](https://przyroda.example/inne/strona.html) [start](https://przyroda.example/)\n"_s;
        QTest::newRow("fragment") << u"<p>Przypis<a href=\"#p1\">1</a></p>"_s
                                  << u"Przypis[1](https://przyroda.example/artykuly/zolw.html#p1)\n"_s;
        QTest::newRow("mailto") << u"<p><a href=\"mailto:redakcja@przyroda.example\">napisz</a></p>"_s
                                << u"[napisz](mailto:redakcja@przyroda.example)\n"_s;
        QTest::newRow("javascript link") << u"<p><a href=\"java&#x09;script:alert(1)\">kliknij</a></p>"_s
                                         << u"kliknij\n"_s;
        QTest::newRow("data link") << u"<p><a href=\"data:text/html,&lt;script&gt;\">tu</a></p>"_s << u"tu\n"_s;
        QTest::newRow("picture linking to itself") << u"<a href=\"duze.jpg\"><img src=\"male.jpg\" alt=\"Żółw\"></a>"_s
                                                   << u"![Żółw](https://przyroda.example/artykuly/male.jpg)\n"_s;
        QTest::newRow("picture linking elsewhere")
                << u"<a href=\"/artykul\"><img src=\"m.jpg\" alt=\"Okładka\"></a>"_s
                << u"[![Okładka](https://przyroda.example/artykuly/m.jpg)](https://przyroda.example/artykul)\n"_s;
        QTest::newRow("youtube player")
                << u"<iframe src=\"https://www.youtube-nocookie.com/embed/AbCdEf12345\" title=\"Film\"></iframe>"_s
                << u"[Film](https://www.youtube.com/watch?v=AbCdEf12345)\n"_s;
        QTest::newRow("video with a source")
                << u"<video controls><source src=\"/media/zolw.mp4\" type=\"video/mp4\">Brak</video>"_s
                << u"[zolw.mp4](https://przyroda.example/media/zolw.mp4)\n"_s;
        QTest::newRow("frame of javascript") << u"<iframe src=\"javascript:alert(1)\"></iframe><p>x</p>"_s << u"x\n"_s;
        QTest::newRow("github code")
                << u"<div class=\"highlight highlight-source-rust notranslate\"><pre>fn main() {}\n</pre></div>"_s
                << u"```rust\nfn main() {}\n```\n"_s;
        QTest::newRow("code language on pre")
                << u"<pre class=\"lang-sql\"><code>SELECT 1;</code></pre>"_s << u"```sql\nSELECT 1;\n```\n"_s;
        QTest::newRow("data-lang") << u"<pre data-lang=\"python\">print('żółw')</pre>"_s
                                   << u"```python\nprint('żółw')\n```\n"_s;
        QTest::newRow("scripts and handlers")
                << u"<p onclick=\"steal()\">Tekst<script>alert(1)</script><style>p{}</style></p>"
                   "<svg><script>alert(2)</script></svg><img src=x onerror=alert(3)>"_s
                << u"Tekst![](https://przyroda.example/artykuly/x)\n"_s;
        QTest::newRow("tokens in the input") << u"<p>a\uE0B00\uE0B1b</p>"_s << u"a0b\n"_s;
        QTest::newRow("formula dollars") << u"<p>Cena $10, a <b>karmy</b> 15$</p>"_s
                                         << u"Cena \\$10, a **karmy** 15$\n"_s;
    }
    void convert()
    {
        QFETCH(QString, html);
        QFETCH(QString, expected);
        QCOMPARE(toMarkdown(html), expected);
    }

    void deepNestingIsQuick()
    {
        const QString html = u"<div>"_s.repeated(20000) + u"głęboko"_s + u"</div>"_s.repeated(20000);
        brulion::test::CpuTimer timer;
        timer.start();
        QCOMPARE(toMarkdown(html), u"głęboko\n"_s);
        QVERIFY2(timer.elapsed() < 5000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(timer.elapsed())));
    }

    void pictureTokens()
    {
        PictureList list;
        const QString a = list.add(u"https://a.example/1.png"_s, u"Jeden\n  obraz"_s);
        QCOMPARE(list.add(u"https://a.example/1.png"_s, u"inny"_s), a);
        QVERIFY(list.add(u"javascript:x"_s, QString()).isEmpty());
        QVERIFY(list.add(u"ftp://a.example/x.png"_s, QString()).isEmpty());
        const QString b = list.add(u"data:image/png;base64,AAAA"_s, QString());
        QCOMPARE(list.pictures().size(), 2);
        QCOMPARE(list.pictures().first().alt, u"Jeden obraz"_s);
        const QString md = u"x "_s + a + u" ["_s + b + u"](https://h.example/) "_s + pictureToken(99) + u" \uE0B0"_s;
        const QString out = replacePictureTokens(
                md, [](int index, bool inLink) { return inLink ? u"LINK%1"_s.arg(index) : u"P%1"_s.arg(index); });
        QCOMPARE(out, u"x P0 LINK1 P99 "_s);
        for (int i = 0; i < MaxPictures + 10; ++i)
            list.add(u"https://a.example/%1.png"_s.arg(i + 2), QString());
        QCOMPARE(list.pictures().size(), MaxPictures);
    }

    void resolve_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("expected");
        QTest::newRow("relative") << u"img/a.png"_s << u"https://przyroda.example/artykuly/img/a.png"_s;
        QTest::newRow("root") << u"/a.png"_s << u"https://przyroda.example/a.png"_s;
        QTest::newRow("protocol-relative") << u"//cdn.example/a.png"_s << u"https://cdn.example/a.png"_s;
        QTest::newRow("absolute") << u"http://inny.example/a.png"_s << u"http://inny.example/a.png"_s;
        QTest::newRow("tab inside") << u"ht\ttps://a.example/x"_s << u"https://a.example/x"_s;
        QTest::newRow("javascript") << u"javascript:alert(1)"_s << QString();
        QTest::newRow("file") << u"file:///etc/passwd"_s << QString();
        QTest::newRow("empty") << QString() << QString();
    }
    void resolve()
    {
        QFETCH(QString, url);
        QFETCH(QString, expected);
        const QUrl r = resolveWebUrl(url, QUrl(u"https://przyroda.example/artykuly/zolw.html"_s));
        QCOMPARE(r.isValid() ? r.toString() : QString(), expected);
    }

    // ---- pictures
    void downloads()
    {
        FileServer server;
        server.files.insert("/a.png", pngBytes());
        server.files.insert("/b.jpg", jpegBytes());
        server.files.insert("/page.html", "<!doctype html><html>Zaloguj się</html>");
        const QList<ClipPicture> pictures{
                {server.url(u"/a.png"_s).toString(), u"A"_s},
                {server.url(u"/b.jpg"_s).toString(), u"B"_s},
                {server.url(u"/brak.png"_s).toString(), QString()},
                {server.url(u"/page.html"_s).toString(), QString()},
                {u"data:image/png;base64,"_s + QString::fromLatin1(pngBytes().toBase64()), QString()},
                {u"data:image/png;base64,!!!"_s, QString()},
                {u"data:image/png;base64,"_s + QString::fromLatin1(QByteArray("not a picture").toBase64()), QString()},
        };
        PictureBatch batch;
        batch.setHostCheck([](const QString &) { return false; }); // 127.0.0.1 as a host of the internet
        QSignalSpy finished(&batch, &PictureBatch::finished);
        QSignalSpy progress(&batch, &PictureBatch::progress);
        batch.start(pictures, PictureLimits());
        QVERIFY(finished.wait(brulion::test::kRoomyMs));
        QCOMPARE(finished.size(), 1);
        QVERIFY(batch.isFinished());
        QStringList errors;
        for (const PictureResult &r : batch.results())
            errors.append(r.error);
        QCOMPARE(errors, (QStringList{QString(), QString(), u"http"_s, u"not_picture"_s, QString(), u"bad_data"_s,
                                      u"not_picture"_s}));
        QCOMPARE(batch.results().at(0).data, pngBytes());
        QCOMPARE(batch.results().at(0).format, u"png"_s);
        QCOMPARE(batch.results().at(1).format, u"jpg"_s);
        QCOMPARE(batch.downloaded(), 3);
        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress.last().at(0).toInt(), int(pictures.size()));
    }

    void downloadLimits()
    {
        FileServer server;
        server.files.insert("/a.png", pngBytes());
        server.files.insert("/b.png", pngBytes() + QByteArray(100, '\0'));
        server.files.insert("/c.png", pngBytes() + QByteArray(1000, '\0'));
        const QList<ClipPicture> three{{server.url(u"/a.png"_s).toString(), {}},
                                       {server.url(u"/b.png"_s).toString(), {}},
                                       {server.url(u"/c.png"_s).toString(), {}}};
        const auto run = [&](const PictureLimits &limits, bool internet) {
            PictureBatch batch;
            if (internet)
                batch.setHostCheck([](const QString &) { return false; });
            QSignalSpy finished(&batch, &PictureBatch::finished);
            batch.start(three, limits);
            if (!finished.wait(brulion::test::kRoomyMs))
                return QStringList{u"no end"_s};
            QStringList errors;
            for (const PictureResult &r : batch.results())
                errors.append(r.error);
            return errors;
        };
        PictureLimits count;
        count.maxCount = 2;
        QCOMPARE(run(count, true), (QStringList{QString(), QString(), u"too_many"_s}));
        PictureLimits size;
        size.maxBytes = 500;
        QCOMPARE(run(size, true), (QStringList{QString(), QString(), u"too_large"_s}));
        PictureLimits total;
        total.maxTotalBytes = 300;
        const QStringList totals = run(total, true);
        QCOMPARE(totals.count(QString()), 2);
        QCOMPARE(totals.count(u"total_size"_s), 1);
        // A page on the internet never makes the computer fetch from its own network …
        QCOMPARE(run(PictureLimits(), false), (QStringList{u"private_host"_s, u"private_host"_s, u"private_host"_s}));
        // … a page there may.
        PictureLimits local;
        local.privateHosts = true;
        QCOMPARE(run(local, false), (QStringList{QString(), QString(), QString()}));
    }

    void downloadTimeout()
    {
        QTcpServer silent; // accepts, never answers
        QVERIFY(silent.listen(QHostAddress::LocalHost));
        PictureBatch batch;
        batch.setHostCheck([](const QString &) { return false; });
        QSignalSpy finished(&batch, &PictureBatch::finished);
        PictureLimits limits;
        limits.timeoutMs = 300;
        batch.start({{u"http://127.0.0.1:%1/x.png"_s.arg(silent.serverPort()), {}}}, limits);
        QVERIFY(finished.wait(brulion::test::kRoomyMs));
        QCOMPARE(batch.results().first().error, u"timeout"_s);
    }

    void nothingToDownload()
    {
        PictureBatch batch;
        QSignalSpy finished(&batch, &PictureBatch::finished);
        batch.start({}, PictureLimits());
        QVERIFY(!batch.isFinished()); // never inside start()
        QVERIFY(finished.wait(brulion::test::kRoomyMs));
        QVERIFY(batch.isFinished());
    }

    void dataPictures()
    {
        QCOMPARE(decodeDataPicture(QString(u"data:image/png;base64,"_s + QString::fromLatin1(pngBytes().toBase64())),
                                   1000),
                 pngBytes());
        QCOMPARE(decodeDataPicture(u"data:image/svg+xml,%3Csvg%3E"_s, 1000), QByteArray("<svg>"));
        QVERIFY(decodeDataPicture(QString(u"data:image/png;base64,"_s + QString(4000, u'A')), 1000)
                        .isEmpty()); // too large
        QVERIFY(decodeDataPicture(u"https://a.example/x.png"_s, 1000).isEmpty());
        QVERIFY(decodeDataPicture(u"data:image/png;base64"_s, 1000).isEmpty());
        QCOMPARE(pictureFileName({u"https://a.example/zdj%C4%99cia/%C5%BC%C3%B3%C5%82w.jpeg"_s, {}}, u"jpg"_s, u"x"_s,
                                 0),
                 u"żółw.jpeg"_s);
        QCOMPARE(pictureFileName({u"data:image/png;base64,AAAA"_s, {}}, u"png"_s, u"Żółw: stepowy"_s, 2),
                 u"Żółw stepowy 2.png"_s);
    }

    // ---- the host's manifests
    void manifests()
    {
        const QJsonObject chromium =
                hostManifest(Family::Chromium, u"/usr/bin/brulion-cli"_s,
                             {u"abcdefghijklmnopabcdefghijklmnop"_s, u"not-an-id"_s, chromeExtensionId()});
        QCOMPARE(chromium.value(u"name"_s).toString(), u"io.github.januszbit.brulion"_s);
        QCOMPARE(chromium.value(u"type"_s).toString(), u"stdio"_s);
        QCOMPARE(chromium.value(u"path"_s).toString(), u"/usr/bin/brulion-cli"_s);
        QCOMPARE(chromium.value(u"allowed_origins"_s).toArray(),
                 (QJsonArray{u"chrome-extension://calcbkckojhmdfecpcohaangneahmkgg/"_s,
                             u"chrome-extension://abcdefghijklmnopabcdefghijklmnop/"_s}));
        QVERIFY(!chromium.contains(u"allowed_extensions"_s));
        const QJsonObject firefox = hostManifest(Family::Firefox, u"C:\\Brulion\\brulion-cli.exe"_s);
        QCOMPARE(firefox.value(u"allowed_extensions"_s).toArray(),
                 QJsonArray{u"brulion-clipper@januszbit.github.io"_s});
        QVERIFY(!firefox.contains(u"allowed_origins"_s));
    }

    void locations_data()
    {
        QTest::addColumn<int>("os");
        QTest::addColumn<int>("scope");
        QTest::addColumn<QString>("browser");
        QTest::addColumn<QString>("manifest");
        QTest::addColumn<QString>("registry");
        QTest::addColumn<QString>("folder");
        const QString file = u"io.github.januszbit.brulion.json"_s;
        const int linux = int(Os::Linux), mac = int(Os::MacOs), win = int(Os::Windows);
        const int user = int(Scope::User), system = int(Scope::System);
        QTest::newRow("linux chrome") << linux << user << u"chrome"_s
                                      << u"/home/ja/.config/google-chrome/NativeMessagingHosts/"_s + file << QString()
                                      << u"/home/ja/.config/google-chrome"_s;
        QTest::newRow("linux chromium xdg")
                << linux << user << u"chromium"_s << u"/home/ja/cfg/chromium/NativeMessagingHosts/"_s + file
                << QString() << u"/home/ja/cfg/chromium"_s;
        QTest::newRow("linux brave") << linux << user << u"brave"_s
                                     << u"/home/ja/.config/BraveSoftware/Brave-Browser/NativeMessagingHosts/"_s + file
                                     << QString() << u"/home/ja/.config/BraveSoftware/Brave-Browser"_s;
        QTest::newRow("linux edge") << linux << user << u"edge"_s
                                    << u"/home/ja/.config/microsoft-edge/NativeMessagingHosts/"_s + file << QString()
                                    << u"/home/ja/.config/microsoft-edge"_s;
        QTest::newRow("linux firefox") << linux << user << u"firefox"_s
                                       << u"/home/ja/.mozilla/native-messaging-hosts/"_s + file << QString()
                                       << u"/home/ja/.mozilla"_s;
        QTest::newRow("linux system chrome")
                << linux << system << u"chrome"_s << u"/etc/opt/chrome/native-messaging-hosts/"_s + file << QString()
                << QString();
        QTest::newRow("linux system chromium")
                << linux << system << u"chromium"_s << u"/etc/chromium/native-messaging-hosts/"_s + file << QString()
                << QString();
        QTest::newRow("linux system edge")
                << linux << system << u"edge"_s << u"/etc/opt/edge/native-messaging-hosts/"_s + file << QString()
                << QString();
        QTest::newRow("linux system firefox")
                << linux << system << u"firefox"_s << u"/usr/lib/mozilla/native-messaging-hosts/"_s + file << QString()
                << QString();
        QTest::newRow("mac chrome") << mac << user << u"chrome"_s
                                    << u"/Users/ja/Library/Application Support/Google/Chrome/NativeMessagingHosts/"_s
                                               + file
                                    << QString() << u"/Users/ja/Library/Application Support/Google/Chrome"_s;
        QTest::newRow("mac firefox") << mac << user << u"firefox"_s
                                     << u"/Users/ja/Library/Application Support/Mozilla/NativeMessagingHosts/"_s + file
                                     << QString() << u"/Users/ja/Library/Application Support/Mozilla"_s;
        QTest::newRow("mac system chrome")
                << mac << system << u"chrome"_s << u"/Library/Google/Chrome/NativeMessagingHosts/"_s + file << QString()
                << QString();
        QTest::newRow("mac system edge") << mac << system << u"edge"_s
                                         << u"/Library/Microsoft/Edge/NativeMessagingHosts/"_s + file << QString()
                                         << QString();
        QTest::newRow("windows chrome")
                << win << user << u"chrome"_s
                << u"C:/Users/ja/AppData/Local/Brulion/NativeMessagingHosts/io.github.januszbit.brulion.chromium.json"_s
                << u"HKEY_CURRENT_USER\\Software\\Google\\Chrome\\NativeMessagingHosts\\io.github.januszbit.brulion"_s
                << u"C:/Users/ja/AppData/Local/Google/Chrome/User Data"_s;
        QTest::newRow("windows edge")
                << win << user << u"edge"_s
                << u"C:/Users/ja/AppData/Local/Brulion/NativeMessagingHosts/io.github.januszbit.brulion.chromium.json"_s
                << u"HKEY_CURRENT_USER\\Software\\Microsoft\\Edge\\NativeMessagingHosts\\io.github.januszbit.brulion"_s
                << u"C:/Users/ja/AppData/Local/Microsoft/Edge/User Data"_s;
        QTest::newRow("windows brave")
                << win << user << u"brave"_s
                << u"C:/Users/ja/AppData/Local/Brulion/NativeMessagingHosts/io.github.januszbit.brulion.chromium.json"_s
                << u"HKEY_CURRENT_USER\\Software\\BraveSoftware\\Brave-Browser\\NativeMessagingHosts\\io.github.januszbit.brulion"_s
                << u"C:/Users/ja/AppData/Local/BraveSoftware/Brave-Browser/User Data"_s;
        QTest::newRow("windows firefox")
                << win << user << u"firefox"_s
                << u"C:/Users/ja/AppData/Local/Brulion/NativeMessagingHosts/io.github.januszbit.brulion.firefox.json"_s
                << u"HKEY_CURRENT_USER\\Software\\Mozilla\\NativeMessagingHosts\\io.github.januszbit.brulion"_s
                << u"C:/Users/ja/AppData/Roaming/Mozilla/Firefox"_s;
        QTest::newRow("windows system chromium")
                << win << system << u"chromium"_s
                << u"C:/ProgramData/Brulion/NativeMessagingHosts/io.github.januszbit.brulion.chromium.json"_s
                << u"HKEY_LOCAL_MACHINE\\Software\\Chromium\\NativeMessagingHosts\\io.github.januszbit.brulion"_s
                << QString();
    }
    void locations()
    {
        QFETCH(int, os);
        QFETCH(int, scope);
        QFETCH(QString, browser);
        QFETCH(QString, manifest);
        QFETCH(QString, registry);
        QFETCH(QString, folder);
        Environment env;
        if (Os(os) == Os::MacOs) {
            env.home = u"/Users/ja"_s;
        } else if (Os(os) == Os::Windows) {
            env.home = u"C:/Users/ja"_s;
            env.localAppData = u"C:/Users/ja/AppData/Local"_s;
            env.appData = u"C:/Users/ja/AppData/Roaming"_s;
            env.programData = u"C:/ProgramData"_s;
        } else {
            env.home = u"/home/ja"_s;
            if (qstrcmp(QTest::currentDataTag(), "linux chromium xdg") == 0)
                env.xdgConfigHome = u"/home/ja/cfg"_s;
        }
        const auto b = parseBrowser(browser);
        QVERIFY(b);
        const HostLocation l = hostLocation(*b, Os(os), Scope(scope), env);
        QCOMPARE(l.manifestPath, manifest);
        QCOMPARE(l.registryKey, registry);
        QCOMPARE(l.browserFolder, folder);
    }

    void hostInstallRoundTrip()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        Environment env;
        env.home = home.path();
        HostSetup setup;
        setup.os = Os::Linux;
        setup.env = env;
        setup.program = QCoreApplication::applicationFilePath();
        // No profile folder: the browser is not there for this user.
        HostState s = hostState(Browser::Chromium, Os::Linux, Scope::User, env, setup.program);
        QVERIFY(!s.browserFound);
        QVERIFY(!s.installed);
        QVERIFY(QDir().mkpath(home.path() + u"/.config/chromium"_s));
        QVERIFY(hostState(Browser::Chromium, Os::Linux, Scope::User, env, setup.program).browserFound);

        // A dry run writes nothing.
        setup.dryRun = true;
        QVERIFY(!installHost(Browser::Chromium, setup).installed);
        QVERIFY(!QFileInfo::exists(s.location.manifestPath));
        setup.dryRun = false;

        s = installHost(Browser::Chromium, setup);
        QVERIFY2(s.error.isEmpty(), qPrintable(s.error));
        QVERIFY(s.installed);
        QVERIFY(s.programExists);
        QVERIFY(s.thisProgram);
        QVERIFY(s.extensionAllowed);
        QCOMPARE(s.program, QDir::toNativeSeparators(setup.program));
        const HostState firefox = installHost(Browser::Firefox, setup);
        QVERIFY(firefox.installed && firefox.extensionAllowed);
        // Another program's manifest is not Brulion's: never counted, never removed.
        const QString foreign = home.path() + u"/.mozilla/native-messaging-hosts/org.example.x.json"_s;
        QFile f(foreign);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"name":"org.example.x","path":"/bin/true"})");
        f.close();
        // A file of Brulion's name that is not its manifest stays too.
        const QString imposter =
                home.path() + u"/.config/google-chrome/NativeMessagingHosts/io.github.januszbit.brulion.json"_s;
        QVERIFY(QDir().mkpath(QFileInfo(imposter).absolutePath()));
        QFile g(imposter);
        QVERIFY(g.open(QIODevice::WriteOnly));
        g.write(R"({"name":"someone.else"})");
        g.close();
        QVERIFY(!hostState(Browser::Chrome, Os::Linux, Scope::User, env, setup.program).installed);

        QVERIFY(!uninstallHost(Browser::Chromium, setup).installed);
        QVERIFY(!uninstallHost(Browser::Firefox, setup).installed);
        QVERIFY(uninstallHost(Browser::Chrome, setup).error.isEmpty());
        QVERIFY(!QFileInfo::exists(s.location.manifestPath));
        QVERIFY(!QFileInfo::exists(firefox.location.manifestPath));
        QVERIFY(QFileInfo::exists(foreign));
        QVERIFY(QFileInfo::exists(imposter));

        // A manifest naming a program that is gone.
        setup.program = home.path() + u"/gone/brulion-cli"_s;
        s = installHost(Browser::Chromium, setup);
        QVERIFY(s.installed);
        QVERIFY(!s.programExists);
        QVERIFY(!s.thisProgram);
    }

    void hostProgramOfAnAppImage()
    {
        QTemporaryDir dir;
        const QString appImage = dir.filePath(u"Brulion-x86_64.AppImage"_s);
        QFile f(appImage);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        const QByteArray old = qgetenv("APPIMAGE");
        qputenv("APPIMAGE", appImage.toLocal8Bit());
        QCOMPARE(defaultHostProgram(u"/tmp/.mount_x/usr/bin/brulion"_s), appImage);
        qunsetenv("APPIMAGE");
        QCOMPARE(defaultHostProgram(u"/usr/bin/brulion-cli"_s), u"/usr/bin/brulion-cli"_s);
        if (!old.isEmpty())
            qputenv("APPIMAGE", old);
    }

    void browsers()
    {
        QCOMPARE(allBrowsers().size(), 5);
        for (const Browser b : allBrowsers())
            QCOMPARE(parseBrowser(browserId(b)), std::optional<Browser>(b));
        QCOMPARE(parseBrowser(u"Google-Chrome"_s), std::optional<Browser>(Browser::Chrome));
        QCOMPARE(parseBrowser(u"msedge"_s), std::optional<Browser>(Browser::Edge));
        QVERIFY(!parseBrowser(u"opera"_s));
        QCOMPARE(familyOf(Browser::Brave), Family::Chromium);
        QCOMPARE(familyOf(Browser::Firefox), Family::Firefox);
    }

    void nativeLaunch_data()
    {
        QTest::addColumn<QStringList>("arguments");
        QTest::addColumn<bool>("host");
        QTest::newRow("chrome") << QStringList{u"chrome-extension://calcbkckojhmdfecpcohaangneahmkgg/"_s} << true;
        QTest::newRow("chrome on windows")
                << QStringList{u"chrome-extension://calcbkckojhmdfecpcohaangneahmkgg/"_s, u"--parent-window=1234"_s}
                << true;
        QTest::newRow("firefox")
                << QStringList{u"/home/ja/.mozilla/native-messaging-hosts/io.github.januszbit.brulion.json"_s,
                               u"brulion-clipper@januszbit.github.io"_s}
                << true;
        QTest::newRow("firefox on windows")
                << QStringList{u"C:\\Users\\ja\\AppData\\Local\\Brulion\\NativeMessagingHosts\\io.github.januszbit.brulion.firefox.json"_s,
                               u"brulion-clipper@januszbit.github.io"_s}
                << true;
        QTest::newRow("another extension")
                << QStringList{u"/x/other.json"_s, u"brulion-clipper@januszbit.github.io"_s} << false;
        QTest::newRow("another id") << QStringList{u"/x/io.github.januszbit.brulion.json"_s, u"evil@example.com"_s}
                                    << false;
        QTest::newRow("a command") << QStringList{u"page"_s, u"list"_s} << false;
        QTest::newRow("chrome with more") << QStringList{u"chrome-extension://x/"_s, u"page"_s, u"delete"_s} << false;
        QTest::newRow("nothing") << QStringList{} << false;
    }
    void nativeLaunch()
    {
        QFETCH(QStringList, arguments);
        QFETCH(bool, host);
        QCOMPARE(isNativeMessagingLaunch(arguments), host);
    }
};

QTEST_GUILESS_MAIN(TestClipper)
#include "tst_clipper.moc"
