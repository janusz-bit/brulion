// Evernote importer: .enex exports (XML with ENML note bodies and base64 resources).
#include "io/htmlmarkdown.h"
#include "io/htmlutil_p.h"
#include "io/importers_p.h"
#include "io/importutil_p.h"
#include "io/messages_p.h"
#include "markdown/tokens.h"
#include "util/paths.h"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

#include <algorithm>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

const QString MediaScheme = u"evernote-media:"_s;

struct Resource {
    QByteArray data;
    QString mime;
    QString fileName;
    QString hash; ///< MD5 of the data (hex), referenced by <en-media hash="…">
};

struct EnexTask {
    QString title;
    bool done = false;
    QDateTime due;
};

struct Note {
    QString title;
    QString content;
    QDateTime created;
    QDateTime updated;
    QStringList tags;
    QString sourceUrl;
    QString author;
    QDateTime reminder;
    QDateTime reminderDone;
    QList<Resource> resources;
    QList<EnexTask> tasks;
};

/// "20261014T101010Z" (UTC). Date and time are read separately: parsing the digits as one
/// QDateTime would interpret them in the system zone first, which shifts times that fall into its
/// DST gap.
QDateTime enexTime(const QString &text)
{
    static const QRegularExpression re(u"^(\\d{8})T(\\d{6})Z$"_s);
    const auto m = re.match(text.trimmed());
    if (!m.hasMatch())
        return {};
    const QDate date = QDate::fromString(m.captured(1), u"yyyyMMdd"_s);
    const QTime time = QTime::fromString(m.captured(2), u"HHmmss"_s);
    if (!date.isValid() || !time.isValid())
        return {};
    return QDateTime(date, time, QTimeZone(QTimeZone::UTC));
}

QString childText(QXmlStreamReader &r)
{
    return r.readElementText(QXmlStreamReader::IncludeChildElements);
}

Resource readResource(QXmlStreamReader &r)
{
    Resource res;
    while (r.readNextStartElement()) {
        if (r.name() == u"data") {
            QByteArray base64 = childText(r).toLatin1();
            base64.replace('\n', QByteArray()).replace('\r', QByteArray()).replace(' ', QByteArray());
            res.data = QByteArray::fromBase64(base64);
        } else if (r.name() == u"mime") {
            res.mime = childText(r).trimmed();
        } else if (r.name() == u"resource-attributes") {
            while (r.readNextStartElement()) {
                if (r.name() == u"file-name")
                    res.fileName = childText(r).trimmed();
                else
                    r.skipCurrentElement();
            }
        } else {
            r.skipCurrentElement();
        }
    }
    res.hash = QString::fromLatin1(QCryptographicHash::hash(res.data, QCryptographicHash::Md5).toHex());
    return res;
}

EnexTask readTask(QXmlStreamReader &r)
{
    EnexTask t;
    while (r.readNextStartElement()) {
        if (r.name() == u"title")
            t.title = childText(r).trimmed();
        else if (r.name() == u"taskStatus")
            t.done = childText(r).trimmed().compare(u"completed"_s, Qt::CaseInsensitive) == 0;
        else if (r.name() == u"dueDate")
            t.due = enexTime(childText(r));
        else
            r.skipCurrentElement();
    }
    return t;
}

Note readNote(QXmlStreamReader &r)
{
    Note n;
    while (r.readNextStartElement()) {
        const auto name = r.name();
        if (name == u"title") {
            n.title = childText(r).simplified();
        } else if (name == u"content") {
            n.content = childText(r);
        } else if (name == u"created") {
            n.created = enexTime(childText(r));
        } else if (name == u"updated") {
            n.updated = enexTime(childText(r));
        } else if (name == u"tag") {
            n.tags.append(childText(r).trimmed());
        } else if (name == u"note-attributes") {
            while (r.readNextStartElement()) {
                const auto attr = r.name();
                if (attr == u"source-url")
                    n.sourceUrl = childText(r).trimmed();
                else if (attr == u"author")
                    n.author = childText(r).trimmed();
                else if (attr == u"reminder-time")
                    n.reminder = enexTime(childText(r));
                else if (attr == u"reminder-done-time")
                    n.reminderDone = enexTime(childText(r));
                else
                    r.skipCurrentElement();
            }
        } else if (name == u"resource") {
            n.resources.append(readResource(r));
        } else if (name == u"task") {
            n.tasks.append(readTask(r));
        } else {
            r.skipCurrentElement();
        }
    }
    return n;
}

QList<Note> parseEnex(const QByteArray &xml, const QString &fileName, ImportContext &ctx)
{
    QList<Note> notes;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement() && r.name() == u"note")
            notes.append(readNote(r));
    }
    if (r.hasError() && r.error() != QXmlStreamReader::PrematureEndOfDocumentError) {
        ctx.warn(IoMessages::tr("“%1” is damaged (line %2: %3); %n note(s) were read.", nullptr, int(notes.size()))
                         .arg(fileName)
                         .arg(r.lineNumber())
                         .arg(r.errorString()));
    } else if (r.hasError()) {
        ctx.warn(IoMessages::tr("“%1” is incomplete; %n note(s) were read.", nullptr, int(notes.size())).arg(fileName));
    }
    return notes;
}

/// End of the element starting at `from` (balanced `<div` / `</div>`), or -1. `html` must be
/// valid UTF-16 (the caller checks it once; it is not checked again here).
qsizetype divEnd(const QString &html, qsizetype from)
{
    static const QRegularExpression tag(u"<(/?)div\\b[^>]*>"_s, QRegularExpression::CaseInsensitiveOption);
    int depth = 0;
    for (auto it = tag.globalMatch(html, from, QRegularExpression::NormalMatch,
                                   QRegularExpression::DontCheckSubjectStringMatchOption);
         it.hasNext();) {
        const auto m = it.next();
        depth += m.capturedLength(1) == 0 ? 1 : -1;
        if (depth == 0)
            return m.capturedEnd();
    }
    return -1;
}

/// ENML -> HTML understood by htmlToMarkdown(): en-todo -> checkbox, en-media -> <img> with a
/// media URL, code blocks -> <pre>, encrypted parts removed.
QString enmlToHtml(QString enml, bool *encrypted)
{
    static const QRegularExpression prolog(u"<\\?xml[^>]*\\?>|<!DOCTYPE[^>]*>"_s,
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression note(u"<(/?)en-note\\b[^>]*>"_s, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression todo(u"<en-todo\\b([^>]*?)/?>(?:\\s*</en-todo>)?"_s,
                                         QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression checked(u"\\bchecked\\s*=\\s*[\"']?true"_s,
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression media(u"<en-media\\b([^>]*?)/?>(?:\\s*</en-media>)?"_s,
                                          QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression hash(u"\\bhash\\s*=\\s*[\"']([0-9a-fA-F]+)[\"']"_s);
    static const QRegularExpression codeBlock(u"<div\\b[^>]*-en-codeblock\\s*:\\s*true[^>]*>"_s,
                                              QRegularExpression::CaseInsensitiveOption);

    enml.remove(prolog);
    enml.replace(note, u"<\\1div>"_s);
    enml = removeElements(enml, {u"en-crypt"_s}, encrypted);
    QString out;
    qsizetype last = 0;
    for (auto it = todo.globalMatch(enml); it.hasNext();) {
        const auto m = it.next();
        out += QStringView(enml).sliced(last, m.capturedStart() - last);
        out += checked.match(m.captured(1)).hasMatch() ? u"<input type=\"checkbox\" checked>"_s
                                                       : u"<input type=\"checkbox\">"_s;
        last = m.capturedEnd();
    }
    out += QStringView(enml).sliced(last);
    enml = out;
    out.clear();
    last = 0;
    for (auto it = media.globalMatch(enml); it.hasNext();) {
        const auto m = it.next();
        out += QStringView(enml).sliced(last, m.capturedStart() - last);
        const auto h = hash.match(m.captured(1));
        if (h.hasMatch())
            out += u"<img src=\""_s + MediaScheme + h.captured(1).toLower() + u"\">"_s;
        last = m.capturedEnd();
    }
    out += QStringView(enml).sliced(last);
    enml = out;
    // Code blocks: the text of the whole <div> subtree in a <pre>. One pass that copies the text
    // between them (the search goes on after the subtree; the <pre> holds no tags). The UTF-16
    // is checked once: QRegularExpression would check the whole text on every match call, and
    // as there, invalid UTF-16 matches nothing.
    if (!QStringView(enml).isValidUtf16())
        return enml;
    out.clear();
    last = 0;
    for (;;) {
        const auto m = codeBlock.match(enml, last, QRegularExpression::NormalMatch,
                                       QRegularExpression::DontCheckSubjectStringMatchOption);
        if (!m.hasMatch())
            break;
        const qsizetype end = divEnd(enml, m.capturedStart());
        if (end < 0)
            break;
        const QString code = htmlToPlainText(enml.sliced(m.capturedStart(), end - m.capturedStart()));
        out += QStringView(enml).sliced(last, m.capturedStart() - last);
        out += u"<pre>"_s + code.toHtmlEscaped() + u"</pre>"_s;
        last = end;
    }
    if (last == 0)
        return enml;
    out += QStringView(enml).sliced(last);
    return out;
}

QString resourceFileName(const Resource &res)
{
    QString name = res.fileName;
    const QString ext = extensionForMime(res.mime);
    if (name.isEmpty())
        name = (res.mime.startsWith(u"image/"_s) ? u"image-"_s : u"attachment-"_s) + res.hash.left(8) + ext;
    else if (!ext.isEmpty() && paths::extension(name).isEmpty())
        name += ext;
    return name;
}

class EvernoteImporter final : public Importer {
public:
    QString id() const override { return u"evernote"_s; }
    QString name() const override { return u"Evernote"_s; }
    QStringList fileTypes() const override { return {u"*.enex"_s}; }

    int detect(const QList<InputFile> &files) const override
    {
        // Only .enex files: certain. An .enex among notes (a backup inside a vault) loses against
        // the importers of the notes.
        const auto isEnex = [](const QString &p) { return paths::extension(p) == u".enex"; };
        if (!anyPath(files, isEnex))
            return 0;
        return std::all_of(files.begin(), files.end(), [&](const InputFile &f) { return isEnex(f.path); }) ? 100 : 15;
    }

    ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        QList<const InputFile *> enex;
        for (const InputFile &f : files) {
            if (paths::extension(f.path) == u".enex")
                enex.append(&f);
        }
        struct Parsed {
            QString notebook;
            QList<Note> notes;
        };
        QList<Parsed> parsed;
        QSet<QString> titles;
        for (const InputFile *f : std::as_const(enex)) {
            Parsed p;
            // Several notebooks: one folder per .enex file.
            p.notebook = enex.size() > 1 ? pageSegment(paths::basename(f->path).chopped(5)) : QString();
            p.notes = parseEnex(f->data, paths::basename(f->path), ctx);
            for (const Note &n : std::as_const(p.notes))
                titles.insert(n.title.toCaseFolded());
            parsed.append(std::move(p));
        }
        bool encryptedWarned = false;
        for (const Parsed &p : std::as_const(parsed)) {
            for (const Note &n : p.notes) {
                const QString title = n.title.isEmpty() ? IoMessages::tr("Untitled") : n.title;
                const VaultPath path = ctx.claimPage(p.notebook.isEmpty() ? pageSegment(title)
                                                                          : p.notebook + u'/' + pageSegment(title));
                bool encrypted = false;
                ctx.addPage(path, noteContent(n, title, titles, ctx, &encrypted));
                if (encrypted && !encryptedWarned) {
                    ctx.warn(IoMessages::tr("Encrypted parts of notes cannot be imported and were left out."));
                    encryptedWarned = true;
                }
            }
        }
        return ctx.take();
    }

private:
    static QString noteContent(const Note &n, const QString &title, const QSet<QString> &titles, ImportContext &ctx,
                               bool *encrypted)
    {
        QHash<QString, const Resource *> byHash;
        for (const Resource &r : n.resources)
            byHash.insert(r.hash, &r);
        QSet<QString> used;
        const auto attachment = [&](const Resource &r) {
            return ctx.addAttachment(paths::basename(resourceFileName(r)), r.data);
        };

        HtmlToMarkdownOptions o;
        o.image = [&](const HtmlImage &img) -> QString {
            if (!img.src.startsWith(MediaScheme)) {
                const QString scheme = img.src.section(u':', 0, 0).toLower();
                return scheme == u"http" || scheme == u"https" ? u"!["_s + escapeMarkdownText(img.alt, false, true)
                                                                         + u"]("_s + encodeLinkPath(img.src) + u')'
                                                               : QString();
            }
            const Resource *r = byHash.value(img.src.sliced(MediaScheme.size()));
            if (!r) {
                ctx.warn(IoMessages::tr("Some attachments referenced by notes are missing in the export."));
                return {};
            }
            used.insert(r->hash);
            return attachmentLink(attachment(*r), resourceFileName(*r));
        };
        o.link = [&](const QString &href, const QString &label) -> std::optional<QString> {
            // Links between notes ("evernote:///view/…") point to notes of the export by title.
            if (!href.startsWith(u"evernote:"_s, Qt::CaseInsensitive))
                return std::nullopt;
            const QString plain = QString(label).remove(u'\\').trimmed();
            if (!titles.contains(plain.toCaseFolded()))
                return label;
            return wikiLink(pageSegment(plain));
        };

        QString body = htmlToMarkdown(enmlToHtml(n.content, encrypted), o).markdown;
        QStringList extra;
        for (const Resource &r : n.resources) {
            if (!used.contains(r.hash) && !r.data.isEmpty())
                extra.append(attachmentLink(attachment(r), resourceFileName(r)));
        }
        if (!extra.isEmpty())
            body = normalizeNewlines(body) + u"\n\n"_s + extra.join(u"\n"_s);

        QStringList head;
        if (n.reminder.isValid()) {
            const QString when = wallClock(n.reminder, ctx.options().timeZone);
            QString line = (n.reminderDone.isValid() ? u"- [x] "_s : u"- [ ] "_s) + escapeMarkdownText(title, false)
                           + u' ' + markdown::tokens::Reminder.toString() + u' ' + when;
            if (n.reminderDone.isValid())
                line += u' ' + markdown::tokens::Done.toString() + u' '
                        + n.reminderDone.toTimeZone(ctx.options().timeZone).date().toString(u"yyyy-MM-dd"_s);
            head.append(line);
        }
        QStringList tasks;
        for (const EnexTask &t : n.tasks) {
            QString line = (t.done ? u"- [x] "_s : u"- [ ] "_s) + escapeMarkdownText(t.title, false);
            if (t.due.isValid())
                line += u' ' + markdown::tokens::Due.toString() + u' ' + wallClock(t.due, ctx.options().timeZone);
            tasks.append(line);
        }
        QString text;
        if (!head.isEmpty())
            text += head.join(u'\n') + u"\n\n"_s;
        text += normalizeNewlines(body);
        if (!tasks.isEmpty())
            text += u"\n\n"_s + tasks.join(u'\n');

        QVariantMap fm;
        if (n.created.isValid())
            fm.insert(u"created"_s, wallClock(n.created, ctx.options().timeZone));
        if (n.updated.isValid())
            fm.insert(u"updated"_s, wallClock(n.updated, ctx.options().timeZone));
        QStringList tags;
        for (const QString &t : n.tags) {
            const QString name = tagName(t);
            if (!name.isEmpty())
                tags.append(name);
        }
        if (!tags.isEmpty())
            fm.insert(u"tags"_s, tags);
        if (!n.sourceUrl.isEmpty())
            fm.insert(u"source"_s, n.sourceUrl);
        if (!n.author.isEmpty())
            fm.insert(u"author"_s, n.author);
        addTitleAlias(fm, n.title);
        return noteWithFrontmatter(fm, {u"aliases"_s, u"created"_s, u"updated"_s, u"tags"_s, u"source"_s, u"author"_s},
                                   text);
    }
};

} // namespace

std::unique_ptr<Importer> makeEvernoteImporter()
{
    return std::make_unique<EvernoteImporter>();
}

} // namespace brulion::io::detail
