#include "attachments/documenttext.h"

#include <QHash>
#include <QIODevice>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QUrl>
#include <QXmlStreamReader>

#include <miniz.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace brulion::attachments {

using namespace Qt::StringLiterals;

namespace {

/// More than any deflate stream can expand (about 1032:1), with room for headers.
constexpr quint64 MaxDeflateRatio = 1100;
/// The most of the central directory read into memory (10,000 entries with long names fit).
constexpr qint64 MaxDirectoryBytes = qint64(16) << 20;
/// The tail searched for the end-of-central-directory record (its comment is at most 65,535 bytes).
constexpr qint64 EndRecordSearch = 65'557;
/// The most spaces one ODF `text:s` stands for.
constexpr int MaxOdfSpaces = 1000;
/// Tokens between two asks of `cancelled`.
constexpr int CancelEvery = 4096;

// ---- Where the text goes ------------------------------------------------------------------------

/// Paragraphs collected under the text limit of a whole document.
struct TextBudget {
    qsizetype left = 0;
    bool truncated = false;
    bool exhausted() const { return left <= 0; }
    /// `text` cut to what is left.
    QString take(QString text)
    {
        if (text.size() > left) {
            text.truncate(std::max<qsizetype>(0, left));
            truncated = true;
        }
        left -= text.size();
        return text;
    }
};

/// The paragraphs of one part of the document being read, in order.
struct Paragraphs {
    QStringList list;
    TextBudget *budget = nullptr;
    bool full() const { return budget->exhausted(); }
    void add(const QString &text)
    {
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty() || full())
            return;
        list.append(budget->take(trimmed));
        budget->left -= 1; // (the line break between paragraphs)
    }
};

/// Pages of about `target` characters: paragraphs are joined by "\n" and a page ends at the first
/// paragraph end past `target` (a paragraph longer than that stays whole: `MaxPageText` cuts it
/// when the record is made).
QStringList paginate(const QStringList &paragraphs, qsizetype target)
{
    QStringList pages;
    QString page;
    for (const QString &paragraph : paragraphs) {
        if (!page.isEmpty())
            page += u'\n';
        page += paragraph;
        if (page.size() >= target) {
            pages.append(std::move(page));
            page.clear();
        }
    }
    if (!page.isEmpty())
        pages.append(std::move(page));
    return pages;
}

// ---- The archive --------------------------------------------------------------------------------

size_t readDevice(void *opaque, mz_uint64 offset, void *buffer, size_t count)
{
    auto *device = static_cast<QIODevice *>(opaque);
    if (offset > quint64(std::numeric_limits<qint64>::max()) || !device->seek(qint64(offset)))
        return 0;
    qint64 done = 0;
    auto *out = static_cast<char *>(buffer);
    while (quint64(done) < count) {
        const qint64 n = device->read(out + done, qint64(count) - done);
        if (n <= 0)
            break;
        done += n;
    }
    return size_t(done);
}

enum class Part : quint8 { Ok, Missing, Locked, Refused };

/// A ZIP archive read through a device: the entries by name, a part unpacked at a time.
class Archive {
public:
    Archive(QIODevice &device, const DocumentLimits &limits) : m_limits(limits), m_device(device) { }
    ~Archive()
    {
        if (m_open)
            mz_zip_reader_end(&m_zip);
    }
    Archive(const Archive &) = delete;
    Archive &operator=(const Archive &) = delete;

    /// False for a file that is no ZIP archive or names too many entries.
    bool open()
    {
        const qint64 size = m_device.size();
        if (size < 22 || size > m_limits.maxFileBytes || !directoryFits(size))
            return false;
        m_zip = {};
        m_zip.m_pRead = readDevice;
        m_zip.m_pIO_opaque = &m_device;
        if (!mz_zip_reader_init(&m_zip, mz_uint64(size), 0))
            return false;
        m_open = true;
        const mz_uint count = mz_zip_reader_get_num_files(&m_zip);
        if (int(count) > m_limits.maxEntries)
            return false;
        for (mz_uint i = 0; i < count; ++i) {
            const mz_uint length = mz_zip_reader_get_filename(&m_zip, i, nullptr, 0);
            if (length <= 1)
                continue;
            QByteArray raw(qsizetype(length), '\0');
            mz_zip_reader_get_filename(&m_zip, i, raw.data(), length);
            raw.truncate(qsizetype(length) - 1);
            const QString name = normalized(QString::fromUtf8(raw));
            if (!name.isEmpty() && !name.endsWith(u'/') && !m_index.contains(name))
                m_index.insert(name, int(i));
        }
        return true;
    }

    bool contains(const QString &name) const { return m_index.contains(normalized(name)); }
    /// The entries' names in archive order.
    QStringList names() const
    {
        QList<std::pair<int, QString>> sorted;
        for (auto it = m_index.cbegin(); it != m_index.cend(); ++it)
            sorted.append({it.value(), it.key()});
        std::ranges::sort(sorted);
        QStringList out;
        for (const auto &entry : std::as_const(sorted))
            out.append(entry.second);
        return out;
    }
    bool anyLocked() const { return m_locked; }
    bool budgetSpent() const { return m_spent; }

    /// The unpacked bytes of the part `name`.
    Part read(const QString &name, QByteArray &out)
    {
        out.clear();
        const auto it = m_index.constFind(normalized(name));
        if (it == m_index.constEnd())
            return Part::Missing;
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&m_zip, mz_uint(*it), &stat))
            return Part::Refused;
        if (stat.m_is_encrypted) {
            m_locked = true;
            return Part::Locked;
        }
        if (!stat.m_is_supported || stat.m_is_directory)
            return Part::Refused;
        // A size its compressed bytes cannot give (a forged header): no memory is set aside for it.
        if (stat.m_method == 0 ? stat.m_uncomp_size != stat.m_comp_size
                               : stat.m_uncomp_size > (stat.m_comp_size + 1) * MaxDeflateRatio)
            return Part::Refused;
        if (stat.m_uncomp_size > quint64(m_limits.maxPartBytes))
            return Part::Refused;
        if (m_total + qint64(stat.m_uncomp_size) > m_limits.maxTotalBytes) {
            m_spent = true;
            return Part::Refused;
        }
        if (stat.m_uncomp_size > 0) {
            out.resize(qsizetype(stat.m_uncomp_size));
            if (!mz_zip_reader_extract_to_mem(&m_zip, mz_uint(*it), out.data(), size_t(out.size()), 0)) {
                out.clear();
                return Part::Refused;
            }
        }
        m_total += out.size();
        return Part::Ok;
    }

private:
    /// "\" → "/", no leading "/", "." and ".." resolved inside the archive.
    static QString normalized(QString name)
    {
        name.replace(u'\\', u'/');
        QStringList kept;
        for (const QString &segment : name.split(u'/')) {
            if (segment.isEmpty() || segment == u"."_s)
                continue;
            if (segment == u".."_s) {
                if (!kept.isEmpty())
                    kept.removeLast();
                continue;
            }
            kept.append(segment);
        }
        return kept.join(u'/') + (name.endsWith(u'/') ? u"/"_s : QString());
    }

    /// The end-of-central-directory record is read first: miniz reads the whole directory into
    /// memory, so a file naming millions of entries is refused before that.
    bool directoryFits(qint64 size)
    {
        const qint64 tail = std::min(size, EndRecordSearch);
        QByteArray bytes(qsizetype(tail), '\0');
        if (readDevice(&m_device, mz_uint64(size - tail), bytes.data(), size_t(tail)) != size_t(tail))
            return false;
        for (qsizetype i = bytes.size() - 22; i >= 0; --i) {
            const auto *p = reinterpret_cast<const unsigned char *>(bytes.constData() + i);
            if (p[0] != 'P' || p[1] != 'K' || p[2] != 5 || p[3] != 6)
                continue;
            const quint32 entries = quint32(p[10]) | (quint32(p[11]) << 8);
            const quint32 directory = quint32(p[12]) | (quint32(p[13]) << 8) | (quint32(p[14]) << 16)
                                      | (quint32(p[15]) << 24);
            // (65,535 entries / a 4 GB directory: a ZIP64 archive, none of the documents.)
            return entries != 0xFFFF && directory != 0xFFFFFFFFu && int(entries) <= m_limits.maxEntries
                   && qint64(directory) <= std::min(MaxDirectoryBytes, size);
        }
        return false;
    }

    DocumentLimits m_limits;
    QIODevice &m_device;
    mz_zip_archive m_zip{};
    bool m_open = false;
    bool m_locked = false;
    bool m_spent = false;
    qint64 m_total = 0;
    QHash<QString, int> m_index;
};

// ---- XML ----------------------------------------------------------------------------------------

/// A reader that never reads anything outside the part: no external entity, the DTD's own entities
/// refused.
void configure(QXmlStreamReader &xml)
{
    xml.setEntityExpansionLimit(256);
    xml.setNamespaceProcessing(true);
}

/// The value of the attribute `name` (its local name; any namespace).
QString attribute(const QXmlStreamReader &xml, QStringView name)
{
    for (const QXmlStreamAttribute &a : xml.attributes()) {
        if (a.name() == name)
            return a.value().toString();
    }
    return {};
}

/// The value of the attribute `name` whose namespace holds `namespacePart` (`r:id` among `id`s).
QString attributeIn(const QXmlStreamReader &xml, QStringView name, QLatin1StringView namespacePart)
{
    for (const QXmlStreamAttribute &a : xml.attributes()) {
        if (a.name() == name && a.namespaceUri().contains(namespacePart))
            return a.value().toString();
    }
    return {};
}

bool inNamespace(const QXmlStreamReader &xml, QLatin1StringView part)
{
    return xml.namespaceUri().contains(part);
}

/// A DOCTYPE that declares entities (nothing legitimate in these formats does) — a part to refuse.
bool declaresEntities(const QXmlStreamReader &xml)
{
    return !xml.entityDeclarations().isEmpty();
}

/// What an entity of HTML 4 that XML does not know stands for (an EPUB 2's XHTML names them without
/// declaring them: the reader cannot resolve them).
QString htmlEntity(QStringView name)
{
    static const QHash<QString, QString> known = {
            {u"nbsp"_s, u" "_s},      {u"shy"_s, QString()},         {u"ensp"_s, u" "_s},
            {u"emsp"_s, u" "_s},      {u"thinsp"_s, u" "_s},         {u"ndash"_s, u"–"_s},
            {u"mdash"_s, u"—"_s},     {u"hellip"_s, u"…"_s},         {u"laquo"_s, u"«"_s},
            {u"raquo"_s, u"»"_s},     {u"ldquo"_s, u"“"_s},          {u"rdquo"_s, u"”"_s},
            {u"lsquo"_s, u"‘"_s},     {u"rsquo"_s, u"’"_s},          {u"bdquo"_s, u"„"_s},
            {u"copy"_s, u"©"_s},      {u"reg"_s, u"®"_s},            {u"trade"_s, u"™"_s},
            {u"deg"_s, u"°"_s},       {u"middot"_s, u"·"_s},         {u"bull"_s, u"•"_s},
            {u"times"_s, u"×"_s},     {u"eacute"_s, u"é"_s},         {u"egrave"_s, u"è"_s},
            {u"agrave"_s, u"à"_s},    {u"ouml"_s, u"ö"_s},           {u"uuml"_s, u"ü"_s},
            {u"auml"_s, u"ä"_s},      {u"szlig"_s, u"ß"_s},          {u"ccedil"_s, u"ç"_s},
    };
    return known.value(name.toString());
}

// ---- DOCX / PPTX / ODT: paragraphs --------------------------------------------------------------

enum class Dialect : quint8 { Word, Slide, OpenDocument };

/// The paragraphs of an Office Open XML or OpenDocument part: a stack of open paragraphs (a text
/// box or a footnote inside one nests), the text pieces by dialect. False: stopped by `cancelled`.
bool readParagraphs(QByteArrayView data, Dialect dialect, Paragraphs &out, const std::function<bool()> &cancelled)
{
    QXmlStreamReader xml(data.toByteArray());
    configure(xml);
    QStringList open;
    int tokens = 0;
    const auto append = [&open, &out](const QString &text) {
        if (open.isEmpty())
            out.add(text); // (text outside any paragraph: its own)
        else
            open.last() += text;
    };
    while (!xml.atEnd() && !out.full()) {
        if (++tokens % CancelEvery == 0 && cancelled && cancelled())
            return false;
        switch (xml.readNext()) {
        case QXmlStreamReader::DTD:
            if (declaresEntities(xml))
                return true; // (what was read stays)
            break;
        case QXmlStreamReader::StartElement: {
            const QStringView name = xml.name();
            if (inNamespace(xml, "markup-compatibility"_L1) && name == u"Fallback") {
                xml.skipCurrentElement(); // (the same text again, for programs that lack the first)
                break;
            }
            switch (dialect) {
            case Dialect::Word:
                if (!inNamespace(xml, "wordprocessingml"_L1))
                    break;
                if (name == u"p")
                    open.append(QString());
                else if (name == u"t")
                    append(xml.readElementText(QXmlStreamReader::SkipChildElements));
                else if (name == u"tab")
                    append(u"\t"_s);
                else if (name == u"br" || name == u"cr")
                    append(u"\n"_s);
                else if (name == u"noBreakHyphen")
                    append(u"-"_s);
                break;
            case Dialect::Slide:
                if (!inNamespace(xml, "drawingml"_L1))
                    break;
                if (name == u"p")
                    open.append(QString());
                else if (name == u"t")
                    append(xml.readElementText(QXmlStreamReader::SkipChildElements));
                else if (name == u"br")
                    append(u"\n"_s);
                break;
            case Dialect::OpenDocument:
                if (inNamespace(xml, "opendocument:xmlns:text"_L1)) {
                    if (name == u"p" || name == u"h") {
                        open.append(QString());
                    } else if (name == u"s") {
                        bool ok = false;
                        const int count = attribute(xml, u"c").toInt(&ok);
                        append(QString(ok ? std::clamp(count, 1, MaxOdfSpaces) : 1, u' '));
                    } else if (name == u"tab") {
                        append(u"\t"_s);
                    } else if (name == u"line-break") {
                        append(u"\n"_s);
                    } else if (name == u"tracked-changes") {
                        xml.skipCurrentElement(); // (text deleted by a tracked change)
                    } else if (name == u"note-citation") {
                        xml.skipCurrentElement(); // (a footnote's number)
                    }
                } else if (name == u"creator" || name == u"date" || name == u"date-string") {
                    xml.skipCurrentElement(); // (the author and the date of a comment)
                }
                break;
            }
            break;
        }
        case QXmlStreamReader::Characters:
            // Only the OpenDocument text is loose in its paragraphs; the others' is in w:t / a:t.
            if (dialect == Dialect::OpenDocument && !open.isEmpty())
                open.last() += xml.text();
            break;
        case QXmlStreamReader::EndElement: {
            const QStringView name = xml.name();
            const bool paragraph = dialect == Dialect::OpenDocument
                                           ? (inNamespace(xml, "opendocument:xmlns:text"_L1)
                                              && (name == u"p" || name == u"h"))
                                           : (name == u"p"
                                              && inNamespace(xml, dialect == Dialect::Word ? "wordprocessingml"_L1
                                                                                           : "drawingml"_L1));
            if (paragraph && !open.isEmpty()) {
                const QString text = open.takeLast();
                out.add(text);
            }
            break;
        }
        default: break;
        }
    }
    // A cut part: the paragraphs still open keep the text they have.
    while (!open.isEmpty() && !out.full())
        out.add(open.takeLast());
    return true;
}

// ---- Relationships and the EPUB package --------------------------------------------------------

/// The part `target` names from the part `base` (a path in the archive): "/x" from the root, others
/// relative to `base`'s folder; a fragment or a query dropped, percent-decoding undone.
QString resolveTarget(const QString &base, QString target)
{
    for (const QChar mark : {u'#', u'?'}) {
        if (const qsizetype at = target.indexOf(mark); at >= 0)
            target.truncate(at);
    }
    target = QUrl::fromPercentEncoding(target.toUtf8());
    if (target.startsWith(u'/'))
        return target.mid(1);
    const qsizetype slash = base.lastIndexOf(u'/');
    return slash < 0 ? target : base.first(slash + 1) + target;
}

struct Relationship {
    QString id;
    QString type;
    QString target;
};

QList<Relationship> readRelationships(QByteArrayView data)
{
    QList<Relationship> out;
    QXmlStreamReader xml(data.toByteArray());
    configure(xml);
    while (!xml.atEnd() && out.size() < 5000) {
        if (xml.readNext() == QXmlStreamReader::DTD && declaresEntities(xml))
            break;
        if (xml.isStartElement() && xml.name() == u"Relationship")
            out.append({attribute(xml, u"Id"), attribute(xml, u"Type"), attribute(xml, u"Target")});
    }
    return out;
}

/// The part of a relationships file next to `part` ("ppt/presentation.xml" →
/// "ppt/_rels/presentation.xml.rels").
QString relationshipsOf(const QString &part)
{
    const qsizetype slash = part.lastIndexOf(u'/');
    return (slash < 0 ? QString() : part.first(slash + 1)) + u"_rels/"_s + part.mid(slash + 1) + u".rels"_s;
}

// ---- The formats --------------------------------------------------------------------------------

struct Context {
    Archive &archive;
    const DocumentLimits &limits;
    const std::function<bool()> &cancelled;
    TextBudget budget;
    bool cancelledNow = false;
    bool locked = false;
};

/// A part's paragraphs (an unreadable part: none; Part::Locked / Missing in `status`).
Part readPart(Context &c, const QString &name, Dialect dialect, Paragraphs &out)
{
    QByteArray data;
    const Part status = c.archive.read(name, data);
    if (status != Part::Ok)
        return status;
    if (!readParagraphs(data, dialect, out, c.cancelled))
        c.cancelledNow = true;
    return Part::Ok;
}

DocumentText readWord(Context &c)
{
    DocumentText result;
    QString main = u"word/document.xml"_s;
    QByteArray rels;
    if (c.archive.read(u"_rels/.rels"_s, rels) == Part::Ok) {
        for (const Relationship &r : readRelationships(rels)) {
            if (r.type.endsWith(u"/officeDocument"_s) && !r.target.isEmpty()) {
                main = resolveTarget(QString(), r.target);
                break;
            }
        }
    }
    Paragraphs paragraphs;
    paragraphs.budget = &c.budget;
    const Part status = readPart(c, main, Dialect::Word, paragraphs);
    if (status == Part::Locked)
        c.locked = true;
    if (status != Part::Ok)
        return result; // (unreadable)
    result.status = DocumentText::Status::Ok;
    // The footnotes and endnotes after the text (the parts a main part's relationships name: in
    // Word's own layout they are next to it).
    const QString folder = main.left(main.lastIndexOf(u'/') + 1);
    for (const QString &note : QStringList{folder + u"footnotes.xml"_s, folder + u"endnotes.xml"_s}) {
        if (c.cancelledNow || paragraphs.full())
            break;
        readPart(c, note, Dialect::Word, paragraphs);
    }
    result.pages = paginate(paragraphs.list, c.limits.partChars);
    return result;
}

DocumentText readSlides(Context &c)
{
    DocumentText result;
    QString presentation = u"ppt/presentation.xml"_s;
    QByteArray rels;
    if (c.archive.read(u"_rels/.rels"_s, rels) == Part::Ok) {
        for (const Relationship &r : readRelationships(rels)) {
            if (r.type.endsWith(u"/officeDocument"_s) && !r.target.isEmpty()) {
                presentation = resolveTarget(QString(), r.target);
                break;
            }
        }
    }
    QStringList slides;
    QByteArray list;
    if (c.archive.read(presentation, list) == Part::Ok && c.archive.read(relationshipsOf(presentation), rels) == Part::Ok) {
        QHash<QString, QString> targets;
        for (const Relationship &r : readRelationships(rels))
            targets.insert(r.id, resolveTarget(presentation, r.target));
        QXmlStreamReader xml(list);
        configure(xml);
        while (!xml.atEnd() && slides.size() < MaxPages) {
            if (xml.readNext() == QXmlStreamReader::DTD && declaresEntities(xml))
                break;
            if (xml.isStartElement() && xml.name() == u"sldId") {
                const QString target = targets.value(attributeIn(xml, u"id", "relationships"_L1));
                if (!target.isEmpty() && !slides.contains(target))
                    slides.append(target);
            }
        }
    }
    if (slides.isEmpty()) {
        // No (readable) list: the slide files in the order of their numbers.
        static const QRegularExpression slide(u"^ppt/slides/slide(\\d+)\\.xml$"_s);
        QList<std::pair<int, QString>> numbered;
        for (const QString &name : c.archive.names()) {
            const QRegularExpressionMatch m = slide.match(name);
            if (m.hasMatch())
                numbered.append({m.captured(1).toInt(), name});
        }
        std::ranges::sort(numbered);
        for (const auto &entry : std::as_const(numbered))
            slides.append(entry.second);
    }
    if (slides.isEmpty() && !c.archive.contains(presentation))
        return result; // (no presentation at all)
    result.status = DocumentText::Status::Ok;
    for (const QString &slide : std::as_const(slides)) {
        if (c.cancelledNow || result.pages.size() >= MaxPages)
            break;
        Paragraphs paragraphs;
        paragraphs.budget = &c.budget;
        const Part status = readPart(c, slide, Dialect::Slide, paragraphs);
        if (status == Part::Locked)
            c.locked = true;
        result.pages.append(paragraphs.list.join(u'\n'));
    }
    return result;
}

DocumentText readOpenDocument(Context &c)
{
    DocumentText result;
    QByteArray manifest;
    if (c.archive.read(u"META-INF/manifest.xml"_s, manifest) == Part::Ok && manifest.contains("encryption-data")) {
        c.locked = true; // (encrypted parts: their bytes would be no XML)
        return result;
    }
    Paragraphs paragraphs;
    paragraphs.budget = &c.budget;
    const Part status = readPart(c, u"content.xml"_s, Dialect::OpenDocument, paragraphs);
    if (status == Part::Locked)
        c.locked = true;
    if (status != Part::Ok)
        return result;
    result.status = DocumentText::Status::Ok;
    result.pages = paginate(paragraphs.list, c.limits.partChars);
    return result;
}

/// XHTML elements that end a paragraph (before and after them).
bool isBlock(QStringView name)
{
    static const QSet<QString> blocks = {
            u"p"_s,     u"div"_s,        u"h1"_s,         u"h2"_s,    u"h3"_s,     u"h4"_s,     u"h5"_s,
            u"h6"_s,    u"li"_s,         u"ul"_s,         u"ol"_s,    u"dl"_s,     u"dt"_s,     u"dd"_s,
            u"tr"_s,    u"td"_s,         u"th"_s,         u"table"_s, u"caption"_s, u"blockquote"_s,
            u"section"_s, u"article"_s,  u"aside"_s,      u"header"_s, u"footer"_s, u"nav"_s,    u"pre"_s,
            u"hr"_s,    u"br"_s,         u"figure"_s,     u"figcaption"_s, u"address"_s, u"body"_s,
            u"title"_s, u"main"_s,       u"details"_s,    u"summary"_s,
    };
    return blocks.contains(name.toString());
}

/// The body's text of an XHTML part, a paragraph at every block element.
bool readXhtml(QByteArrayView data, Paragraphs &out, const std::function<bool()> &cancelled)
{
    QXmlStreamReader xml(data.toByteArray());
    configure(xml);
    QString current;
    int tokens = 0;
    const auto flush = [&] {
        if (!current.isEmpty()) {
            out.add(current.simplified());
            current.clear();
        }
    };
    while (!xml.atEnd() && !out.full()) {
        if (++tokens % CancelEvery == 0 && cancelled && cancelled())
            return false;
        switch (xml.readNext()) {
        case QXmlStreamReader::DTD:
            // (An XHTML DOCTYPE only names its DTD: nothing is fetched. Own entities: refused.)
            if (declaresEntities(xml))
                return true;
            break;
        case QXmlStreamReader::StartElement: {
            const QStringView name = xml.name();
            if (name == u"script" || name == u"style" || name == u"head" || name == u"svg" || name == u"rp"
                || name == u"noscript" || name == u"template") {
                xml.skipCurrentElement();
                break;
            }
            if (isBlock(name))
                flush();
            break;
        }
        case QXmlStreamReader::EndElement:
            if (isBlock(xml.name()))
                flush();
            break;
        case QXmlStreamReader::Characters:
            current += xml.text();
            break;
        case QXmlStreamReader::EntityReference:
            current += htmlEntity(xml.name());
            break;
        default: break;
        }
    }
    flush();
    return true;
}

DocumentText readEpub(Context &c)
{
    DocumentText result;
    QByteArray bytes;
    QString package;
    if (c.archive.read(u"META-INF/container.xml"_s, bytes) == Part::Ok) {
        QXmlStreamReader xml(bytes);
        configure(xml);
        while (!xml.atEnd()) {
            if (xml.readNext() == QXmlStreamReader::DTD && declaresEntities(xml))
                break;
            if (xml.isStartElement() && xml.name() == u"rootfile") {
                package = resolveTarget(QString(), attribute(xml, u"full-path"));
                break;
            }
        }
    }
    // The spine: the package's items in reading order. Without a readable package: every XHTML
    // file by its name.
    QStringList files;
    if (!package.isEmpty() && c.archive.read(package, bytes) == Part::Ok) {
        struct Item {
            QString href;
            QString type;
        };
        QHash<QString, Item> manifest;
        QStringList spine;
        QXmlStreamReader xml(bytes);
        configure(xml);
        while (!xml.atEnd() && manifest.size() < 100'000 && spine.size() < 100'000) {
            if (xml.readNext() == QXmlStreamReader::DTD && declaresEntities(xml))
                break;
            if (!xml.isStartElement())
                continue;
            if (xml.name() == u"item")
                manifest.insert(attribute(xml, u"id"), {attribute(xml, u"href"), attribute(xml, u"media-type")});
            else if (xml.name() == u"itemref")
                spine.append(attribute(xml, u"idref"));
        }
        for (const QString &id : std::as_const(spine)) {
            const auto it = manifest.constFind(id);
            if (it == manifest.constEnd() || it->href.isEmpty())
                continue;
            const bool text = it->type.isEmpty() || it->type.contains(u"html"_s) || it->type.contains(u"dtbook"_s);
            const QString file = resolveTarget(package, it->href);
            if (text && !files.contains(file))
                files.append(file);
        }
    }
    if (files.isEmpty()) {
        if (package.isEmpty() && !c.archive.contains(u"mimetype"_s))
            return result; // (no EPUB at all)
        for (const QString &name : c.archive.names()) {
            const QString lower = name.toLower();
            if (lower.endsWith(u".xhtml"_s) || lower.endsWith(u".html"_s) || lower.endsWith(u".htm"_s))
                files.append(name);
        }
        std::ranges::sort(files);
    }
    result.status = DocumentText::Status::Ok;
    for (const QString &file : std::as_const(files)) {
        if (c.cancelledNow || c.budget.exhausted() || result.pages.size() >= MaxPages)
            break;
        QByteArray data;
        const Part status = c.archive.read(file, data);
        if (status == Part::Locked)
            c.locked = true;
        if (status != Part::Ok)
            continue; // (a part missing or refused: the rest is read)
        Paragraphs paragraphs;
        paragraphs.budget = &c.budget;
        if (!readXhtml(data, paragraphs, c.cancelled))
            c.cancelledNow = true;
        if (paragraphs.list.isEmpty())
            continue; // (a cover, a title page of pictures: no page for it)
        result.pages.append(paginate(paragraphs.list, c.limits.epubUnitChars));
    }
    return result;
}

} // namespace

std::optional<DocumentFormat> documentFormatOf(QStringView path)
{
    struct Extension {
        QStringView suffix;
        DocumentFormat format;
    };
    static constexpr Extension extensions[] = {{u".docx", DocumentFormat::Docx},
                                               {u".pptx", DocumentFormat::Pptx},
                                               {u".odt", DocumentFormat::Odt},
                                               {u".epub", DocumentFormat::Epub}};
    for (const Extension &e : extensions) {
        if (path.endsWith(e.suffix, Qt::CaseInsensitive) && path.size() > e.suffix.size())
            return e.format;
    }
    return std::nullopt;
}

Kind kindOf(DocumentFormat format)
{
    switch (format) {
    case DocumentFormat::Docx: return Kind::Docx;
    case DocumentFormat::Pptx: return Kind::Pptx;
    case DocumentFormat::Odt: return Kind::Odt;
    case DocumentFormat::Epub: return Kind::Epub;
    }
    return Kind::Docx;
}

std::optional<DocumentFormat> documentFormatOf(Kind kind)
{
    switch (kind) {
    case Kind::Docx: return DocumentFormat::Docx;
    case Kind::Pptx: return DocumentFormat::Pptx;
    case Kind::Odt: return DocumentFormat::Odt;
    case Kind::Epub: return DocumentFormat::Epub;
    case Kind::Pdf:
    case Kind::Picture: return std::nullopt;
    }
    return std::nullopt;
}

bool isDocumentKind(Kind kind)
{
    return documentFormatOf(kind).has_value();
}

QString kindName(Kind kind)
{
    switch (kind) {
    case Kind::Pdf: return u"pdf"_s;
    case Kind::Picture: return u"picture"_s;
    case Kind::Pptx: return u"presentation"_s;
    case Kind::Docx:
    case Kind::Odt:
    case Kind::Epub: return u"document"_s;
    }
    return u"document"_s;
}

std::optional<DocumentText> extractDocumentText(QIODevice &device, DocumentFormat format, const DocumentLimits &limits,
                                                const std::function<bool()> &cancelled)
{
    DocumentText result;
    if (!device.isOpen() || !device.isReadable() || device.isSequential())
        return result;
    if (device.size() > limits.maxFileBytes) {
        result.status = DocumentText::Status::TooLarge;
        return result;
    }
    Archive archive(device, limits);
    if (!archive.open())
        return result;
    Context context{archive, limits, cancelled, TextBudget{limits.maxTextChars}};
    switch (format) {
    case DocumentFormat::Docx: result = readWord(context); break;
    case DocumentFormat::Pptx: result = readSlides(context); break;
    case DocumentFormat::Odt: result = readOpenDocument(context); break;
    case DocumentFormat::Epub: result = readEpub(context); break;
    }
    if (context.cancelledNow || (cancelled && cancelled()))
        return std::nullopt;
    if (context.locked && result.status != DocumentText::Status::Ok)
        result.status = DocumentText::Status::Locked;
    result.truncated = context.budget.truncated || archive.budgetSpent();
    return result;
}

Record documentRecord(DocumentFormat format, const DocumentText &text, quint32 extractor)
{
    Record record;
    record.kind = kindOf(format);
    record.extractor = extractor;
    record.locked = text.status == DocumentText::Status::Locked;
    record.unreadable = text.status == DocumentText::Status::Unreadable || text.status == DocumentText::Status::TooLarge;
    if (text.status != DocumentText::Status::Ok)
        return record;
    const int pages = int(std::min<qsizetype>(text.pages.size(), MaxPages));
    record.pageCount = pages;
    record.pages.reserve(pages);
    for (int i = 0; i < pages; ++i) {
        Page page;
        page.source = PageSource::TextLayer;
        page.text = text.pages.at(i).left(MaxPageText);
        record.pages.append(std::move(page));
    }
    return record;
}

} // namespace brulion::attachments
