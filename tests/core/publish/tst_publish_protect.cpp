// Password-protected sites (publish/protect.h, docs/UI.md "Publishing" → "Password-protected
// pages"): what the files hold (only ciphertext — no Polish word of the notes, no title, no page or
// file name), the pages opened again by a decryption written here from the scheme in protect.h
// with core crypto's primitives (PBKDF2, HMAC and AES-GCM, checked against the NIST and RFC
// vectors in tst_crypto) — the browser's WebCrypto opens them in tests/web/publish.e2e.mjs —, a
// wrong password, the links between protected pages and their pictures, the one-document site,
// the same bytes for an unchanged page (the incremental publish), the target's salt and the
// secret's file, and a folder target turned protected.
#include "publishtest.h"

#include "crypto/aead.h"
#include "crypto/base64.h"
#include "crypto/hash.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::publish;
using publishtest::Notebook;

namespace {

const QString kPassword = u"Źdźbło trawy na łące"_s;
constexpr quint32 kFastIterations = 1000;

PublishTarget protectedTarget(SiteContent content = SiteContent::Site)
{
    PublishTarget t;
    t.id = u"t0000000000aa"_s;
    t.name = u"Rodzina"_s;
    t.kind = TargetKind::GitHubPages;
    t.repository = u"jan/rodzina"_s;
    t.title = u"Sekretny dziennik Zosi"_s;
    t.content = content;
    t.protect = true;
    t.protectSalt = crypto::encodeBase64(QByteArray("0123456789abcdef"));
    PublishScope scope;
    scope.add({PublishItem::Kind::Folder, u"Blog"_s});
    t.scope = scope;
    return t;
}

SiteBuild buildProtected(const Notebook &nb, const PublishTarget &t, const QString &password = kPassword,
                         quint32 iterations = kFastIterations)
{
    SiteInput input =
            prepareSite(*nb.index, t, nb.reader(), u"Zeszyt"_s, QDateTime(QDate(2026, 11, 23), QTime(9, 0))).input;
    input.protection.password = password;
    input.protection.iterations = iterations;
    return buildSite(input);
}

// ---- The decryption, from the scheme alone -------------------------------------------------------

struct LockPage {
    QJsonObject sealed;
    QString html;
};

std::optional<LockPage> lockPage(const QByteArray &data)
{
    static const QRegularExpression json(
            uR"re(<script type="application/json" id="brulion-sealed">([^<]*)</script>)re"_s);
    const QString html = QString::fromUtf8(data);
    const QRegularExpressionMatch m = json.match(html);
    if (!m.hasMatch())
        return std::nullopt;
    return LockPage{QJsonDocument::fromJson(m.captured(1).toUtf8()).object(), html};
}

QByteArray b64(const QJsonObject &o, const QString &key)
{
    return crypto::decodeBase64(o.value(key).toString()).value_or(QByteArray());
}

/// K of the site whose lock page holds `sealed`.
QByteArray siteKey(const QJsonObject &sealed, const QString &password)
{
    return crypto::pbkdf2(crypto::Hash::Sha256, password.toUtf8(), b64(sealed, u"salt"_s),
                          quint32(sealed.value(u"iterations"_s).toInteger()), 32)
            .toByteArray();
}

std::optional<QByteArray> openSealed(const QByteArray &k, const QByteArray &salt, const QByteArray &iv,
                                     const QByteArray &data)
{
    const QByteArray key = crypto::hmac(crypto::Hash::Sha256, k, salt);
    const auto plain = crypto::aesgcm::open(key, iv, {}, data);
    return plain ? std::optional<QByteArray>(plain->toByteArray()) : std::nullopt;
}

/// The page's HTML, nullopt for a wrong password.
std::optional<QString> openPage(const QByteArray &data, const QString &password)
{
    const auto page = lockPage(data);
    if (!page)
        return std::nullopt;
    const QJsonObject &s = page->sealed;
    const auto plain = openSealed(siteKey(s, password), b64(s, u"page"_s), b64(s, u"iv"_s), b64(s, u"data"_s));
    return plain ? std::optional<QString>(QString::fromUtf8(*plain)) : std::nullopt;
}

struct OpenedFile {
    QJsonObject head;
    QByteArray bytes;
};

std::optional<OpenedFile> openFile(const QByteArray &data, const QByteArray &k)
{
    if (!data.startsWith("BRL1") || data.size() < 4 + 16 + 12 + 16)
        return std::nullopt;
    const auto plain = openSealed(k, data.mid(4, 16), data.mid(20, 12), data.mid(32));
    if (!plain)
        return std::nullopt;
    const qsizetype nl = plain->indexOf('\n');
    return OpenedFile{QJsonDocument::fromJson(plain->left(nl)).object(), plain->mid(nl + 1)};
}

QStringList pathsOf(const SiteBuild &site)
{
    QStringList out;
    for (const SiteFile &f : site.files)
        out.append(f.path);
    return out;
}

/// The page whose decrypted text holds `text`.
const SiteFile *pageWith(const SiteBuild &site, const QString &text)
{
    for (const SiteFile &f : site.files) {
        if (!f.path.endsWith(u".html"_s))
            continue;
        if (const auto html = openPage(f.data, kPassword); html && html->contains(text))
            return &f;
    }
    return nullptr;
}

} // namespace

class TestPublishProtect : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void namesAndPlaintext_data()
    {
        QTest::addColumn<int>("content");
        QTest::newRow("web pages") << int(SiteContent::Site);
        QTest::newRow("one document") << int(SiteContent::Document);
    }

    /// Only ciphertext: no word of the notes, no title, no name of a page, folder, tag or file.
    void namesAndPlaintext()
    {
        QFETCH(int, content);
        Notebook nb;
        publishtest::writeBlog(nb);
        const SiteBuild site = buildProtected(nb, protectedTarget(SiteContent(content)));
        QVERIFY2(site.errorCode.isEmpty(), qPrintable(site.errorCode));
        QVERIFY(site.protectedPages);
        QCOMPARE(site.pages, 2);
        const QStringList paths = pathsOf(site);
        QVERIFY(paths.contains(u"index.html"_s));
        static const QRegularExpression shape(
                uR"(^(index\.html|style\.css|\.nojekyll|p/[0-9a-f]{20}\.html|f/[0-9a-f]{20})$)"_s);
        for (const QString &p : paths)
            QVERIFY2(shape.match(p).hasMatch(), qPrintable(p));
        QVERIFY(!paths.contains(u"search.js"_s));
        QVERIFY(!paths.contains(u"search-index.js"_s));
        if (SiteContent(content) == SiteContent::Site) {
            // index, 2 pages, the tag page, the picture, style.css, .nojekyll
            QCOMPARE(paths.size(), 7);
            QCOMPARE(paths.filter(u"f/"_s).size(), 1);
        } else {
            QCOMPARE(paths, QStringList{u"index.html"_s});
        }
        const QStringList secrets{u"Zażółć gęślą jaźń"_s,
                                  u"Pierwszy wpis"_s,
                                  u"Drugi tekst o łąkach"_s,
                                  u"Pierwszy%20wpis"_s,
                                  u"Sekretny dziennik Zosi"_s,
                                  u"Zeszyt"_s,
                                  u"kot.png"_s,
                                  u"Blog/"_s,
                                  u"tags/blog"_s,
                                  kPassword};
        for (const SiteFile &f : site.files) {
            for (const QString &secret : secrets)
                QVERIFY2(!f.data.contains(secret.toUtf8()), qPrintable(f.path + u": "_s + secret));
            QVERIFY(!f.data.contains("\x89PNG"));
            if (!f.path.endsWith(u".html"_s))
                continue;
            const QString html = QString::fromUtf8(f.data);
            // The lock page: a generic title, never indexed, its script inline and without eval.
            QVERIFY(html.contains(u"<title>Protected page</title>"_s));
            QVERIFY(html.contains(u"<meta name=\"robots\" content=\"noindex, nofollow, noarchive\">"_s));
            QVERIFY(!html.contains(u"og:"_s));
            QVERIFY(!html.contains(u"eval("_s));
            QVERIFY(!html.contains(u"<script src"_s));
            QVERIFY(!html.contains(u"http://"_s) && !html.contains(u"https://"_s));
            QVERIFY(!html.contains(u" onclick="_s) && !html.contains(u" onsubmit="_s));
            const auto page = lockPage(f.data);
            QVERIFY(page);
            QCOMPARE(page->sealed.value(u"v"_s).toInt(), 1);
            QCOMPARE(page->sealed.value(u"hash"_s).toString(), u"SHA-256"_s);
            QCOMPARE(page->sealed.value(u"iterations"_s).toInteger(), qint64(kFastIterations));
            QCOMPARE(b64(page->sealed, u"salt"_s), QByteArray("0123456789abcdef"));
            QCOMPARE(b64(page->sealed, u"page"_s).size(), 16);
            QCOMPARE(b64(page->sealed, u"iv"_s).size(), 12);
        }
        // Opened with the password: the notes' text, the real title.
        const bool document = SiteContent(content) == SiteContent::Document;
        const SiteFile *first = document ? site.file(u"index.html"_s) : pageWith(site, u"<title>Pierwszy wpis"_s);
        QVERIFY(first);
        const QString html = *openPage(first->data, kPassword);
        QVERIFY(html.contains(u"Zażółć gęślą jaźń"_s));
        QVERIFY2(html.contains(document ? u"<title>Sekretny dziennik Zosi</title>"_s
                                        : u"<title>Pierwszy wpis – Sekretny dziennik Zosi</title>"_s),
                 qPrintable(html.left(400)));
        if (document)
            QVERIFY(html.contains(u"data:image/png;base64,"_s)); // the picture inside
        QVERIFY(!html.contains(u"Jeszcze nie gotowe"_s));        // publish: false stays out
    }

    void wrongPassword_data()
    {
        QTest::addColumn<QString>("password");
        QTest::newRow("another") << u"Źdźbło trawy na łąc"_s;
        QTest::newRow("letter case") << u"źdźbło trawy na łące"_s;
        QTest::newRow("no diacritics") << u"Zdzblo trawy na lace"_s;
        QTest::newRow("empty") << QString();
        QTest::newRow("a space more") << u"Źdźbło trawy na łące "_s;
    }

    void wrongPassword()
    {
        QFETCH(QString, password);
        Notebook nb;
        publishtest::writeBlog(nb);
        const SiteBuild site = buildProtected(nb, protectedTarget());
        for (const SiteFile &f : site.files)
            if (f.path.endsWith(u".html"_s))
                QVERIFY2(!openPage(f.data, password), qPrintable(f.path));
        QVERIFY(openPage(site.file(u"index.html"_s)->data, kPassword));
        // A changed byte of the ciphertext: no page either.
        QByteArray tampered = site.file(u"index.html"_s)->data;
        const qsizetype at = tampered.indexOf("\"data\":\"") + 12;
        tampered[at] = tampered[at] == 'A' ? 'B' : 'A';
        QVERIFY(!openPage(tampered, kPassword));
    }

    /// The pages' links lead to the other pages' new names; the picture is a sealed file.
    void linksAndFiles()
    {
        Notebook nb;
        publishtest::writeBlog(nb);
        nb.write(u"Blog/Drugi wpis.md"_s,
                 u"Drugi tekst o łąkach, link do [[Pierwszy wpis#Rozdział]] i [[Notatki prywatne]].\n\n"
                 u"[Plan](../plan.pdf)\n"_s);
        nb.write(u"plan.pdf"_s, QByteArray("%PDF-1.4 źródło"));
        const SiteBuild site = buildProtected(nb, protectedTarget());
        const SiteFile *first = pageWith(site, u"<title>Pierwszy wpis"_s);
        const SiteFile *second = pageWith(site, u"<title>Drugi wpis"_s);
        QVERIFY(first && second);
        QVERIFY(first->path.startsWith(u"p/"_s));
        const QString firstName = first->path.mid(2);
        const QString secondName = second->path.mid(2);
        const QString one = *openPage(first->data, kPassword);
        const QString two = *openPage(second->data, kPassword);
        QVERIFY2(one.contains(u"href=\""_s + secondName + u'"'), qPrintable(one));
        QVERIFY(one.contains(u"href=\"../style.css\""_s));
        QVERIFY(one.contains(u"href=\"../index.html\""_s));
        QVERIFY2(two.contains(u"href=\""_s + firstName + u'#'), qPrintable(two));
        QVERIFY(!one.contains(u"assets/kot.png"_s));
        static const QRegularExpression sealedSrc(uR"re(<img data-sealed-src="\.\./(f/[0-9a-f]{20})")re"_s);
        const QRegularExpressionMatch picture = sealedSrc.match(one);
        QVERIFY2(picture.hasMatch(), qPrintable(one));
        const QByteArray k = siteKey(lockPage(first->data)->sealed, kPassword);
        const auto kot = openFile(site.file(picture.captured(1))->data, k);
        QVERIFY(kot);
        QCOMPARE(kot->bytes, QByteArray("\x89PNG\r\n\x1a\nkot", 11));
        QCOMPARE(kot->head.value(u"name"_s).toString(), u"kot.png"_s);
        QCOMPARE(kot->head.value(u"type"_s).toString(), u"image/png"_s);
        // A link to a file: data-sealed-href, its name and type inside.
        static const QRegularExpression sealedHref(uR"re(data-sealed-href="\.\./(f/[0-9a-f]{20})")re"_s);
        const QRegularExpressionMatch pdf = sealedHref.match(two);
        QVERIFY2(pdf.hasMatch(), qPrintable(two));
        const auto plan = openFile(site.file(pdf.captured(1))->data, k);
        QVERIFY(plan);
        QCOMPARE(plan->bytes, QByteArray("%PDF-1.4 źródło"));
        QCOMPARE(plan->head.value(u"name"_s).toString(), u"plan.pdf"_s);
        QCOMPARE(plan->head.value(u"type"_s).toString(), u"application/pdf"_s);
        QVERIFY(!openFile(site.file(pdf.captured(1))->data, siteKey(lockPage(first->data)->sealed, u"zle"_s)));
        // The index leads to both pages.
        const QString index = *openPage(site.file(u"index.html"_s)->data, kPassword);
        QVERIFY(index.contains(u"href=\""_s + first->path + u'"'));
        QVERIFY(index.contains(u"href=\""_s + second->path + u'"'));
        QVERIFY(index.contains(u"href=\"style.css\""_s));
    }

    /// The same notes give the same bytes (the incremental publish sends an unchanged page once);
    /// another password, other names and bytes.
    void deterministic()
    {
        Notebook nb;
        publishtest::writeBlog(nb);
        nb.write(u"Blog/Trzeci.md"_s, u"Trzeci, osobny — bez linków.\n"_s);
        const SiteBuild site = buildProtected(nb, protectedTarget());
        const SiteBuild again = buildProtected(nb, protectedTarget());
        QCOMPARE(pathsOf(again), pathsOf(site));
        for (qsizetype i = 0; i < site.files.size(); ++i)
            QCOMPARE(again.files.at(i).sha256, site.files.at(i).sha256);
        const QString thirdPath = pageWith(site, u"<title>Trzeci"_s)->path;
        nb.write(u"Blog/Trzeci.md"_s, u"Trzeci, osobny — zmieniony.\n"_s);
        const SiteBuild changed = buildProtected(nb, protectedTarget());
        QCOMPARE(pathsOf(changed), pathsOf(site));
        for (const SiteFile &f : changed.files) {
            if (f.path == thirdPath)
                QVERIFY(f.sha256 != site.file(f.path)->sha256);
            else
                QVERIFY2(f.sha256 == site.file(f.path)->sha256, qPrintable(f.path));
        }
        QVERIFY(openPage(changed.file(thirdPath)->data, kPassword)->contains(u"zmieniony"_s));
        // Another password: other names, and the old one opens nothing.
        const SiteBuild other = buildProtected(nb, protectedTarget(), u"inne hasło"_s);
        QVERIFY(pathsOf(other) != pathsOf(site));
        QVERIFY(!openPage(other.file(u"index.html"_s)->data, kPassword));
        QVERIFY(openPage(other.file(u"index.html"_s)->data, u"inne hasło"_s));
        // Another salt: other names too.
        PublishTarget salted = protectedTarget();
        salted.protectSalt = crypto::encodeBase64(QByteArray("fedcba9876543210"));
        QVERIFY(pathsOf(buildProtected(nb, salted)) != pathsOf(changed));
    }

    void noPassword()
    {
        Notebook nb;
        publishtest::writeBlog(nb);
        const SiteBuild site = buildProtected(nb, protectedTarget(), QString());
        QCOMPARE(site.errorCode, u"no-page-password"_s);
        QVERIFY(site.files.isEmpty());
        QVERIFY(!errorText(site.errorCode).isEmpty());
        QVERIFY(errorText(site.errorCode) != errorText(u"something-else"_s));
    }

    /// The default rounds, and the script's WebCrypto calls (the browser test runs them).
    void defaultsAndScript()
    {
        Notebook nb;
        nb.write(u"Blog/Jedna.md"_s, u"Jedna strona — żółć.\n"_s);
        const SiteBuild site = buildProtected(nb, protectedTarget(), kPassword, ProtectIterations);
        const auto page = lockPage(site.file(u"index.html"_s)->data);
        QVERIFY(page);
        QCOMPARE(page->sealed.value(u"iterations"_s).toInteger(), qint64(600000));
        QVERIFY(openPage(site.file(u"index.html"_s)->data, kPassword)->contains(u"Jedna"_s));
        for (const QString &call : {u"deriveBits"_s, u"\"PBKDF2\""_s, u"\"AES-GCM\""_s, u"\"HMAC\""_s,
                                    u"sessionStorage"_s, u"DOMParser"_s, u"data-sealed-"_s})
            QVERIFY2(page->html.contains(call), qPrintable(call));
        QVERIFY(!page->html.contains(u"innerHTML"_s));
        QVERIFY(!page->html.contains(u"document.write"_s));
        QVERIFY(!page->html.contains(u"localStorage"_s));
    }

    void targetAndSecrets()
    {
        PublishTarget t = protectedTarget();
        t.protectSalt.clear();
        QVERIFY(ensureProtectSalt(t));
        QCOMPARE(crypto::decodeBase64(t.protectSalt)->size(), ProtectSaltSize);
        QVERIFY(!ensureProtectSalt(t));
        const PublishTarget back = targetFromJson(toJson(t));
        QVERIFY(back.protect);
        QCOMPARE(back.protectSalt, t.protectSalt);
        PublishTarget plain;
        QVERIFY(!ensureProtectSalt(plain));
        QVERIFY(!toJson(plain).contains(u"protect"_s));
        QVERIFY(!toJson(plain).contains(u"protectSalt"_s));
        // The store gives a protected target its salt; the password is never in it.
        PublishStore store({});
        t.protectSalt.clear();
        QVERIFY(store.saveTarget(t));
        QCOMPARE(crypto::decodeBase64(store.target(t.id)->protectSalt)->size(), ProtectSaltSize);
        QTemporaryDir state;
        PublishStore onDisk(state.path());
        QVERIFY(onDisk.saveTarget(t));
        QFile targets(onDisk.folder() + u"/targets.json"_s);
        QVERIFY(targets.open(QIODevice::ReadOnly));
        const QByteArray json = targets.readAll();
        QVERIFY(json.contains("\"protect\": true"));
        QVERIFY(!json.contains(kPassword.toUtf8()));
        // Its own secret file, beside the others.
        QTemporaryDir data;
        const QString file = secretFile(data.path(), t.id, u"pages"_s);
        QCOMPARE(file, data.path() + u"/secrets/publish-"_s + t.id + u"-pages"_s);
        QVERIFY(file != secretFile(data.path(), t.id, u"share"_s));
        QVERIFY(file != secretFile(data.path(), t.id, u"password"_s));
        QVERIFY(writeSecret(file, kPassword));
        QCOMPARE(readSecret(file), kPassword);
        // The input of a protected target: its salt, never a password of its own.
        const PublishTarget stored = *store.target(t.id);
        const SiteInput input = siteInputFor(stored, {}, {}, u"Zeszyt"_s, {});
        QVERIFY(input.protect);
        QVERIFY(input.protection.password.isEmpty());
        QCOMPARE(input.protection.salt, crypto::decodeBase64(stored.protectSalt).value_or(QByteArray()));
        QCOMPARE(input.protection.iterations, ProtectIterations);
    }

    /// A folder target published plainly, then protected: the plain files go, the next publish
    /// without changes sends nothing.
    void folderTurnedProtected()
    {
        Notebook nb;
        publishtest::writeBlog(nb);
        QTemporaryDir www;
        PublishTarget t = protectedTarget();
        t.kind = TargetKind::Folder;
        t.path = www.path() + u"/strona"_s;
        t.protect = false;
        Publisher publisher(nullptr);
        PublishManifest manifest;
        SiteBuild site = nb.site(t);
        PublishReport r = publishtest::run([&](auto done) { publisher.publish(t, site, manifest, {}, false, done); });
        QVERIFY2(r.ok, qPrintable(r.errorCode));
        manifest = r.manifest;
        QVERIFY(QFileInfo::exists(t.path + u"/Blog/Pierwszy wpis.html"_s));
        t.protect = true;
        site = buildProtected(nb, t);
        r = publishtest::run([&](auto done) { publisher.publish(t, site, manifest, {}, false, done); });
        QVERIFY2(r.ok, qPrintable(r.errorCode));
        manifest = r.manifest;
        QVERIFY(!QFileInfo::exists(t.path + u"/Blog/Pierwszy wpis.html"_s));
        QVERIFY(!QFileInfo::exists(t.path + u"/Blog"_s));
        QVERIFY(!QFileInfo::exists(t.path + u"/assets/kot.png"_s));
        QVERIFY(!QFileInfo::exists(t.path + u"/search-index.js"_s));
        QFile index(t.path + u"/index.html"_s);
        QVERIFY(index.open(QIODevice::ReadOnly));
        const QByteArray lock = index.readAll();
        QVERIFY(!lock.contains("Pierwszy wpis"));
        QVERIFY(openPage(lock, kPassword)->contains(u"Pierwszy wpis"_s));
        site = buildProtected(nb, t);
        r = publishtest::run([&](auto done) { publisher.publish(t, site, manifest, {}, false, done); });
        QVERIFY(r.ok);
        QCOMPARE(r.uploaded + r.updated + r.deleted, 0);
        QCOMPARE(r.unchanged, int(site.files.size()));
    }
};

QTEST_GUILESS_MAIN(TestPublishProtect)
#include "tst_publish_protect.moc"
