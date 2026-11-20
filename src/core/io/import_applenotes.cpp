// Apple Notes importer (docs/IMPORT.md "Apple Notes"): the notes database of a Mac — the folder
// ~/Library/Group Containers/group.com.apple.notes copied from the Mac (or a ZIP of it): its
// NoteStore.sqlite (with NoteStore.sqlite-wal, whose committed changes count) and the attachments
// under Accounts/<account>/ (Media, FallbackImages, FallbackPDFs, Previews). Read with Brulion's own
// SQLite reader (sqlitereader_p.h) and protocol buffers decoder (protobuf_p.h), after Obsidian
// Importer's Apple Notes importer and the Apple Cloud Notes Parser's schema.
//
// Folders become folders (the default "Notes" folder the import folder itself, several accounts a
// folder each); notes in "Recently Deleted" and locked notes are left out (counted). A note's
// formatted text (the gzipped NoteStoreProto: text + attribute runs) becomes Markdown: title,
// heading and subheading, bold / italic / strikethrough, highlights, links, monospaced blocks as
// code fences, bulleted, dashed, numbered lists and checklists (`- [x]`) with their levels, block
// quotes; its first line is the title (left out of the text). Attachments: pictures and files
// (Media), drawings (their rendered image), scanned documents (the PDF, else the page images),
// tables (the CRDT of the table: a Markdown table), web links (a link with the page's title),
// #tags, @mentions, links to other notes (wiki links).
#include "io/htmlmarkdown.h"
#include "io/htmlutil_p.h"
#include "io/importers_p.h"
#include "io/importutil_p.h"
#include "io/links_p.h"
#include "io/messages_p.h"
#include "io/protobuf_p.h"
#include "io/sqlitereader_p.h"
#include "io/zip.h"
#include "util/paths.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

/// Seconds between the Unix epoch and Core Data's (2001-01-01 UTC).
constexpr qint64 CoreDataEpoch = 978307200;
/// Largest unpacked note or table.
constexpr qint64 MaxNoteBytes = 64LL << 20;
constexpr int MaxFolderDepth = 32;
constexpr int MaxTitleLength = 200;
constexpr QChar AttachmentChar = QChar(0xFFFC);
constexpr QChar SoftReturn = QChar(0x2028);

enum StyleType {
    Default = -1,
    Title = 0,
    Heading = 1,
    Subheading = 2,
    Monospaced = 4,
    DottedList = 100,
    DashedList = 101,
    NumberedList = 102,
    Checkbox = 103
};
enum FontWeight { Regular = 0, Bold = 1, Italic = 2, BoldItalic = 3 };

bool isListStyle(int s)
{
    return s == DottedList || s == DashedList || s == NumberedList || s == Checkbox;
}

bool isNoteStore(const InputFile &f)
{
    return paths::basename(f.path) == u"NoteStore.sqlite" && f.data.startsWith(QByteArrayView("SQLite format 3\0", 16));
}

/// One attribute run of a note's text.
struct Run {
    int length = 0;
    int style = Default;
    int indent = 0;
    bool checklist = false;
    bool done = false;
    bool blockquote = false;
    int weight = Regular;
    bool strikethrough = false;
    QString link;
    int emphasis = 0;
    QString attachmentId;
    QString attachmentUti;

    bool sameLook(const Run &o) const
    {
        return style == o.style && indent == o.indent && checklist == o.checklist && done == o.done
               && blockquote == o.blockquote && weight == o.weight && strikethrough == o.strikethrough && link == o.link
               && emphasis == o.emphasis && attachmentId == o.attachmentId && attachmentUti == o.attachmentUti;
    }
    bool blockAttachment() const
    {
        return !attachmentId.isEmpty() && !attachmentUti.contains(u"com.apple.notes.inlinetextattachment"_s);
    }
};

struct NoteText {
    QString text;
    QList<Run> runs;
    bool valid = false;
};

/// A Note message (ciofecaforensics.Note: 2 noteText, 5 attributeRun).
NoteText parseNote(const ProtoMessage &note)
{
    NoteText out;
    if (!note.valid())
        return out;
    out.text = note.string(2);
    for (const ProtoField &f : note.repeated(5)) {
        const ProtoMessage r(f.bytes);
        if (!r.valid())
            continue;
        Run run;
        run.length = int(std::clamp<qint64>(r.integer(1), 0, 1 << 30));
        const ProtoMessage ps = r.message(2);
        if (ps.valid()) {
            run.style = ps.has(1) ? int(qint32(ps.integer(1))) : Default;
            run.indent = int(std::clamp<qint64>(ps.integer(4), 0, 12));
            const ProtoMessage cl = ps.message(5);
            run.checklist = cl.valid() && ps.has(5);
            run.done = cl.integer(2) != 0;
            run.blockquote = ps.integer(8) != 0;
        }
        run.weight = int(r.integer(5));
        run.strikethrough = r.integer(7) != 0;
        run.link = r.string(9);
        run.emphasis = int(r.integer(14));
        const ProtoMessage att = r.message(12);
        if (att.valid() && r.has(12)) {
            run.attachmentId = att.string(1);
            run.attachmentUti = att.string(2);
        }
        out.runs.append(run);
    }
    out.valid = true;
    return out;
}

/// A NoteStoreProto (gzipped): 2 Document -> 3 Note.
NoteText decodeNoteData(const QByteArray &zdata)
{
    const auto raw = gunzip(zdata, MaxNoteBytes);
    if (!raw)
        return {};
    return parseNote(ProtoMessage(*raw).message(2).message(3));
}

QString firstLine(const QString &text)
{
    for (QString line : text.split(u'\n')) {
        line.remove(AttachmentChar).replace(SoftReturn, u' ').replace(QChar(0x2029), u' ');
        line = line.trimmed();
        if (!line.isEmpty())
            return line;
    }
    return {};
}

QString noteTitle(const QString &text, const QString &stored)
{
    QString line = firstLine(text);
    if (line.isEmpty())
        return stored.simplified();
    static const QRegularExpression marker(u"^(?:[-+*]|\\d+[.)])\\s+"_s);
    const QString stripped = QString(line).remove(marker).trimmed();
    if (!stripped.isEmpty())
        line = stripped;
    if (line.size() > MaxTitleLength)
        line = line.left(MaxTitleLength).trimmed();
    return line;
}

/// One row of ZICCLOUDSYNCINGOBJECT (the columns the import reads).
struct CloudObject {
    qint64 pk = 0;
    int entity = 0;
    QHash<QString, QVariant> values;

    QString text(const QString &column) const { return values.value(column).toString(); }
    qint64 integer(const QString &column) const { return values.value(column).toLongLong(); }
    double real(const QString &column) const { return values.value(column).toDouble(); }
    QByteArray blob(const QString &column) const { return values.value(column).toByteArray(); }
    bool isNull(const QString &column) const { return !values.value(column).isValid(); }
};

class AppleNotesImporter final : public Importer {
public:
    QString id() const override { return u"applenotes"_s; }
    QString name() const override { return u"Apple Notes"_s; }
    QStringList fileTypes() const override { return {u"NoteStore.sqlite"_s, u"*.zip"_s}; }

    int detect(const QList<InputFile> &files) const override
    {
        return std::ranges::any_of(files, isNoteStore) ? 95 : 0;
    }

    ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        const auto store = std::ranges::find_if(files, isNoteStore);
        if (store == files.end()) {
            ctx.warn(IoMessages::tr("No NoteStore.sqlite was found: choose the group.com.apple.notes folder copied "
                                    "from the Mac."));
            return ctx.take();
        }
        Database db(files, *store, ctx);
        if (db.open())
            db.importNotes();
        return ctx.take();
    }

private:
    class Database {
    public:
        Database(const QList<InputFile> &files, const InputFile &store, ImportContext &ctx) : m_store(store), m_ctx(ctx)
        {
            m_root = paths::dirname(store.path);
            const QString prefix = m_root.isEmpty() ? QString() : m_root + u'/';
            static const QRegularExpression account(u"^Accounts/([^/]+)/(.+)$"_s,
                                                    QRegularExpression::CaseInsensitiveOption);
            for (const InputFile &f : files) {
                if (!prefix.isEmpty() && !f.path.startsWith(prefix))
                    continue;
                const QString rel = f.path.sliced(prefix.size());
                m_byPath.insert(rel.toCaseFolded(), &f);
                if (const auto m = account.match(rel); m.hasMatch()) {
                    m_byAccountPath.insert(QString(m.captured(1) + u'/' + m.captured(2)).toCaseFolded(), &f);
                    m_byRelative.insert(m.captured(2).toCaseFolded(), &f);
                }
            }
        }

        bool open()
        {
            QByteArray wal;
            if (const InputFile *w = m_byPath.value(u"notestore.sqlite-wal"_s))
                wal = w->data;
            QString error;
            auto reader = SqliteReader::open(m_store.data, wal, &error);
            if (!reader) {
                m_ctx.warn(IoMessages::tr("NoteStore.sqlite cannot be read: %1").arg(error));
                return false;
            }
            m_db.emplace(std::move(*reader));
            const SqliteReader::Table *keys = m_db->table(u"Z_PRIMARYKEY"_s);
            const SqliteReader::Table *objects = m_db->table(u"ZICCLOUDSYNCINGOBJECT"_s);
            const SqliteReader::Table *noteData = m_db->table(u"ZICNOTEDATA"_s);
            if (!keys || !objects || !noteData) {
                m_ctx.warn(IoMessages::tr("This NoteStore.sqlite has no notes Brulion can read (it may come from a "
                                          "macOS version before 10.11)."));
                return false;
            }
            const int entColumn = int(keys->columns.indexOf(u"Z_ENT"_s));
            const int nameColumn = int(keys->columns.indexOf(u"Z_NAME"_s));
            m_db->scan(*keys, [&](qint64, const QList<QVariant> &v) {
                m_entities.insert(v.value(nameColumn).toString(), int(v.value(entColumn).toLongLong()));
                return true;
            });
            // The columns differ between macOS versions: those the table has.
            static const QStringList wanted{u"Z_PK"_s,
                                            u"Z_ENT"_s,
                                            u"ZIDENTIFIER"_s,
                                            u"ZTITLE1"_s,
                                            u"ZTITLE2"_s,
                                            u"ZNAME"_s,
                                            u"ZFOLDER"_s,
                                            u"ZPARENT"_s,
                                            u"ZFOLDERTYPE"_s,
                                            u"ZOWNER"_s,
                                            u"ZNOTE"_s,
                                            u"ZMEDIA"_s,
                                            u"ZTYPEUTI"_s,
                                            u"ZFILENAME"_s,
                                            u"ZGENERATION1"_s,
                                            u"ZFALLBACKIMAGEGENERATION"_s,
                                            u"ZFALLBACKPDFGENERATION"_s,
                                            u"ZSIZEWIDTH"_s,
                                            u"ZSIZEHEIGHT"_s,
                                            u"ZALTTEXT"_s,
                                            u"ZTOKENCONTENTIDENTIFIER"_s,
                                            u"ZTITLE"_s,
                                            u"ZURLSTRING"_s,
                                            u"ZMERGEABLEDATA1"_s,
                                            u"ZCREATIONDATE"_s,
                                            u"ZCREATIONDATE1"_s,
                                            u"ZCREATIONDATE2"_s,
                                            u"ZCREATIONDATE3"_s,
                                            u"ZMODIFICATIONDATE"_s,
                                            u"ZMODIFICATIONDATE1"_s,
                                            u"ZISPASSWORDPROTECTED"_s,
                                            u"ZISPINNED"_s,
                                            u"ZMARKEDFORDELETION"_s};
            QList<int> columns;
            QStringList names;
            for (const QString &c : wanted) {
                const qsizetype i = objects->columns.indexOf(c);
                if (i >= 0) {
                    columns.append(int(i));
                    names.append(c);
                }
            }
            const bool complete = m_db->scan(
                    *objects,
                    [&](qint64 rowid, const QList<QVariant> &v) {
                        CloudObject o;
                        o.pk = rowid;
                        for (qsizetype k = 0; k < columns.size(); ++k) {
                            const QVariant &value = v.value(columns[k]);
                            if (value.isValid())
                                o.values.insert(names[k], value);
                        }
                        o.entity = int(o.integer(u"Z_ENT"_s));
                        if (o.values.contains(u"Z_PK"_s))
                            o.pk = o.integer(u"Z_PK"_s);
                        m_byPk.insert(o.pk, m_objects.size());
                        if (const QString id = o.text(u"ZIDENTIFIER"_s); !id.isEmpty())
                            m_byIdentifier.insert(id.toUpper(), m_objects.size());
                        m_objects.append(std::move(o));
                        return true;
                    },
                    columns);
            const int noteColumn = int(noteData->columns.indexOf(u"ZNOTE"_s));
            const int dataColumn = int(noteData->columns.indexOf(u"ZDATA"_s));
            const bool notesComplete = m_db->scan(*noteData,
                                                  [&](qint64, const QList<QVariant> &v) {
                                                      const QByteArray data = v.value(dataColumn).toByteArray();
                                                      if (!data.isEmpty())
                                                          m_noteData.insert(v.value(noteColumn).toLongLong(), data);
                                                      return true;
                                                  },
                                                  {noteColumn, dataColumn});
            if (!complete || !notesComplete)
                m_ctx.warn(IoMessages::tr("NoteStore.sqlite is damaged; the notes Brulion could read were imported."));
            return true;
        }

        void importNotes()
        {
            const int noteEntity = m_entities.value(u"ICNote"_s, -1);
            // Notes to import and where they go.
            QSet<qint64> accountsWithNotes;
            QList<qsizetype> notes;
            int locked = 0, deleted = 0;
            for (qsizetype i = 0; i < m_objects.size(); ++i) {
                const CloudObject &o = m_objects[i];
                if (o.entity != noteEntity || o.isNull(u"ZTITLE1"_s))
                    continue;
                if (o.integer(u"ZMARKEDFORDELETION"_s) != 0 || folderType(o.integer(u"ZFOLDER"_s)) == 1) {
                    ++deleted;
                    continue;
                }
                if (o.integer(u"ZISPASSWORDPROTECTED"_s) != 0) {
                    ++locked;
                    continue;
                }
                notes.append(i);
                accountsWithNotes.insert(accountOf(o.integer(u"ZFOLDER"_s)));
            }
            m_multiAccount = accountsWithNotes.size() > 1;
            // Titles and paths first: notes link to each other.
            QHash<qsizetype, NoteText> texts;
            for (const qsizetype i : std::as_const(notes)) {
                const CloudObject &o = m_objects[i];
                NoteText text = decodeNoteData(m_noteData.value(o.pk));
                if (!text.valid) {
                    ++m_unreadable;
                    continue;
                }
                const QString title = noteTitle(text.text, o.text(u"ZTITLE1"_s));
                QStringList segments = folderSegments(o.integer(u"ZFOLDER"_s));
                segments.append(pageSegment(title.isEmpty() ? IoMessages::tr("Untitled note") : title));
                const VaultPath path = m_ctx.claimPage(segments.join(u'/'));
                m_notePaths.insert(o.text(u"ZIDENTIFIER"_s).toUpper(), path);
                m_titles.insert(i, title);
                m_paths.insert(i, path);
                texts.insert(i, std::move(text));
            }
            for (const qsizetype i : std::as_const(notes)) {
                if (!texts.contains(i))
                    continue;
                writeNote(i, texts.value(i));
            }
            if (locked > 0)
                m_ctx.warn(
                        IoMessages::tr("%n locked note(s) were left out: unlock them in Apple Notes (on the Mac) and "
                                       "copy the folder again.",
                                       nullptr, locked));
            if (deleted > 0)
                m_ctx.warn(IoMessages::tr("%n recently deleted note(s) were left out.", nullptr, deleted));
            if (m_unreadable > 0)
                m_ctx.warn(IoMessages::tr("%n note(s) could not be read and were left out.", nullptr, m_unreadable));
            if (m_missingFiles > 0)
                m_ctx.warn(IoMessages::tr("%n attachment(s) are not in the copied folder (still in iCloud, or the "
                                          "folder was copied without them) and were left out.",
                                          nullptr, m_missingFiles));
            if (m_unknownAttachments > 0)
                m_ctx.warn(IoMessages::tr("%n attachment(s) of a kind Brulion does not know were left out.", nullptr,
                                          m_unknownAttachments));
            if (notes.isEmpty())
                m_ctx.warn(IoMessages::tr("No notes were found in NoteStore.sqlite."));
        }

    private:
        const CloudObject *object(qint64 pk) const
        {
            const auto it = m_byPk.constFind(pk);
            return it == m_byPk.constEnd() ? nullptr : &m_objects[*it];
        }
        const CloudObject *byIdentifier(const QString &id) const
        {
            const auto it = m_byIdentifier.constFind(id.toUpper());
            return it == m_byIdentifier.constEnd() ? nullptr : &m_objects[*it];
        }

        int folderType(qint64 folder) const
        {
            const CloudObject *f = object(folder);
            return f ? int(f->integer(u"ZFOLDERTYPE"_s)) : 0;
        }

        qint64 accountOf(qint64 folder) const
        {
            const CloudObject *f = object(folder);
            return f ? f->integer(u"ZOWNER"_s) : 0;
        }

        QString accountUuid(qint64 folder) const
        {
            const CloudObject *a = object(accountOf(folder));
            return a ? a->text(u"ZIDENTIFIER"_s) : QString();
        }

        /// Folders from the account down to the note's folder (the default "Notes" folder adds none).
        QStringList folderSegments(qint64 folder) const
        {
            QStringList out;
            QSet<qint64> seen;
            qint64 current = folder;
            while (current != 0 && !seen.contains(current) && out.size() < MaxFolderDepth) {
                seen.insert(current);
                const CloudObject *f = object(current);
                if (!f)
                    break;
                const QString name = f->text(u"ZTITLE2"_s).simplified();
                if (!f->text(u"ZIDENTIFIER"_s).startsWith(u"DefaultFolder"_s) && !name.isEmpty())
                    out.prepend(pageSegment(name));
                current = f->integer(u"ZPARENT"_s);
            }
            if (m_multiAccount) {
                if (const CloudObject *a = object(accountOf(folder)); a && !a->text(u"ZNAME"_s).trimmed().isEmpty())
                    out.prepend(pageSegment(a->text(u"ZNAME"_s).simplified()));
            }
            return out;
        }

        static QString wall(double coreData, const QTimeZone &zone)
        {
            if (coreData <= 0)
                return {};
            const QDateTime at = QDateTime::fromSecsSinceEpoch(qint64(coreData) + CoreDataEpoch, QTimeZone::UTC);
            return wallClock(at, zone);
        }

        void writeNote(qsizetype index, const NoteText &text)
        {
            const CloudObject &o = m_objects[index];
            m_currentFolder = o.integer(u"ZFOLDER"_s);
            QString body = convert(text, false);
            QVariantMap fm;
            const QTimeZone &zone = m_ctx.options().timeZone;
            double created = o.real(u"ZCREATIONDATE3"_s);
            if (created <= 0)
                created = o.real(u"ZCREATIONDATE2"_s);
            if (created <= 0)
                created = o.real(u"ZCREATIONDATE1"_s);
            if (const QString c = wall(created, zone); !c.isEmpty())
                fm.insert(u"created"_s, c);
            if (const QString u = wall(o.real(u"ZMODIFICATIONDATE1"_s), zone); !u.isEmpty())
                fm.insert(u"updated"_s, u);
            if (o.integer(u"ZISPINNED"_s) != 0)
                fm.insert(u"pinned"_s, true);
            fm.insert(u"source"_s, u"Apple Notes"_s);
            addTitleAlias(fm, m_titles.value(index));
            m_ctx.addPage(m_paths.value(index),
                          noteWithFrontmatter(fm, {u"aliases"_s, u"created"_s, u"updated"_s, u"pinned"_s, u"source"_s},
                                              escapeTextFormulas(body)));
        }

        // ---- Text --------------------------------------------------------------------------------

        struct Fragment {
            Run run;
            QString text;
        };

        /// Runs with the same look joined, then split at line breaks and outer whitespace (Markdown
        /// marks must not cross lines nor start or end at a space).
        static QList<Fragment> fragments(const NoteText &note)
        {
            QList<Fragment> out;
            qsizetype offset = 0;
            static const QRegularExpression split(u"(^\\s+|\\s*\\n\\s*|\\s+$)"_s);
            for (qsizetype i = 0; i < note.runs.size();) {
                const Run &run = note.runs[i];
                QString text;
                do {
                    const qsizetype length = std::max(0, note.runs[i].length);
                    text += QStringView(note.text).mid(offset, length);
                    offset += length;
                    ++i;
                } while (i < note.runs.size() && note.runs[i].sameLook(run));
                qsizetype pos = 0;
                for (auto it = split.globalMatch(text); it.hasNext();) {
                    const auto m = it.next();
                    if (m.capturedLength() == 0)
                        continue;
                    if (m.capturedStart() > pos)
                        out.append(Fragment{run, text.sliced(pos, m.capturedStart() - pos)});
                    out.append(Fragment{run, m.captured(0)});
                    pos = m.capturedEnd();
                }
                if (pos < text.size())
                    out.append(Fragment{run, text.sliced(pos)});
            }
            // Text without runs (a damaged note): plain.
            if (offset < note.text.size() && note.runs.isEmpty())
                out.append(Fragment{Run{}, note.text});
            return out;
        }

        static bool hasText(const QString &s)
        {
            return std::ranges::any_of(s, [](QChar c) { return !c.isSpace(); });
        }

        QString convert(const NoteText &note, bool table)
        {
            QList<Fragment> frags = fragments(note);
            // A heading's bold is the heading's own.
            for (Fragment &f : frags) {
                if ((f.run.style == Title || f.run.style == Heading || f.run.style == Subheading)
                    && (f.run.weight == Bold || f.run.weight == BoldItalic))
                    f.run.weight = f.run.weight == BoldItalic ? Italic : Regular;
            }
            static const QRegularExpression urlLine(u"^https?://\\S+$"_s);
            bool skipTitle = !table && note.text.contains(u'\n') && !urlLine.match(firstLine(note.text)).hasMatch();
            bool titleStarted = false;
            bool quoted = false;
            QString out;
            m_mode = Mode::None;
            m_listNumber = 0;
            m_listIndent = 0;
            for (qsizetype j = 0; j < frags.size(); ++j) {
                const Run &run = frags[j].run;
                QString fragment = frags[j].text;
                if (skipTitle) {
                    if (!run.attachmentId.isEmpty()) {
                        skipTitle = false;
                    } else {
                        if (hasText(fragment))
                            titleStarted = true;
                        if (titleStarted && fragment.contains(u'\n'))
                            skipTitle = false;
                        continue;
                    }
                }
                const bool atLineStart =
                        j == 0 || frags[j - 1].text.contains(u'\n') || out.isEmpty() || out.endsWith(u'\n');
                if (atLineStart && hasText(fragment)) {
                    // A blank line ends a quote (else the next line would continue it).
                    if (quoted && !run.blockquote && !out.endsWith(u"\n\n"_s))
                        out += u'\n';
                    quoted = run.blockquote;
                }
                out += modeChange(run);
                if (fragment.contains(SoftReturn))
                    fragment.replace(SoftReturn,
                                     m_mode == Mode::List ? u"\n"_s + QString(run.indent + 1, u'\t') : u"\n"_s);
                if (!hasText(fragment) || m_mode == Mode::Code) {
                    if (m_mode == Mode::Code)
                        fragment.remove(AttachmentChar);
                    out += fragment;
                    continue;
                }
                if (!run.attachmentId.isEmpty()) {
                    const QString a = attachment(run);
                    out += atLineStart && !run.blockAttachment() ? paragraph(run, a) : a;
                    continue;
                }
                fragment.remove(AttachmentChar);
                QString t;
                if (!run.link.isEmpty() && run.link == fragment) {
                    t = isWebLink(run.link) ? fragment : escapeMarkdownText(fragment, false);
                } else {
                    t = escapeMarkdownText(fragment, false);
                    switch (run.weight) {
                    case Bold: t = u"**"_s + t + u"**"_s; break;
                    case Italic: t = u'*' + t + u'*'; break;
                    case BoldItalic: t = u"***"_s + t + u"***"_s; break;
                    default: break;
                    }
                    if (run.strikethrough)
                        t = u"~~"_s + t + u"~~"_s;
                    if (!run.link.isEmpty())
                        t = linkTo(run.link, t);
                }
                if (run.emphasis > 0)
                    t = u"=="_s + t + u"=="_s;
                out += atLineStart ? paragraph(run, t) : t;
            }
            if (m_mode == Mode::Code)
                out += u"\n```"_s;
            m_mode = Mode::None;
            out = collapseBlankLines(out).trimmed();
            if (table)
                out = out.replace(QRegularExpression(u" *\\n"_s), u"<br>"_s).replace(u'|', u"\\|"_s);
            return out;
        }

        enum class Mode { None, Code, List };

        /// Runs of blank lines as one, outside code fences.
        static QString collapseBlankLines(const QString &text)
        {
            QStringList out;
            bool fence = false;
            for (const QString &line : text.split(u'\n')) {
                if (line.startsWith(u"```"_s))
                    fence = !fence;
                if (!fence && line.trimmed().isEmpty() && !out.isEmpty() && out.last().trimmed().isEmpty())
                    continue;
                out.append(line);
            }
            return out.join(u'\n');
        }

        QString modeChange(const Run &run)
        {
            QString prefix;
            if (m_mode == Mode::List && ((run.indent == 0 && !isListStyle(run.style)) || run.blockAttachment())) {
                m_mode = Mode::None;
                // A blank line ends the list (else the paragraph would continue its last item).
                prefix += u'\n';
            }
            if (m_mode == Mode::Code && run.style != Monospaced) {
                m_mode = Mode::None;
                prefix += u"```\n"_s;
            }
            if (m_mode == Mode::None) {
                if (run.style == Monospaced) {
                    m_mode = Mode::Code;
                    prefix += u"\n```\n"_s;
                } else if (isListStyle(run.style)) {
                    m_mode = Mode::List;
                }
            }
            return prefix;
        }

        QString paragraph(const Run &run, const QString &text)
        {
            const QString indent(run.indent, u'\t');
            QString prelude = run.blockquote ? u"> "_s : QString();
            if (m_listNumber != 0 && (run.style != NumberedList || m_listIndent != run.indent)) {
                m_listIndent = run.indent;
                m_listNumber = 0;
            }
            switch (run.style) {
            case Title: return prelude + u"# "_s + text;
            case Heading: return prelude + u"## "_s + text;
            case Subheading: return prelude + u"### "_s + text;
            case DashedList:
            case DottedList: return prelude + indent + u"- "_s + text;
            case NumberedList: return prelude + indent + QString::number(++m_listNumber) + u". "_s + text;
            case Checkbox: return prelude + indent + (run.done ? u"- [x] "_s : u"- [ ] "_s) + text;
            default: break;
            }
            if (m_mode == Mode::List)
                prelude += indent;
            // Text that would read as Markdown at a line start ("1. ", "- ", "# ").
            static const QRegularExpression blockStart(u"^(?:[-+*>]\\s|#{1,6}\\s|\\d+[.)]\\s)"_s);
            if (blockStart.match(text).hasMatch())
                return prelude + u'\\' + text;
            return prelude + text;
        }

        static bool isWebLink(const QString &url)
        {
            const QString scheme = QUrl(url).scheme().toLower();
            return scheme == u"http" || scheme == u"https" || scheme == u"mailto" || scheme == u"tel";
        }

        QString linkTo(const QString &url, const QString &label)
        {
            static const QRegularExpression noteUri(u"applenotes:note/([^?&#/\\s]+)"_s);
            if (const auto m = noteUri.match(url); m.hasMatch()) {
                const VaultPath target = m_notePaths.value(m.captured(1).toUpper());
                if (target.isEmpty())
                    return label;
                return wikiLink(paths::stripMarkdownExtension(target), {}, {}, label);
            }
            if (!isWebLink(url))
                return label;
            return u'[' + label + u"]("_s
                   + QString(url).replace(u' ', u"%20"_s).replace(u'(', u"%28"_s).replace(u')', u"%29"_s) + u')';
        }

        // ---- Attachments -------------------------------------------------------------------------

        const InputFile *file(const QString &relative) const
        {
            const QString account = accountUuid(m_currentFolder);
            if (!account.isEmpty()) {
                if (const InputFile *f = m_byAccountPath.value(QString(account + u'/' + relative).toCaseFolded()))
                    return f;
            }
            if (const InputFile *f = m_byPath.value(relative.toCaseFolded()))
                return f;
            return m_byRelative.value(relative.toCaseFolded());
        }

        /// The attachment's file added to the vault: its embed / link, "" when it is not there.
        QString addFile(const QString &relative, const QString &name)
        {
            const InputFile *f = file(relative);
            if (!f) {
                ++m_missingFiles;
                return {};
            }
            QString fileName = name.trimmed().isEmpty() ? paths::basename(f->path) : name.trimmed();
            const VaultPath path = m_ctx.addAttachment(fileName, f->data);
            return attachmentLink(path);
        }

        QString mediaOf(const CloudObject &attachment)
        {
            const CloudObject *media = object(attachment.integer(u"ZMEDIA"_s));
            if (!media || media->text(u"ZFILENAME"_s).isEmpty()) {
                ++m_missingFiles;
                return {};
            }
            const QString id = media->text(u"ZIDENTIFIER"_s);
            const QString name = media->text(u"ZFILENAME"_s);
            const QString generation = media->text(u"ZGENERATION1"_s);
            const QString path = generation.isEmpty() ? u"Media/%1/%2"_s.arg(id, name)
                                                      : u"Media/%1/%2/%3"_s.arg(id, generation, name);
            if (file(path))
                return addFile(path, name);
            return addFile(u"Media/%1/%2"_s.arg(id, name), name);
        }

        QString drawing(const CloudObject &a)
        {
            const QString id = a.text(u"ZIDENTIFIER"_s);
            const QString generation = a.text(u"ZFALLBACKIMAGEGENERATION"_s);
            if (!generation.isEmpty() && file(u"FallbackImages/%1/%2/FallbackImage.png"_s.arg(id, generation)))
                return addFile(u"FallbackImages/%1/%2/FallbackImage.png"_s.arg(id, generation),
                               IoMessages::tr("Drawing") + u".png"_s);
            for (const QString &ext : {u".png"_s, u".jpg"_s, u".jpeg"_s}) {
                if (file(u"FallbackImages/"_s + id + ext))
                    return addFile(u"FallbackImages/"_s + id + ext, IoMessages::tr("Drawing") + ext);
            }
            ++m_missingFiles;
            return {};
        }

        QString scanPages(const CloudObject &a)
        {
            const auto raw = gunzip(a.blob(u"ZMERGEABLEDATA1"_s), MaxNoteBytes);
            if (!raw) {
                ++m_missingFiles;
                return {};
            }
            QStringList links;
            const ProtoMessage data = ProtoMessage(*raw).message(2).message(3);
            for (const ProtoField &e : data.repeated(3)) {
                const ProtoMessage entry(e.bytes);
                const ProtoMessage map = entry.message(13);
                if (!map.valid() || !entry.has(13))
                    continue;
                const QString imageId = map.first(3).message(2).string(4);
                const CloudObject *image = byIdentifier(imageId);
                if (!image)
                    continue;
                const QString preview = u"Previews/%1-1-%2x%3-0.jpeg"_s.arg(image->text(u"ZIDENTIFIER"_s))
                                                .arg(image->integer(u"ZSIZEWIDTH"_s))
                                                .arg(image->integer(u"ZSIZEHEIGHT"_s));
                QString link =
                        file(preview) ? addFile(preview, IoMessages::tr("Scan page") + u".jpg"_s) : mediaOf(*image);
                if (!link.isEmpty())
                    links.append(link);
            }
            return links.join(u"\n\n"_s);
        }

        QString attachment(const Run &run)
        {
            const QString &uti = run.attachmentUti;
            const CloudObject *a = byIdentifier(run.attachmentId);
            if (uti == u"com.apple.notes.inlinetextattachment.hashtag") {
                const QString tag = a ? tagName(a->text(u"ZALTTEXT"_s)) : QString();
                return tag.isEmpty() ? QString() : u'#' + tag;
            }
            if (uti == u"com.apple.notes.inlinetextattachment.mention")
                return a ? escapeMarkdownText(a->text(u"ZALTTEXT"_s), false) : QString();
            if (uti == u"com.apple.notes.inlinetextattachment.link")
                return a ? linkTo(a->text(u"ZTOKENCONTENTIDENTIFIER"_s), QString()) : QString();
            if (!a) {
                ++m_unknownAttachments;
                return {};
            }
            QString out;
            if (uti == u"com.apple.notes.table") {
                out = table(*a);
            } else if (uti == u"public.url") {
                const QString url = a->text(u"ZURLSTRING"_s);
                const QString title = a->text(u"ZTITLE"_s).simplified();
                out = isWebLink(url)
                              ? u"[**"_s + escapeMarkdownText(title.isEmpty() ? url : title, false, true) + u"**]("_s
                                        + QString(url)
                                                  .replace(u' ', u"%20"_s)
                                                  .replace(u'(', u"%28"_s)
                                                  .replace(u')', u"%29"_s)
                                        + u')'
                              : escapeMarkdownText(title, false);
            } else if (uti == u"com.apple.notes.gallery") {
                out = scanPages(*a);
            } else if (uti == u"com.apple.paper.doc.scan") {
                const QString id = a->text(u"ZIDENTIFIER"_s);
                const QString pdf =
                        u"FallbackPDFs/%1/%2/FallbackPDF.pdf"_s.arg(id, a->text(u"ZFALLBACKPDFGENERATION"_s));
                const QString plainPdf = u"FallbackPDFs/%1/FallbackPDF.pdf"_s.arg(id);
                out = file(pdf) ? addFile(pdf, IoMessages::tr("Scan") + u".pdf"_s)
                                : addFile(plainPdf, IoMessages::tr("Scan") + u".pdf"_s);
            } else if (uti == u"com.apple.paper" || uti == u"com.apple.drawing" || uti == u"com.apple.drawing.2") {
                out = drawing(*a);
            } else {
                out = mediaOf(*a);
            }
            if (out.isEmpty())
                return {};
            return run.blockAttachment() ? u'\n' + out + u'\n' : out;
        }

        /// A table attachment's CRDT (MergableDataProto) as a Markdown table.
        QString table(const CloudObject &a)
        {
            const auto raw = gunzip(a.blob(u"ZMERGEABLEDATA1"_s), MaxNoteBytes);
            if (!raw)
                return {};
            const ProtoMessage data = ProtoMessage(*raw).message(2).message(3);
            QStringList keys, types;
            QList<QByteArray> uuids;
            QList<ProtoMessage> objects;
            for (const ProtoField &f : data.fields()) {
                if (f.wire != 2)
                    continue;
                if (f.number == 3)
                    objects.append(ProtoMessage(f.bytes));
                else if (f.number == 4)
                    keys.append(f.asString());
                else if (f.number == 5)
                    types.append(f.asString());
                else if (f.number == 6)
                    uuids.append(f.bytes.toByteArray().toHex());
            }
            const auto objectAt = [&objects](qint64 i) {
                return i >= 0 && i < objects.size() ? objects[i] : ProtoMessage();
            };
            // ObjectID {2 unsignedIntegerValue, 4 stringValue, 6 objectIndex} -> the referenced
            // object's UUID (its custom map's first entry names the index into `uuids`).
            const auto targetUuid = [&](const ProtoMessage &objectId) -> QByteArray {
                const ProtoMessage ref = objectAt(objectId.integer(6, -1));
                return uuids.value(ref.message(13).first(3).message(2).integer(2, -1));
            };
            const auto locations = [&](const ProtoMessage &object, int *count) {
                QHash<QByteArray, int> indices;
                QList<QByteArray> ordering;
                const ProtoMessage ordered = object.message(16).message(1);
                for (const ProtoField &att : ordered.message(1).repeated(2))
                    ordering.append(ProtoMessage(att.bytes).bytes(2).toByteArray().toHex());
                for (const ProtoField &el : ordered.message(2).repeated(1)) {
                    const ProtoMessage element(el.bytes);
                    indices.insert(targetUuid(element.message(2)),
                                   int(ordering.indexOf(targetUuid(element.message(1)))));
                }
                *count = int(ordering.size());
                return indices;
            };
            const qsizetype rootIndex = [&]() -> qsizetype {
                for (qsizetype i = 0; i < objects.size(); ++i) {
                    const ProtoMessage map = objects[i].message(13);
                    if (objects[i].has(13) && types.value(map.integer(1, -1)) == u"com.apple.notes.ICTable")
                        return i;
                }
                return -1;
            }();
            if (rootIndex < 0)
                return {};
            QHash<QByteArray, int> rows, columns;
            int rowCount = 0, columnCount = 0;
            ProtoMessage cells;
            for (const ProtoField &e : objects[rootIndex].message(13).repeated(3)) {
                const ProtoMessage entry(e.bytes);
                const QString key = keys.value(entry.integer(1, -1));
                const ProtoMessage target = objectAt(entry.message(2).integer(6, -1));
                if (key == u"crRows")
                    rows = locations(target, &rowCount);
                else if (key == u"crColumns")
                    columns = locations(target, &columnCount);
                else if (key == u"cellColumns")
                    cells = target;
            }
            if (rowCount <= 0 || columnCount <= 0 || rowCount > 10000 || columnCount > 200)
                return {};
            QList<QStringList> grid(rowCount, QStringList(columnCount));
            for (const ProtoField &c : cells.message(6).repeated(1)) {
                const ProtoMessage column(c.bytes);
                const int x = columns.value(targetUuid(column.message(1)), -1);
                const ProtoMessage rowData = objectAt(column.message(2).integer(6, -1));
                for (const ProtoField &r : rowData.message(6).repeated(1)) {
                    const ProtoMessage row(r.bytes);
                    const int y = rows.value(targetUuid(row.message(1)), -1);
                    const ProtoMessage content = objectAt(row.message(2).integer(6, -1));
                    if (x < 0 || y < 0 || x >= columnCount || y >= rowCount || !content.has(10))
                        continue;
                    const NoteText cell = parseNote(content.message(10));
                    const Mode saved = m_mode;
                    grid[y][x] = convert(cell, true);
                    m_mode = saved;
                }
            }
            QString out = u"\n"_s;
            for (int y = 0; y < rowCount; ++y) {
                out += u"| "_s + grid[y].join(u" | "_s) + u" |\n"_s;
                if (y == 0) {
                    out += u'|';
                    for (int x = 0; x < columnCount; ++x)
                        out += u" --- |"_s;
                    out += u'\n';
                }
            }
            return out;
        }

        const InputFile &m_store;
        ImportContext &m_ctx;
        QString m_root;
        QHash<QString, const InputFile *> m_byPath;        ///< folded path under the group folder
        QHash<QString, const InputFile *> m_byAccountPath; ///< folded "<account>/<path>"
        QHash<QString, const InputFile *> m_byRelative;    ///< folded path under any account
        std::optional<SqliteReader> m_db;
        QHash<QString, int> m_entities;
        QList<CloudObject> m_objects;
        QHash<qint64, qsizetype> m_byPk;
        QHash<QString, qsizetype> m_byIdentifier;
        QHash<qint64, QByteArray> m_noteData;
        QHash<QString, VaultPath> m_notePaths; ///< note identifier (upper case) -> page
        QHash<qsizetype, QString> m_titles;
        QHash<qsizetype, VaultPath> m_paths;
        bool m_multiAccount = false;
        qint64 m_currentFolder = 0;
        Mode m_mode = Mode::None;
        int m_listNumber = 0;
        int m_listIndent = 0;
        int m_unreadable = 0;
        int m_missingFiles = 0;
        int m_unknownAttachments = 0;
    };
};

} // namespace

std::unique_ptr<Importer> makeAppleNotesImporter()
{
    return std::make_unique<AppleNotesImporter>();
}

} // namespace brulion::io::detail
