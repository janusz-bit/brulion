// Spell checking with Hunspell (spellbackends.h): the desktop. The dictionaries are the .aff/.dic
// pairs found by spelldictionaries.h — DICPATH, next to the app (Windows: dictionaries\, the
// AppImage: usr/share/hunspell), the build's BRULION_SPELL_DICTIONARY_DIRS (the Nix package's
// store paths), XDG_DATA_DIRS and the usual system folders. Hunspell works in the dictionary's
// own encoding (the Polish one is ISO8859-2): words are converted by spell/dictencoding.h, and a
// word the encoding cannot hold is not in that dictionary.
#include "spellbackends.h"
#include "spelldictionaries.h"

#include "spell/dictencoding.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QStandardPaths>

#include <hunspell.hxx>

#include <memory>
#include <mutex>
#include <string>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

Q_LOGGING_CATEGORY(lcSpell, "brulion.spell")

/// Longer "words" are not checked (Hunspell's suggestions grow with the length).
constexpr qsizetype MaxWordLength = 100;

QStringList builtInDirs()
{
#ifdef BRULION_SPELL_DICTIONARY_DIRS
    return QString::fromUtf8(BRULION_SPELL_DICTIONARY_DIRS).split(u'|', Qt::SkipEmptyParts);
#else
    return {};
#endif
}

QList<SpellDictionary> dictionaries()
{
    static std::once_flag once;
    static QList<SpellDictionary> found;
    std::call_once(once, [] {
#ifdef Q_OS_WIN
        const QChar separator = u';';
        const bool system = false;
#else
        const QChar separator = u':';
        const bool system = true;
#endif
        QStringList dataDirs = qEnvironmentVariable("XDG_DATA_DIRS").split(separator, Qt::SkipEmptyParts);
        dataDirs += QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
        const QString appDir = QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : QString();
        const QStringList dirs = spellDictionaryDirs(qEnvironmentVariable("DICPATH"), separator, appDir, builtInDirs(),
                                                     dataDirs, system);
        found = findSpellDictionaries(dirs);
        QStringList names;
        for (const SpellDictionary &d : std::as_const(found))
            names.append(d.name);
        qCInfo(lcSpell) << "Hunspell dictionaries:" << (names.isEmpty() ? u"none"_s : names.join(u", "_s));
    });
    return found;
}

/// A path as Hunspell opens it: UTF-8 with the long-path prefix on Windows (its FileMgr then
/// uses _wfopen, so Polish folder names work), the local 8-bit encoding elsewhere.
std::string hunspellPath(const QString &path)
{
#ifdef Q_OS_WIN
    return QString(u"\\\\?\\"_s + QDir::toNativeSeparators(QDir::cleanPath(path))).toStdString();
#else
    return QFile::encodeName(path).toStdString();
#endif
}

class HunspellBackend final : public spell::SpellBackend {
public:
    explicit HunspellBackend(const SpellDictionary &dictionary)
        : m_language(dictionary.language)
        , m_hunspell(std::make_unique<Hunspell>(hunspellPath(dictionary.affPath).c_str(),
                                                hunspellPath(dictionary.dicPath).c_str()))
        , m_encoding(QByteArray::fromStdString(m_hunspell->get_dict_encoding()))
    {
        if (!spell::isKnownDictionaryEncoding(m_encoding)) {
            qCWarning(lcSpell) << "dictionary" << dictionary.name << "has the unsupported encoding" << m_encoding;
            m_hunspell.reset();
        }
    }

    bool valid() const { return m_hunspell != nullptr; }
    QString language() const override { return m_language; }

    bool check(const QString &word) override
    {
        if (word.size() > MaxWordLength)
            return true;
        const auto bytes = spell::encodeDictionaryText(word, m_encoding);
        if (!bytes)
            return false;
        return m_hunspell->spell(bytes->toStdString());
    }

    QStringList suggest(const QString &word, int max) override
    {
        if (word.size() > MaxWordLength || max <= 0)
            return {};
        const auto bytes = spell::encodeDictionaryText(word, m_encoding);
        if (!bytes)
            return {};
        QStringList out;
        for (const std::string &s : m_hunspell->suggest(bytes->toStdString())) {
            out.append(spell::decodeDictionaryText(QByteArray::fromStdString(s), m_encoding));
            if (out.size() >= max)
                break;
        }
        return out;
    }

private:
    QString m_language;
    std::unique_ptr<Hunspell> m_hunspell;
    QByteArray m_encoding;
};

} // namespace

QString spellEngine()
{
    return u"hunspell"_s;
}

QStringList spellLanguages()
{
    QStringList out;
    for (const SpellDictionary &d : dictionaries())
        out.append(d.language);
    return out;
}

std::unique_ptr<spell::SpellBackend> createSpellBackend(const QString &language)
{
    for (const SpellDictionary &d : dictionaries()) {
        if (d.language != language)
            continue;
        auto backend = std::make_unique<HunspellBackend>(d);
        if (!backend->valid())
            return nullptr;
        qCInfo(lcSpell) << "loaded" << d.dicPath;
        return backend;
    }
    return nullptr;
}

} // namespace brulion::platform
