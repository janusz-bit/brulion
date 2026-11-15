#include "util/paths.h"
#include "util/unicode.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <array>

namespace brulion::paths {

VaultPath normalize(const QString &path)
{
    const QString unified = unicode::nfc(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
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

namespace {

/// The folders that are never notes: the app's own, the trash, what git, Obsidian, Logseq,
/// Syncthing and npm keep in a notebook's folder. Some of them hold code those programs run
/// (.git/hooks, .git/config, .obsidian/plugins).
constexpr std::array<QLatin1StringView, 9> kIgnoredDirs = {
        QLatin1StringView(".brulion"),    QLatin1StringView(".trash"),       QLatin1StringView(".git"),
        QLatin1StringView(".obsidian"),   QLatin1StringView("logseq"),       QLatin1StringView(".stfolder"),
        QLatin1StringView(".stversions"), QLatin1StringView("node_modules"), QLatin1StringView(".github"),
};

/// Code points a file system may leave out when it compares names (Unicode's
/// Default_Ignorable_Code_Point: the soft hyphen, zero-width spaces and joiners, direction marks,
/// variation selectors, tags). The case-folding directories of Linux — Android's shared storage on
/// newer phones — and HFS+ do: ".g<zero-width joiner>it" is the folder ".git" there.
constexpr bool isIgnorableCodePoint(char32_t c)
{
    return c == 0x00AD || c == 0x034F || c == 0x061C || (c >= 0x115F && c <= 0x1160) || (c >= 0x17B4 && c <= 0x17B5)
           || (c >= 0x180B && c <= 0x180F) || (c >= 0x200B && c <= 0x200F) || (c >= 0x202A && c <= 0x202E)
           || (c >= 0x2060 && c <= 0x206F) || c == 0x3164 || (c >= 0xFE00 && c <= 0xFE0F) || c == 0xFEFF || c == 0xFFA0
           || (c >= 0xFFF0 && c <= 0xFFF8) || (c >= 0x1BCA0 && c <= 0x1BCA3) || (c >= 0x1D173 && c <= 0x1D17A)
           || (c >= 0xE0000 && c <= 0xE0FFF);
}

/// A name every storage reads as it is written: ASCII, no dot at its start (no hidden folder), no
/// `:` (NTFS streams) or `~` (short names), no dot or space at its end (Windows drops them).
bool isPlainName(QStringView name)
{
    if (name.front() == u'.' || name.back() == u'.' || name.back() == u' ')
        return false;
    return std::ranges::all_of(name, [](QChar c) { return c.unicode() < 128 && c != u':' && c != u'~'; });
}

/// Windows' short name of a folder whose own name is none — "GIT~1" for ".git", "OBSIDI~1" for
/// ".obsidian": the first letters of the name without its dot, `~` and a number; after four of
/// a kind, the first two letters and four hex digits of a hash ("GI1A2B~1"). `upper`: upper case.
bool isShortNameOf(QStringView upper, QLatin1StringView dir)
{
    const bool hidden = dir.startsWith(u'.');
    if (!hidden && dir.size() <= 8)
        return false; // a short name of its own ("logseq")
    const QLatin1StringView stem = hidden ? dir.sliced(1) : dir;
    const qsizetype tilde = upper.indexOf(u'~');
    if (tilde < 2 || tilde > 6 || tilde + 1 >= upper.size())
        return false;
    const auto isDigit = [](QChar c) { return c >= u'0' && c <= u'9'; };
    if (!std::ranges::all_of(upper.sliced(tilde + 1), isDigit))
        return false;
    const QStringView head = upper.first(tilde);
    // (The folders' names are ASCII: a letter of the short name is the name's in upper case.)
    const auto sameLetter = [](QChar a, char b) { return a.unicode() == QChar::toUpper(char32_t(uchar(b))); };
    const auto startsStem = [&](qsizetype letters) {
        return letters <= stem.size() && std::ranges::equal(head.first(letters), stem.first(letters), sameLetter);
    };
    if (head.size() >= std::min<qsizetype>(3, stem.size()) && startsStem(head.size()))
        return true;
    const auto isHex = [&](QChar c) { return isDigit(c) || (c >= u'A' && c <= u'F'); };
    return head.size() == 6 && startsStem(2) && std::ranges::all_of(head.sliced(2), isHex);
}

/// Is `segment` another name of an ignored folder on some storage? What the folder is called
/// decides where a file lands, and the name need not be the one written: storage that folds
/// case (Windows, Android's shared storage, FAT) reads ".GIT" and ".Obsidian" as the folders
/// themselves; Windows drops the dots and spaces at a name's end (".git." and ".git " are .git),
/// reads an NTFS stream after a colon (".git::$INDEX_ALLOCATION" is the folder) and knows every
/// folder by a short name too ("GIT~1"); case-folding directories skip ignorable code points.
/// A file from a server, a repository or an archive under such a name would be a git hook or an
/// Obsidian plugin. Visible names keep their case: "Import/Logseq/…" is a folder of notes.
bool isIgnoredElsewhere(QStringView segment)
{
    // The name without ignorable code points, an NTFS stream and the end Windows drops.
    QString name;
    name.reserve(segment.size());
    for (qsizetype i = 0; i < segment.size(); ++i) {
        const QChar c = segment[i];
        if (c.isHighSurrogate() && i + 1 < segment.size() && segment[i + 1].isLowSurrogate()) {
            if (!isIgnorableCodePoint(QChar::surrogateToUcs4(c, segment[i + 1])))
                name += segment.sliced(i, 2);
            ++i;
        } else if (!isIgnorableCodePoint(c.unicode())) {
            name += c;
        }
    }
    const bool hidden = name.startsWith(u'.');
    if (const qsizetype colon = name.indexOf(u':'); colon >= 0) {
        // (A visible name before any colon is a title, "logseq: tips.md"; before a stream type
        // it is the folder.)
        if (hidden || name.indexOf(QLatin1StringView(":$"), colon) >= 0)
            name.truncate(colon);
    }
    while (name.endsWith(u'.') || name.endsWith(u' '))
        name.chop(1);
    if (name.isEmpty())
        return false;
    // Folded both ways: storage compares names in upper case (NTFS: "ı" and "ſ" become "I" and
    // "S") or case-folded (Linux: "ſ" and the Kelvin sign become "s" and "k").
    const QString folded = hidden ? name.toCaseFolded() : QString();
    const QString upper = name.toUpper();
    for (const QLatin1StringView dir : kIgnoredDirs) {
        if (name == dir || isShortNameOf(upper, dir))
            return true;
        if (hidden && dir.startsWith(u'.') && (folded == dir || upper == QString(dir).toUpper()))
            return true;
    }
    return false;
}

bool isIgnoredDir(QStringView segment)
{
    if (segment.isEmpty())
        return false;
    // As written — every name a scan meets, most of them plain: nine comparisons of lengths.
    if (std::ranges::any_of(kIgnoredDirs, [&](QLatin1StringView dir) { return segment == dir; }))
        return true;
    return !isPlainName(segment) && isIgnoredElsewhere(segment);
}

} // namespace

bool isTempFile(const QString &name)
{
    // (The expressions only for names that start like their matches: a scan asks this of every
    // file of the notebook.)
    static const QRegularExpression syncthingTmp(QStringLiteral("^~syncthing~.*\\.tmp$"));
    static const QRegularExpression syncthingTmp2(QStringLiteral("^\\.syncthing\\..*\\.tmp$"));
    static const QRegularExpression lockFile(QStringLiteral("^\\.~lock\\..*#$"));
    return (name.startsWith(u'~') && syncthingTmp.match(name).hasMatch())
           || (name.startsWith(u'.') && (syncthingTmp2.match(name).hasMatch() || lockFile.match(name).hasMatch()))
           || name.endsWith(QLatin1String(".part")) || name.startsWith(QLatin1String(".brulion-tmp-"))
           || name == QLatin1String(".DS_Store") || name == QLatin1String("Thumbs.db")
           || name == QLatin1String(".stignore") || name == QLatin1String(".nomedia");
}

bool isIgnored(const VaultPath &path)
{
    QStringView last; // (never without a segment: "" gives one empty segment)
    for (const QStringView segment : QStringView(path).tokenize(u'/')) {
        if (isIgnoredDir(segment))
            return true;
        last = segment;
    }
    const QString name = last.toString();
    // (A hidden file in any letter case, as the folders above: ".STIGNORE" is Syncthing's
    // .stignore on storage that folds case.)
    return isTempFile(name) || (name.startsWith(u'.') && isTempFile(name.toLower()));
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
    QString out = unicode::nfc(name);
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
