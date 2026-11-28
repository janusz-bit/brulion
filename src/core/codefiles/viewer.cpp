#include "codefiles/viewer.h"

#include "codefiles/fileindex.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::codefiles {

namespace {

bool isMentionChar(QChar c)
{
    return c.isLetterOrNumber() || c.isMark() || c == u'-' || c == u'_' || c == u'.' || c == u'/' || c == u'+'
           || c == u'~';
}

/// "12", "12:5", "12-20": the line part of a link; false when it is none.
bool parseLinePart(QStringView s, FileLink *link)
{
    if (s.isEmpty())
        return false;
    qsizetype i = 0;
    const auto number = [&s, &i](int *out) {
        const qsizetype from = i;
        while (i < s.size() && s.at(i).isDigit() && i - from < 9)
            ++i;
        if (i == from)
            return false;
        *out = s.sliced(from, i - from).toInt();
        return true;
    };
    int line = 0;
    if (!number(&line) || line <= 0)
        return false;
    int column = 0;
    int end = 0;
    if (i < s.size() && s.at(i) == u':') {
        ++i;
        if (!number(&column))
            return false;
    } else if (i < s.size() && s.at(i) == u'-') {
        ++i;
        if (!number(&end))
            return false;
    }
    if (i != s.size())
        return false;
    link->line = line;
    link->column = column;
    link->endLine = end >= line ? end : 0;
    return true;
}

/// "L12", "L12-L20", "L12-20", "L12C5" (GitHub's anchors).
bool parseAnchor(QStringView s, FileLink *link)
{
    if (s.size() < 2 || (s.at(0) != u'L' && s.at(0) != u'l'))
        return false;
    QString rest = s.sliced(1).toString();
    rest.replace(u"-L"_s, u"-"_s).replace(u"-l"_s, u"-"_s);
    if (const qsizetype c = rest.indexOf(u'C', 0, Qt::CaseInsensitive); c > 0)
        rest = rest.left(c) + u':' + rest.mid(c + 1);
    return parseLinePart(rest, link);
}

QString fenceFor(QStringView lines)
{
    int longest = 0;
    int run = 0;
    for (const QChar c : lines) {
        run = c == u'`' ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    return QString(std::max(3, longest + 1), u'`');
}

} // namespace

bool looksBinary(QByteArrayView head)
{
    const QByteArrayView start = head.first(std::min<qsizetype>(head.size(), 8192));
    if (start.contains('\0'))
        return true;
    qsizetype control = 0;
    for (const char ch : start) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != 0x1b)
            ++control;
    }
    return control * 10 > start.size();
}

bool isPictureName(QStringView name)
{
    const qsizetype dot = name.lastIndexOf(u'.');
    if (dot < 0)
        return false;
    const QString ext = name.sliced(dot + 1).toString().toLower();
    static const QStringList pictures{u"png"_s, u"jpg"_s, u"jpeg"_s, u"gif"_s, u"webp"_s, u"bmp"_s, u"svg"_s, u"ico"_s};
    return pictures.contains(ext);
}

ViewerFile readForViewer(const QString &absolutePath, qint64 maxBytes)
{
    ViewerFile out;
    const QFileInfo info(absolutePath);
    if (!info.exists()) {
        out.kind = FileKind::Missing;
        return out;
    }
    if (info.isDir()) {
        out.kind = FileKind::Folder;
        return out;
    }
    out.size = info.size();
    if (isPictureName(info.fileName())) {
        out.kind = FileKind::Picture;
        return out;
    }
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        out.kind = FileKind::Unreadable;
        return out;
    }
    QByteArray bytes = file.read(std::max<qint64>(0, maxBytes) + 1);
    if (looksBinary(bytes)) {
        out.kind = FileKind::Binary;
        return out;
    }
    if (bytes.size() > maxBytes) {
        out.truncated = true;
        bytes.truncate(maxBytes);
        // After its last whole line (a line longer than the limit: the bytes as they are, the
        // decoder replaces a cut character).
        if (const qsizetype lf = bytes.lastIndexOf('\n'); lf >= 0)
            bytes.truncate(lf + 1);
    }
    out.kind = FileKind::Text;
    out.text = QString::fromUtf8(bytes);
    // (A byte order mark is no character of the text.)
    if (out.text.startsWith(QChar(0xfeff)))
        out.text.remove(0, 1);
    return out;
}

QString languageForPath(QStringView path)
{
    const QStringView name = path.sliced(path.lastIndexOf(u'/') + 1);
    const QString lower = name.toString().toLower();
    static const QHash<QString, QString> names{
            {u"dockerfile"_s, u"dockerfile"_s}, {u"containerfile"_s, u"dockerfile"_s},
            {u"makefile"_s, u"makefile"_s},     {u"gnumakefile"_s, u"makefile"_s},
            {u"cmakelists.txt"_s, u"cmake"_s},  {u"justfile"_s, u"makefile"_s},
            {u".bashrc"_s, u"sh"_s},            {u".zshrc"_s, u"sh"_s},
            {u".profile"_s, u"sh"_s},           {u".gitignore"_s, u"gitignore"_s},
            {u".gitattributes"_s, u"gitignore"_s}, {u".editorconfig"_s, u"ini"_s},
            {u"flake.lock"_s, u"json"_s},       {u"cargo.lock"_s, u"toml"_s},
            {u"gemfile"_s, u"ruby"_s},          {u"rakefile"_s, u"ruby"_s},
            {u"meson.build"_s, u"meson"_s},     {u"build.gradle"_s, u"groovy"_s}};
    if (const auto it = names.constFind(lower); it != names.cend())
        return *it;
    if (lower.startsWith(u"dockerfile."_s))
        return u"dockerfile"_s;
    const qsizetype dot = lower.lastIndexOf(u'.');
    if (dot < 0)
        return {};
    const QString ext = lower.mid(dot + 1);
    static const QHash<QString, QString> extensions{
            {u"c"_s, u"c"_s},          {u"h"_s, u"cpp"_s},        {u"cc"_s, u"cpp"_s},       {u"cpp"_s, u"cpp"_s},
            {u"cxx"_s, u"cpp"_s},      {u"hpp"_s, u"cpp"_s},      {u"hh"_s, u"cpp"_s},       {u"hxx"_s, u"cpp"_s},
            {u"ipp"_s, u"cpp"_s},      {u"inl"_s, u"cpp"_s},      {u"m"_s, u"objectivec"_s}, {u"mm"_s, u"objectivec"_s},
            {u"cs"_s, u"cs"_s},        {u"java"_s, u"java"_s},    {u"kt"_s, u"kotlin"_s},    {u"kts"_s, u"kotlin"_s},
            {u"scala"_s, u"scala"_s},  {u"groovy"_s, u"groovy"_s}, {u"gradle"_s, u"groovy"_s}, {u"swift"_s, u"swift"_s},
            {u"go"_s, u"go"_s},        {u"rs"_s, u"rust"_s},      {u"zig"_s, u"zig"_s},      {u"dart"_s, u"dart"_s},
            {u"js"_s, u"js"_s},        {u"mjs"_s, u"js"_s},       {u"cjs"_s, u"js"_s},       {u"jsx"_s, u"jsx"_s},
            {u"ts"_s, u"ts"_s},        {u"mts"_s, u"ts"_s},       {u"cts"_s, u"ts"_s},       {u"tsx"_s, u"tsx"_s},
            {u"vue"_s, u"vue"_s},      {u"svelte"_s, u"svelte"_s}, {u"qml"_s, u"qml"_s},     {u"py"_s, u"python"_s},
            {u"pyi"_s, u"python"_s},   {u"rb"_s, u"ruby"_s},      {u"php"_s, u"php"_s},      {u"pl"_s, u"perl"_s},
            {u"pm"_s, u"perl"_s},      {u"lua"_s, u"lua"_s},      {u"luau"_s, u"luau"_s},    {u"sh"_s, u"sh"_s},
            {u"bash"_s, u"sh"_s},      {u"zsh"_s, u"sh"_s},       {u"fish"_s, u"fish"_s},    {u"ps1"_s, u"powershell"_s},
            {u"bat"_s, u"bat"_s},      {u"cmd"_s, u"bat"_s},      {u"json"_s, u"json"_s},    {u"jsonc"_s, u"jsonc"_s},
            {u"json5"_s, u"jsonc"_s},  {u"yaml"_s, u"yaml"_s},    {u"yml"_s, u"yaml"_s},     {u"toml"_s, u"toml"_s},
            {u"ini"_s, u"ini"_s},      {u"cfg"_s, u"ini"_s},      {u"conf"_s, u"ini"_s},     {u"properties"_s, u"ini"_s},
            {u"cmake"_s, u"cmake"_s},  {u"nix"_s, u"nix"_s},      {u"mk"_s, u"makefile"_s},  {u"md"_s, u"markdown"_s},
            {u"markdown"_s, u"markdown"_s}, {u"html"_s, u"html"_s}, {u"htm"_s, u"html"_s},    {u"xml"_s, u"xml"_s},
            {u"svg"_s, u"xml"_s},      {u"ui"_s, u"xml"_s},       {u"qrc"_s, u"xml"_s},      {u"ts.xml"_s, u"xml"_s},
            {u"xaml"_s, u"xml"_s},     {u"plist"_s, u"xml"_s},    {u"css"_s, u"css"_s},      {u"scss"_s, u"scss"_s},
            {u"sass"_s, u"scss"_s},    {u"less"_s, u"less"_s},    {u"sql"_s, u"sql"_s},      {u"diff"_s, u"diff"_s},
            {u"patch"_s, u"diff"_s},   {u"hs"_s, u"haskell"_s},   {u"ml"_s, u"ocaml"_s},     {u"ex"_s, u"elixir"_s},
            {u"exs"_s, u"elixir"_s},   {u"erl"_s, u"erlang"_s},   {u"clj"_s, u"clojure"_s},  {u"r"_s, u"r"_s},
            {u"jl"_s, u"julia"_s},     {u"tf"_s, u"hcl"_s},       {u"hcl"_s, u"hcl"_s},      {u"proto"_s, u"protobuf"_s},
            {u"graphql"_s, u"graphql"_s}, {u"gql"_s, u"graphql"_s}, {u"vim"_s, u"vim"_s},    {u"tex"_s, u"latex"_s},
            {u"glsl"_s, u"glsl"_s},    {u"frag"_s, u"glsl"_s},    {u"vert"_s, u"glsl"_s},    {u"wgsl"_s, u"wgsl"_s},
            {u"txt"_s, u"text"_s},     {u"log"_s, u"text"_s},     {u"csv"_s, u"csv"_s},      {u"tsv"_s, u"csv"_s},
            {u"env"_s, u"sh"_s}};
    if (const auto it = extensions.constFind(ext); it != extensions.cend())
        return *it;
    return {};
}

std::optional<FileLink> parseFileLink(QStringView link)
{
    QString s = link.trimmed().toString();
    if (s.isEmpty() || s.contains(u'\n') || s.contains(u'\r'))
        return std::nullopt;
    FileLink out;
    if (s.startsWith(u"file:"_s, Qt::CaseInsensitive)) {
        const QUrl url(s);
        if (!url.isValid() || (!url.host().isEmpty() && url.host() != u"localhost"))
            return std::nullopt;
        // toLocalFile(): "C:/…" on Windows (path() keeps the "/" before the drive: "/C:/…"); without
        // the "localhost" host, which it would turn into a network path.
        QUrl local = url;
        local.setHost(QString());
        s = local.toLocalFile();
        if (url.hasFragment())
            s += u'#' + url.fragment(QUrl::FullyDecoded);
    } else {
        // A scheme ("http:", "mailto:") — but not a Windows drive ("C:\…", "C:/…").
        const qsizetype colon = s.indexOf(u':');
        if (colon > 1) {
            bool scheme = s.at(0).isLetter();
            for (qsizetype i = 1; i < colon && scheme; ++i)
                scheme = s.at(i).isLetterOrNumber() || s.at(i) == u'+' || s.at(i) == u'-' || s.at(i) == u'.';
            // ("src/a.cpp:12": a "/" or "." before the colon makes it a path.)
            if (scheme && !s.left(colon).contains(u'.') && (s.size() == colon + 1 || !s.at(colon + 1).isDigit()))
                return std::nullopt;
        }
        s = QUrl::fromPercentEncoding(s.toUtf8());
    }
    // "#L12", "#L12-L20".
    if (const qsizetype hash = s.lastIndexOf(u'#'); hash >= 0) {
        FileLink anchored;
        if (parseAnchor(QStringView(s).sliced(hash + 1), &anchored)) {
            out.line = anchored.line;
            out.column = anchored.column;
            out.endLine = anchored.endLine;
        }
        s.truncate(hash);
    }
    // ":12", ":12:5", ":12-20" (the last colon that leaves a line part; a drive's colon stays).
    if (out.line == 0) {
        for (qsizetype colon = s.indexOf(u':', 2); colon >= 0; colon = s.indexOf(u':', colon + 1)) {
            FileLink lined;
            if (parseLinePart(QStringView(s).sliced(colon + 1), &lined)) {
                out.line = lined.line;
                out.column = lined.column;
                out.endLine = lined.endLine;
                s.truncate(colon);
                break;
            }
        }
    }
    if (s.isEmpty())
        return std::nullopt;
    out.path = s;
    return out;
}

QString relativeInside(const QString &folder, const QString &path)
{
    if (folder.isEmpty() || path.isEmpty())
        return {};
    const QString base = QDir::cleanPath(QDir(folder).absolutePath());
    const QString full = QDir::cleanPath(QDir::isAbsolutePath(path) ? path : base + u'/' + path);
    const QString prefix = base.endsWith(u'/') ? base : base + u'/';
    if (!full.startsWith(prefix) || full.size() == prefix.size())
        return {};
    return cleanRelativePath(QStringView(full).sliced(prefix.size()));
}

namespace {
/// "@path", quoted when it has a space.
QString mentionToken(const QString &path)
{
    return path.contains(u' ') ? QString(u"@\""_s + path + u'"') : QString(u'@' + path);
}

/// `text` in a code span: a fence of one more backtick than its longest run.
QString codeSpan(const QString &text)
{
    int longest = 0, run = 0;
    for (const QChar c : text) {
        run = c == u'`' ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    const QString fence(longest + 1, u'`');
    return fence + text + fence;
}

bool isPicturePath(const QString &path)
{
    static const QStringList pictures{u"png"_s, u"jpg"_s, u"jpeg"_s, u"gif"_s, u"webp"_s, u"bmp"_s, u"svg"_s};
    return pictures.contains(QFileInfo(path).suffix().toLower());
}
} // namespace

Dropped droppedFiles(const QString &folder, const QStringList &paths)
{
    Dropped out;
    QStringList named;
    for (const QString &path : paths) {
        if (path.isEmpty())
            continue;
        if (isPicturePath(path)) {
            if (!out.pictures.contains(path))
                out.pictures << path;
            continue;
        }
        const bool itself = !folder.isEmpty() && QDir::cleanPath(path) == QDir::cleanPath(folder);
        const QString inside = relativeInside(folder, path);
        const QString token = itself ? u"@./"_s : !inside.isEmpty() ? mentionToken(inside) : codeSpan(path);
        if (!named.contains(token))
            named << token;
    }
    out.text = named.join(u' ');
    return out;
}

MentionAt mentionAt(QStringView text, int caret)
{
    if (caret < 0 || caret > text.size())
        return {};
    qsizetype at = caret;
    while (at > 0 && isMentionChar(text.at(at - 1)))
        --at;
    if (at > 0 && text.at(at - 1) == u'@'
        && (at == 1 || text.at(at - 2).isSpace() || text.at(at - 2) == u'(' || text.at(at - 2) == u'['))
        return {int(at - 1), text.sliced(at, caret - at).toString()};
    return {};
}

QString withMention(QStringView text, int caret, QStringView path)
{
    const MentionAt m = mentionAt(text, caret);
    const QString p = path.trimmed().toString();
    if (m.start < 0 || p.isEmpty())
        return {};
    const QString written = mentionToken(p);
    QStringView rest = text.sliced(caret);
    // (The rest of a word the caret was in belongs to the mention: replaced too.)
    qsizetype skip = 0;
    while (skip < rest.size() && isMentionChar(rest.at(skip)))
        ++skip;
    rest = rest.sliced(skip);
    const bool space = !rest.startsWith(u' ');
    return text.first(m.start).toString() + written + (space ? u" "_s : QString()) + rest.toString();
}

int mentionEnd(QStringView text, int caret, QStringView path)
{
    const MentionAt m = mentionAt(text, caret);
    const QString p = path.trimmed().toString();
    if (m.start < 0 || p.isEmpty())
        return caret;
    return m.start + int(p.size()) + 1 + (p.contains(u' ') ? 2 : 0) + 1;
}

QStringList mentionedPaths(QStringView message)
{
    QStringList out;
    const qsizetype n = message.size();
    for (qsizetype i = 0; i < n; ++i) {
        if (message.at(i) != u'@')
            continue;
        if (i > 0 && !(message.at(i - 1).isSpace() || message.at(i - 1) == u'(' || message.at(i - 1) == u'['))
            continue;
        QString path;
        qsizetype j = i + 1;
        if (j < n && message.at(j) == u'"') {
            const qsizetype close = message.indexOf(u'"', j + 1);
            if (close < 0)
                continue;
            path = message.sliced(j + 1, close - j - 1).toString();
            j = close + 1;
        } else {
            while (j < n && isMentionChar(message.at(j)))
                ++j;
            path = message.sliced(i + 1, j - i - 1).toString();
            while (!path.isEmpty() && QStringView(u".,:;!?").contains(path.back()))
                path.chop(1);
        }
        i = j - 1;
        if (path.isEmpty() || path.contains(u'\n') || out.contains(path))
            continue;
        out.append(path);
    }
    return out;
}

QString reviewComment(QStringView path, int from, int to, QStringView lines, QStringView comment)
{
    if (to < from)
        std::swap(from, to);
    QString body = lines.toString();
    while (body.endsWith(u'\n') || body.endsWith(u'\r'))
        body.chop(1);
    const QString fence = fenceFor(body);
    QString out = path.toString() + u':' + QString::number(from);
    if (to > from)
        out += u'-' + QString::number(to);
    out += u'\n' + fence + languageForPath(path) + u'\n' + body + u'\n' + fence;
    const QString note = comment.trimmed().toString();
    if (!note.isEmpty())
        out += u'\n' + note;
    return out;
}

} // namespace brulion::codefiles
