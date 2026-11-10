#include "spelldictionaries.h"

#include "spell/languages.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>

#include <tuple>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

/// How good `name` is for its language: lower is better.
int rankOf(const QString &language, const QString &name)
{
    if (language == u"pl")
        return name == u"pl_PL" ? 0 : 1;
    if (language == u"en") {
        static const QStringList order{u"en_US"_s, u"en_GB"_s, u"en_CA"_s, u"en_AU"_s};
        const qsizetype i = order.indexOf(name);
        return i >= 0 ? int(i) : int(order.size());
    }
    return 0;
}

} // namespace

QStringList spellDictionaryDirs(const QString &dicPathVariable, QChar listSeparator, const QString &appDir,
                                const QStringList &builtIn, const QStringList &dataDirs, bool systemDirs)
{
    QStringList dirs;
    const auto add = [&dirs](const QString &dir) {
        const QString d = dir.trimmed();
        if (d.isEmpty())
            return;
        const QString clean = QDir::cleanPath(d);
        if (!dirs.contains(clean))
            dirs.append(clean);
    };
    const QStringList listed = dicPathVariable.split(listSeparator, Qt::SkipEmptyParts);
    for (const QString &d : listed)
        add(d);
    if (!appDir.isEmpty()) {
        add(appDir + u"/dictionaries"_s);
        add(appDir + u"/../share/hunspell"_s);
    }
    for (const QString &d : builtIn)
        add(d);
    for (const QString &d : dataDirs) {
        if (d.trimmed().isEmpty())
            continue;
        add(d + u"/hunspell"_s);
        add(d + u"/myspell"_s);
    }
    if (systemDirs) {
        for (const QString &d : {u"/usr/share/hunspell"_s, u"/usr/share/myspell"_s, u"/usr/share/myspell/dicts"_s,
                                 u"/usr/local/share/hunspell"_s})
            add(d);
    }
    return dirs;
}

QList<SpellDictionary> findSpellDictionaries(const QStringList &dirs)
{
    using Key = std::tuple<int, QString, qsizetype>;
    QMap<QString, std::pair<Key, SpellDictionary>> best;
    for (qsizetype i = 0; i < dirs.size(); ++i) {
        const QDir dir(dirs.at(i));
        if (!dir.exists())
            continue;
        const QFileInfoList affs = dir.entryInfoList({u"*.aff"_s}, QDir::Files | QDir::Readable, QDir::Name);
        for (const QFileInfo &aff : affs) {
            const QString name = aff.completeBaseName();
            const QFileInfo dic(dir.filePath(name + u".dic"_s));
            if (!dic.isFile() || !dic.isReadable())
                continue;
            const QString language = spell::primaryLanguage(name);
            if (language.isEmpty())
                continue;
            const Key key{rankOf(language, name), name, i};
            const auto it = best.constFind(language);
            if (it != best.constEnd() && !(key < it->first))
                continue;
            best.insert(language,
                        {key, SpellDictionary{language, name, aff.absoluteFilePath(), dic.absoluteFilePath()}});
        }
    }
    QList<SpellDictionary> out;
    for (const auto &entry : std::as_const(best))
        out.append(entry.second);
    return out;
}

} // namespace brulion::platform
