#include "util/paths.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace brulion::paths {

VaultPath normalize(const QString &path)
{
    const QString unified = path.normalized(QString::NormalizationForm_C).replace(QLatin1Char('\\'), QLatin1Char('/'));
    // Most paths are clean already (no empty, "." or ".." segment): as they are, without
    // splitting and joining (resolving links normalizes several paths per link).
    bool clean = !unified.isEmpty();
    for (qsizetype start = 0; clean && start <= unified.size();) {
        qsizetype end = unified.indexOf(QLatin1Char('/'), start);
        if (end < 0)
            end = unified.size();
        const QStringView segment = QStringView(unified).sliced(start, end - start);
        clean = !segment.isEmpty() && segment != QLatin1String(".") && segment != QLatin1String("..");
        start = end + 1;
    }
    if (clean)
        return unified;
    QStringList parts;
    for (const QStringView seg : unified.tokenize(u'/', Qt::SkipEmptyParts)) {
        if (seg == QLatin1String("..")) {
            if (!parts.isEmpty())
                parts.removeLast();
        } else if (seg != QLatin1String(".")) {
            parts.append(seg.toString());
        }
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

bool isAttachmentExtension(QStringView ext)
{
    constexpr qsizetype MaxExtensionLength = 10;
    if (ext.size() < 2 || ext.size() > MaxExtensionLength + 1 || ext[0] != u'.')
        return false;
    if (ext.compare(QLatin1StringView(".md"), Qt::CaseInsensitive) == 0
        || ext.compare(QLatin1StringView(".markdown"), Qt::CaseInsensitive) == 0)
        return false;
    bool letter = false;
    for (const QChar c : ext.sliced(1)) {
        const bool alpha = (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
        if (!alpha && !(c >= u'0' && c <= u'9'))
            return false;
        letter = letter || alpha;
    }
    return letter;
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

bool isPdfPath(const VaultPath &path)
{
    // (Not extension(): a hidden file ".pdf" has none there, but it is a PDF by its name.)
    return basename(path).endsWith(QLatin1StringView(".pdf"), Qt::CaseInsensitive);
}

QString stripMarkdownExtension(const VaultPath &path)
{
    // (Called for every link and title: no regular expression.)
    for (const QLatin1StringView ext : {QLatin1StringView(".md"), QLatin1StringView(".markdown")}) {
        if (path.endsWith(ext, Qt::CaseInsensitive))
            return path.chopped(ext.size());
    }
    return path;
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
    const QStringList segs = path.split(QLatin1Char('/')); // never empty: "" gives {""}
    return std::ranges::any_of(segs, [](const QString &s) { return ignoredDirs().contains(s); })
           || isTempFile(segs.last());
}

std::optional<SyncthingConflict> parseSyncthingConflict(const VaultPath &path)
{
    static const QRegularExpression re(
        QStringLiteral("^(.*)\\.sync-conflict-(\\d{8})-(\\d{6})-([A-Z0-9]{7})(\\.[^./]*)?$"));
    const auto m = re.match(basename(path));
    if (!m.hasMatch())
        return std::nullopt;
    const QStringView d = m.capturedView(2), t = m.capturedView(3); // "20261014", "101112"
    SyncthingConflict c;
    c.original = join(dirname(path), m.captured(1) + m.captured(5));
    c.time = QStringLiteral("%1-%2-%3T%4:%5:%6") .arg(d.first(4), d.sliced(4, 2), d.sliced(6, 2), t.first(2),
                                                      t.sliced(2, 2), t.sliced(4, 2));
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
    constexpr qsizetype MaxLength = 180;
    QString out = name.normalized(QString::NormalizationForm_C);
    out.replace(reserved, QStringLiteral(" "));
    out.replace(spaces, QStringLiteral(" "));
    // No leading dot (a hidden file), not even behind a space: ". .x" -> "x", not ". x".
    qsizetype lead = 0;
    while (lead < out.size() && (out.at(lead) == u'.' || out.at(lead).isSpace()))
        ++lead;
    out.remove(0, lead);
    // At most MaxLength units, but never half of a surrogate pair (an emoji).
    if (out.size() > MaxLength)
        out.truncate(out.at(MaxLength - 1).isHighSurrogate() ? MaxLength - 1 : MaxLength);
    out = std::move(out).trimmed();
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
