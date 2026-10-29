#include "notebook/share.h"

#include "text/decode.h"
#include "util/paths.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace brulion::notebook {

namespace {

QString stringValue(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

/// Add `file` to `files` under the caps every share has (sharedContentFromJson,
/// openedFilesShare, mergeOpenedShares): an entry with nothing in it is skipped, a file without
/// a path is "unreadable", readable files after the first SharedFilesMax are "tooMany" (counted
/// in `accepted`). False once SharedEntriesMax entries are listed (nothing added).
bool appendFile(QList<SharedFile> &files, SharedFile file, int &accepted)
{
    if (files.size() >= SharedEntriesMax)
        return false;
    if (file.path.isEmpty() && file.name.isEmpty() && file.error.isEmpty())
        return true;
    if (file.error.isEmpty() && file.path.isEmpty())
        file.error = u"unreadable"_s;
    if (file.error.isEmpty() && ++accepted > SharedFilesMax) {
        file.error = u"tooMany"_s;
        file.path.clear();
    }
    files.append(file);
    return true;
}

/// `text` cut to SharedTextMaxChars, never between the two halves of a surrogate pair.
QString limitSharedText(const QString &text)
{
    if (text.size() <= SharedTextMaxChars)
        return text;
    qsizetype keep = SharedTextMaxChars;
    if (text.at(keep - 1).isHighSurrogate())
        --keep;
    return text.left(keep);
}

int hexValue(QChar c)
{
    const char16_t u = c.unicode();
    if (u >= u'0' && u <= u'9')
        return u - u'0';
    if (u >= u'a' && u <= u'f')
        return u - u'a' + 10;
    if (u >= u'A' && u <= u'F')
        return u - u'A' + 10;
    return -1;
}

/// One name or value of an application/x-www-form-urlencoded query: "+" is a space, "%XX" a
/// byte, the rest its UTF-8; the bytes read as UTF-8 (invalid sequences: U+FFFD). A "%" without
/// two hex digits after it stays as it is. (QUrlQuery keeps "+" and decodes differently.)
QString formDecode(QStringView value)
{
    QByteArray bytes;
    bytes.reserve(value.size());
    qsizetype run = 0; // start of the characters not yet converted
    const auto flush = [&](qsizetype end) {
        if (end > run)
            bytes += value.sliced(run, end - run).toUtf8();
    };
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c == u'+') {
            flush(i);
            bytes += ' ';
            run = i + 1;
        } else if (c == u'%' && i + 2 < value.size() && hexValue(value.at(i + 1)) >= 0
                   && hexValue(value.at(i + 2)) >= 0) {
            flush(i);
            bytes += char(hexValue(value.at(i + 1)) * 16 + hexValue(value.at(i + 2)));
            i += 2;
            run = i + 1;
        }
    }
    flush(value.size());
    return QString::fromUtf8(bytes);
}

/// A command line argument that is a URL rather than a path: "file:" (any form), or a scheme with
/// "://" ("smb://", "sftp://", "https://"). "C:/x.md" and a Linux name with a colon are paths.
bool isUrlArgument(const QString &argument)
{
    static const QRegularExpression withAuthority(u"^[A-Za-z][A-Za-z0-9+.-]+://"_s);
    return argument.startsWith(u"file:"_s, Qt::CaseInsensitive) || withAuthority.match(argument).hasMatch();
}

/// The last segment of `path` ("/a/b.md" -> "b.md").
QString lastSegment(const QString &path)
{
    QString trimmed = path;
    while (trimmed.size() > 1 && trimmed.endsWith(u'/'))
        trimmed.chop(1);
    return trimmed.mid(trimmed.lastIndexOf(u'/') + 1);
}

/// Text extensions a shared file may have (lower case, without the dot).
bool isTextExtension(const QString &suffix)
{
    static const QStringList known = {u"md"_s, u"markdown"_s, u"mdown"_s, u"mkd"_s, u"txt"_s, u"text"_s};
    return known.contains(suffix.toLower());
}

/// `name` without a text extension ("Lista.md" -> "Lista"); other names unchanged.
QString withoutTextExtension(const QString &name)
{
    const qsizetype dot = name.lastIndexOf(u'.');
    if (dot < 0 || !isTextExtension(name.mid(dot + 1)))
        return name;
    return name.left(dot);
}

/// The lines of `text` without a leading YAML frontmatter ("---" … "---").
QStringList withoutFrontmatter(QStringList lines)
{
    if (lines.isEmpty() || lines.first().trimmed() != u"---"_s)
        return lines;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (line == u"---"_s || line == u"..."_s)
            return lines.mid(i + 1);
    }
    return lines; // not closed: not a frontmatter
}

QStringList splitLines(const QString &text)
{
    QString unified = text;
    unified.replace(u"\r\n"_s, u"\n"_s);
    unified.replace(u'\r', u'\n');
    if (unified.startsWith(QChar(0xFEFF)))
        unified.remove(0, 1);
    return unified.split(u'\n');
}

/// A link destination that stays one destination inside "[title](…)" and "<…>".
QString safeDestination(QString url)
{
    url.replace(u'(', u"%28"_s);
    url.replace(u')', u"%29"_s);
    url.replace(u'<', u"%3C"_s);
    url.replace(u'>', u"%3E"_s);
    url.replace(u' ', u"%20"_s);
    return url;
}

/// `text` without `length` characters at `from`, the blanks around them joined into one space
/// (a line break kept), trimmed.
QString withoutPart(const QString &text, qsizetype from, qsizetype length)
{
    const auto horizontal = [](QChar c) { return c == u' ' || c == u'\t'; };
    QString left = text.left(from);
    QString right = text.mid(from + length);
    while (!left.isEmpty() && horizontal(left.back()))
        left.chop(1);
    while (!right.isEmpty() && horizontal(right.front()))
        right.remove(0, 1);
    // No space before what closes the sentence or a bracket: "(zob. <link>)" leaves "(zob.)".
    const bool space = !left.isEmpty() && !right.isEmpty() && !left.endsWith(u'\n') && !right.startsWith(u'\n')
                       && !QStringView(u".,;:!?)]>").contains(right.front());
    return QString(left + (space ? u" "_s : QString()) + right).trimmed();
}

/// One line of a text as a page title: Markdown markers dropped.
QString plainTitleLine(QString line)
{
    static const QRegularExpression heading(u"^#{1,6}\\s+"_s);
    static const QRegularExpression quote(u"^(>\\s*)+"_s);
    static const QRegularExpression listMarker(u"^([-*+]|\\d+[.)])\\s+"_s);
    static const QRegularExpression checkbox(u"^\\[[ xX/-]\\]\\s+"_s);
    static const QRegularExpression wikiLink(u"\\[\\[([^\\]|]*\\|)?([^\\]]*)\\]\\]"_s);
    static const QRegularExpression mdLink(u"!?\\[([^\\]]*)\\]\\([^)]*\\)"_s);
    static const QRegularExpression autoLink(u"<https?://[^>]*>"_s);
    static const QRegularExpression bareLink(u"https?://\\S+"_s);
    static const QRegularExpression emphasis(u"(\\*\\*|__|~~|==|\\*|`)"_s);
    static const QRegularExpression spaces(u"\\s+"_s);
    static const QRegularExpression edgePunctuation(u"^[\\s—–:;,|-]+|[\\s—–:;,|-]+$"_s);
    line = line.trimmed();
    line.remove(quote);
    line.remove(heading);
    line.remove(listMarker);
    line.remove(checkbox);
    line.replace(wikiLink, u"\\2"_s);
    line.replace(mdLink, u"\\1"_s);
    line.remove(autoLink);
    line.remove(bareLink); // a link is no title: the text around it is
    line.remove(emphasis);
    line.replace(spaces, u" "_s);
    line.remove(edgePunctuation); // what was left beside a dropped link
    return line.trimmed();
}

/// `title` cut on a word to at most `max` characters (a single long word is cut inside).
QString cutOnWord(const QString &title, qsizetype max)
{
    if (title.size() <= max)
        return title;
    QString cut = title.left(max + 1);
    const qsizetype space = cut.lastIndexOf(u' ');
    cut = space > max / 3 ? cut.left(space) : title.left(max);
    // Trailing punctuation of a cut sentence reads badly in a file name.
    while (!cut.isEmpty()
           && (cut.back().isSpace() || cut.back() == u',' || cut.back() == u';' || cut.back() == u':'
               || cut.back() == u'-' || cut.back() == u'—'))
        cut.chop(1);
    return cut;
}

} // namespace

bool SharedContent::isEmpty() const
{
    return subject.trimmed().isEmpty() && text.trimmed().isEmpty() && url.trimmed().isEmpty() && files.isEmpty();
}

SharedContent sharedContentFromJson(const QJsonObject &object)
{
    SharedContent share;
    share.subject = stringValue(object, u"subject"_s);
    share.text = stringValue(object, u"text"_s);
    share.url = stringValue(object, u"url"_s);
    share.dir = stringValue(object, u"dir"_s);
    if (stringValue(object, u"source"_s) == u"open")
        share.source = u"open"_s;
    const QJsonValue files = object.value(u"files"_s);
    if (!files.isArray())
        return share;
    int accepted = 0;
    for (const QJsonValue &value : files.toArray()) {
        if (share.files.size() >= SharedEntriesMax)
            break;
        if (!value.isObject())
            continue;
        const QJsonObject entry = value.toObject();
        SharedFile file;
        file.path = stringValue(entry, u"path"_s);
        file.name = stringValue(entry, u"name"_s);
        file.mime = stringValue(entry, u"mime"_s);
        const QJsonValue size = entry.value(u"size"_s);
        file.size = size.isDouble() ? qint64(size.toDouble()) : -1;
        if (file.size < -1)
            file.size = -1;
        file.error = stringValue(entry, u"error"_s);
        appendFile(share.files, file, accepted);
    }
    return share;
}

QJsonObject sharedContentToJson(const SharedContent &share)
{
    QJsonObject object;
    const auto put = [](QJsonObject &target, const QString &key, const QString &value) {
        if (!value.isEmpty())
            target.insert(key, value);
    };
    put(object, u"subject"_s, share.subject);
    put(object, u"text"_s, share.text);
    put(object, u"url"_s, share.url);
    put(object, u"dir"_s, share.dir);
    put(object, u"source"_s, share.source);
    QJsonArray files;
    for (const SharedFile &file : share.files) {
        QJsonObject entry;
        put(entry, u"path"_s, file.path);
        put(entry, u"name"_s, file.name);
        put(entry, u"mime"_s, file.mime);
        if (file.size >= 0)
            entry.insert(u"size"_s, double(file.size));
        put(entry, u"error"_s, file.error);
        files.append(entry);
    }
    if (!files.isEmpty())
        object.insert(u"files"_s, files);
    return object;
}

SharedContent sharedContentFromQuery(QStringView query)
{
    SharedContent share;
    if (query.startsWith(u'?'))
        query = query.sliced(1);
    bool title = false;
    bool text = false;
    bool url = false;
    for (const QStringView part : query.tokenize(u'&')) {
        const qsizetype equals = part.indexOf(u'=');
        if (equals < 0)
            continue; // a name alone: no value
        const QString key = formDecode(part.first(equals));
        const auto take = [&](bool &seen, QString &field) {
            if (seen)
                return; // a repeated key keeps its first value
            seen = true;
            field = limitSharedText(formDecode(part.sliced(equals + 1)));
        };
        if (key == u"share-title")
            take(title, share.subject);
        else if (key == u"share-text")
            take(text, share.text);
        else if (key == u"share-url")
            take(url, share.url);
    }
    return share;
}

OpenArguments splitOpenArguments(const QStringList &arguments)
{
    const qsizetype open = arguments.indexOf(u"--open"_s);
    if (open < 0)
        return {arguments, {}};
    return {arguments.first(open), arguments.sliced(open + 1)};
}

SharedContent openedFilesShare(const QStringList &arguments, const QString &workingDirectory)
{
    SharedContent share;
    share.source = u"open"_s;
    const QDir base(workingDirectory.isEmpty() ? QDir::currentPath() : workingDirectory);
    QSet<QString> seen;
    int accepted = 0;
    for (const QString &argument : arguments) {
        if (argument.isEmpty())
            continue;
        SharedFile file;
        QString key;
        QString path;
        if (isUrlArgument(argument)) {
            const QUrl url(argument);
            if (url.isLocalFile()) {
                path = url.toLocalFile();
            } else {
                // A file of another computer (smb://, sftp://, a web address): none Brulion reads.
                file.name = lastSegment(url.path());
                if (file.name.isEmpty())
                    file.name = argument;
                file.error = u"unreadable"_s;
                key = argument;
            }
        } else {
            path = QDir::fromNativeSeparators(argument);
        }
        if (!path.isEmpty()) {
            if (QDir::isRelativePath(path))
                path = base.absoluteFilePath(path);
            path = QDir::cleanPath(QDir::fromNativeSeparators(path));
            file.path = path;
            file.name = lastSegment(path);
            key = path;
        }
        if (key.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        if (!appendFile(share.files, file, accepted))
            break;
    }
    return share;
}

SharedContent mergeOpenedShares(const SharedContent &waiting, const SharedContent &newer)
{
    SharedContent merged;
    merged.source = u"open"_s;
    const bool newerText = !newer.subject.isEmpty() || !newer.text.isEmpty() || !newer.url.isEmpty();
    const SharedContent &text = newerText ? newer : waiting;
    merged.subject = text.subject;
    merged.text = text.text;
    merged.url = text.url;
    // The same file once: by its path, or — an entry without one (another computer's file, one over
    // the cap) — by its name and reason.
    QSet<QString> seen;
    int accepted = 0;
    for (const SharedContent *share : {&waiting, &newer}) {
        for (const SharedFile &file : share->files) {
            const QString key = !file.path.isEmpty() ? u"p:"_s + file.path : u"e:%1:%2"_s.arg(file.error, file.name);
            if (seen.contains(key))
                continue;
            seen.insert(key);
            if (!appendFile(merged.files, file, accepted))
                return merged;
        }
    }
    return merged;
}

QString pageForOpenedFile(const QString &file, const QString &vaultRoot, Qt::CaseSensitivity rootCase)
{
    if (file.isEmpty() || vaultRoot.isEmpty())
        return {};
    QString root = vaultRoot;
    while (root.size() > 1 && root.endsWith(u'/'))
        root.chop(1);
    const QString prefix = root.endsWith(u'/') ? root : root + u'/';
    if (file.size() <= prefix.size() || !file.startsWith(prefix, rootCase))
        return {};
    const QString page = file.mid(prefix.size());
    for (const QString &segment : page.split(u'/')) {
        if (segment.isEmpty() || segment == u"." || segment == u"..")
            return {}; // not a clean path
    }
    // The vault's rule (vaultutil::isTracked): a folder starting with "." (".szkice") holds pages
    // too, only the folders paths::isIgnored() names (.trash, .obsidian, logseq…) do not.
    if (!paths::isMarkdown(page) || paths::isIgnored(page))
        return {};
    return page;
}

SharedContent sharedContentFromJson(const QByteArray &json)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {};
    return sharedContentFromJson(document.object());
}

QString composeSharedText(const QString &subject, const QString &text, const QString &url)
{
    static const QRegularExpression spaces(u"\\s+"_s);
    static const QRegularExpression urlInText(u"https?://\\S+"_s);
    QString title = QString(subject).replace(spaces, u" "_s).trimmed();
    QString body = text.trimmed();
    QString link = url.trimmed();
    if (link.isEmpty()) {
        // Android puts a shared page's link into the text: it becomes the link.
        const QRegularExpressionMatch match = urlInText.match(body);
        if (match.hasMatch()) {
            link = match.captured();
            // A sentence's closing punctuation is not part of the link, and neither is a bracket
            // around it: ")" or "]" without its opening one in the link (Wikipedia's
            // "Kraków_(ujednoznacznienie)" keeps its own), or ">".
            const auto unbalanced = [&link](QChar close, QChar open) {
                return link.back() == close && link.count(open) < link.count(close);
            };
            qsizetype closer = -1; // the bracket that ended the link, in the match
            while (!link.isEmpty()) {
                if (QStringView(u".,;:!?\"'").contains(link.back())) {
                    link.chop(1);
                } else if (unbalanced(u')', u'(') || unbalanced(u']', u'[') || link.back() == u'>') {
                    link.chop(1);
                    closer = link.size();
                } else {
                    break;
                }
            }
            qsizetype from = match.capturedStart();
            qsizetype length = match.capturedLength();
            if (closer >= 0) {
                // "(<link>)" goes with its brackets; otherwise the bracket stays in the text.
                const QChar close = match.captured().at(closer);
                const QChar open = close == u')' ? u'(' : close == u']' ? u'[' : u'<';
                if (from > 0 && body.at(from - 1) == open) {
                    --from;
                    length = closer + 2;
                } else {
                    length = closer;
                }
            }
            body = withoutPart(body, from, length);
        }
    } else if (body.contains(link)) {
        body = withoutPart(body, body.indexOf(link), link.size());
    }
    if (!title.isEmpty() && (title == link || body == title || (link.isEmpty() && body.contains(title))))
        title.clear();
    QString head;
    if (!link.isEmpty()) {
        title = title.remove(u'[').remove(u']').trimmed();
        head = title.isEmpty() ? u"<%1>"_s.arg(safeDestination(link)) : u"[%1](%2)"_s.arg(title, safeDestination(link));
    } else {
        head = title;
    }
    if (head.isEmpty())
        return body;
    if (body.isEmpty())
        return head;
    return head + u" — "_s + body;
}

SharedFileProblem checkSharedFile(const QString &name, const QString &mime, qint64 size)
{
    const qsizetype dot = name.lastIndexOf(u'.');
    const bool textName = dot >= 0 && isTextExtension(name.mid(dot + 1));
    const bool textType = mime.trimmed().toLower().startsWith(u"text/"_s);
    if (!textName && !textType)
        return SharedFileProblem::NotText;
    if (size > SharedFileMaxBytes)
        return SharedFileProblem::TooLarge;
    return SharedFileProblem::None;
}

bool looksBinary(const QByteArray &bytes)
{
    if (bytes.startsWith("\xFF\xFE") || bytes.startsWith("\xFE\xFF"))
        return false;
    return bytes.contains('\0');
}

QString sharedFileError(SharedFileProblem problem)
{
    switch (problem) {
    case SharedFileProblem::None: return {};
    case SharedFileProblem::NotText: return u"notText"_s;
    case SharedFileProblem::TooLarge: return u"tooLarge"_s;
    case SharedFileProblem::Binary: return u"binary"_s;
    }
    return {};
}

QString decodeSharedBytes(const QByteArray &bytes)
{
    return text::decodeText(bytes);
}

QString sharedPageTitle(const QString &fileName, const QString &text, const QString &fallback)
{
    constexpr qsizetype MaxTitle = 60;
    const auto usable = [](const QString &candidate) {
        const QString trimmed = candidate.trimmed();
        // sanitizeFileName() names an empty result "Untitled", and markers alone ("-", "---")
        // are no title: a title has a letter or a digit.
        static const QRegularExpression wordCharacter(u"[\\p{L}\\p{N}]"_s);
        if (!wordCharacter.match(trimmed).hasMatch())
            return QString();
        return paths::sanitizeFileName(trimmed);
    };
    // A display name may carry a folder ("Download/Lista.md" from some file managers).
    QString base = fileName;
    base = base.mid(std::max(base.lastIndexOf(u'/'), base.lastIndexOf(u'\\')) + 1);
    if (QString fromName = usable(withoutTextExtension(base)); !fromName.isEmpty())
        return fromName;
    for (const QString &line : withoutFrontmatter(splitLines(text))) {
        const QString plain = plainTitleLine(line);
        if (plain.isEmpty())
            continue;
        if (QString fromText = usable(cutOnWord(plain, MaxTitle)); !fromText.isEmpty())
            return cutOnWord(fromText, MaxTitle);
    }
    return fallback;
}

QString sharedPageContent(const QString &text)
{
    if (text.trimmed().isEmpty())
        return {};
    if (text.endsWith(u'\n') || text.endsWith(u'\r'))
        return text;
    return text + (text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s);
}

QString sharedFileBlock(const QString &title, const QString &text)
{
    const QString cleanTitle = QString(title).remove(u'*').trimmed();
    QString block = cleanTitle.isEmpty() ? u"- "_s : u"- **%1**"_s.arg(cleanTitle);
    QStringList lines = withoutFrontmatter(splitLines(text));
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty())
        lines.removeFirst();
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    bool previousBlank = false;
    for (const QString &line : std::as_const(lines)) {
        QString kept = line;
        while (!kept.isEmpty() && kept.back().isSpace())
            kept.chop(1);
        if (kept.isEmpty()) {
            if (!previousBlank)
                block += u'\n';
            previousBlank = true;
            continue;
        }
        previousBlank = false;
        block += u"\n  "_s + kept;
    }
    return block;
}

} // namespace brulion::notebook
