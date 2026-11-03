// Where Hunspell's dictionaries are (src/platform/spelldictionaries.h): the folders in order, the
// best dictionary of each language, a .aff without its .dic, Polish folder names.
#include "spelldictionaries.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::platform;

namespace {

void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("SET UTF-8\n");
}

void dictionary(const QString &dir, const QString &name)
{
    touch(dir + u"/"_s + name + u".aff"_s);
    touch(dir + u"/"_s + name + u".dic"_s);
}

QStringList names(const QList<SpellDictionary> &list)
{
    QStringList out;
    for (const SpellDictionary &d : list)
        out.append(d.language + u":"_s + d.name);
    return out;
}

} // namespace

class TestSpellDictionaries : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void dirs()
    {
        const QStringList dirs = spellDictionaryDirs(u"/a/dicts:/b//dicts/::/a/dicts"_s, u':', u"/opt/brulion/bin"_s,
                                                     {u"/nix/store/pl/share/hunspell"_s, QString()},
                                                     {u"/run/current-system/sw/share"_s, u" "_s}, true);
        QCOMPARE(dirs,
                 (QStringList{u"/a/dicts"_s, u"/b/dicts"_s, u"/opt/brulion/bin/dictionaries"_s,
                              u"/opt/brulion/share/hunspell"_s, u"/nix/store/pl/share/hunspell"_s,
                              u"/run/current-system/sw/share/hunspell"_s, u"/run/current-system/sw/share/myspell"_s,
                              u"/usr/share/hunspell"_s, u"/usr/share/myspell"_s, u"/usr/share/myspell/dicts"_s,
                              u"/usr/local/share/hunspell"_s}));
    }

    void dirsWindows()
    {
        const QStringList dirs =
                spellDictionaryDirs(u"C:/Słowniki;D:/x"_s, u';', u"C:/Program Files/Brulion"_s, {}, {}, false);
        QCOMPARE(dirs, (QStringList{u"C:/Słowniki"_s, u"D:/x"_s, u"C:/Program Files/Brulion/dictionaries"_s,
                                    u"C:/Program Files/share/hunspell"_s}));
    }

    void dirsEmpty() { QCOMPARE(spellDictionaryDirs({}, u':', {}, {}, {}, false), QStringList{}); }

    void best()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString first = tmp.filePath(u"pierwszy — słowniki"_s);
        const QString second = tmp.filePath(u"drugi"_s);
        dictionary(first, u"en_GB"_s);
        dictionary(first, u"de_DE_frami"_s);
        dictionary(first, u"de_AT"_s);
        dictionary(second, u"en_US"_s);
        dictionary(second, u"pl_PL"_s);
        dictionary(first, u"pl_PL"_s);
        // Only an .aff (a broken install) and only a .dic (a hyphenation pattern) are no dictionaries.
        touch(first + u"/fr_FR.aff"_s);
        touch(first + u"/hyph_pl_PL.dic"_s);
        touch(first + u"/README.txt"_s);

        const QList<SpellDictionary> found = findSpellDictionaries({first, tmp.filePath(u"brak"_s), second});
        QCOMPARE(names(found), (QStringList{u"de:de_AT"_s, u"en:en_US"_s, u"pl:pl_PL"_s}));
        // pl_PL is in both folders: the first one's.
        QCOMPARE(found[2].affPath, QDir(first).absoluteFilePath(u"pl_PL.aff"_s));
        QCOMPARE(found[2].dicPath, QDir(first).absoluteFilePath(u"pl_PL.dic"_s));
        QCOMPARE(found[1].affPath, QDir(second).absoluteFilePath(u"en_US.aff"_s));
    }

    void englishVariants()
    {
        QTemporaryDir tmp;
        dictionary(tmp.path(), u"en_GB-ise"_s);
        dictionary(tmp.path(), u"en_ZA"_s);
        QCOMPARE(names(findSpellDictionaries({tmp.path()})), QStringList{u"en:en_GB-ise"_s});
        dictionary(tmp.path(), u"en_AU"_s);
        QCOMPARE(names(findSpellDictionaries({tmp.path()})), QStringList{u"en:en_AU"_s});
        dictionary(tmp.path(), u"en_GB"_s);
        QCOMPARE(names(findSpellDictionaries({tmp.path()})), QStringList{u"en:en_GB"_s});
    }

    void nothing()
    {
        QCOMPARE(findSpellDictionaries({}), QList<SpellDictionary>{});
        QCOMPARE(findSpellDictionaries({u"/nie/ma/takiego/folderu"_s}), QList<SpellDictionary>{});
    }
};

QTEST_GUILESS_MAIN(TestSpellDictionaries)
#include "tst_spelldictionaries.moc"
