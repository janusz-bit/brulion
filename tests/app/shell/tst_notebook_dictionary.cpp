// NotebookDictionary: the personal dictionary of the open notebook (.brulion/dictionary.txt, synced
// between devices as a set of words — docs/UI.md "Spell checking"), the device's list
// (`spelling/words`) when none is open; a notebook without a dictionary starts from the device's
// list; a change of the file behind the app's back (a sync, Syncthing) reaches the spell checker.
#include "appsettings.h"
#include "notebookdictionary.h"
#include "workspace.h"

#include "vault/vault.h"
#include "vault/vaultconfig.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::shell::AppSettings;
using brulion::shell::NotebookDictionary;
using brulion::shell::Workspace;

class TestNotebookDictionary : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;

    QString folder(const QByteArray &dictionary = {})
    {
        const QString root = m_tmp.filePath(u"notatnik-%1"_s.arg(++m_n));
        QDir().mkpath(root + u"/.brulion"_s);
        if (!dictionary.isNull())
            writeBehind(root, dictionary);
        return root;
    }
    static void writeBehind(const QString &root, const QByteArray &bytes)
    {
        QFile f(root + u'/' + brulion::vaultDictionaryPath());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(bytes);
    }
    static QByteArray onDisk(const QString &root)
    {
        QFile f(root + u'/' + brulion::vaultDictionaryPath());
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    std::unique_ptr<AppSettings> settings(const QStringList &words)
    {
        auto s = std::make_unique<AppSettings>(m_tmp.filePath(u"settings-%1.ini"_s.arg(++m_n)), m_tmp.path());
        s->setSpellingWords(words);
        return s;
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    void withoutANotebookTheDeviceListCounts()
    {
        Workspace ws;
        auto s = settings({u"Brulion"_s});
        NotebookDictionary dictionary(&ws, s.get());
        QCOMPARE(dictionary.words(), QStringList{u"Brulion"_s});
        dictionary.setWords({u"Brulion"_s, u"żółw"_s});
        QCOMPARE(s->spellingWords(), (QStringList{u"Brulion"_s, u"żółw"_s}));
        QCOMPARE(dictionary.words(), (QStringList{u"Brulion"_s, u"żółw"_s}));
    }

    void aNotebookWithoutADictionaryStartsFromTheDeviceList()
    {
        const QString root = folder();
        Workspace ws;
        auto s = settings({u"żółw"_s, u"Brulion"_s});
        NotebookDictionary dictionary(&ws, s.get());
        QSignalSpy changed(&dictionary, &NotebookDictionary::wordsChanged);
        QVERIFY(ws.open(root, root + u"-state"_s));
        QCOMPARE(dictionary.words(), (QStringList{u"Brulion"_s, u"żółw"_s}));
        QCOMPARE(onDisk(root), "Brulion\nżółw\n"_ba); // (written: other devices get it)
        // Words added now go to the notebook, not to the device.
        dictionary.setWords({u"Brulion"_s, u"Kraków"_s, u"żółw"_s});
        QCOMPARE(dictionary.words(), (QStringList{u"Brulion"_s, u"Kraków"_s, u"żółw"_s}));
        QCOMPARE(onDisk(root), "Brulion\nKraków\nżółw\n"_ba);
        QCOMPARE(s->spellingWords(), (QStringList{u"żółw"_s, u"Brulion"_s}));
        QVERIFY(changed.count() >= 1);
        ws.close();
        QCOMPARE(dictionary.words(), (QStringList{u"Brulion"_s, u"żółw"_s})); // (the device's list, sorted)
    }

    void aNotebooksOwnDictionaryIsNotMixedWithTheDevices()
    {
        const QString root = folder("Łódź\nkot\n  kot \n\n");
        Workspace ws;
        auto s = settings({u"Brulion"_s});
        NotebookDictionary dictionary(&ws, s.get());
        QVERIFY(ws.open(root, root + u"-state"_s));
        QCOMPARE(dictionary.words(), (QStringList{u"kot"_s, u"Łódź"_s}));
        QCOMPARE(onDisk(root), "Łódź\nkot\n  kot \n\n"_ba); // (read, not rewritten)
    }

    void anEmptyDictionaryStaysEmpty()
    {
        const QString root = folder(""_ba);
        Workspace ws;
        auto s = settings({u"Brulion"_s});
        NotebookDictionary dictionary(&ws, s.get());
        QVERIFY(ws.open(root, root + u"-state"_s));
        QVERIFY(dictionary.words().isEmpty());
        QCOMPARE(onDisk(root), ""_ba);
    }

    void aChangeBehindTheAppsBackIsTakenIn()
    {
        const QString root = folder("kot\n");
        Workspace ws;
        auto s = settings({});
        NotebookDictionary dictionary(&ws, s.get());
        QVERIFY(ws.open(root, root + u"-state"_s));
        QSignalSpy changed(&dictionary, &NotebookDictionary::wordsChanged);
        writeBehind(root, "kot\nźdźbło\n"); // (another device's words, brought by Syncthing)
        ws.vault()->rescan();
        QTRY_COMPARE(dictionary.words(), (QStringList{u"kot"_s, u"źdźbło"_s}));
        QCOMPARE(changed.count(), 1);
        // The same words again: nothing to tell.
        dictionary.setWords({u"źdźbło"_s, u"kot"_s});
        QCOMPARE(changed.count(), 1);
        QCOMPARE(onDisk(root), "kot\nźdźbło\n"_ba);
        // Deleted behind the app's back: no words.
        QVERIFY(QFile::remove(root + u'/' + brulion::vaultDictionaryPath()));
        ws.vault()->rescan();
        QTRY_VERIFY(dictionary.words().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestNotebookDictionary)
#include "tst_notebook_dictionary.moc"
