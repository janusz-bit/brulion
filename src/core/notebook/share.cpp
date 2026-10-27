#include "notebook/share.h"

#include "text/decode.h"
#include "util/paths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace brulion::notebook {

namespace {

/// Hard cap on the entries a share lists (the platform copies at most SharedFilesMax files; a
/// forged share must not make the dialog list thousands).
constexpr int SharedEntriesMax = 100;

QString stringValue(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

/// Text extensions a shared file may have (lower case, without the dot).
bool isTextExtension(const QString &suffix)
{
    static const QStringList known = {u"md"_s,  u"markdown"_s, u"mdown"_s, u"mkd"_s,
                                      u"txt"_s, u"text"_s};
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
    const bool space = !left.isEmpty() && !right.isEmpty() && !left.endsWith(u'\n') && !right.startsWith(u'\n');
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
    while (!cut.isEmpty() && (cut.back().isSpace() || cut.back() == u',' || cut.back() == u';' || cut.back() == u':'
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
        if (file.path.isEmpty() && file.name.isEmpty() && file.error.isEmpty())
            continue;
        if (file.error.isEmpty() && file.path.isEmpty())
            file.error = u"unreadable"_s;
        if (file.error.isEmpty() && ++accepted > SharedFilesMax) {
            file.error = u"tooMany"_s;
            file.path.clear();
        }
        share.files.append(file);
    }
    return share;
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
            // A sentence's closing punctuation is not part of the link.
            while (!link.isEmpty() && QStringView(u".,;:!?\"'").contains(link.back()))
                link.chop(1);
            body = withoutPart(body, match.capturedStart(), match.capturedLength());
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
