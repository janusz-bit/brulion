#include "io/exportutil_p.h"

#include "index/vaultindex.h"
#include "io/importutil_p.h"
#include "io/logseq_p.h"
#include "io/zip.h"
#include "markdown/journal.h"
#include "markdown/linkformat.h"
#include "util/paths.h"

#include <QRegularExpression>

#include <algorithm>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

ExportVault::ExportVault(const ExportInput &input) : m_input(input), m_index(std::make_unique<VaultIndex>(input.config))
{
    QList<FileEntry> files = input.files;
    std::sort(files.begin(), files.end(), [](const FileEntry &a, const FileEntry &b) { return a.path < b.path; });
    {
        VaultIndex::Batch batch(*m_index);
        for (const FileEntry &f : std::as_const(files)) {
            if (paths::isIgnored(f.path) || paths::isConflictCopy(f.path))
                continue;
            if (paths::isMarkdown(f.path)) {
                const QString content = decodeText(f.data);
                m_index->update(f.path, content);
                m_noteIndex.insert(f.path, m_notes.size());
                m_notes.append(Note{f.path, content, nullptr, markdown::journalDateFromPath(f.path, input.config)});
            } else {
                m_index->update(f.path, QString());
                m_attachmentIndex.insert(f.path, m_attachments.size());
                m_attachments.append(f);
            }
        }
    }
    for (Note &n : m_notes)
        n.parsed = m_index->note(n.path);
}

ExportVault::~ExportVault() = default;

const ExportVault::Note *ExportVault::note(const VaultPath &path) const
{
    const auto it = m_noteIndex.constFind(path);
    return it == m_noteIndex.constEnd() ? nullptr : &m_notes[*it];
}

bool ExportVault::isAttachment(const VaultPath &path) const
{
    return m_attachmentIndex.contains(path);
}

const FileEntry *ExportVault::attachment(const VaultPath &path) const
{
    const auto it = m_attachmentIndex.constFind(path);
    return it == m_attachmentIndex.constEnd() ? nullptr : &m_attachments[*it];
}

std::optional<VaultPath> ExportVault::resolve(const QString &target, const VaultPath &from) const
{
    if (target.isEmpty())
        return std::nullopt;
    // VaultIndex::resolveLink() reads '#' and '|' as fragment / display separators, but a parsed
    // Markdown target ("C#.md" from "[C#](C%23.md)") can hold them as part of the file name.
    if (target.contains(u'#') || target.contains(u'|')) {
        for (const QString &candidate : {paths::join(paths::dirname(from), target), paths::normalize(target)}) {
            for (const QString &p : {candidate, QString(candidate + u".md"_s)}) {
                if (note(p) || isAttachment(p))
                    return p;
            }
        }
    }
    return m_index->resolveLink(target, from);
}

std::optional<std::pair<VaultPath, int>> ExportVault::block(const QString &id) const
{
    const auto ref = m_index->blockById(id);
    if (!ref)
        return std::nullopt;
    return std::make_pair(ref->path, ref->blockIndex);
}

QString ExportVault::vaultName() const
{
    const QString name = m_input.vaultName.trimmed();
    return name.isEmpty() ? u"Brulion"_s : name;
}

QString ExportVault::fileName(const QString &suffix, const QString &extension) const
{
    QString out = pageSegment(vaultName());
    if (m_input.now.isValid())
        out += u'-' + m_input.now.date().toString(u"yyyy-MM-dd"_s);
    if (!suffix.isEmpty())
        out += u'-' + suffix;
    return out + extension;
}

QByteArray ExportVault::zip(const QList<FileEntry> &entries) const
{
    ZipWriteOptions o;
    o.modified = m_input.now;
    return writeZip(entries, o);
}

QString mimeForPath(const QString &path)
{
    static const QHash<QString, QString> map = {
            {u".md"_s, u"text/markdown"_s},    {u".markdown"_s, u"text/markdown"_s}, {u".txt"_s, u"text/plain"_s},
            {u".png"_s, u"image/png"_s},       {u".jpg"_s, u"image/jpeg"_s},         {u".jpeg"_s, u"image/jpeg"_s},
            {u".gif"_s, u"image/gif"_s},       {u".webp"_s, u"image/webp"_s},        {u".svg"_s, u"image/svg+xml"_s},
            {u".bmp"_s, u"image/bmp"_s},       {u".heic"_s, u"image/heic"_s},        {u".avif"_s, u"image/avif"_s},
            {u".pdf"_s, u"application/pdf"_s}, {u".mp3"_s, u"audio/mpeg"_s},         {u".m4a"_s, u"audio/mp4"_s},
            {u".wav"_s, u"audio/wav"_s},       {u".ogg"_s, u"audio/ogg"_s},          {u".mp4"_s, u"video/mp4"_s},
            {u".webm"_s, u"video/webm"_s},     {u".mov"_s, u"video/quicktime"_s},    {u".html"_s, u"text/html"_s},
            {u".css"_s, u"text/css"_s},        {u".json"_s, u"application/json"_s},  {u".csv"_s, u"text/csv"_s},
            {u".zip"_s, u"application/zip"_s}, {u".ics"_s, u"text/calendar"_s},      {u".opml"_s, u"text/x-opml"_s},
    };
    return map.value(paths::extension(path), u"application/octet-stream"_s);
}

bool isUuid(const QString &id)
{
    static const QRegularExpression re(
            u"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"_s);
    return re.match(id).hasMatch();
}

QString logseqUuid(const QString &id)
{
    return isUuid(id) ? id.toLower() : uuidFromSeed(u"brulion-block:"_s + id);
}

QString momentFormat(const QString &qtFormat)
{
    QString out;
    for (qsizetype i = 0; i < qtFormat.size();) {
        const QChar c = qtFormat[i];
        if (c == u'\'') {
            const qsizetype close = qtFormat.indexOf(u'\'', i + 1);
            const QString literal = qtFormat.sliced(i + 1, (close < 0 ? qtFormat.size() : close) - i - 1);
            out += u'[' + literal + u']';
            i = close < 0 ? qtFormat.size() : close + 1;
            continue;
        }
        qsizetype n = 1;
        while (i + n < qtFormat.size() && qtFormat[i + n] == c)
            ++n;
        const QString run = qtFormat.sliced(i, n);
        if (c == u'y')
            out += n == 2 ? u"YY"_s : u"YYYY"_s;
        else if (c == u'd')
            out += n == 1 ? u"D"_s : n == 2 ? u"DD"_s : run; // ddd / dddd are the same in moment
        else if (c == u'M')
            out += run;
        else if (c.isLetter())
            out += u'[' + run + u']';
        else
            out += run;
        i += n;
    }
    return out;
}

QString blockText(const ParsedNote &note, int index)
{
    if (index < 0 || index >= note.blocks.size())
        return note.title;
    const QString t = note.blocks[index].text.section(u'\n', 0, 0).trimmed();
    return t.isEmpty() ? note.title : t;
}

int linkedHeading(const ParsedNote &note, const QString &fragment)
{
    const int found = markdown::findHeading(note.headings, fragment);
    if (found < 0)
        return -1;
    for (int i = 0; i < note.blocks.size(); ++i) {
        const Block &b = note.blocks[i];
        if (b.parent < 0 && b.kind == BlockKind::Heading && b.line == note.headings.at(found).line)
            return i;
    }
    return -1;
}

QList<int> sectionBlocks(const ParsedNote &note, const QString &heading)
{
    QList<int> out;
    const int first = linkedHeading(note, heading);
    if (first < 0)
        return out;
    const int level = note.blocks[first].headingLevel;
    out.append(first);
    for (int i = first + 1; i < note.blocks.size(); ++i) {
        const Block &b = note.blocks[i];
        if (b.parent >= 0)
            continue;
        if (b.kind == BlockKind::Heading && b.headingLevel <= level)
            break;
        out.append(i);
    }
    return out;
}

QList<const ExportVault::Note *> orderedNotes(const ExportVault &vault)
{
    QList<const ExportVault::Note *> pages;
    QList<const ExportVault::Note *> journals;
    for (const ExportVault::Note &n : vault.notes())
        (n.journal ? journals : pages).append(&n);
    std::stable_sort(journals.begin(), journals.end(),
                     [](const ExportVault::Note *a, const ExportVault::Note *b) { return *a->journal > *b->journal; });
    return pages + journals;
}

} // namespace brulion::io::detail
