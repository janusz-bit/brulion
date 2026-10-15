#include "io/importutil_p.h"

#include "markdown/frontmatter.h"
#include "markdown/inline.h"
#include "markdown/journal.h"
#include "text/text.h"
#include "util/paths.h"

#include <QRegularExpression>
#include <QStringDecoder>

#include <algorithm>
#include <array>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

const QSet<QString> &imageExtensions()
{
    static const QSet<QString> ext = {u".png"_s,  u".jpg"_s,  u".jpeg"_s, u".gif"_s, u".webp"_s, u".svg"_s, u".bmp"_s,
                                      u".heic"_s, u".heif"_s, u".avif"_s, u".tif"_s, u".tiff"_s, u".ico"_s};
    return ext;
}

const QSet<QString> &embeddableExtensions()
{
    static const QSet<QString> ext = {u".pdf"_s,  u".mp3"_s, u".m4a"_s, u".wav"_s, u".ogg"_s, u".opus"_s, u".flac"_s,
                                      u".webm"_s, u".mp4"_s, u".mov"_s, u".m4v"_s, u".3gp"_s, u".aac"_s};
    return ext;
}

QString stripTrailingDotsAndSpaces(QString s)
{
    while (!s.isEmpty() && (s.back() == u'.' || s.back().isSpace()))
        s.chop(1);
    return s;
}

/// File systems allow 255 bytes per name (ext4, F2FS, exFAT): a segment keeps room for the
/// extension and a " 99" uniqueness suffix.
constexpr qsizetype MaxSegmentBytes = 200;

/// `s` shortened to at most `maxBytes` bytes of UTF-8, never inside a surrogate pair (a lone
/// surrogate left by an earlier cut is dropped too).
QString fitUtf8(QString s, qsizetype maxBytes)
{
    qsizetype bytes = 0;
    qsizetype i = 0;
    for (; i < s.size(); ++i) {
        const char16_t c = s[i].unicode();
        qsizetype width = c < 0x80 ? 1 : c < 0x800 ? 2 : 3;
        if (QChar::isHighSurrogate(c)) {
            if (i + 1 >= s.size() || !QChar::isLowSurrogate(s[i + 1].unicode()))
                break; // lone high surrogate
            width = 4;
        } else if (QChar::isLowSurrogate(c)) {
            break; // lone low surrogate
        }
        if (bytes + width > maxBytes)
            break;
        bytes += width;
        if (width == 4)
            ++i;
    }
    s.truncate(i);
    return s;
}

} // namespace

QString pathKey(const QString &path)
{
    return paths::normalize(path).toCaseFolded();
}

PathClaims::PathClaims(std::function<bool(const VaultPath &)> exists) : m_exists(std::move(exists)) { }

bool PathClaims::isTaken(const VaultPath &path) const
{
    return m_keys.contains(pathKey(path)) || (m_exists && m_exists(path));
}

VaultPath PathClaims::claim(const VaultPath &path)
{
    // Like paths::uniquePath() (" 2", " 3", … before the extension), but the search resumes after
    // the suffix found last time: claims only grow, so smaller numbers are still taken. Thousands
    // of "Untitled" notes stay linear.
    VaultPath free = path;
    if (isTaken(path)) {
        const QString ext = paths::extension(path);
        const QString stem = path.first(path.size() - ext.size());
        const QString suffixExt = path.sliced(stem.size()); // the extension as written
        int &next = m_nextSuffix[pathKey(path)];
        for (int i = std::max(next, 2);; ++i) {
            const QString candidate = stem + u' ' + QString::number(i) + suffixExt;
            if (!isTaken(candidate)) {
                free = candidate;
                next = i + 1;
                break;
            }
        }
    }
    m_keys.insert(pathKey(free));
    return free;
}

void PathClaims::reserve(const VaultPath &path)
{
    m_keys.insert(pathKey(path));
}

QString pageSegment(const QString &name)
{
    const QString s = stripTrailingDotsAndSpaces(fitUtf8(paths::sanitizeFileName(name), MaxSegmentBytes));
    return s.isEmpty() ? u"Untitled"_s : s;
}

void addTitleAlias(QVariantMap &frontmatter, const QString &title)
{
    const QString t = title.trimmed();
    if (t.isEmpty() || pageSegment(t) == t)
        return;
    QStringList aliases = frontmatter.value(u"aliases"_s).toStringList();
    if (!aliases.contains(t))
        aliases.prepend(t);
    frontmatter.insert(u"aliases"_s, aliases);
}

QString attachmentFileName(const QString &name)
{
    static const QRegularExpression extRe(u"\\.([A-Za-z0-9]{1,10})$"_s);
    const QString trimmed = name.trimmed();
    const auto m = extRe.match(trimmed);
    if (!m.hasMatch() || m.capturedStart() == 0)
        return pageSegment(trimmed);
    return pageSegment(trimmed.first(m.capturedStart())) + u'.' + m.captured(1).toLower();
}

QString safeRelativePath(const QString &relative, bool attachment)
{
    QStringList parts = QString(relative).replace(u'\\', u'/').split(u'/', Qt::SkipEmptyParts);
    parts.removeAll(u"."_s);
    parts.removeAll(u".."_s);
    for (qsizetype k = 0; k < parts.size(); ++k) {
        const bool last = k + 1 == parts.size();
        parts[k] = last && attachment ? attachmentFileName(parts[k]) : pageSegment(parts[k]);
    }
    if (parts.isEmpty())
        parts.append(u"Untitled"_s);
    return parts.join(u'/');
}

ImportContext::ImportContext(const ImportOptions &options, const QString &importerName)
    : m_options(options), m_claims(options.exists)
{
    m_options.targetFolder = paths::normalize(options.targetFolder);
    QString subfolder;
    if (!options.attachmentSubfolder.trimmed().isEmpty()) {
        subfolder = safeRelativePath(options.attachmentSubfolder, false);
    } else if (!m_options.targetFolder.isEmpty()) {
        subfolder = pageSegment(paths::basename(m_options.targetFolder));
    } else {
        subfolder = pageSegment(importerName);
    }
    QString base = paths::normalize(options.config.attachmentsFolder);
    if (base.isEmpty())
        base = u"assets"_s;
    m_attachmentFolder = paths::join(base, subfolder);
}

VaultPath ImportContext::pagePath(const QString &relative) const
{
    const QString rel = safeRelativePath(paths::stripMarkdownExtension(relative), false);
    return paths::join(m_options.targetFolder, rel + u".md"_s);
}

VaultPath ImportContext::claimPage(const QString &relative)
{
    return m_claims.claim(pagePath(relative));
}

VaultPath ImportContext::claimAttachment(const QString &relative)
{
    return m_claims.claim(paths::join(m_attachmentFolder, safeRelativePath(relative, true)));
}

VaultPath ImportContext::journalPath(QDate date) const
{
    return markdown::journalPathForDate(date, m_options.config);
}

QString ImportContext::journalLinkTarget(QDate date) const
{
    const VaultPath path = journalPath(date);
    const QString title = paths::pageTitle(path);
    // The title is unique when it holds the whole date (it parses back to `date`).
    for (const QString &format :
         {m_options.config.journalFileFormat.section(u'/', -1), u"yyyy-MM-dd"_s, u"yyyy_MM_dd"_s}) {
        if (!format.isEmpty() && QDate::fromString(title, format) == date)
            return title;
    }
    return paths::stripMarkdownExtension(path);
}

void ImportContext::addPage(const VaultPath &path, const QString &content)
{
    m_result.files.append(ImportedFile{path, content.toUtf8(), false});
}

VaultPath ImportContext::addJournal(QDate date, const QString &content)
{
    const VaultPath path = journalPath(date);
    const QString key = pathKey(path);
    const auto it = m_journalIndex.constFind(key);
    if (it != m_journalIndex.constEnd()) {
        ImportedFile &file = m_result.files[*it];
        file.data = appendImported(file.data, content.toUtf8());
        return path;
    }
    m_journalIndex.insert(key, m_result.files.size());
    m_result.files.append(ImportedFile{path, content.toUtf8(), true});
    return path;
}

VaultPath ImportContext::addAttachment(const QString &relative, const QByteArray &data)
{
    const QString key = pathKey(safeRelativePath(relative, true));
    QList<std::pair<QByteArray, VaultPath>> &stored = m_attachments[key];
    for (const auto &[bytes, path] : std::as_const(stored)) {
        if (bytes == data)
            return path;
    }
    const VaultPath path = claimAttachment(relative);
    stored.append({data, path});
    addFile(path, data);
    return path;
}

void ImportContext::addFile(const VaultPath &path, const QByteArray &data)
{
    m_result.files.append(ImportedFile{path, data, false});
}

void ImportContext::warn(const QString &message)
{
    if (!m_result.warnings.contains(message))
        m_result.warnings.append(message);
}

ImportResult ImportContext::take()
{
    ImportResult out = std::move(m_result);
    m_result = ImportResult();
    m_journalIndex.clear();
    m_attachments.clear();
    return out;
}

// --- SourceTree ----------------------------------------------------------------------------------

void SourceTree::add(const QString &source, const VaultPath &destination)
{
    m_bestByBaseName.clear();
    const QString key = pathKey(source);
    if (!m_sources.contains(key)) {
        const QString base = pathKey(paths::basename(source));
        m_byBaseName[base].append(source);
        const QString stripped = pathKey(paths::stripMarkdownExtension(paths::basename(source)));
        if (stripped != base)
            m_byBaseName[stripped].append(source);
    }
    m_sources.insert(key, source);
    m_destinations.insert(key, destination);
    if (paths::isMarkdown(destination))
        m_pagesByTitle[pathKey(paths::pageTitle(destination))].insert(pathKey(destination));
}

QString SourceTree::wikiTarget(const VaultPath &destination) const
{
    const QString title = paths::pageTitle(destination);
    return m_pagesByTitle.value(pathKey(title)).size() > 1 ? paths::stripMarkdownExtension(destination) : title;
}

bool SourceTree::contains(const QString &source) const
{
    return m_sources.contains(pathKey(source));
}

VaultPath SourceTree::destination(const QString &source) const
{
    return m_destinations.value(pathKey(source));
}

std::optional<QString> SourceTree::resolve(const QString &target, const QString &fromSource, bool wiki) const
{
    QString t = target.trimmed();
    t.replace(u'\\', u'/');
    if (t.isEmpty())
        return std::nullopt;
    const auto lookup = [&](const QString &p) -> std::optional<QString> {
        if (p.isEmpty())
            return std::nullopt;
        const auto it = m_sources.constFind(pathKey(p));
        if (it != m_sources.constEnd())
            return *it;
        if (!paths::isMarkdown(p)) {
            const auto md = m_sources.constFind(pathKey(p + u".md"_s));
            if (md != m_sources.constEnd())
                return *md;
        }
        return std::nullopt;
    };
    const bool rooted = t.startsWith(u'/');
    if (!rooted) {
        if (auto r = lookup(paths::join(paths::dirname(fromSource), t)))
            return r;
    }
    if (auto r = lookup(paths::normalize(t)))
        return r;
    if (!wiki)
        return std::nullopt;
    const auto best = [](const QStringList &candidates) -> std::optional<QString> {
        if (candidates.isEmpty())
            return std::nullopt;
        return *std::min_element(candidates.begin(), candidates.end(), [](const QString &a, const QString &b) {
            return a.size() != b.size() ? a.size() < b.size() : a < b;
        });
    };
    const QString normalized = paths::normalize(t);
    if (!normalized.contains(u'/')) {
        // Many pages may share a base name ("Untitled", "README"): the pick is cached.
        const QString key = pathKey(normalized);
        const auto cached = m_bestByBaseName.constFind(key);
        if (cached != m_bestByBaseName.constEnd())
            return *cached;
        const std::optional<QString> picked = best(m_byBaseName.value(key));
        m_bestByBaseName.insert(key, picked);
        return picked;
    }
    // A path suffix ("Folder/Page" for "Vault/Folder/Page.md"): candidates share the base name.
    const QString suffix = u'/' + pathKey(normalized);
    QStringList candidates;
    for (const QString &source : m_byBaseName.value(pathKey(paths::basename(normalized)))) {
        const QString key = pathKey(source);
        if ((key.endsWith(suffix) || key.endsWith(suffix + u".md"_s)) && !candidates.contains(source))
            candidates.append(source);
    }
    return best(candidates);
}

QString SourceTree::rewrite(const QString &content, const QString &source, PageLinks style) const
{
    const VaultPath self = destination(source);
    const QString selfDir = paths::dirname(self);
    return replaceLinks(content, [&](const FoundLink &l) -> std::optional<QString> {
        if (l.target.isEmpty())
            return std::nullopt;
        switch (l.kind) {
        case LinkKind::Wiki:
        case LinkKind::Embed: {
            const auto src = resolve(l.target, source, true);
            if (!src)
                return std::nullopt;
            const VaultPath to = destination(*src);
            const bool page = paths::isMarkdown(to);
            const QString newTarget = page ? paths::stripMarkdownExtension(to) : to;
            if (!l.target.contains(u'/') && !l.target.contains(u'\\')) {
                const QString written = page ? paths::stripMarkdownExtension(l.target) : l.target;
                if (pathKey(paths::basename(newTarget)) == pathKey(written))
                    return std::nullopt; // a bare name still resolves by base name
            }
            return wikiLink(newTarget, l.heading, l.blockId, l.display, l.kind == LinkKind::Embed);
        }
        case LinkKind::Markdown: {
            if (l.hasUrlScheme)
                return std::nullopt;
            const auto src = resolve(l.target, source, false);
            if (!src)
                return std::nullopt;
            const VaultPath to = destination(*src);
            if (style == PageLinks::Wiki && paths::isMarkdown(to) && !l.image) {
                QString label = l.display.trimmed();
                const QString target = wikiTarget(to);
                if (label.contains(u"]]"_s) || label.contains(u'|') || label.contains(u'['))
                    label = QString();
                return wikiLink(target, l.heading, l.blockId,
                                label.compare(paths::basename(target), Qt::CaseInsensitive) == 0 ? QString() : label,
                                false);
            }
            const QString relative = relativePath(selfDir, to);
            QString written = l.destination;
            if (written.startsWith(u'<') && written.endsWith(u'>'))
                written = written.sliced(1, written.size() - 2);
            const qsizetype hash = written.indexOf(u'#');
            if (percentDecode(hash < 0 ? written : written.first(hash)) == relative)
                return std::nullopt;
            return markdownLink(l.display, encodeLinkPath(relative) + markdownFragment(l.heading, l.blockId), l.image,
                                l.title);
        }
        default: return std::nullopt;
        }
    });
}

// --- small helpers -------------------------------------------------------------------------------

namespace {

/// windows-1252 bytes 0x80–0x9F (0xA0–0xFF are Latin-1); undefined bytes keep their C1 code.
constexpr std::array<char16_t, 32> Cp1252C1 = {0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                               0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
                                               0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                               0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

/// windows-1250 bytes 0x80–0xFF (Polish, Czech, Hungarian, … Windows "ANSI").
constexpr std::array<char16_t, 128> Cp1250High = {
        0x20AC, 0x0081, 0x201A, 0x0083, 0x201E, 0x2026, 0x2020, 0x2021, 0x0088, 0x2030, 0x0160, 0x2039, 0x015A,
        0x0164, 0x017D, 0x0179, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0098, 0x2122,
        0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A, 0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6,
        0x00A7, 0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B, 0x00B0, 0x00B1, 0x02DB, 0x0142,
        0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C, 0x0154,
        0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7, 0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD,
        0x00CE, 0x010E, 0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7, 0x0158, 0x016E, 0x00DA,
        0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF, 0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
        0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F, 0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4,
        0x0151, 0x00F6, 0x00F7, 0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9};

/// Does legacy 8-bit text look like windows-1250 rather than windows-1252? Bytes of Polish
/// letters that are rare symbols in windows-1252 (ą ł ś ź Ś Ź Ą Ż = ¹ ³ œ Ÿ Œ – ¥ ¯) are weighed
/// against Western letters that windows-1250 lacks (à è ì ò ù å ã õ ø …).
bool looksLikeCp1250(const QByteArray &data)
{
    int polish = 0;
    int western = 0;
    for (const char ch : data) {
        switch (static_cast<unsigned char>(ch)) {
        case 0xB9:
        case 0xB3:
        case 0x9C:
        case 0x9F:
        case 0x8C:
        case 0x8F:
        case 0xA5:
        case 0xAF: ++polish; break;
        case 0xC0:
        case 0xC3:
        case 0xC5:
        case 0xC8:
        case 0xCC:
        case 0xD2:
        case 0xD5:
        case 0xD8:
        case 0xD9:
        case 0xE0:
        case 0xE3:
        case 0xE5:
        case 0xE8:
        case 0xEC:
        case 0xF2:
        case 0xF5:
        case 0xF8:
        case 0xF9: ++western; break;
        default: break;
        }
    }
    return polish > western;
}

/// 8-bit text that is not UTF-8: windows-1250 when it looks Polish (see looksLikeCp1250()),
/// else windows-1252.
QString decodeLegacy(const QByteArray &data)
{
    const bool cp1250 = looksLikeCp1250(data);
    QString out(data.size(), Qt::Uninitialized);
    QChar *dst = out.data();
    for (const char ch : data) {
        const auto b = static_cast<unsigned char>(ch);
        char16_t c = b;
        if (b >= 0x80)
            c = cp1250 ? Cp1250High[b - 0x80] : b < 0xA0 ? Cp1252C1[b - 0x80] : char16_t(b);
        *dst++ = QChar(c);
    }
    return out;
}

} // namespace

QString decodeText(const QByteArray &data)
{
    if (data.startsWith("\xEF\xBB\xBF"))
        return QString::fromUtf8(data.sliced(3));
    if (data.startsWith("\xFF\xFE") || data.startsWith("\xFE\xFF")) {
        QStringDecoder decoder(data.startsWith("\xFF\xFE") ? QStringConverter::Utf16LE : QStringConverter::Utf16BE);
        return decoder(data.sliced(2));
    }
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = utf8(data);
    if (!utf8.hasError())
        return text;
    return decodeLegacy(data);
}

QString wallClock(const QDateTime &dateTime, const QTimeZone &zone)
{
    if (!dateTime.isValid())
        return {};
    return dateTime.toTimeZone(zone).toString(u"yyyy-MM-dd HH:mm"_s);
}

QString noteWithFrontmatter(const QVariantMap &data, const QStringList &order, const QString &body)
{
    QString out = data.isEmpty() ? QString() : markdown::stringifyFrontmatter(data, order);
    const QString trimmedBody = normalizeNewlines(body);
    if (!trimmedBody.isEmpty())
        out += trimmedBody + u'\n';
    if (out.isEmpty())
        return out;
    return out.endsWith(u'\n') ? out : out + u'\n';
}

QString normalizeNewlines(const QString &text)
{
    QString out = text;
    out.replace(u"\r\n"_s, u"\n"_s).replace(u'\r', u'\n');
    QStringList lines = out.split(u'\n');
    for (QString &l : lines) {
        while (!l.isEmpty() && (l.back() == u' ' || l.back() == u'\t') && !l.endsWith(u"  "_s))
            l.chop(1);
    }
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty())
        lines.removeFirst();
    return lines.join(u'\n');
}

QString attachmentLink(const VaultPath &path, const QString &label)
{
    const QString ext = paths::extension(path);
    if (imageExtensions().contains(ext) || embeddableExtensions().contains(ext))
        return wikiLink(path, {}, {}, {}, true);
    return wikiLink(path, {}, {}, label.isEmpty() ? paths::basename(path) : label, false);
}

QString extensionForMime(const QString &mime)
{
    static const QHash<QString, QString> map = {
            {u"image/png"_s, u".png"_s},
            {u"image/jpeg"_s, u".jpg"_s},
            {u"image/jpg"_s, u".jpg"_s},
            {u"image/gif"_s, u".gif"_s},
            {u"image/webp"_s, u".webp"_s},
            {u"image/svg+xml"_s, u".svg"_s},
            {u"image/bmp"_s, u".bmp"_s},
            {u"image/heic"_s, u".heic"_s},
            {u"image/tiff"_s, u".tif"_s},
            {u"application/pdf"_s, u".pdf"_s},
            {u"audio/mpeg"_s, u".mp3"_s},
            {u"audio/mp3"_s, u".mp3"_s},
            {u"audio/mp4"_s, u".m4a"_s},
            {u"audio/x-m4a"_s, u".m4a"_s},
            {u"audio/aac"_s, u".aac"_s},
            {u"audio/wav"_s, u".wav"_s},
            {u"audio/x-wav"_s, u".wav"_s},
            {u"audio/ogg"_s, u".ogg"_s},
            {u"audio/amr"_s, u".amr"_s},
            {u"audio/3gpp"_s, u".3gp"_s},
            {u"video/mp4"_s, u".mp4"_s},
            {u"video/quicktime"_s, u".mov"_s},
            {u"video/webm"_s, u".webm"_s},
            {u"text/plain"_s, u".txt"_s},
            {u"text/html"_s, u".html"_s},
            {u"text/csv"_s, u".csv"_s},
            {u"application/json"_s, u".json"_s},
            {u"application/zip"_s, u".zip"_s},
            {u"application/msword"_s, u".doc"_s},
            {u"application/vnd.openxmlformats-officedocument.wordprocessingml.document"_s, u".docx"_s},
            {u"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"_s, u".xlsx"_s},
            {u"application/vnd.openxmlformats-officedocument.presentationml.presentation"_s, u".pptx"_s},
            {u"application/vnd.oasis.opendocument.text"_s, u".odt"_s},
            {u"application/vnd.oasis.opendocument.spreadsheet"_s, u".ods"_s},
    };
    return map.value(mime.trimmed().toLower().section(u';', 0, 0).trimmed());
}

QString tagName(const QString &text)
{
    static const QRegularExpression spaces(u"\\s+"_s);
    QString s = text.trimmed();
    while (s.startsWith(u'#'))
        s.remove(0, 1);
    s.replace(spaces, u"-"_s);
    QString out;
    for (const QChar c : std::as_const(s)) {
        if (c.isLetterOrNumber() || c == u'_' || c == u'-' || c == u'/' || c.category() == QChar::Mark_NonSpacing)
            out += c;
    }
    while (out.startsWith(u'/') || out.startsWith(u'-'))
        out.remove(0, 1);
    while (out.endsWith(u'/'))
        out.chop(1);
    const bool digitsOnly = std::all_of(out.cbegin(), out.cend(), [](QChar c) { return c.isDigit(); });
    return digitsOnly ? QString() : out;
}

QString titleFromText(const QString &text, int maxLength)
{
    static const QRegularExpression prefix(u"^(?:#{1,6}\\s+|[-*+]\\s+(?:\\[.\\]\\s+)?|\\[.\\]\\s+)"_s);
    for (const QString &rawLine : normalizeNewlines(text).split(u'\n')) {
        QString line = rawLine.trimmed();
        line.remove(prefix);
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        if (line.size() > maxLength) {
            const qsizetype space = line.lastIndexOf(u' ', maxLength);
            qsizetype cut = space > maxLength / 2 ? space : maxLength;
            if (cut > 0 && line[cut - 1].isHighSurrogate())
                --cut; // never split a surrogate pair (emoji)
            line = line.first(cut).trimmed();
        }
        return line;
    }
    return {};
}

bool anyPath(const QList<InputFile> &files, const std::function<bool(const QString &)> &pred)
{
    return std::any_of(files.begin(), files.end(), [&](const InputFile &f) { return pred(f.path); });
}

QList<InputFile> stripCommonRoot(const QList<InputFile> &files, const std::function<bool(const QString &)> &keep)
{
    QList<InputFile> out = files;
    for (;;) {
        if (out.isEmpty())
            return out;
        QString root;
        for (const InputFile &f : std::as_const(out)) {
            const qsizetype slash = f.path.indexOf(u'/');
            if (slash <= 0)
                return out;
            const QString first = f.path.first(slash);
            if (root.isEmpty())
                root = first;
            else if (root != first)
                return out;
        }
        if (keep && keep(root))
            return out;
        for (InputFile &f : out)
            f.path = f.path.sliced(root.size() + 1);
    }
}

QString mapLinesOutsideCode(const QString &content,
                            const std::function<QString(const QString &line, const QString &masked)> &fn)
{
    const QList<text::Line> lines = text::splitLines(content);
    QString out;
    out.reserve(content.size());
    QString fence;
    for (const text::Line &l : lines) {
        const QStringView trimmed = QStringView(l.text).trimmed();
        QStringView bare = trimmed;
        // A fence may follow a list marker or quote prefix ("- ```js", "> ```").
        while (bare.startsWith(u'>') || bare.startsWith(u"- ") || bare.startsWith(u"* ") || bare.startsWith(u"+ "))
            bare = bare.sliced(bare.startsWith(u'>') ? 1 : 2).trimmed();
        if (!fence.isEmpty()) {
            if (bare.startsWith(fence) && bare.trimmed().size() == bare.count(fence[0]))
                fence.clear();
            out += l.text + l.eol;
            continue;
        }
        if (bare.startsWith(u"```") || bare.startsWith(u"~~~")) {
            qsizetype n = 0;
            while (n < bare.size() && bare[n] == bare[0])
                ++n;
            fence = bare.first(n).toString();
            out += l.text + l.eol;
            continue;
        }
        out += fn(l.text, markdown::maskInlineCode(l.text)) + l.eol;
    }
    return out;
}

SourceTree importTree(const QList<InputFile> &files, ImportContext &ctx, const TreeImport &options)
{
    SourceTree tree;
    struct Page {
        const InputFile *file;
        VaultPath path;
    };
    QList<Page> pages;
    QList<Page> attachments;
    const auto defaultIsPage = [](const QString &p) { return paths::isMarkdown(p); };
    for (const InputFile &f : files) {
        if (paths::isIgnored(f.path) || (options.skip && options.skip(f.path)))
            continue;
        const bool page = options.isPage ? options.isPage(f.path) : defaultIsPage(f.path);
        if (page) {
            QString rel = options.pageRelative ? options.pageRelative(f.path) : f.path;
            const qsizetype dot = rel.lastIndexOf(u'.');
            if (!options.pageRelative && dot > rel.lastIndexOf(u'/'))
                rel.truncate(dot);
            pages.append(Page{&f, ctx.claimPage(rel)});
        } else {
            const QString rel = options.attachmentRelative ? options.attachmentRelative(f.path) : f.path;
            attachments.append(Page{&f, ctx.claimAttachment(rel)});
        }
    }
    for (const Page &p : std::as_const(pages))
        tree.add(p.file->path, p.path);
    for (const Page &a : std::as_const(attachments))
        tree.add(a.file->path, a.path);
    for (const Page &p : std::as_const(pages)) {
        QString content = decodeText(p.file->data);
        if (options.convert)
            content = options.convert(content, p.file->path);
        ctx.addPage(p.path, tree.rewrite(content, p.file->path, options.links));
    }
    for (const Page &a : std::as_const(attachments))
        ctx.addFile(a.path, a.file->data);
    return tree;
}

} // namespace brulion::io::detail
