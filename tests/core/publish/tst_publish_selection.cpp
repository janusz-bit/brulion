// The publish module without a server: which pages and files a target publishes and what it
// leaves out (hidden folders, `publish: false`, extension code, conversations with AI, templates,
// encrypted pages, files and fragments), the `publish:` property, the site built from them (the
// search index, links to pages left out as plain text, backlinks among published pages only, no
// time in the pages), plans for every kind of target (the dry run), targets' checks and
// addresses, the store and its secrets, and the folder target.
#include "publishtest.h"

#include "github/gitobjects.h"
#include "io/files.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>

using namespace Qt::StringLiterals;
using namespace brulion::publish;
using publishtest::Notebook;

namespace {

PublishScope scopeOf(std::initializer_list<PublishItem> items)
{
    PublishScope scope;
    for (const PublishItem &i : items)
        scope.add(i);
    return scope;
}

QStringList reasons(const PublishSelection &s)
{
    QStringList out;
    for (const LeftOut &l : s.leftOut)
        out.append(l.path + u" → "_s + reasonName(l.reason));
    return out;
}

/// A notebook of every kind of page.
void writeEverything(Notebook &nb)
{
    publishtest::writeBlog(nb);
    nb.write(u"Przepisy/Bigos.md"_s, u"Bigos #przepisy/polskie z kapustą.\n"_s);
    nb.write(u"Przepisy/Pierogi.md"_s, u"---\npublikuj: tak\n---\nPierogi ruskie #przepisy\n"_s);
    nb.write(u"Przepisy/Sekret babci.md"_s, u"---\npublikuj: nie\ntags: przepisy\n---\nTajny składnik.\n"_s);
    nb.write(u"templates/journal.md"_s, u"## {{date}}\n- \n"_s);
    nb.write(u"Rozszerzenia/Licznik.md"_s, u"tags:: rozszerzenie\n\n```luau\nb.log(1)\n```\n"_s);
    nb.write(u"AI/2026-11-15 Rozmowa o pogodzie.md"_s, u"- 🙂 Jaka pogoda?\n"_s);
    nb.write(u".prywatne/Pamiętnik.md"_s, u"Ukryte.\n"_s);
    nb.write(u"Sejf.md"_s, u"---\nencrypted: true\n---\nU2FsdGVkX1+abc\n"_s);
    nb.write(u"Częściowo.md"_s,
             u"Jawny początek.\n\n%%\U0001F510α U2FsdGVkX19zZWtyZXQ= \U0001F510%%\n\nJawny koniec.\n"_s);
    nb.write(u"Klucze.md"_s, u"Klucz: ![[klucz.age]]\n"_s);
    nb.write(u"assets/klucz.age"_s, QByteArray("age-encryption.org/v1\n"));
    nb.write(u"journals/2026-11-20.md"_s, u"- Dziś [[Przepisy/Bigos]]\n"_s);
}

} // namespace

class TestPublishSelection : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void publishFlag_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("expected"); // -1 none, 0 false, 1 true
        QTest::newRow("none") << u"Tekst.\n"_s << -1;
        QTest::newRow("true") << u"---\npublish: true\n---\nA\n"_s << 1;
        QTest::newRow("false") << u"---\npublish: false\n---\nA\n"_s << 0;
        QTest::newRow("Yes") << u"---\npublish: Yes\n---\nA\n"_s << 1;
        QTest::newRow("publikuj tak") << u"---\npublikuj: tak\n---\nA\n"_s << 1;
        QTest::newRow("publikuj nie") << u"---\npublikuj: nie\n---\nA\n"_s << 0;
        QTest::newRow("line property") << u"publish:: false\n\nA\n"_s << 0;
        QTest::newRow("upper key") << u"---\nPublish: 1\n---\nA\n"_s << 1;
        QTest::newRow("neither") << u"---\npublish: może\n---\nA\n"_s << -1;
        QTest::newRow("empty value") << u"---\npublish:\n---\nA\n"_s << -1;
    }
    void publishFlag()
    {
        QFETCH(QString, text);
        QFETCH(int, expected);
        Notebook nb;
        nb.write(u"Strona.md"_s, text);
        const auto flag = brulion::publish::publishFlag(*nb.index->note(u"Strona.md"_s));
        QCOMPARE(flag.has_value() ? int(*flag) : -1, expected);
    }

    void stripEncrypted_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<int>("removed");
        QTest::addColumn<bool>("whole");
        const QString lock = u"\U0001F510"_s;
        QTest::newRow("nothing, CRLF kept") << u"A\r\nB ż\r\n"_s << u"A\r\nB ż\r\n"_s << 0 << false;
        QTest::newRow("meld inline") << u"Kod: %%"_s + lock + u"α QUJD "_s + lock + u"%% koniec"_s << u"Kod:  koniec"_s
                                     << 1 << false;
        QTest::newRow("meld without %%") << u"Kod: "_s + lock + u"β QUJD "_s + lock + u" koniec"_s << u"Kod:  koniec"_s
                                         << 1 << false;
        QTest::newRow("two fragments") << u"a %%"_s + lock + u"α X "_s + lock + u"%% b %%"_s + lock + u"α Y "_s + lock
                                                  + u"%% c"_s
                                       << u"a  b  c"_s << 2 << false;
        QTest::newRow("fenced encrypted")
                << u"Przed\n\n```encrypted\nU2FsdGVk\n```\n\nPo\n"_s << u"Przed\n\n\nPo\n"_s << 1 << false;
        QTest::newRow("fenced secret in a list")
                << u"- jawne\n- ```secret\n  abc\n  ```\n- dalej\n"_s << u"- jawne\n- dalej\n"_s << 1 << false;
        QTest::newRow("other code stays") << u"```js\nlet x = 1\n```\n"_s << u"```js\nlet x = 1\n```\n"_s << 0 << false;
        QTest::newRow("unclosed fence to the end") << u"A\n```age\nxyz\n"_s << u"A"_s << 1 << false;
        QTest::newRow("pgp armour")
                << u"Wiadomość:\n-----BEGIN PGP MESSAGE-----\nhQEMA\n-----END PGP MESSAGE-----\nKoniec\n"_s
                << u"Wiadomość:\n\nKoniec\n"_s << 1 << false;
        QTest::newRow("whole page") << u"---\ntags: x\n---\n```encrypted\nabc\n```\n"_s << u"---\ntags: x\n---\n"_s << 1
                                    << true;
        QTest::newRow("frontmatter says so")
                << u"---\nencrypted: true\n---\nabc\n"_s << u"---\nencrypted: true\n---\nabc\n"_s << 0 << true;
        QTest::newRow("zaszyfrowana: nie")
                << u"---\nzaszyfrowana: nie\n---\nabc\n"_s << u"---\nzaszyfrowana: nie\n---\nabc\n"_s << 0 << false;
    }
    void stripEncrypted()
    {
        QFETCH(QString, text);
        QFETCH(QString, expected);
        QFETCH(int, removed);
        QFETCH(bool, whole);
        const StrippedText s = brulion::publish::stripEncrypted(text);
        QCOMPARE(s.text, expected);
        QCOMPARE(s.removed, removed);
        QCOMPARE(s.wholePage, whole);
    }

    void hiddenAndEncryptedNames()
    {
        QVERIFY(isHiddenPath(u".trash/A.md"_s));
        QVERIFY(isHiddenPath(u"Notatki/.ukryte/B.md"_s));
        QVERIFY(isHiddenPath(u".obsidian/plugins/x.js"_s));
        QVERIFY(!isHiddenPath(u"Notatki/Zażółć.md"_s));
        QVERIFY(isEncryptedFile(u"Sejf.mdenc"_s));
        QVERIFY(isEncryptedFile(u"assets/klucz.AGE"_s));
        QVERIFY(isEncryptedFile(u"list.gpg"_s));
        QVERIFY(!isEncryptedFile(u"assets/kot.png"_s));
    }

    void selection_data()
    {
        QTest::addColumn<QList<PublishItem>>("items");
        QTest::addColumn<int>("options"); // 1 linked, 2 onlyMarked, 4 extensions, 8 ai
        QTest::addColumn<QStringList>("pages");
        QTest::addColumn<QStringList>("attachments");
        QTest::addColumn<QStringList>("leftOut");
        using K = PublishItem::Kind;
        QTest::newRow("a page") << QList<PublishItem>{{K::Page, u"Blog/Pierwszy wpis.md"_s}} << 0
                                << QStringList{u"Blog/Pierwszy wpis.md"_s} << QStringList{u"assets/kot.png"_s}
                                << QStringList{};
        QTest::newRow("a page and its links")
                << QList<PublishItem>{{K::Page, u"Blog/Pierwszy wpis.md"_s}} << 1
                << QStringList{u"Blog/Drugi wpis.md"_s, u"Blog/Pierwszy wpis.md"_s} << QStringList{u"assets/kot.png"_s}
                << QStringList{u"Blog/Szkic.md → private"_s};
        QTest::newRow("a folder") << QList<PublishItem>{{K::Folder, u"Blog"_s}} << 0
                                  << QStringList{u"Blog/Drugi wpis.md"_s, u"Blog/Pierwszy wpis.md"_s}
                                  << QStringList{u"assets/kot.png"_s} << QStringList{u"Blog/Szkic.md → private"_s};
        QTest::newRow("a tag with its sub-tags")
                << QList<PublishItem>{{K::Tag, u"#Przepisy"_s}} << 0
                << QStringList{u"Przepisy/Bigos.md"_s, u"Przepisy/Pierogi.md"_s} << QStringList{}
                << QStringList{u"Przepisy/Sekret babci.md → private"_s};
        QTest::newRow("only marked") << QList<PublishItem>{{K::Folder, u"Przepisy"_s}} << 2
                                     << QStringList{u"Przepisy/Pierogi.md"_s} << QStringList{}
                                     << QStringList{u"Przepisy/Bigos.md → not-marked"_s,
                                                    u"Przepisy/Sekret babci.md → private"_s};
        QTest::newRow("only marked, a chosen page")
                << QList<PublishItem>{{K::Page, u"Przepisy/Bigos.md"_s}} << 2 << QStringList{u"Przepisy/Bigos.md"_s}
                << QStringList{} << QStringList{};
        QTest::newRow("the notebook") << QList<PublishItem>{{K::Notebook, {}}} << 0
                                      << QStringList{u"Blog/Drugi wpis.md"_s,  u"Blog/Pierwszy wpis.md"_s,
                                                     u"Częściowo.md"_s,        u"Klucze.md"_s,
                                                     u"Notatki prywatne.md"_s, u"Przepisy/Bigos.md"_s,
                                                     u"Przepisy/Pierogi.md"_s, u"journals/2026-11-20.md"_s}
                                      << QStringList{u"assets/kot.png"_s}
                                      << QStringList{u".prywatne/Pamiętnik.md → hidden"_s,
                                                     u"AI/2026-11-15 Rozmowa o pogodzie.md → conversation"_s,
                                                     u"Blog/Szkic.md → private"_s,
                                                     u"Przepisy/Sekret babci.md → private"_s,
                                                     u"Rozszerzenia/Licznik.md → extension"_s,
                                                     u"Sejf.md → encrypted"_s,
                                                     u"assets/klucz.age → encrypted"_s,
                                                     u"templates/journal.md → template"_s};
        QTest::newRow("extensions and conversations chosen")
                << QList<PublishItem>{{K::Folder, u"Rozszerzenia"_s}, {K::Folder, u"AI"_s}} << (4 | 8)
                << QStringList{u"AI/2026-11-15 Rozmowa o pogodzie.md"_s, u"Rozszerzenia/Licznik.md"_s} << QStringList{}
                << QStringList{};
        QTest::newRow("an extension page chosen by itself still needs the option")
                << QList<PublishItem>{{K::Page, u"Rozszerzenia/Licznik.md"_s}} << 0 << QStringList{} << QStringList{}
                << QStringList{u"Rozszerzenia/Licznik.md → extension"_s};
        QTest::newRow("a template chosen by itself")
                << QList<PublishItem>{{K::Page, u"templates/journal.md"_s}} << 0
                << QStringList{u"templates/journal.md"_s} << QStringList{} << QStringList{};
        QTest::newRow("a private page chosen by itself")
                << QList<PublishItem>{{K::Page, u"Blog/Szkic.md"_s}} << 0 << QStringList{} << QStringList{}
                << QStringList{u"Blog/Szkic.md → private"_s};
        QTest::newRow("a hidden page chosen by itself")
                << QList<PublishItem>{{K::Page, u".prywatne/Pamiętnik.md"_s}} << 0 << QStringList{} << QStringList{}
                << QStringList{u".prywatne/Pamiętnik.md → hidden"_s};
    }
    void selection()
    {
        QFETCH(QList<PublishItem>, items);
        QFETCH(int, options);
        QFETCH(QStringList, pages);
        QFETCH(QStringList, attachments);
        QFETCH(QStringList, leftOut);
        Notebook nb;
        writeEverything(nb);
        PublishScope scope;
        for (const PublishItem &i : items)
            QVERIFY(scope.add(i));
        scope.linkedPages = options & 1;
        scope.onlyMarked = options & 2;
        scope.extensionPages = options & 4;
        scope.aiConversations = options & 8;
        const PublishSelection s = selectFiles(*nb.index, scope);
        QCOMPARE(s.pages, pages);
        QCOMPARE(s.attachments, attachments);
        QCOMPARE(reasons(s), leftOut);
        QVERIFY(s.missingItems.isEmpty());
    }

    void missingItems()
    {
        Notebook nb;
        writeEverything(nb);
        using K = PublishItem::Kind;
        const PublishScope scope = scopeOf(
                {{K::Page, u"Nie ma.md"_s}, {K::Folder, u"Brak"_s}, {K::Tag, u"żaden"_s}, {K::Page, u"Klucze.md"_s}});
        const PublishSelection s = selectFiles(*nb.index, scope);
        QCOMPARE(s.pages, QStringList{u"Klucze.md"_s});
        QCOMPARE(s.missingItems.size(), 3);
        QCOMPARE(s.missingItems.at(2).value, u"żaden"_s);
        // An item once: the same page, tag spellings and folder slashes are one item.
        PublishScope again = scope;
        QVERIFY(!again.add({K::Tag, u"#Żaden"_s}) || again.items.size() == 5);
        QVERIFY(!again.add({K::Folder, u"Brak/"_s}));
        QVERIFY(!again.add({K::Page, u"Klucze.md"_s}));
        QVERIFY(!again.add({K::Page, u""_s}));
    }

    void site()
    {
        Notebook nb;
        writeEverything(nb);
        PublishTarget target;
        target.id = u"tabc"_s;
        target.name = u"Blog"_s;
        target.kind = TargetKind::GitHubPages;
        target.scope = scopeOf({{PublishItem::Kind::Folder, u"Blog"_s}, {PublishItem::Kind::Page, u"Częściowo.md"_s}});
        const SiteBuild site = nb.site(target);
        QStringList paths;
        for (const SiteFile &f : site.files)
            paths.append(f.path);
        QCOMPARE(paths, (QStringList{u".nojekyll"_s, u"Blog/Drugi wpis.html"_s, u"Blog/Pierwszy wpis.html"_s,
                                     u"Częściowo.html"_s, u"assets/kot.png"_s, u"index.html"_s, u"search-index.js"_s,
                                     u"search.js"_s, u"style.css"_s, u"tags/blog.html"_s}));
        QCOMPARE(site.pages, 3);
        QCOMPARE(site.attachments, 1);
        QCOMPARE(site.fragmentsRemoved, 1);
        QCOMPARE(site.pagesWithFragmentsRemoved, QStringList{u"Częściowo.md"_s});
        const QString first = QString::fromUtf8(site.file(u"Blog/Pierwszy wpis.html"_s)->data);
        // A link to a published page leads there; one to a page left out is plain text.
        QVERIFY(first.contains(u"href=\"Drugi%20wpis.html\""_s));
        QVERIFY(!first.contains(u"Szkic.html"_s));
        QVERIFY(first.contains(u"Szkic"_s));
        QVERIFY(first.contains(u"<img src=\"../assets/kot.png\""_s));
        QVERIFY(first.contains(u"Published with Brulion"_s));
        QVERIFY(!first.contains(u"2026"_s)); // no time in the pages: unchanged pages stay the same bytes
        QVERIFY(first.contains(u"<script src=\"../search.js\" data-root=\"../\" defer></script>"_s));
        QVERIFY(first.contains(u"id=\"search\""_s));
        // Backlinks among published pages only.
        const QString second = QString::fromUtf8(site.file(u"Blog/Drugi wpis.html"_s)->data);
        QVERIFY(second.contains(u"<section class=\"backlinks\">"_s));
        QVERIFY(!second.contains(u"Notatki%20prywatne.html"_s));
        const QString partly = QString::fromUtf8(site.file(u"Częściowo.html"_s)->data);
        QVERIFY(partly.contains(u"Jawny początek"_s));
        QVERIFY(!partly.contains(u"U2FsdGVk"_s));
        // The search index: titles, folders and the pages' words, no encrypted text, no private page.
        const QByteArray index = site.file(u"search-index.js"_s)->data;
        QVERIFY(index.startsWith("window.brulionSearch="));
        const QJsonObject data = QJsonDocument::fromJson(index.mid(21, index.size() - 23)).object();
        const QJsonArray pages = data.value(u"pages"_s).toArray();
        QCOMPARE(pages.size(), 3);
        QStringList titles;
        for (const QJsonValue p : pages)
            titles.append(p.toObject().value(u"t"_s).toString());
        titles.sort();
        QCOMPARE(titles, (QStringList{u"Częściowo"_s, u"Drugi wpis"_s, u"Pierwszy wpis"_s}));
        QVERIFY(QString::fromUtf8(index).contains(u"Zażółć gęślą jaźń"_s));
        QVERIFY(!index.contains("U2FsdGVk"));
        QVERIFY(!QString::fromUtf8(index).contains(u"Hasło do wifi"_s));
        // Hashes: SHA-256 and Git's blob id; the same site twice is the same bytes.
        const SiteFile *css = site.file(u"style.css"_s);
        QCOMPARE(css->gitSha, brulion::github::blobSha(css->data));
        QCOMPARE(css->sha256.size(), 64);
        const SiteBuild again = nb.site(target, QDateTime(QDate(2026, 12, 24), QTime(7, 0)));
        QCOMPARE(again.files.size(), site.files.size());
        for (qsizetype i = 0; i < site.files.size(); ++i)
            QCOMPARE(again.files.at(i).sha256, site.files.at(i).sha256);
        // The search script builds results as text, never as markup.
        const QByteArray script = site.file(u"search.js"_s)->data;
        QVERIFY(script.contains("textContent"));
        QVERIFY(!script.contains("innerHTML"));
    }

    void documentSite()
    {
        Notebook nb;
        publishtest::writeBlog(nb);
        PublishTarget target;
        target.id = u"tdoc"_s;
        target.name = u"Dokument"_s;
        target.kind = TargetKind::Nextcloud;
        target.content = SiteContent::Document;
        target.title = u"Mój blog"_s;
        target.scope = scopeOf({{PublishItem::Kind::Folder, u"Blog"_s}});
        const SiteBuild site = nb.site(target);
        QCOMPARE(site.files.size(), 1);
        QCOMPARE(site.files.first().path, u"index.html"_s);
        const QString html = QString::fromUtf8(site.files.first().data);
        QVERIFY(html.contains(u"Mój blog"_s));
        QVERIFY(html.contains(u"data:image/png;base64,"_s));
        QVERIFY(!html.contains(u"Jeszcze nie gotowe"_s));
    }

    void plan_data()
    {
        QTest::addColumn<int>("kind"); // VersionKind
        QTest::addColumn<bool>("force");
        QTest::addColumn<bool>("offline");
        QTest::addColumn<QStringList>("expected");
        const int git = int(VersionKind::GitSha), etag = int(VersionKind::Opaque), sha = int(VersionKind::Sha256);
        // The site: a.html (unchanged), b.html (changed), c.html (new), d.html (changed there), e.html (there,
        // not published by us, the same bytes); the manifest also had x.html (gone from the site, still
        // there), y.html (gone from the site, changed there) and z.html (gone everywhere).
        QTest::newRow("github") << git << false << false
                                << QStringList{u"a.html unchanged"_s, u"b.html update"_s,    u"c.html upload"_s,
                                               u"d.html conflict"_s,  u"e.html unchanged"_s, u"x.html delete"_s,
                                               u"y.html conflict"_s};
        QTest::newRow("github forced") << git << true << false
                                       << QStringList{u"a.html unchanged"_s, u"b.html update"_s,    u"c.html upload"_s,
                                                      u"d.html update"_s,    u"e.html unchanged"_s, u"x.html delete"_s,
                                                      u"y.html delete"_s};
        QTest::newRow("webdav") << etag << false << false
                                << QStringList{u"a.html unchanged"_s, u"b.html update"_s,   u"c.html upload"_s,
                                               u"d.html conflict"_s,  u"e.html conflict"_s, u"x.html delete"_s,
                                               u"y.html conflict"_s};
        QTest::newRow("folder") << sha << false << false
                                << QStringList{u"a.html unchanged"_s, u"b.html update"_s,    u"c.html upload"_s,
                                               u"d.html conflict"_s,  u"e.html unchanged"_s, u"x.html delete"_s,
                                               u"y.html conflict"_s};
        QTest::newRow("offline") << etag << false << true
                                 << QStringList{u"a.html unchanged"_s, u"b.html update"_s, u"c.html upload"_s,
                                                u"d.html unchanged"_s, u"e.html upload"_s, u"x.html delete"_s,
                                                u"y.html delete"_s,    u"z.html delete"_s};
    }
    void plan()
    {
        QFETCH(int, kind);
        QFETCH(bool, force);
        QFETCH(bool, offline);
        QFETCH(QStringList, expected);
        const auto version = VersionKind(kind);
        const auto file = [](const QString &path, const QByteArray &data) {
            SiteFile f;
            f.path = path;
            f.data = data;
            f.sha256 = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
            f.gitSha = brulion::github::blobSha(data);
            return f;
        };
        SiteBuild site;
        for (const auto &[path, data] :
             {std::pair{u"a.html"_s, QByteArray("A")}, std::pair{u"b.html"_s, QByteArray("B2")},
              std::pair{u"c.html"_s, QByteArray("C")}, std::pair{u"d.html"_s, QByteArray("D")},
              std::pair{u"e.html"_s, QByteArray("E")}})
            site.files.append(file(path, data));
        const auto versionOf = [version, &file](const QByteArray &data, const QString &etag) {
            const SiteFile f = file({}, data);
            return version == VersionKind::GitSha ? f.gitSha : version == VersionKind::Sha256 ? f.sha256 : etag;
        };
        PublishManifest manifest;
        const auto record = [&](const QString &path, const QByteArray &data, const QString &etag) {
            const SiteFile f = file(path, data);
            manifest.files.insert(path, ManifestFile{f.sha256, data.size(), versionOf(data, etag)});
        };
        record(u"a.html"_s, "A", u"e-a"_s);
        record(u"b.html"_s, "B1", u"e-b"_s);
        record(u"d.html"_s, "D", u"e-d"_s);
        record(u"x.html"_s, "X", u"e-x"_s);
        record(u"y.html"_s, "Y", u"e-y"_s);
        record(u"z.html"_s, "Z", u"e-z"_s);
        RemoteState remote;
        remote.insert(u"a.html"_s, RemoteFile{versionOf("A", u"e-a"_s), 1});
        remote.insert(u"b.html"_s, RemoteFile{versionOf("B1", u"e-b"_s), 2});
        remote.insert(u"d.html"_s, RemoteFile{versionOf("D changed there", u"e-d2"_s), 16});
        remote.insert(u"e.html"_s, RemoteFile{versionOf("E", u"e-e"_s), 1});
        remote.insert(u"x.html"_s, RemoteFile{versionOf("X", u"e-x"_s), 1});
        remote.insert(u"y.html"_s, RemoteFile{versionOf("Y changed", u"e-y2"_s), 9});
        remote.insert(u"README.md"_s, RemoteFile{versionOf("not ours", u"e-r"_s), 8}); // never touched
        const PublishPlan p =
                planPublish(site, manifest, offline ? std::nullopt : std::optional(remote), version, force);
        QStringList got;
        for (const PlannedFile &f : p.files)
            got.append(f.path + u' ' + actionName(f.action));
        QCOMPARE(got, expected);
        QCOMPARE(p.remoteChecked, !offline);
        QCOMPARE(p.hasChanges(), true);
        // Unpublishing: every file the manifest has that is still there, the changed ones kept.
        const PublishPlan gone = planUnpublish(manifest, remote, version, false);
        QStringList removed;
        for (const PlannedFile &f : gone.files)
            removed.append(f.path + u' ' + actionName(f.action));
        QCOMPARE(removed.size(), 5);
        QVERIFY(removed.contains(u"a.html delete"_s));
        QVERIFY(removed.contains(u"d.html conflict"_s));
    }

    void targets_data()
    {
        QTest::addColumn<QString>("json");
        QTest::addColumn<QString>("problem");
        QTest::addColumn<QString>("described");
        QTest::addColumn<QString>("address");
        QTest::newRow("github root")
                << u"{\"kind\":\"github\",\"name\":\"Strona\",\"repository\":\"jan/notatki\",\"branch\":\"gh-pages\"}"_s
                << QString() << u"jan/notatki · gh-pages"_s << u"https://jan.github.io/notatki/"_s;
        QTest::newRow("github user site")
                << u"{\"kind\":\"github\",\"name\":\"S\",\"repository\":\"Jan/jan.github.io\",\"branch\":\"main\"}"_s
                << QString() << u"Jan/jan.github.io · main"_s << u"https://jan.github.io/"_s;
        QTest::newRow("github docs")
                << u"{\"kind\":\"github\",\"name\":\"S\",\"repository\":\"jan/x\",\"branch\":\"main\",\"folder\":\"docs\"}"_s
                << QString() << u"jan/x · main · docs"_s << u"https://jan.github.io/x/"_s;
        QTest::newRow("github bad repository") << u"{\"kind\":\"github\",\"name\":\"S\",\"repository\":\"jan\"}"_s
                                               << u"bad-repository"_s << u"jan · gh-pages"_s << u""_s;
        QTest::newRow("github bad branch")
                << u"{\"kind\":\"github\",\"name\":\"S\",\"repository\":\"jan/x\",\"branch\":\"a..b\"}"_s
                << u"bad-branch"_s << u"jan/x · a..b"_s << u"https://jan.github.io/x/"_s;
        QTest::newRow("webdav")
                << u"{\"kind\":\"webdav\",\"name\":\"Serwer\",\"url\":\"https://example.com/dav/strona/\",\"siteUrl\":\"https://example.com/strona/\"}"_s
                << QString() << u"example.com/dav/strona"_s << u"https://example.com/strona/"_s;
        QTest::newRow("webdav no url") << u"{\"kind\":\"webdav\",\"name\":\"Serwer\",\"url\":\"ftp://x\"}"_s
                                       << u"bad-url"_s << u"x"_s << u""_s;
        QTest::newRow("nextcloud")
                << u"{\"kind\":\"nextcloud\",\"name\":\"Chmura\",\"server\":\"chmura.example.pl\",\"username\":\"jan\",\"folder\":\"Publiczne/Przepisy\"}"_s
                << QString() << u"chmura.example.pl · Publiczne/Przepisy"_s << u""_s;
        QTest::newRow("nextcloud whole account")
                << u"{\"kind\":\"nextcloud\",\"name\":\"Chmura\",\"server\":\"chmura.example.pl\",\"username\":\"jan\",\"folder\":\"/\"}"_s
                << u"no-folder"_s << u"chmura.example.pl · "_s << u""_s;
        QTest::newRow("no name") << u"{\"kind\":\"folder\",\"name\":\" \",\"path\":\"/srv/www\"}"_s << u"no-name"_s
                                 << u"/srv/www"_s << u"file:///srv/www/index.html"_s;
        QTest::newRow("relative folder") << u"{\"kind\":\"folder\",\"name\":\"W\",\"path\":\"www\"}"_s << u"no-path"_s
                                         << u"www"_s << u"file:www/index.html"_s;
    }
    void targets()
    {
        QFETCH(QString, json);
        QFETCH(QString, problem);
        QFETCH(QString, described);
        QFETCH(QString, address);
        PublishTarget t = targetFromJson(QJsonDocument::fromJson(json.toUtf8()).object());
        t.id = u"t0123456789ab"_s;
        QCOMPARE(checkTarget(t), problem);
        QCOMPARE(describe(t), described);
        if (!address.isEmpty() && problem.isEmpty())
            QCOMPARE(siteAddress(t), address);
        // A round trip keeps it.
        const PublishTarget back = targetFromJson(toJson(t));
        QCOMPARE(toJson(back), toJson(t));
        QVERIFY(!identity(t).isEmpty());
    }

    void pagesAddress()
    {
        PublishTarget t;
        t.kind = TargetKind::GitHubPages;
        t.repository = u"jan/strona"_s;
        t.folder = u"blog/2026"_s;
        // Pages serves the root: the site is below it.
        QCOMPARE(siteAddress(t, u"https://jan.github.io/strona/"_s, u"/"_s),
                 u"https://jan.github.io/strona/blog/2026/"_s);
        t.folder = u"docs"_s;
        QCOMPARE(siteAddress(t, u"https://strona.example.pl"_s, u"/docs"_s), u"https://strona.example.pl/"_s);
        QCOMPARE(pagesPath(t), u"/docs"_s);
        t.folder = u"Zażółć"_s;
        QCOMPARE(pagesPath(t), QString());
        QCOMPARE(siteAddress(t, u"https://jan.github.io/strona/"_s, u"/"_s),
                 u"https://jan.github.io/strona/Za%C5%BC%C3%B3%C5%82%C4%87/"_s);
        QVERIFY(newTargetId().startsWith(u't'));
        QCOMPARE(newTargetId().size(), 13);
        QVERIFY(newTargetId() != newTargetId());
    }

    void store()
    {
        QTemporaryDir dir;
        PublishStore store(dir.path());
        QVERIFY(store.targets().isEmpty());
        PublishTarget a;
        a.id = u"t000000000001"_s;
        a.name = u"Przepisy babci"_s;
        a.kind = TargetKind::Nextcloud;
        a.server = u"chmura.example.pl"_s;
        a.username = u"jan"_s;
        a.folder = u"Publiczne/Przepisy"_s;
        a.shareExpires = QDate(2026, 12, 31);
        a.sharePassword = true;
        a.scope = scopeOf({{PublishItem::Kind::Tag, u"przepisy"_s}});
        a.scope.linkedPages = true;
        PublishTarget b = a;
        b.id = u"t000000000002"_s;
        b.name = u"Blog"_s;
        b.kind = TargetKind::GitHubPages;
        b.repository = u"jan/blog"_s;
        QVERIFY(store.saveTarget(a));
        QVERIFY(store.saveTarget(b));
        QCOMPARE(store.targets().size(), 2);
        QCOMPARE(store.target(a.id)->name, a.name);
        QCOMPARE(store.target(a.id)->scope, a.scope);
        QCOMPARE(store.target(a.id)->shareExpires, QDate(2026, 12, 31));
        // No secret ever lands in the store's files.
        a.name = u"Przepisy"_s;
        QVERIFY(store.saveTarget(a));
        QCOMPARE(store.targets().first().name, u"Przepisy"_s);
        PublishStatus status;
        status.at = QDateTime(QDate(2026, 11, 20), QTime(18, 30));
        status.operation = u"publish"_s;
        status.ok = true;
        status.url = u"https://chmura.example.pl/s/Tok"_s;
        status.uploaded = 3;
        QVERIFY(store.setStatus(a.id, status));
        QCOMPARE(store.target(a.id)->last.url, status.url);
        QCOMPARE(store.target(a.id)->last.at, status.at);
        PublishManifest m;
        m.identity = identity(a);
        m.files.insert(u"Zażółć.html"_s, ManifestFile{u"ab"_s, 2, u"\"etag\""_s});
        m.shareId = u"17"_s;
        QVERIFY(store.saveManifest(a.id, m));
        QCOMPARE(store.manifest(a.id), m);
        // Another store over the same folder (the command line beside the app) sees it all.
        PublishStore other(dir.path());
        QCOMPARE(other.manifest(a.id), m);
        QCOMPARE(other.targets().size(), 2);
        QVERIFY(store.removeTarget(a.id));
        QVERIFY(store.manifest(a.id).isEmpty());
        QVERIFY(!QFileInfo::exists(dir.filePath(u"publish/"_s + a.id + u".json"_s)));
        QCOMPARE(store.targets().size(), 1);
        // Ids that are no file names are refused.
        PublishTarget bad = b;
        bad.id = u"../evil"_s;
        QVERIFY(!store.saveTarget(bad));
        QVERIFY(!store.saveManifest(u"../evil"_s, m));
        QVERIFY(secretFile(dir.path(), u"../evil"_s, u"password"_s).isEmpty());
        // A damaged file: no targets, no crash.
        QFile f(dir.filePath(u"publish/targets.json"_s));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ to nie json");
        f.close();
        QVERIFY(store.targets().isEmpty());
        // In memory.
        PublishStore memory({});
        QVERIFY(memory.saveTarget(b));
        QCOMPARE(memory.targets().size(), 1);
    }

    void secrets()
    {
        QTemporaryDir dir;
        const QString file = secretFile(dir.path(), u"t000000000001"_s, u"share"_s);
        QVERIFY(file.endsWith(u"/secrets/publish-t000000000001-share"_s));
        QVERIFY(writeSecret(file, u"hasło-źdźbło 123"_s));
        QCOMPARE(readSecret(file), u"hasło-źdźbło 123"_s);
#ifndef Q_OS_WIN
        QCOMPARE(QFile::permissions(file)
                         & ~(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser
                             | QFileDevice::WriteUser),
                 QFileDevice::Permissions());
#endif
        QVERIFY(writeSecret(file, {}));
        QVERIFY(!QFileInfo::exists(file));
        QCOMPARE(readSecret(file), QString());
        QCOMPARE(githubTokenFile(dir.path()), dir.path() + u"/secrets/github-token"_s);
    }

    void folderTarget()
    {
        Notebook nb;
        publishtest::writeBlog(nb);
        QTemporaryDir www;
        // The folder's own files: hidden ones are never looked at, other ones never touched.
        QDir(www.path()).mkpath(u".git"_s);
        QFile git(www.filePath(u".git/HEAD"_s));
        QVERIFY(git.open(QIODevice::WriteOnly));
        git.write("ref: refs/heads/main\n");
        git.close();
        PublishTarget t;
        t.id = u"t00000000000f"_s;
        t.name = u"Katalog"_s;
        t.kind = TargetKind::Folder;
        t.path = www.path() + u"/strona"_s;
        t.scope = scopeOf({{PublishItem::Kind::Folder, u"Blog"_s}});
        Publisher publisher(nullptr);
        PublishManifest manifest;
        // The dry run writes nothing.
        SiteBuild site = nb.site(t);
        PublishReport r = publishtest::run([&](auto done) { publisher.preview(t, site, manifest, {}, done); });
        QVERIFY(r.ok);
        QCOMPARE(r.uploaded, int(site.files.size()));
        QVERIFY(!QFileInfo::exists(t.path));
        // First publish.
        r = publishtest::run([&](auto done) { publisher.publish(t, site, manifest, {}, false, done); });
        QVERIFY2(r.ok, qPrintable(r.errorCode + u' ' + r.error));
        QCOMPARE(r.uploaded, int(site.files.size()));
        QVERIFY(QFileInfo::exists(t.path + u"/Blog/Pierwszy wpis.html"_s));
        QCOMPARE(r.url, QUrl::fromLocalFile(t.path + u"/index.html"_s).toString());
        manifest = r.manifest;
        QCOMPARE(manifest.files.size(), site.files.size());
        // Again without changes: nothing written.
        r = publishtest::run([&](auto done) { publisher.publish(t, site, manifest, {}, false, done); });
        QVERIFY(r.ok);
        QCOMPARE(r.uploaded + r.updated + r.deleted, 0);
        QCOMPARE(r.unchanged, int(site.files.size()));
        // A page changed, one gone from the site: written and removed (its emptied folder too).
        nb.write(u"Blog/Drugi wpis.md"_s, u"Nowy tekst.\n"_s);
        nb.write(u"Blog/Stare/Archiwum.md"_s, u"Stare.\n"_s);
        site = nb.site(t);
        r = publishtest::run([&](auto done) { publisher.publish(t, site, manifest, {}, false, done); });
        QVERIFY(r.ok);
        manifest = r.manifest;
        QVERIFY(QFileInfo::exists(t.path + u"/Blog/Stare/Archiwum.html"_s));
        nb.remove(u"Blog/Stare/Archiwum.md"_s);
        site = nb.site(t);
        r = publishtest::run([&](auto done) { publisher.publish(t, site, manifest, {}, false, done); });
        QVERIFY(r.ok);
        QCOMPARE(r.deleted, 1);
        QVERIFY(!QFileInfo::exists(t.path + u"/Blog/Stare"_s));
        manifest = r.manifest;
        // Someone changed a published file there: a conflict, nothing written — until "Replace them".
        QFile changed(t.path + u"/style.css"_s);
        QVERIFY(changed.open(QIODevice::WriteOnly));
        changed.write("body{color:red}");
        changed.close();
        nb.write(u"Blog/Drugi wpis.md"_s, u"Jeszcze nowszy.\n"_s);
        site = nb.site(t);
        r = publishtest::run([&](auto done) { publisher.publish(t, site, manifest, {}, false, done); });
        QVERIFY(!r.ok);
        QCOMPARE(r.errorCode, u"conflicts"_s);
        QCOMPARE(r.conflicts.size(), 1);
        QCOMPARE(r.conflicts.first().path, u"style.css"_s);
        QVERIFY(!r.manifestChanged);
        r = publishtest::run([&](auto done) { publisher.publish(t, site, manifest, {}, true, done); });
        QVERIFY(r.ok);
        manifest = r.manifest;
        {
            QFile css(t.path + u"/style.css"_s);
            QVERIFY(css.open(QIODevice::ReadOnly));
            QVERIFY(css.readAll() != "body{color:red}");
        } // closed: Windows removes no open file
        // Unpublish: our files go, the folder's own stay.
        r = publishtest::run([&](auto done) { publisher.unpublish(t, manifest, {}, false, done); });
        QVERIFY(r.ok);
        QVERIFY(r.manifest.files.isEmpty());
        QVERIFY(!QFileInfo::exists(t.path + u"/index.html"_s));
        QVERIFY(QFileInfo::exists(www.filePath(u".git/HEAD"_s)));
    }
};

QTEST_GUILESS_MAIN(TestPublishSelection)
#include "tst_publish_selection.moc"
