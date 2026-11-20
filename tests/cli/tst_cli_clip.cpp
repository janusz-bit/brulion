// brulion-cli's Web Clipper (docs/CLIPPER.md) as a process, on temporary notebooks with a private home:
// `clip save` (an article with its pictures from a server on 127.0.0.1 into a new page, the journal
// and a heading's section — the note's other bytes and line endings untouched —, a journal link, a
// preview and a dry run that write nothing, refusals), the native messaging host (`clip host` and
// the arguments browsers start it with: hello, folders, pages, headings, preview, clip with
// progress, notebooks it does not know, malformed and oversized frames) and the host manifests
// (`clip install` / `status` / `uninstall` / `manifest` in the private home).
#include "clipper/nativehost.h"
#include "clipper/nativemessaging.h"

#include "fileserver.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::clipper;

namespace {

constexpr int kTimeoutMs = 30000 * BRULION_TEST_SLOWDOWN;
const QString kNow = u"2026-11-20 18:30"_s;

struct Run {
    int code = -1;
    QJsonObject json;
    QByteArray out;
    QByteArray err;
    QJsonObject result() const { return json.value(u"result"_s).toObject(); }
    QString errorCode() const { return json.value(u"error"_s).toObject().value(u"code"_s).toString(); }
};

void writeFile(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
    QVERIFY(f.commit());
}

QByteArray readFile(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QByteArray pngBytes()
{
    return QByteArray::fromBase64(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mNgAAIAAAUAAen63NgAAAAASUVORK5CYII=");
}

/// A host process and its frames.
class HostProcess {
public:
    HostProcess(const QStringList &args, const QProcessEnvironment &env)
    {
        m_process.setProcessEnvironment(env);
        m_process.start(QStringLiteral(BRULION_CLI), args);
    }
    ~HostProcess()
    {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished();
        }
    }
    bool started() { return m_process.waitForStarted(kTimeoutMs); }
    void send(const QJsonObject &message) { m_process.write(encodeFrame(message, MaxIncomingBytes)); }
    void sendRaw(const QByteArray &bytes) { m_process.write(bytes); }
    /// The next frame from the host (an empty object after a timeout).
    QJsonObject receive()
    {
        QElapsedTimer t;
        t.start();
        for (;;) {
            if (auto f = m_decoder.next())
                return f->message;
            if (t.elapsed() > kTimeoutMs)
                return {};
            if (!m_process.waitForReadyRead(200) && m_process.state() == QProcess::NotRunning
                && m_process.bytesAvailable() == 0)
                return {};
            m_decoder.feed(m_process.readAllStandardOutput());
        }
    }
    /// Close standard input and wait: the exit code.
    int finish()
    {
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(kTimeoutMs))
            return -1;
        m_decoder.feed(m_process.readAllStandardOutput());
        return m_process.exitStatus() == QProcess::NormalExit ? m_process.exitCode() : -2;
    }
    QJsonObject rest()
    {
        auto f = m_decoder.next();
        return f ? f->message : QJsonObject();
    }

private:
    QProcess m_process;
    FrameDecoder m_decoder;
};

} // namespace

class TestCliClip : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;
    QString m_home;
    int m_n = 0;

    QProcessEnvironment environment() const
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(u"HOME"_s, m_home);
        env.insert(u"XDG_CONFIG_HOME"_s, m_home + u"/config"_s);
        env.insert(u"XDG_DATA_HOME"_s, m_home + u"/data"_s);
        env.insert(u"XDG_STATE_HOME"_s, m_home + u"/state"_s);
        env.insert(u"XDG_CACHE_HOME"_s, m_home + u"/cache"_s);
        env.insert(u"XDG_RUNTIME_DIR"_s, m_home + u"/run"_s);
        env.remove(u"BRULION_VAULT"_s);
        env.remove(u"APPIMAGE"_s);
        env.insert(u"LANG"_s, u"C.UTF-8"_s);
        for (const QString &v : {u"LC_ALL"_s, u"LC_MESSAGES"_s, u"LANGUAGE"_s})
            env.remove(v);
        return env;
    }

    Run cli(const QStringList &args, const QByteArray &input = {}, QProcessEnvironment env = {})
    {
        QProcess p;
        p.setProcessEnvironment(env.isEmpty() ? environment() : env);
        p.start(QStringLiteral(BRULION_CLI), args);
        Run r;
        if (!p.waitForStarted(kTimeoutMs))
            return r;
        if (!input.isEmpty())
            p.write(input);
        p.closeWriteChannel();
        // Pictures come from a server living in this process: keep its event loop running.
        QElapsedTimer t;
        t.start();
        while (p.state() != QProcess::NotRunning && t.elapsed() < kTimeoutMs)
            QTest::qWait(10);
        if (p.state() != QProcess::NotRunning) {
            p.kill();
            p.waitForFinished();
            return r;
        }
        r.code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -2;
        r.out = p.readAllStandardOutput();
        r.err = p.readAllStandardError();
        r.json = QJsonDocument::fromJson(r.out).object();
        return r;
    }

    Run run(const QString &vault, QStringList args, const QByteArray &input = {})
    {
        args = QStringList{u"--vault"_s, vault, u"--now"_s, kNow} + args;
        return cli(args, input);
    }

    QString vault(const QHash<QString, QByteArray> &files = {})
    {
        const QString dir = m_tmp.filePath(u"Notatnik %1"_s.arg(++m_n));
        QDir().mkpath(dir);
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            writeFile(dir + u'/' + it.key(), it.value());
        return dir;
    }

    static QByteArray fixture(const QString &name)
    {
        return readFile(QStringLiteral(BRULION_CLIPPER_FIXTURES) + u'/' + name);
    }

    /// The app's settings naming `current` and `recent` notebooks (what the host offers).
    void appNotebooks(const QString &current, const QStringList &recent)
    {
        QSettings settings(m_home + u"/config/Brulion/Brulion.ini"_s, QSettings::IniFormat);
        settings.setValue(u"vaults/current"_s, current);
        QVariantList list;
        for (const QString &path : recent)
            list.append(QVariantMap{{u"path"_s, path}, {u"name"_s, QFileInfo(path).fileName()}});
        settings.setValue(u"vaults/recent"_s, list);
        settings.sync();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        m_home = m_tmp.filePath(u"home"_s);
        QDir().mkpath(m_home + u"/run"_s);
        QFile::setPermissions(m_home + u"/run"_s,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QVERIFY(QFileInfo::exists(QStringLiteral(BRULION_CLI)));
#ifdef Q_OS_WIN
        QSKIP("the private home does not move Windows' folders and registry (docs/WINDOWS.md)");
#endif
    }

    void articleIntoNewPage()
    {
        FileServer server;
        server.files.insert("/artykuly/zdjecia/zolw-ma%C5%82y.jpg", pngBytes());
        const QString dir = vault();
        const QString url = server.url(u"/artykuly/zolw-stepowy.html"_s).toString();
        const Run r = run(dir,
                          {u"clip"_s, u"save"_s, u"--url"_s, url, u"--title"_s, u"Żółw stepowy: w ogrodzie?"_s,
                           u"--author"_s, u"Zofia Łęcka"_s, u"--published"_s, u"2026-11-19T08:15:00+01:00"_s,
                           u"--tags"_s, u"gady, ogród zimą"_s, u"--folder"_s, u"Wycinki/Przyroda"_s, u"--journal-link"_s,
                           u"--stdin"_s},
                          fixture(u"artykul.html"_s));
        QVERIFY2(r.code == 0, r.out.constData());
        const QJsonObject result = r.result();
        QCOMPARE(result.value(u"path"_s).toString(), u"Wycinki/Przyroda/Żółw stepowy w ogrodzie.md"_s);
        QCOMPARE(result.value(u"created"_s).toBool(), true);
        QCOMPARE(result.value(u"template"_s).toString(), u"brulion"_s);
        const QJsonObject pictures = result.value(u"pictures"_s).toObject();
        // The page's own picture (on 127.0.0.1, like the page) is saved; "x" is no picture.
        QCOMPARE(pictures.value(u"saved"_s).toInt(), 1);
        QCOMPARE(pictures.value(u"failed"_s).toArray().size(), 1);
        QCOMPARE(pictures.value(u"failed"_s).toArray().first().toObject().value(u"reason"_s).toString(), u"http"_s);
        const QString page = QString::fromUtf8(readFile(dir + u"/Wycinki/Przyroda/Żółw stepowy w ogrodzie.md"_s));
        QVERIFY2(page.startsWith(u"---\nsource: "_s + url + u"\nauthor: Zofia Łęcka\npublished: 2026-11-19\n"
                                 u"clipped: 2026-11-20 18:30\ntags: [gady, ogród zimą]\n---\n\n"_s),
                 qPrintable(page));
        // Named by what the bytes are (a PNG behind a .jpg address).
        QVERIFY2(page.contains(u"![[zolw-mały.png]]\n\n*Żółw na trawie. Fot.*"_s), qPrintable(page));
        QVERIFY(page.contains(u"```python\ndef temperatura(c):"_s));
        QVERIFY(page.contains(u"![](http://127.0.0.1:"_s)); // the one not saved stays a picture from there
        QVERIFY(!page.contains(u"tracker"_s));
        QCOMPARE(readFile(dir + u"/assets/zolw-mały.png"_s), pngBytes());
        // The journal links to it.
        const QJsonObject journal = result.value(u"journal"_s).toObject();
        QCOMPARE(journal.value(u"path"_s).toString(), u"journals/2026-11-20.md"_s);
        QCOMPARE(QString::fromUtf8(readFile(dir + u"/journals/2026-11-20.md"_s)), u"- [[Żółw stepowy w ogrodzie]]\n"_s);

        // The same title again: a new page, never the first one.
        const Run again = run(dir, {u"clip"_s, u"save"_s, u"--url"_s, url, u"--title"_s, u"żółw stepowy w ogrodzie"_s,
                                    u"--folder"_s, u"Wycinki/Przyroda"_s, u"--no-pictures"_s, u"--html"_s, u"<p>Inny</p>"_s});
        QCOMPARE(again.code, 0);
        QCOMPARE(again.result().value(u"path"_s).toString(), u"Wycinki/Przyroda/żółw stepowy w ogrodzie 2.md"_s);
    }

    void intoJournalAndHeading()
    {
        const QByteArray journal = "- [ ] Zadanie\r\n- \r\n";
        const QByteArray reading = "# Czytelnia\r\n\r\n## Do przeczytania\r\n- [Stary](https://a.example/)\r\n\r\n## Przeczytane\r\n- nic\r\n";
        const QString dir = vault({{u"journals/2026-11-20.md"_s, journal}, {u"Czytelnia.md"_s, reading}});
        Run r = run(dir, {u"clip"_s, u"save"_s, u"--place"_s, u"journal"_s, u"--url"_s, u"https://przyroda.example/a"_s,
                          u"--title"_s, u"Żółw [stepowy]"_s, u"--selection"_s, u"<p>gad, który <b>źle znosi</b> chłód</p>"_s,
                          u"--tags"_s, u"gady"_s});
        QVERIFY2(r.code == 0, r.out.constData());
        QCOMPARE(r.result().value(u"template"_s).toString(), u"link"_s);
        QCOMPARE(readFile(dir + u"/journals/2026-11-20.md"_s),
                 QByteArray("- [ ] Zadanie\r\n- [Żółw \\[stepowy\\]](https://przyroda.example/a) #gady\r\n"
                            "  > gad, który **źle znosi** chłód\r\n"));
        r = run(dir, {u"clip"_s, u"save"_s, u"--page"_s, u"Czytelnia"_s, u"--heading"_s, u"Do przeczytania"_s,
                      u"--url"_s, u"https://b.example/x"_s, u"--title"_s, u"Nowy"_s});
        QVERIFY2(r.code == 0, r.out.constData());
        QCOMPARE(r.result().value(u"path"_s).toString(), u"Czytelnia.md"_s);
        QCOMPARE(readFile(dir + u"/Czytelnia.md"_s),
                 QByteArray("# Czytelnia\r\n\r\n## Do przeczytania\r\n- [Stary](https://a.example/)\r\n"
                            "- [Nowy](https://b.example/x)\r\n\r\n## Przeczytane\r\n- nic\r\n"));
        // An article with the Brulion template in the journal: its frontmatter as the block's properties.
        r = run(dir, {u"clip"_s, u"save"_s, u"--place"_s, u"journal"_s, u"--template"_s, u"brulion"_s, u"--url"_s,
                      u"https://c.example/"_s, u"--html"_s, u"<p>Akapit pierwszy.</p><p>Drugi.</p>"_s, u"--no-pictures"_s});
        QVERIFY2(r.code == 0, r.out.constData());
        QVERIFY2(readFile(dir + u"/journals/2026-11-20.md"_s)
                         .endsWith("- Akapit pierwszy.\r\n  source:: https://c.example/\r\n  clipped:: 2026-11-20 18:30\r\n"
                                   "  \r\n  Drugi.\r\n"),
                 readFile(dir + u"/journals/2026-11-20.md"_s).constData());
        // A heading the page does not have, a page that is not there.
        r = run(dir, {u"clip"_s, u"save"_s, u"--page"_s, u"Czytelnia"_s, u"--heading"_s, u"Brak"_s, u"--url"_s, u"https://b.example/"_s});
        QCOMPARE(r.code, 3);
        QCOMPARE(r.errorCode(), u"under_not_found"_s);
        r = run(dir, {u"clip"_s, u"save"_s, u"--page"_s, u"Nie ma"_s, u"--heading"_s, u"X"_s});
        QCOMPARE(r.code, 3);
    }

    void previewAndDryRunWriteNothing()
    {
        const QString dir = vault();
        Run r = run(dir, {u"clip"_s, u"save"_s, u"--preview"_s, u"--url"_s, u"https://a.example/"_s, u"--title"_s, u"Podgląd"_s,
                          u"--html"_s, u"<h2>Śródtytuł</h2><p>Tekst <img src=\"/a.png\" alt=\"A\"></p>"_s, u"--template"_s,
                          u"joplin"_s});
        QVERIFY2(r.code == 0, r.out.constData());
        QCOMPARE(r.result().value(u"path"_s).toString(), u"Podgląd.md"_s);
        QVERIFY2(r.result().value(u"markdown"_s).toString().contains(u"## Śródtytuł\n\nTekst ![A](https://a.example/a.png)"_s),
                 r.out.constData());
        r = run(dir, {u"--dry-run"_s, u"clip"_s, u"save"_s, u"--url"_s, u"https://a.example/"_s, u"--title"_s, u"Próba"_s,
                      u"--html"_s, u"<p>x</p>"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(r.result().value(u"diff"_s).toString().contains(u"+++ b/Próba.md"_s));
        QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot), QStringList());
        // Read-only: refused.
        r = run(dir, {u"--read-only"_s, u"clip"_s, u"save"_s, u"--html"_s, u"<p>x</p>"_s});
        QCOMPARE(r.code, 7);
    }

    void refusals()
    {
        const QString dir = vault();
        Run r = run(dir, {u"clip"_s, u"save"_s, u"--folder"_s, u"../poza"_s, u"--html"_s, u"<p>x</p>"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"bad_folder"_s);
        r = run(dir, {u"clip"_s, u"save"_s, u"--folder"_s, u".git/hooks"_s, u"--html"_s, u"<p>x</p>"_s});
        QCOMPARE(r.errorCode(), u"bad_folder"_s);
        r = run(dir, {u"clip"_s, u"save"_s, u"--template"_s, u"nie-ma-takiego"_s, u"--html"_s, u"<p>x</p>"_s});
        QCOMPARE(r.code, 3);
        // A title that walks out of the folder is only a name.
        r = run(dir, {u"clip"_s, u"save"_s, u"--title"_s, u"../../etc/passwd"_s, u"--html"_s, u"<p>x</p>"_s, u"--folder"_s,
                      u"Wycinki"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"path"_s).toString(), u"Wycinki/etc passwd.md"_s);
        QCOMPARE(QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot), QStringList{u"Wycinki"_s});
    }

    void host()
    {
        const QString dir = vault({{u"Czytelnia.md"_s, "# Czytelnia\n\n## Do przeczytania\n- a\n### Pilne\n"},
                                   {u"Projekty/Dom.md"_s, "x\n"},
                                   {u"templates/Przepis.md"_s, "# {{title}}\n{{content}}\n"}});
        const QString other = vault();
        appNotebooks(dir, {dir, other});
        HostProcess h({u"--now"_s, kNow, u"clip"_s, u"host"_s}, environment());
        QVERIFY(h.started());
        h.send({{u"type"_s, u"hello"_s}, {u"id"_s, 1}});
        QJsonObject a = h.receive();
        QCOMPARE(a.value(u"type"_s).toString(), u"hello"_s);
        QCOMPARE(a.value(u"id"_s).toInt(), 1);
        QCOMPARE(a.value(u"protocol"_s).toInt(), 1);
        QCOMPARE(a.value(u"notebooks"_s).toArray().size(), 2);
        QCOMPARE(a.value(u"notebook"_s).toObject().value(u"path"_s).toString(), QDir::toNativeSeparators(dir));
        QVERIFY(a.value(u"folders"_s).toArray().contains(u"Projekty"_s));
        const QJsonArray templates = a.value(u"templates"_s).toArray();
        QCOMPARE(templates.first().toObject().value(u"id"_s).toString(), u"brulion"_s);
        QCOMPARE(templates.last().toObject().value(u"page"_s).toString(), u"templates/Przepis.md"_s);

        h.send({{u"type"_s, u"pages"_s}, {u"id"_s, u"p"_s}, {u"query"_s, u"czyt"_s}});
        a = h.receive();
        QCOMPARE(a.value(u"pages"_s).toArray().first().toObject().value(u"path"_s).toString(), u"Czytelnia.md"_s);
        h.send({{u"type"_s, u"headings"_s}, {u"id"_s, 3}, {u"page"_s, u"Czytelnia.md"_s}});
        a = h.receive();
        QCOMPARE(a.value(u"headings"_s).toArray().size(), 3);
        QCOMPARE(a.value(u"headings"_s).toArray().at(1).toObject().value(u"text"_s).toString(), u"Do przeczytania"_s);

        const QJsonObject clip{{u"kind"_s, u"selection"_s},
                               {u"url"_s, u"https://przyroda.example/a"_s},
                               {u"title"_s, u"Żółw"_s},
                               {u"selectionHtml"_s, u"<p>gad</p>"_s},
                               {u"place"_s, QJsonObject{{u"kind"_s, u"heading"_s},
                                                        {u"page"_s, u"Czytelnia.md"_s},
                                                        {u"heading"_s, u"Do przeczytania"_s}}}};
        h.send({{u"type"_s, u"preview"_s}, {u"id"_s, 4}, {u"clip"_s, clip}});
        a = h.receive();
        QCOMPARE(a.value(u"type"_s).toString(), u"preview"_s);
        QCOMPARE(a.value(u"result"_s).toObject().value(u"markdown"_s).toString(),
                 u"[Żółw](https://przyroda.example/a)\n> gad\n"_s);
        QCOMPARE(readFile(dir + u"/Czytelnia.md"_s), QByteArray("# Czytelnia\n\n## Do przeczytania\n- a\n### Pilne\n"));
        h.send({{u"type"_s, u"clip"_s}, {u"id"_s, 5}, {u"clip"_s, clip}, {u"progress"_s, true}});
        a = h.receive();
        QCOMPARE(a.value(u"type"_s).toString(), u"clip"_s);
        QCOMPARE(a.value(u"result"_s).toObject().value(u"path"_s).toString(), u"Czytelnia.md"_s);
        QCOMPARE(readFile(dir + u"/Czytelnia.md"_s),
                 QByteArray("# Czytelnia\n\n## Do przeczytania\n- a\n### Pilne\n- [Żółw](https://przyroda.example/a)\n  > gad\n"));

        // Another notebook of the app; one it does not know.
        h.send({{u"type"_s, u"clip"_s}, {u"id"_s, 6}, {u"vault"_s, other},
                {u"clip"_s, QJsonObject{{u"kind"_s, u"bookmark"_s}, {u"url"_s, u"https://d.example/"_s},
                                        {u"place"_s, QJsonObject{{u"kind"_s, u"journal"_s}}}}}});
        a = h.receive();
        QCOMPARE(a.value(u"result"_s).toObject().value(u"path"_s).toString(), u"journals/2026-11-20.md"_s);
        QVERIFY(QFileInfo::exists(other + u"/journals/2026-11-20.md"_s));
        h.send({{u"type"_s, u"folders"_s}, {u"id"_s, 7}, {u"vault"_s, m_tmp.path()}});
        a = h.receive();
        QCOMPARE(a.value(u"type"_s).toString(), u"error"_s);
        QCOMPARE(a.value(u"code"_s).toString(), u"unknown_notebook"_s);

        // Hostile or broken requests are answered, the session goes on.
        h.send({{u"type"_s, u"clip"_s}, {u"id"_s, 8}, {u"clip"_s, QJsonObject{{u"place"_s, QJsonObject{{u"folder"_s, u"../../x"_s}}}}}});
        QCOMPARE(h.receive().value(u"code"_s).toString(), u"bad_folder"_s);
        h.send({{u"type"_s, u"rm -rf"_s}, {u"id"_s, 9}});
        QCOMPARE(h.receive().value(u"code"_s).toString(), u"unknown_type"_s);
        h.sendRaw(QByteArray("\x05\x00\x00\x00{oops", 9));
        QCOMPARE(h.receive().value(u"code"_s).toString(), u"invalid_json"_s);
        h.send({{u"type"_s, u"headings"_s}, {u"id"_s, 10}, {u"page"_s, u"../../etc/passwd"_s}});
        QCOMPARE(h.receive().value(u"type"_s).toString(), u"error"_s);
        QCOMPARE(h.finish(), 0);
    }

    void hostEndsOnOversizedFrames()
    {
        const QString dir = vault();
        appNotebooks(dir, {dir});
        HostProcess h({u"clip"_s, u"host"_s}, environment());
        QVERIFY(h.started());
        h.sendRaw(QByteArray("\xff\xff\xff\x7f", 4) + QByteArray(100, 'x'));
        const QJsonObject a = h.receive();
        QCOMPARE(a.value(u"code"_s).toString(), u"too_large"_s);
        QCOMPARE(h.finish(), 2);
        HostProcess cut({u"clip"_s, u"host"_s}, environment());
        QVERIFY(cut.started());
        cut.sendRaw(QByteArray("\x20\x00\x00\x00{\"type\":", 12));
        QCOMPARE(cut.finish(), 2);
        QCOMPARE(cut.rest().value(u"code"_s).toString(), u"truncated"_s);
    }

    void browsersStartTheHost_data()
    {
        QTest::addColumn<QStringList>("arguments");
        QTest::newRow("chromium") << QStringList{u"chrome-extension://calcbkckojhmdfecpcohaangneahmkgg/"_s};
        QTest::newRow("firefox") << QStringList{m_home + u"/.mozilla/native-messaging-hosts/io.github.januszbit.brulion.json"_s,
                                                u"brulion-clipper@januszbit.github.io"_s};
    }
    void browsersStartTheHost()
    {
        QFETCH(QStringList, arguments);
        const QString dir = vault();
        appNotebooks(dir, {dir});
        HostProcess h(arguments, environment());
        QVERIFY(h.started());
        h.send({{u"type"_s, u"hello"_s}, {u"id"_s, u"x"_s}});
        const QJsonObject a = h.receive();
        QCOMPARE(a.value(u"type"_s).toString(), u"hello"_s);
        QCOMPARE(a.value(u"notebook"_s).toObject().value(u"path"_s).toString(), QDir::toNativeSeparators(dir));
        QCOMPARE(h.finish(), 0);
    }

    void hostWithoutNotebook()
    {
        appNotebooks(QString(), {});
        HostProcess h({u"clip"_s, u"host"_s}, environment());
        QVERIFY(h.started());
        h.send({{u"type"_s, u"hello"_s}, {u"id"_s, 1}});
        const QJsonObject a = h.receive();
        QCOMPARE(a.value(u"type"_s).toString(), u"hello"_s);
        QCOMPARE(a.value(u"notebookError"_s).toObject().value(u"code"_s).toString(), u"no_vault"_s);
        QCOMPARE(h.finish(), 0);
    }

    void installStatusUninstall()
    {
        if (currentOs() != Os::Linux)
            QSKIP("the manifests' places of Linux");
        const QString program = QFileInfo(QStringLiteral(BRULION_CLI)).absoluteFilePath();
        // No browser in the private home: nothing installed, said so.
        Run r = cli({u"clip"_s, u"install"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"installed"_s).toArray().size(), 0);
        QCOMPARE(r.result().value(u"skipped"_s).toArray().size(), 5);
        // Chromium found (its profile folder), Firefox named.
        QDir().mkpath(m_home + u"/config/chromium"_s);
        r = cli({u"clip"_s, u"install"_s, u"--browser"_s, u"chromium,firefox"_s, u"--extension-id"_s,
                 u"abcdefghijklmnopabcdefghijklmnop"_s});
        QVERIFY2(r.code == 0, r.out.constData());
        QCOMPARE(r.result().value(u"installed"_s).toArray().size(), 2);
        const QString chromium = m_home + u"/config/chromium/NativeMessagingHosts/io.github.januszbit.brulion.json"_s;
        const QString firefox = m_home + u"/.mozilla/native-messaging-hosts/io.github.januszbit.brulion.json"_s;
        const QJsonObject cm = QJsonDocument::fromJson(readFile(chromium)).object();
        QCOMPARE(cm.value(u"path"_s).toString(), program);
        QCOMPARE(cm.value(u"allowed_origins"_s).toArray().size(), 2);
        const QJsonObject fm = QJsonDocument::fromJson(readFile(firefox)).object();
        QCOMPARE(fm.value(u"allowed_extensions"_s).toArray(), QJsonArray{u"brulion-clipper@januszbit.github.io"_s});

        r = cli({u"clip"_s, u"status"_s});
        QCOMPARE(r.code, 0);
        int connected = 0;
        for (const QJsonValue &b : r.result().value(u"browsers"_s).toArray()) {
            const QJsonObject o = b.toObject();
            if (o.value(u"installed"_s).toBool()) {
                ++connected;
                QVERIFY(o.value(u"programExists"_s).toBool());
                QVERIFY(o.value(u"thisProgram"_s).toBool());
                QVERIFY(o.value(u"extensionAllowed"_s).toBool());
            }
        }
        QCOMPARE(connected, 2);

        // Another program's manifest of another name stays.
        const QString foreign = m_home + u"/.mozilla/native-messaging-hosts/org.example.other.json"_s;
        writeFile(foreign, "{\"name\":\"org.example.other\"}");
        r = cli({u"clip"_s, u"uninstall"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"removed"_s).toArray().size(), 2);
        QVERIFY(!QFileInfo::exists(chromium));
        QVERIFY(!QFileInfo::exists(firefox));
        QVERIFY(QFileInfo::exists(foreign));
        r = cli({u"clip"_s, u"install"_s, u"--browser"_s, u"opera"_s});
        QCOMPARE(r.code, 2);
        QCOMPARE(r.errorCode(), u"unknown_browser"_s);
    }

    void manifestForPackages()
    {
        const Run r = cli({u"clip"_s, u"manifest"_s, u"firefox"_s, u"--program"_s, u"/run/current-system/sw/bin/brulion-cli"_s});
        QCOMPARE(r.code, 0);
        const QJsonObject m = QJsonDocument::fromJson(r.out).object();
        QCOMPARE(m.value(u"name"_s).toString(), u"io.github.januszbit.brulion"_s);
        QCOMPARE(m.value(u"path"_s).toString(), QDir::toNativeSeparators(u"/run/current-system/sw/bin/brulion-cli"_s));
        QCOMPARE(m.value(u"type"_s).toString(), u"stdio"_s);
        QVERIFY(m.contains(u"allowed_extensions"_s));
    }
};

QTEST_GUILESS_MAIN(TestCliClip)
#include "tst_cli_clip.moc"
