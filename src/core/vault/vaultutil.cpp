#include "vault/vaultutil_p.h"

#include "util/paths.h"
#include "vault/vaultconfig.h"

#include <QRegularExpression>
#include <QSet>

using namespace Qt::StringLiterals;

namespace brulion {

Q_LOGGING_CATEGORY(lcVault, "brulion.vault")

namespace vaultutil {

QString configFolder()
{
    return u".brulion"_s;
}

QStringList ancestors(const VaultPath &path)
{
    QStringList out;
    for (qsizetype i = path.indexOf(u'/'); i >= 0; i = path.indexOf(u'/', i + 1))
        out.append(path.left(i));
    return out;
}

bool isWithin(const VaultPath &path, const VaultPath &dir)
{
    return dir.isEmpty() || path == dir || isStrictlyWithin(path, dir);
}

bool isStrictlyWithin(const VaultPath &path, const VaultPath &dir)
{
    if (dir.isEmpty())
        return !path.isEmpty();
    return path.size() > dir.size() && path.at(dir.size()) == u'/' && path.startsWith(dir);
}

VaultPath rebase(const VaultPath &path, const VaultPath &from, const VaultPath &to)
{
    if (path == from)
        return to;
    const QString rest = from.isEmpty() ? path : path.mid(from.size() + 1);
    if (to.isEmpty())
        return rest;
    return to + u'/' + rest;
}

VaultPath insertBeforeExtension(const VaultPath &path, const QString &suffix, bool isDir)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    const qsizetype dot = path.lastIndexOf(u'.');
    if (isDir || dot <= slash + 1)
        return path + suffix;
    return path.left(dot) + suffix + path.mid(dot);
}

VaultPath uniqueName(const VaultPath &base, const std::function<bool(const VaultPath &)> &isTaken, bool isDir)
{
    if (!isTaken(base))
        return base;
    for (int i = 2;; ++i) {
        const VaultPath candidate = insertBeforeExtension(base, u' ' + QString::number(i), isDir);
        if (!isTaken(candidate))
            return candidate;
    }
}

QString trashStamp(const QDateTime &time)
{
    return time.toString(u"yyyyMMdd-HHmmss"_s);
}

QString stripTrashStamp(const QString &segment)
{
    static const QRegularExpression stamp(u" \\(\\d{8}-\\d{6}\\)(?: \\d+)?(?=\\.[^.]*\\z|\\z)"_s);
    QString out = segment;
    return out.remove(stamp);
}

bool isTextPath(const VaultPath &path)
{
    static const QSet<QString> textExtensions = {
            u".md"_s,   u".markdown"_s, u".txt"_s, u".text"_s, u".json"_s, u".canvas"_s, u".excalidraw"_s, u".base"_s,
            u".csv"_s,  u".tsv"_s,      u".css"_s, u".html"_s, u".htm"_s,  u".xml"_s,    u".svg"_s,        u".yml"_s,
            u".yaml"_s, u".toml"_s,     u".ini"_s, u".ics"_s,  u".opml"_s, u".org"_s,    u".tex"_s,        u".rst"_s,
            u".adoc"_s, u".log"_s,      u".js"_s,  u".mjs"_s,  u".ts"_s,   u".edn"_s,
    };
    return textExtensions.contains(paths::extension(path));
}

bool isTracked(const VaultPath &path)
{
    return !path.isEmpty() && (path == vaultConfigPath() || !paths::isIgnored(path));
}

QByteArray utf8Bom()
{
    return QByteArrayLiteral("\xEF\xBB\xBF");
}

QString decodeUtf8(const QByteArray &bytes)
{
    if (bytes.startsWith(utf8Bom()))
        return QString::fromUtf8(QByteArrayView(bytes).sliced(3));
    return QString::fromUtf8(bytes);
}

} // namespace vaultutil
} // namespace brulion
