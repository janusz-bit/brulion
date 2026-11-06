// The page's look and coloured highlights in the block editor (docs/UI.md "Page icon and cover",
// "Coloured highlights"): PageController.look following the text (also source mode), the icon and
// cover edits (one undo step each, only their lines change, refusals), a cover's file and a cover
// from the internet (nothing fetched before it is allowed), and "Highlight" (setHighlight) on the
// edited text with its undo.
#include "blocklistmodel.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"

#include <QSignalSpy>
#include <QTest>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Page = u"Notatki/Wycieczka.md"_s;

struct Fixture {
    FakeEditorHost host;
    PageController page;

    explicit Fixture(const QString &content)
    {
        host.setFile(Page, content);
        page.setNow(u"2026-10-28 10:00"_s);
        page.setSaveDelay(0);
        page.setHost(&host);
        page.setPath(Page);
    }
    QVariantMap icon() const { return page.look().value(u"icon"_s).toMap(); }
    QVariantMap cover() const { return page.look().value(u"cover"_s).toMap(); }
};

/// A tiny valid PNG (1×1).
QByteArray png()
{
    return QByteArray::fromBase64(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==");
}

} // namespace

class TestEditorLook : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void readsTheLook()
    {
        Fixture f(u"---\nicon: 🚀\nbanner: \"![[góry.png]]\"\nbanner_y: 0.2\n---\n- Zażółć gęślą jaźń\n"_s);
        QCOMPARE(f.icon().value(u"kind"_s).toString(), u"emoji"_s);
        QCOMPARE(f.icon().value(u"name"_s).toString(), u"🚀"_s);
        QCOMPARE(f.cover().value(u"kind"_s).toString(), u"file"_s);
        QCOMPARE(f.cover().value(u"target"_s).toString(), u"góry.png"_s);
        QCOMPARE(f.cover().value(u"position"_s).toDouble(), 0.2);
        // No such file in the notebook (yet).
        QCOMPARE(f.cover().value(u"state"_s).toString(), u"missing"_s);
    }

    void coverFileFollowsTheNotebook()
    {
        Fixture f(u"---\nbanner: \"![[góry.png]]\"\n---\nTekst\n"_s);
        QCOMPARE(f.cover().value(u"state"_s).toString(), u"missing"_s);
        QSignalSpy changed(&f.page, &PageController::lookChanged);
        const QString path = f.host.saveAttachment(u"góry.png"_s, png());
        QVERIFY(!path.isEmpty());
        Q_EMIT f.host.indexChanged();
        QTRY_COMPARE(f.cover().value(u"state"_s).toString(), u"ready"_s);
        QVERIFY(!changed.isEmpty());
        QVERIFY(!f.cover().value(u"url"_s).toString().isEmpty());
    }

    void setAndRemoveTheIcon()
    {
        const QString before = u"---\ntags: [dom]\n---\n- Zażółć gęślą jaźń\r\n"_s;
        Fixture f(before);
        QVariantMap r = f.page.setPageIcon(u"emoji"_s, u"🌳"_s);
        QVERIFY(r.value(u"ok"_s).toBool());
        QCOMPARE(f.page.text(), u"---\ntags: [dom]\nicon: 🌳\n---\n- Zażółć gęślą jaźń\r\n"_s);
        QCOMPARE(f.icon().value(u"name"_s).toString(), u"🌳"_s);
        // An app icon is written as Iconize writes it.
        r = f.page.setPageIcon(u"app"_s, u"book-open"_s);
        QVERIFY(r.value(u"ok"_s).toBool());
        QCOMPARE(f.page.text(), u"---\ntags: [dom]\nicon: LiBookOpen\n---\n- Zażółć gęślą jaźń\r\n"_s);
        // Removed: the text as it was; each edit was one undo step.
        r = f.page.setPageIcon(QString(), QString());
        QVERIFY(r.value(u"ok"_s).toBool());
        QCOMPARE(f.page.text(), before);
        f.page.undo();
        QCOMPARE(f.page.text(), u"---\ntags: [dom]\nicon: LiBookOpen\n---\n- Zażółć gęślą jaźń\r\n"_s);
        f.page.undo();
        QCOMPARE(f.icon().value(u"name"_s).toString(), u"🌳"_s);
        f.page.undo();
        QCOMPARE(f.page.text(), before);
    }

    void removingTakesBannersIconToo()
    {
        Fixture f(u"---\nicon: FasHome\nbanner_icon: 🌳\na: 1\n---\nTekst\n"_s);
        QCOMPARE(f.icon().value(u"name"_s).toString(), u"🌳"_s);
        QVERIFY(f.page.setPageIcon(QString(), QString()).value(u"ok"_s).toBool());
        QCOMPARE(f.page.text(), u"---\na: 1\n---\nTekst\n"_s);
        QCOMPARE(f.icon().value(u"kind"_s).toString(), QString());
    }

    void refusals()
    {
        const QString text = u"---\na: 1\n---\nTekst\n"_s;
        Fixture f(text);
        QVariantMap r = f.page.setPageIcon(u"emoji"_s, u"dom"_s);
        QVERIFY(!r.value(u"ok"_s).toBool());
        QCOMPARE(r.value(u"status"_s).toString(), u"invalidValue"_s);
        QVERIFY(!r.value(u"reason"_s).toString().isEmpty());
        r = f.page.setPageIcon(u"app"_s, u"Zła nazwa"_s);
        QVERIFY(!r.value(u"ok"_s).toBool());
        r = f.page.setPageCover(u"assets/a]]b.png"_s);
        QVERIFY(!r.value(u"ok"_s).toBool());
        QCOMPARE(f.page.text(), text);
        // A frontmatter that cannot be edited safely is left alone.
        Fixture bad(u"---\n- lista\n---\nTekst\n"_s);
        r = bad.page.setPageIcon(u"emoji"_s, u"🚀"_s);
        QVERIFY(!r.value(u"ok"_s).toBool());
        QCOMPARE(bad.page.text(), u"---\n- lista\n---\nTekst\n"_s);
    }

    void logseqPage()
    {
        Fixture f(u"tags:: dom\n\n- blok\n"_s);
        QVERIFY(f.page.setPageIcon(u"emoji"_s, u"📚"_s).value(u"ok"_s).toBool());
        QCOMPARE(f.page.text(), u"tags:: dom\nicon:: 📚\n\n- blok\n"_s);
        QCOMPARE(f.icon().value(u"name"_s).toString(), u"📚"_s);
    }

    void setTheCover()
    {
        Fixture f(u"---\na: 1\n---\nTekst\n"_s);
        const QString path = f.host.saveAttachment(u"Mój kot.png"_s, png());
        Q_EMIT f.host.indexChanged();
        QVERIFY(f.page.setPageCover(path).value(u"ok"_s).toBool());
        QString link = f.host.linkTarget(path, Page);
        if (link.isEmpty())
            link = path;
        QCOMPARE(f.page.text(), u"---\na: 1\nbanner: \"![["_s + link + u"]]\"\n---\nTekst\n"_s);
        QCOMPARE(f.cover().value(u"state"_s).toString(), u"ready"_s);
        // Its position: one line more, two decimals.
        QVERIFY(f.page.setPageCoverPosition(0.333).value(u"ok"_s).toBool());
        QVERIFY(f.page.text().contains(u"\nbanner_y: 0.33\n"_s));
        QCOMPARE(f.cover().value(u"position"_s).toDouble(), 0.33);
        // Removed with its position: the text as it was.
        QVERIFY(f.page.setPageCover(QString()).value(u"ok"_s).toBool());
        QCOMPARE(f.page.text(), u"---\na: 1\n---\nTekst\n"_s);
        QCOMPARE(f.cover().value(u"kind"_s).toString(), QString());
    }

    void webCoverIsFetchedOnlyWhenAllowed()
    {
        const QString url = u"https://example.com/góry.png"_s;
        Fixture f(u"Tekst\n"_s);
        f.host.setWebPicture(url, png());
        QVERIFY(f.page.setPageCover(url).value(u"ok"_s).toBool());
        QCOMPARE(f.page.text(), u"---\nbanner: "_s + url + u"\n---\nTekst\n"_s);
        QCOMPARE(f.cover().value(u"kind"_s).toString(), u"web"_s);
        QCOMPARE(f.cover().value(u"state"_s).toString(), u"ask"_s);
        QCOMPARE(f.cover().value(u"host"_s).toString(), u"example.com"_s);
        QCOMPARE(f.host.webFetches(), 0);
        f.page.showWebPicture(url);
        QTRY_COMPARE(f.cover().value(u"state"_s).toString(), u"ready"_s);
        QVERIFY(!f.cover().value(u"url"_s).toString().isEmpty());
        QCOMPARE(f.host.webFetches(), 1);
    }

    void sourceModeFollows()
    {
        Fixture f(u"Tekst\n"_s);
        f.page.setSourceMode(true);
        QVERIFY(f.page.setPageIcon(u"emoji"_s, u"🌱"_s).value(u"ok"_s).toBool());
        QCOMPARE(f.page.sourceText(), u"---\nicon: 🌱\n---\nTekst\n"_s);
        QCOMPARE(f.icon().value(u"name"_s).toString(), u"🌱"_s);
        // Typed in the source: the look follows when the source is left.
        f.page.setSourceText(u"---\nicon: 🌳\n---\nTekst\n"_s);
        f.page.setSourceMode(false);
        QCOMPARE(f.icon().value(u"name"_s).toString(), u"🌳"_s);
    }

    void highlightTheEditedText()
    {
        Fixture f(u"- Zażółć gęślą jaźń\n"_s);
        f.page.startEditing(0, 0);
        QVariantMap r = f.page.setHighlight(0, 7, 12, u"red"_s);
        QVERIFY(r.value(u"changed"_s).toBool());
        const QString red = u"<mark style=\"background: #FF5582A6;\">"_s;
        QCOMPARE(f.page.text(), u"- Zażółć "_s + red + u"gęślą</mark> jaźń\n"_s);
        QCOMPARE(r.value(u"to"_s).toInt() - r.value(u"from"_s).toInt(), 5);
        // Recoloured to the default: `==`.
        r = f.page.setHighlight(0, r.value(u"from"_s).toInt(), r.value(u"to"_s).toInt(), u"default"_s);
        QVERIFY(r.value(u"changed"_s).toBool());
        QCOMPARE(f.page.text(), u"- Zażółć ==gęślą== jaźń\n"_s);
        // Removed.
        r = f.page.setHighlight(0, 10, 10, QString());
        QVERIFY(r.value(u"changed"_s).toBool());
        QCOMPARE(f.page.text(), u"- Zażółć gęślą jaźń\n"_s);
        // Each one undo step.
        f.page.undo();
        QCOMPARE(f.page.text(), u"- Zażółć ==gęślą== jaźń\n"_s);
        f.page.undo();
        QCOMPARE(f.page.text(), u"- Zażółć "_s + red + u"gęślą</mark> jaźń\n"_s);
        f.page.undo();
        QCOMPARE(f.page.text(), u"- Zażółć gęślą jaźń\n"_s);
        // A selection cutting a highlight's marker: refused, said so.
        const QVariantMap wrapped = f.page.setHighlight(0, 0, 3, u"red"_s);
        QVERIFY(wrapped.value(u"changed"_s).toBool());
        r = f.page.setHighlight(0, wrapped.value(u"from"_s).toInt(), wrapped.value(u"to"_s).toInt(), QString());
        QVERIFY(r.value(u"changed"_s).toBool());
        QCOMPARE(f.page.text(), u"- Zażółć gęślą jaźń\n"_s);
        QVERIFY(f.page.setHighlight(0, 7, 12, u"default"_s).value(u"changed"_s).toBool());
        r = f.page.setHighlight(0, 5, 11, u"red"_s);
        QVERIFY(!r.value(u"changed"_s).toBool());
        QVERIFY(r.value(u"refused"_s).toBool());
        QCOMPARE(f.page.text(), u"- Zażółć ==gęślą== jaźń\n"_s);
        f.page.setHighlight(0, 10, 10, QString());
        // Refused: an unknown colour, a bad row.
        QVERIFY(!f.page.setHighlight(0, 0, 3, u"czerwony"_s).value(u"changed"_s).toBool());
        QVERIFY(!f.page.setHighlight(7, 0, 3, u"red"_s).value(u"changed"_s).toBool());
        QCOMPARE(f.page.text(), u"- Zażółć gęślą jaźń\n"_s);
    }

    void linksShowThePageIcon()
    {
        Fixture f(u"- Zobacz [[Dom]] i [[Inna]]\n"_s);
        f.host.setFile(u"Dom.md"_s, u"---\nicon: 🏠\n---\nTekst\n"_s);
        f.host.setFile(u"Inna.md"_s, u"Tekst\n"_s);
        Q_EMIT f.host.indexChanged();
        BlockListModel *model = f.page.blocks();
        const int htmlRole = model->roleNames().key("html");
        QTRY_VERIFY(model->data(model->index(0), htmlRole).toString().contains(u"🏠</a>&nbsp;"_s));
        QCOMPARE(model->data(model->index(0), htmlRole).toString().count(u"🏠"_s), 1);
    }
};

QTEST_MAIN(TestEditorLook)
#include "tst_editor_look.moc"
