// `brulion-cli import` as a process on temporary notebooks with a private home: a CSV (Windows-1250,
// ";") with the column mapping as options, the dry run that writes nothing, a second import that
// renames instead of overwriting, daily notes appended under "## Import (…)" (an iCalendar file),
// Apple Notes from the fixture folder, `import formats`, and refusals (no path, an unknown
// format, files Brulion does not recognise, --read-only, a token without --from).
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int kTimeoutMs = 30000 * BRULION_TEST_SLOWDOWN;

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

QString readText(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : u"<missing "_s + file + u'>';
}

} // namespace

class TestCliImport : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    QString m_home;
    int m_n = 0;

    Run run(const QString &vault, QStringList args)
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(u"HOME"_s, m_home);
        env.insert(u"XDG_CONFIG_HOME"_s, m_home + u"/config"_s);
        env.insert(u"XDG_DATA_HOME"_s, m_home + u"/data"_s);
        env.insert(u"XDG_STATE_HOME"_s, m_home + u"/state"_s);
        env.insert(u"XDG_CACHE_HOME"_s, m_home + u"/cache"_s);
        env.insert(u"XDG_RUNTIME_DIR"_s, m_home + u"/run"_s);
        env.insert(u"LANG"_s, u"C.UTF-8"_s);
        env.insert(u"TZ"_s, u"Europe/Warsaw"_s);
        env.remove(u"BRULION_IMPORT_TOKEN"_s);
        QProcess p;
        p.setProcessEnvironment(env);
        p.start(QStringLiteral(BRULION_CLI),
                               QStringList{u"--vault"_s, vault, u"--now"_s, u"2026-11-20 18:30"_s} + args);
        Run r;
        if (!p.waitForStarted(kTimeoutMs) || !p.waitForFinished(kTimeoutMs))
            return r;
        r.code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -2;
        r.out = p.readAllStandardOutput();
        r.err = p.readAllStandardError();
        r.json = QJsonDocument::fromJson(r.out).object();
        return r;
    }

    QString vault()
    {
        const QString dir = m_tmp.filePath(u"Notatnik %1"_s.arg(++m_n));
        QDir().mkpath(dir);
        return dir;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        m_home = m_tmp.filePath(u"home"_s);
        QDir().mkpath(m_home);
    }

    void csvWithMapping()
    {
        const QString v = vault();
        // "Tytuł;Rok;Uwagi" in Windows-1250, as Excel on Polish Windows saves it.
        const QString csv = m_tmp.filePath(u"Książki.csv"_s);
        writeFile(csv, "Tytu\xb3;Rok;Uwagi\r\nPan Tadeusz;1834;epopeja\r\n\xa3\xf3"
                       "d\x9f;2026;miasto\r\n");
        // The dry run: the plan and the mapping, nothing written.
        Run r = run(v, {u"import"_s, csv, u"--dry-run"_s, u"--columns"_s, u"Rok=text:Rok wydania; Uwagi=skip"_s});
        QCOMPARE(r.code, 0);
        QJsonObject result = r.result();
        QCOMPARE(result.value(u"importer"_s).toString(), u"csv"_s);
        QCOMPARE(result.value(u"dryRun"_s).toBool(), true);
        QCOMPARE(result.value(u"pages"_s).toInt(), 2);
        const QJsonObject file = result.value(u"csv"_s).toArray().first().toObject();
        QCOMPARE(file.value(u"encoding"_s).toString(), u"windows-1250"_s);
        QCOMPARE(file.value(u"separator"_s).toString(), u";"_s);
        QCOMPARE(file.value(u"titleColumn"_s).toString(), u"Tytuł"_s);
        QVERIFY(!QFile::exists(v + u"/Import/CSV/Pan Tadeusz.md"_s));
        // The real import.
        r = run(v, {u"import"_s, csv, u"--columns"_s, u"Rok=text:Rok wydania; Uwagi=skip"_s, u"--folder"_s,
                    u"Biblioteka"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"written"_s).toInt(), 3);
        QCOMPARE(readText(v + u"/Biblioteka/Łódź.md"_s), u"---\nRok wydania: \"2026\"\n---\n"_s);
        QVERIFY(QFile::exists(v + u"/Biblioteka/Książki.base"_s));
        // Again: nothing overwritten.
        r = run(v, {u"import"_s, csv, u"--folder"_s, u"Biblioteka"_s, u"--csv-mode"_s, u"table"_s});
        QCOMPARE(r.code, 0);
        QVERIFY(QFile::exists(v + u"/Biblioteka/Książki.md"_s));
        QCOMPARE(readText(v + u"/Biblioteka/Łódź.md"_s), u"---\nRok wydania: \"2026\"\n---\n"_s);
    }

    void dailyNotesAppended()
    {
        const QString v = vault();
        writeFile(v + u"/journals/2026-11-21.md"_s, QStringLiteral("- Rano bieganie\n").toUtf8());
        const QString ics = m_tmp.filePath(u"kalendarz.ics"_s);
        writeFile(ics, "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:1\r\nDTSTART:20261121T090000\r\n"
                       "SUMMARY:Dentysta\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
        const Run r = run(v, {u"import"_s, ics});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"importer"_s).toString(), u"ics"_s);
        QCOMPARE(r.result().value(u"appended"_s).toInt(), 1);
        const QString day = readText(v + u"/journals/2026-11-21.md"_s);
        QVERIFY2(day.startsWith(u"- Rano bieganie\n\n## Import (iCalendar)\n"_s) && day.contains(u"Dentysta"_s),
                 qPrintable(day));
    }

    void appleNotes()
    {
        const QString v = vault();
        const Run r = run(v, {u"import"_s, QStringLiteral(BRULION_APPLENOTES_FIXTURES) + u"/group.com.apple.notes"_s});
        QCOMPARE(r.code, 0);
        QCOMPARE(r.result().value(u"importer"_s).toString(), u"applenotes"_s);
        QCOMPARE(r.result().value(u"pages"_s).toInt(), 5);
        QVERIFY(readText(v + u"/Import/Apple Notes/Lista zakupów.md"_s).contains(u"- [x] Mleko 2%"_s));
        QVERIFY(r.json.value(u"warnings"_s).toArray().size() >= 3);
    }

    void formats()
    {
        const Run r = run(vault(), {u"import"_s, u"formats"_s});
        QCOMPARE(r.code, 0);
        QStringList ids;
        for (const auto &f : r.result().value(u"formats"_s).toArray())
            ids.append(f.toObject().value(u"id"_s).toString());
        for (const QString &id : {u"csv"_s, u"airtable"_s, u"todoist"_s, u"onenote"_s, u"applenotes"_s})
            QVERIFY2(ids.contains(id), qPrintable(id));
    }

    void refusals_data()
    {
        QTest::addColumn<QStringList>("args");
        QTest::addColumn<int>("exit");
        QTest::addColumn<QString>("code");
        QTest::newRow("no path") << QStringList{u"import"_s} << 2 << u"path_required"_s;
        QTest::newRow("missing file") << QStringList{u"import"_s, u"/nie/ma/takiego.csv"_s} << 3 << u"not_found"_s;
        QTest::newRow("unknown format") << QStringList{u"import"_s, u"x"_s, u"--from"_s, u"lotus"_s} << 2 << QString();
        QTest::newRow("read-only") << QStringList{u"--read-only"_s, u"import"_s, u"x"_s} << 7 << QString();
        QTest::newRow("token for another format")
                << QStringList{u"import"_s, u"--from"_s, u"csv"_s, u"--token-file"_s, u"-"_s} << 2 << u"bad_format"_s;
        QTest::newRow("token missing") << QStringList{u"import"_s, u"--from"_s, u"todoist"_s} << 2
                                       << u"token_required"_s;
    }
    void refusals()
    {
        QFETCH(QStringList, args);
        QFETCH(int, exit);
        QFETCH(QString, code);
        const Run r = run(vault(), args);
        QCOMPARE(r.code, exit);
        if (!code.isEmpty())
            QCOMPARE(r.errorCode(), code);
    }

    void notRecognised()
    {
        const QString junk = m_tmp.filePath(u"dane.bin"_s);
        writeFile(junk, QByteArray("\x00\x01\x02 binarne", 11));
        const Run r = run(vault(), {u"import"_s, junk});
        QCOMPARE(r.code, 6);
        QCOMPARE(r.errorCode(), u"not_recognised"_s);
    }
};

QTEST_GUILESS_MAIN(TestCliImport)
#include "tst_cli_import.moc"
