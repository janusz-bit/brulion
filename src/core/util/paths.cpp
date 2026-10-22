#include "util/paths.h"

#include <QRegularExpression>
#include <QSet>

namespace brulion::paths {

VaultPath normalize(const QString &path)
{
    QStringList parts;
    const QString unified = path.normalized(QString::NormalizationForm_C).replace(QLatin1Char('\\'), QLatin1Char('/'));
    for (const QString &seg : unified.split(QLatin1Char('/'))) {
        if (seg.isEmpty() || seg == QLatin1String("."))
            continue;
        if (seg == QLatin1String("..")) {
            if (!parts.isEmpty())
                parts.removeLast();
            continue;
        }
        parts.append(seg);
    }
    return parts.join(QLatin1Char('/'));
}

VaultPath join(const QString &a, const QString &b)
{
    if (a.isEmpty())
        return normalize(b);
    if (b.isEmpty())
        return normalize(a);
    return normalize(a + QLatin1Char('/') + b);
}

QString dirname(const VaultPath &path)
{
    const qsizetype i = path.lastIndexOf(QLatin1Char('/'));
    return i < 0 ? QString() : path.left(i);
}

QString basename(const VaultPath &path)
{
    const qsizetype i = path.lastIndexOf(QLatin1Char('/'));
    return i < 0 ? path : path.mid(i + 1);
}

QString extension(const VaultPath &path)
{
    const QString base = basename(path);
    const qsizetype i = base.lastIndexOf(QLatin1Char('.'));
    return i <= 0 ? QString() : base.mid(i).toLower();
}

bool isMarkdown(const VaultPath &path)
{
    const QString ext = extension(path);
    return ext == QLatin1String(".md") || ext == QLatin1String(".markdown");
}

const QStringList &pictureExtensions()
{
    using namespace Qt::StringLiterals;
    static const QStringList ext = {u".png"_s, u".jpg"_s, u".jpeg"_s, u".gif"_s, u".webp"_s,
                                    u".svg"_s, u".bmp"_s, u".avif"_s, u".ico"_s};
    return ext;
}

bool isPicturePath(const VaultPath &path)
{
    return pictureExtensions().contains(extension(path));
}

QString stripMarkdownExtension(const VaultPath &path)
{
    static const QRegularExpression re(QStringLiteral("\\.(md|markdown)$"), QRegularExpression::CaseInsensitiveOption);
    QString out = path;
    return out.remove(re);
}

QString pageTitle(const VaultPath &path)
{
    return basename(stripMarkdownExtension(path));
}

static const QSet<QString> &ignoredDirs()
{
    static const QSet<QString> dirs = {
        QStringLiteral(".brulion"), QStringLiteral(".trash"), QStringLiteral(".git"),
        QStringLiteral(".obsidian"), QStringLiteral("logseq"), QStringLiteral(".stfolder"),
        QStringLiteral(".stversions"), QStringLiteral("node_modules"), QStringLiteral(".github"),
    };
    return dirs;
}

bool isTempFile(const QString &name)
{
    static const QRegularExpression syncthingTmp(QStringLiteral("^~syncthing~.*\\.tmp$"));
    static const QRegularExpression syncthingTmp2(QStringLiteral("^\\.syncthing\\..*\\.tmp$"));
    static const QRegularExpression lockFile(QStringLiteral("^\\.~lock\\..*#$"));
    return syncthingTmp.match(name).hasMatch() || syncthingTmp2.match(name).hasMatch()
        || lockFile.match(name).hasMatch() || name.endsWith(QLatin1String(".part"))
        || name.startsWith(QLatin1String(".brulion-tmp-")) || name == QLatin1String(".DS_Store")
        || name == QLatin1String("Thumbs.db") || name == QLatin1String(".stignore")
        || name == QLatin1String(".nomedia");
}

bool isIgnored(const VaultPath &path)
{
    const QStringList segs = path.split(QLatin1Char('/'));
    for (const QString &s : segs) {
        if (ignoredDirs().contains(s))
            return true;
    }
    return segs.isEmpty() ? false : isTempFile(segs.last());
}

std::optional<SyncthingConflict> parseSyncthingConflict(const VaultPath &path)
{
    static const QRegularExpression re(
        QStringLiteral("^(.*)\\.sync-conflict-(\\d{8})-(\\d{6})-([A-Z0-9]{7})(\\.[^./]*)?$"));
    const auto m = re.match(basename(path));
    if (!m.hasMatch())
        return std::nullopt;
    const QString d = m.captured(2), t = m.captured(3);
    SyncthingConflict c;
    c.original = join(dirname(path), m.captured(1) + m.captured(5));
    c.time = d.left(4) + QLatin1Char('-') + d.mid(4, 2) + QLatin1Char('-') + d.mid(6, 2) + QLatin1Char('T')
        + t.left(2) + QLatin1Char(':') + t.mid(2, 2) + QLatin1Char(':') + t.mid(4, 2);
    c.device = m.captured(4);
    return c;
}

bool isConflictCopy(const VaultPath &path)
{
    static const QRegularExpression own(QStringLiteral("\\.conflict-[^/]*\\.md$"), QRegularExpression::CaseInsensitiveOption);
    return parseSyncthingConflict(path).has_value() || own.match(path).hasMatch();
}

QString sanitizeFileName(const QString &name)
{
    static const QRegularExpression reserved(QStringLiteral("[\\\\/:*?\"<>|#^\\[\\]\\x{0000}-\\x{001f}]"));
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    static const QRegularExpression leadingDots(QStringLiteral("^\\.+"));
    QString out = name.normalized(QString::NormalizationForm_C);
    out.replace(reserved, QStringLiteral(" "));
    out.replace(spaces, QStringLiteral(" "));
    out = out.trimmed();
    out.remove(leadingDots);
    out = out.left(180).trimmed();
    return out.isEmpty() ? QStringLiteral("Untitled") : out;
}

VaultPath uniquePath(const VaultPath &path, const std::function<bool(const VaultPath &)> &exists)
{
    if (!exists(path))
        return path;
    const QString ext = extension(path);
    const QString stem = ext.isEmpty() ? path : path.left(path.size() - ext.size());
    for (int i = 2;; ++i) {
        const QString candidate = stem + QLatin1Char(' ') + QString::number(i) + ext;
        if (!exists(candidate))
            return candidate;
    }
}

} // namespace brulion::paths
