// LinkPreview (src/app/editor/linkpreview.h) on a FakeEditorHost: what the hover preview of a
// link shows — pages, sections, blocks of a page, vault-wide ((block)) references, a section of
// the note itself —, the "does not exist yet" and "not found" cases, links without a preview
// (tags, web addresses, chips, attachments, drawings), malformed URLs, the escaping of a note's
// own HTML, the deferred first build of a QML-created object, reading the note again, and the
// links followed from the preview (the routes the host is asked to navigate to).
#include "fakeeditorhost.h"
#include "linkpreview.h"

#include <QSignalSpy>
#include <QTest>

using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Budget = u"# Budżet\n\n## Październik\n\n- Czynsz **1800 zł** #opłaty\n\t- [ ] Zapłacić 📅 2026-10-20\n"
                       u"- Jedzenie\n\n## Listopad\n\n- Prezenty\n"_s;

void populate(FakeEditorHost &host)
{
    host.setFile(u"Budżet.md"_s, Budget);
    host.setFile(u"Kontakty.md"_s, u"- Hydraulik: pan Marek ^kont\n\t- 600 100 200\n- Elektryk\n"_s);
    host.setFile(u"Notatka.md"_s, u"- Wstęp [[#Plan]]\n- # Plan\n\t- Zażółć gęślą jaźń\n"_s);
    host.setFile(u"Pusta.md"_s, QString());
    host.setFile(u"Zażółć gęślą jaźń.md"_s, u"Źdźbło łąki — ćma, żółw.\n"_s);
    host.setFile(u"Html.md"_s,
                 u"<script>alert(1)</script>\n\n- <b onclick=\"x()\">pogrubione</b> &amp; <img src=x>\n"_s);
}

QVariantMap previewOf(FakeEditorHost &host, const QString &href, const QString &from = u"Notatka.md"_s)
{
    LinkPreview p;
    p.setHost(&host);
    p.setSourcePath(from);
    p.setHref(href);
    return p.preview();
}

QString allHtml(const QVariantMap &preview)
{
    QString out;
    for (const QVariant &b : preview.value(u"blocks"_s).toList())
        out += b.toMap().value(u"html"_s).toString() + u'\n';
    return out;
}

} // namespace

class TestLinkPreview : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void previewable_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("expected");
        QTest::newRow("page") << u"page:Bud%C5%BCet"_s << true;
        QTest::newRow("section") << u"page:Bud%C5%BCet#Pa%C5%BAdziernik"_s << true;
        QTest::newRow("own section") << u"page:#Plan"_s << true;
        QTest::newRow("block") << u"block:kont"_s << true;
        QTest::newRow("tag") << u"tag:op%C5%82aty"_s << false;
        QTest::newRow("web") << u"ext:https://example.org"_s << false;
        QTest::newRow("task") << u"task:toggle"_s << false;
        QTest::newRow("date chip") << u"date:3-14"_s << false;
        QTest::newRow("day chip") << u"day:0-12"_s << false;
        QTest::newRow("path part") << u"part:0:Firma%2FAdam"_s << false;
        QTest::newRow("empty") << QString() << false;
        QTest::newRow("bare page scheme") << u"page:"_s << false;
        QTest::newRow("bare block scheme") << u"block:"_s << false;
        QTest::newRow("no scheme") << u"Budżet"_s << false;
    }
    void previewable()
    {
        QFETCH(QString, url);
        QFETCH(bool, expected);
        QCOMPARE(LinkPreview::isPreviewable(url), expected);
        LinkPreview p;
        QCOMPARE(p.previewable(url), expected);
    }

    void previews_data()
    {
        QTest::addColumn<QString>("href");
        QTest::addColumn<QString>("from");
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("title");
        QTest::addColumn<bool>("found");
        QTest::addColumn<QString>("message");
        QTest::addColumn<int>("blocks");
        QTest::addColumn<QString>("contains"); // in the blocks' HTML
        QTest::addColumn<QString>("absent");   // nowhere in the blocks' HTML

        QTest::newRow("page without its title heading")
                << u"page:Bud%C5%BCet"_s << u"Notatka.md"_s << u"page"_s << u"Budżet"_s << true << QString() << 6
                << u"<b>1800 zł</b>"_s << u">Budżet<"_s;
        QTest::newRow("section up to the next heading")
                << u"page:Bud%C5%BCet#Pa%C5%BAdziernik"_s << u"Notatka.md"_s << u"heading"_s
                << u"Budżet › Październik"_s << true << QString() << 3 << u"Jedzenie"_s << u"Prezenty"_s;
        QTest::newRow("block of a page with its children")
                << u"page:Kontakty#^kont"_s << u"Notatka.md"_s << u"block"_s << u"Kontakty"_s << true << QString() << 2
                << u"600 100 200"_s << u"Elektryk"_s;
        QTest::newRow("vault-wide block reference") << u"block:kont"_s << u"Budżet.md"_s << u"block"_s << u"Kontakty"_s
                                                    << true << QString() << 2 << u"pan Marek"_s << u"Elektryk"_s;
        QTest::newRow("section of the note itself")
                << u"page:#Plan"_s << u"Notatka.md"_s << u"heading"_s << u"Notatka › Plan"_s << true << QString() << 1
                << u"Zażółć gęślą jaźń"_s << u"Wstęp"_s;
        QTest::newRow("polish page name")
                << u"page:Za%C5%BC%C3%B3%C5%82%C4%87%20g%C4%99%C5%9Bl%C4%85%20ja%C5%BA%C5%84"_s << u"Notatka.md"_s
                << u"page"_s << u"Zażółć gęślą jaźń"_s << true << QString() << 1 << u"Źdźbło łąki"_s << QString();
        QTest::newRow("page not written yet")
                << u"page:Nowy%20pomys%C5%82"_s << u"Notatka.md"_s << u"page"_s << u"Nowy pomysł"_s << false
                << u"missingPage"_s << 0 << QString() << QString();
        QTest::newRow("missing section") << u"page:Bud%C5%BCet#Grudzie%C5%84"_s << u"Notatka.md"_s << u"heading"_s
                                         << u"Budżet › Grudzień"_s << false << u"missingSection"_s << 0 << QString()
                                         << QString();
        QTest::newRow("missing block of a page")
                << u"page:Kontakty#^brak"_s << u"Notatka.md"_s << u"block"_s << u"Kontakty"_s << false
                << u"missingBlock"_s << 0 << QString() << QString();
        QTest::newRow("unknown block reference")
                << u"block:nieznany"_s << u"Notatka.md"_s << u"block"_s << u"((nieznany))"_s << false
                << u"missingBlock"_s << 0 << QString() << QString();
        QTest::newRow("empty page") << u"page:Pusta"_s << u"Notatka.md"_s << u"page"_s << u"Pusta"_s << true
                                    << u"empty"_s << 0 << QString() << QString();
        QTest::newRow("a note's own HTML stays text") << u"page:Html"_s << u"Notatka.md"_s << u"page"_s << u"Html"_s
                                                      << true << QString() << 2 << u"&lt;script&gt;"_s << u"<script"_s;
    }
    void previews()
    {
        QFETCH(QString, href);
        QFETCH(QString, from);
        QFETCH(QString, kind);
        QFETCH(QString, title);
        QFETCH(bool, found);
        QFETCH(QString, message);
        QFETCH(int, blocks);
        QFETCH(QString, contains);
        QFETCH(QString, absent);
        FakeEditorHost host;
        populate(host);
        LinkPreview p;
        p.setHost(&host);
        p.setSourcePath(from);
        p.setHref(href);
        QVERIFY(p.available());
        const QVariantMap preview = p.preview();
        QCOMPARE(preview.value(u"kind"_s).toString(), kind);
        QCOMPARE(preview.value(u"title"_s).toString(), title);
        QCOMPARE(preview.value(u"found"_s).toBool(), found);
        QCOMPARE(preview.value(u"message"_s).toString(), message);
        QCOMPARE(preview.value(u"blocks"_s).toList().size(), blocks);
        const QString html = allHtml(preview);
        if (!contains.isEmpty())
            QVERIFY2(html.contains(contains), qPrintable(html));
        if (!absent.isEmpty())
            QVERIFY2(!html.contains(absent), qPrintable(html));
        // Never an onclick or another attribute a note wrote: the renderer's own markup only.
        QVERIFY(!html.contains(u"<b onclick"_s, Qt::CaseInsensitive));
        QVERIFY(!html.contains(u"<img"_s, Qt::CaseInsensitive));
    }

    void blocksCarryWhereTheyAre()
    {
        FakeEditorHost host;
        populate(host);
        const QVariantList blocks = previewOf(host, u"page:Bud%C5%BCet#Pa%C5%BAdziernik"_s).value(u"blocks"_s).toList();
        QCOMPARE(blocks.size(), 3);
        // Their file and line (a link inside resolves from there), depth and task status.
        QCOMPARE(blocks[1].toMap().value(u"path"_s).toString(), u"Budżet.md"_s);
        QCOMPARE(blocks[1].toMap().value(u"line"_s).toInt(), 5);
        QCOMPARE(blocks[1].toMap().value(u"depth"_s).toInt(), 1);
        QCOMPARE(blocks[1].toMap().value(u"task"_s).toString(), u"todo"_s);
        // The task's marker is the checkbox the view draws, its date a chip.
        QVERIFY(!blocks[1].toMap().value(u"html"_s).toString().contains(u"[ ]"_s));
        QVERIFY(blocks[1].toMap().value(u"html"_s).toString().contains(u"background-color:"_s));
    }

    void noPreview_data()
    {
        QTest::addColumn<QString>("href");
        QTest::newRow("tag") << u"tag:op%C5%82aty"_s;
        QTest::newRow("web address") << u"ext:https://example.org/strona"_s;
        QTest::newRow("task checkbox") << u"task:toggle"_s;
        QTest::newRow("reminder") << u"reminder:3-20"_s;
        QTest::newRow("priority chip") << u"prio:4-6"_s;
        QTest::newRow("path part") << u"part:0:Firma%2FAdam"_s;
        QTest::newRow("picture") << u"page:zdj%C4%99cie.png"_s;
        QTest::newRow("pdf with a page") << u"page:wyk%C5%82ad.pdf#page=3"_s;
        QTest::newRow("bare page scheme") << u"page:"_s;
        QTest::newRow("only a hash") << u"page:#"_s;
        QTest::newRow("only a caret") << u"page:#^"_s;
        QTest::newRow("blank block id") << u"block:%20"_s;
        QTest::newRow("garbage") << u"\u0000page:x"_s;
    }
    void noPreview()
    {
        QFETCH(QString, href);
        FakeEditorHost host;
        populate(host);
        host.setFile(u"wykład.pdf"_s, u"%PDF-1.4"_s);
        LinkPreview p;
        p.setHost(&host);
        p.setSourcePath(u"Notatka.md"_s);
        p.setHref(href);
        QVERIFY(!p.available());
        QVERIFY(p.preview().isEmpty());
    }

    void drawingPageHasNoPreview()
    {
        // A host that tells drawings apart (the shell's Workspace reads the file's start).
        struct DrawingHost : FakeEditorHost {
            bool isDrawing(const QString &path) const override { return path.endsWith(u".excalidraw.md"_s); }
        } host;
        populate(host);
        host.setFile(u"Plan mieszkania.excalidraw.md"_s, u"---\nexcalidraw-plugin: parsed\n---\n# Excalidraw Data\n"_s);
        QVERIFY(previewOf(host, u"page:Plan%20mieszkania.excalidraw"_s).isEmpty());
        QVERIFY(!previewOf(host, u"page:Kontakty"_s).isEmpty()); // (a note still has one)
    }

    void malformedPercentEncodingIsHarmless()
    {
        FakeEditorHost host;
        populate(host);
        // A broken escape is taken as it is: a page with that name does not exist yet.
        const QVariantMap p = previewOf(host, u"page:Bud%ZZet"_s);
        QVERIFY(!p.isEmpty());
        QVERIFY(!p.value(u"found"_s).toBool());
        QCOMPARE(p.value(u"message"_s).toString(), u"missingPage"_s);
    }

    void noHostNoPreview()
    {
        LinkPreview p;
        p.setHref(u"page:Bud%C5%BCet"_s);
        QVERIFY(!p.available());
        FakeEditorHost host;
        populate(host);
        p.setHost(&host);
        QVERIFY(p.available());
        p.setHost(nullptr);
        QVERIFY(!p.available());
    }

    void deferredUntilComplete()
    {
        FakeEditorHost host;
        populate(host);
        LinkPreview p;
        QSignalSpy changed(&p, &LinkPreview::previewChanged);
        p.classBegin();
        p.setHost(&host);
        p.setSourcePath(u"Notatka.md"_s);
        p.setHref(u"block:kont"_s);
        p.setLocale(u"en"_s);
        QVERIFY(!p.available());
        QCOMPARE(changed.count(), 0);
        p.componentComplete();
        QVERIFY(p.available());
        QCOMPARE(changed.count(), 1); // one build, with every property set
    }

    void refreshReadsTheNoteAgain()
    {
        FakeEditorHost host;
        populate(host);
        LinkPreview p;
        p.setHost(&host);
        p.setHref(u"page:Kontakty"_s);
        QVERIFY(allHtml(p.preview()).contains(u"Elektryk"_s));
        host.setFile(u"Kontakty.md"_s, u"- Ślusarz\n"_s);
        QSignalSpy changed(&p, &LinkPreview::previewChanged);
        p.refresh();
        QCOMPARE(changed.count(), 1);
        QVERIFY(allHtml(p.preview()).contains(u"Ślusarz"_s));
        QVERIFY(!allHtml(p.preview()).contains(u"Elektryk"_s));
        p.refresh(); // unchanged: no signal
        QCOMPARE(changed.count(), 1);
    }

    void paletteColoursTheText()
    {
        FakeEditorHost host;
        populate(host);
        LinkPreview p;
        p.setHost(&host);
        p.setHref(u"page:Bud%C5%BCet#Pa%C5%BAdziernik"_s);
        QVERIFY(!allHtml(p.preview()).contains(u"#123456"_s));
        p.setPalette({{u"tagFg"_s, u"#123456"_s}, {u"tagBg"_s, QColor(u"#abcdef"_s)}, {u"link"_s, u"no colour"_s}});
        const QString html = allHtml(p.preview());
        QVERIFY2(html.contains(u"#123456"_s), qPrintable(html));
        QVERIFY2(html.contains(u"#abcdef"_s), qPrintable(html));
    }

    void bigPagesAreCut()
    {
        FakeEditorHost host;
        QString big;
        for (int i = 0; i < 200; ++i)
            big += u"- Wiersz %1 — źdźbło\n"_s.arg(i);
        host.setFile(u"Duża.md"_s, big);
        const QVariantMap p = previewOf(host, u"page:Du%C5%BCa"_s);
        QVERIFY(p.value(u"found"_s).toBool());
        QVERIFY(p.value(u"blocks"_s).toList().size() < 200);
        QCOMPARE(p.value(u"blocks"_s).toList().size() + p.value(u"more"_s).toInt(), 200);
    }

    void openLink_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("from");
        QTest::addColumn<QVariantMap>("route");
        QTest::newRow("page") << u"page:Bud%C5%BCet"_s << u"Notatka.md"_s
                              << QVariantMap{{u"view"_s, u"page"_s},
                                             {u"path"_s, u"Budżet.md"_s},
                                             {u"target"_s, u"Budżet"_s}};
        QTest::newRow("section") << u"page:Bud%C5%BCet#Pa%C5%BAdziernik"_s << u"Notatka.md"_s
                                 << QVariantMap{{u"view"_s, u"page"_s},
                                                {u"path"_s, u"Budżet.md"_s},
                                                {u"target"_s, u"Budżet"_s},
                                                {u"heading"_s, u"Październik"_s}};
        QTest::newRow("block of a page") << u"page:Kontakty#^kont"_s << u"Notatka.md"_s
                                         << QVariantMap{{u"view"_s, u"page"_s},
                                                        {u"path"_s, u"Kontakty.md"_s},
                                                        {u"target"_s, u"Kontakty"_s},
                                                        {u"blockId"_s, u"kont"_s}};
        QTest::newRow("own section") << u"page:#Plan"_s << u"Notatka.md"_s
                                     << QVariantMap{{u"view"_s, u"page"_s},
                                                    {u"path"_s, u"Notatka.md"_s},
                                                    {u"heading"_s, u"Plan"_s}};
        QTest::newRow("new page") << u"page:Nowy%20pomys%C5%82"_s << u"Notatka.md"_s
                                  << QVariantMap{{u"view"_s, u"page"_s}, {u"target"_s, u"Nowy pomysł"_s}};
        QTest::newRow("block reference") << u"block:kont"_s << u"Notatka.md"_s
                                         << QVariantMap{{u"view"_s, u"page"_s}, {u"blockId"_s, u"kont"_s}};
        QTest::newRow("tag") << u"tag:op%C5%82aty"_s << u"Notatka.md"_s
                             << QVariantMap{{u"view"_s, u"tag"_s}, {u"tag"_s, u"opłaty"_s}};
    }
    void openLink()
    {
        QFETCH(QString, url);
        QFETCH(QString, from);
        QFETCH(QVariantMap, route);
        FakeEditorHost host;
        populate(host);
        LinkPreview p;
        p.setHost(&host);
        QSignalSpy navigated(&host, &FakeEditorHost::navigated);
        QVERIFY(p.openLink(url, from, false));
        QCOMPARE(navigated.count(), 1);
        QCOMPARE(host.lastRoute(), route);
    }

    void linksNotFollowed_data()
    {
        QTest::addColumn<QString>("url");
        QTest::newRow("task") << u"task:toggle"_s;
        QTest::newRow("reminder") << u"reminder:3-20"_s;
        QTest::newRow("date chip") << u"date:3-14"_s;
        QTest::newRow("no scheme") << u"Budżet"_s;
        QTest::newRow("own section of no note") << u"page:#Plan"_s;
        QTest::newRow("malformed part") << u"part:x"_s;
    }
    void linksNotFollowed()
    {
        QFETCH(QString, url);
        FakeEditorHost host;
        populate(host);
        LinkPreview p;
        p.setHost(&host);
        QSignalSpy navigated(&host, &FakeEditorHost::navigated);
        QVERIFY(!p.openLink(url, QString(), false));
        QCOMPARE(navigated.count(), 0);
        LinkPreview noHost;
        QVERIFY(!noHost.openLink(u"page:Bud%C5%BCet"_s, QString(), false));
    }
};

QTEST_MAIN(TestLinkPreview)
#include "tst_editor_linkpreview.moc"
