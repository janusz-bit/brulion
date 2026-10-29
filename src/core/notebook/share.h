// What the app does with text and files shared to Brulion from another app ("Udostępnij →
// Brulion" on Android, docs/ANDROID.md; the browser version's share target, docs/WEB.md; files
// opened with Brulion on a desktop — "Otwórz za pomocą", `--open` —, docs/WINDOWS.md and
// docs/NIXOS.md; the `--share` developer option): the shared content as the platform hands it over
// (JSON, the share target's query, the files of a launch), one capture text from a shared title,
// text and link, the title and the content of a new page made from a shared file, the journal
// block of a shared file, and the page of the open notebook an opened file already is. Pure
// functions: nothing here reads the clock, looks at a file or writes one.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace brulion::notebook {

/// Largest shared file Brulion saves (bytes); bigger ones are refused (SharedFileProblem::TooLarge).
inline constexpr qint64 SharedFileMaxBytes = 2 * 1024 * 1024;
/// Most files one share saves; the rest are listed as refused ("tooMany").
inline constexpr int SharedFilesMax = 20;
/// Most entries one share lists (a forged share or a launch with thousands of files must not make
/// the dialog list them all); the rest are dropped.
inline constexpr int SharedEntriesMax = 100;
/// Most characters of shared text kept (Android's ShareIntent.MAX_TEXT_CHARS); longer text is cut,
/// never inside a surrogate pair.
inline constexpr qsizetype SharedTextMaxChars = 1'000'000;

/// One shared file. On Android a copy in the app's files (files/share-in/<stamp>/<n>): the
/// sending app's read permission ends with the receiving activity; elsewhere any local file.
struct SharedFile {
    QString path;     ///< the local copy to read ("" when it could not be copied: `error`)
    QString name;     ///< display name as the sending app gave it ("Lista zakupów.md"; may be "")
    QString mime;     ///< its MIME type ("text/markdown"; may be "")
    qint64 size = -1; ///< bytes (-1 = unknown)
    /// Why it cannot be saved: "tooLarge", "notText", "binary", "unreadable", "tooMany" or "".
    QString error;
};

/// A share as it reaches the app.
struct SharedContent {
    QString subject; ///< EXTRA_SUBJECT (a web page's title)
    QString text;    ///< EXTRA_TEXT (plain text; a link when a browser shares a page)
    QString url;     ///< a separate link (the developer option; Android puts links into `text`)
    QString dir;     ///< folder of the local copies, removed once the share is saved or dismissed
    QList<SharedFile> files;
    /// Where it came from: "open" — files opened with Brulion on a desktop ("Otwórz za pomocą", a
    /// launcher: `--open`); "" — another app's share (Android, the web share target, `--share`).
    QString source;

    /// Nothing to save: no text, no link and no file.
    bool isEmpty() const;
};

/// `{"subject", "text", "url", "dir", "source", "files": [{"path", "name", "mime", "size",
/// "error"}]}`; missing keys and values of the wrong type count as absent, a source other than
/// "open" is "", a file without a path is "unreadable", readable files after the first
/// SharedFilesMax are "tooMany" (at most SharedEntriesMax entries are kept), and anything that is
/// not a JSON object is an empty share. Never throws.
SharedContent sharedContentFromJson(const QJsonObject &object);
SharedContent sharedContentFromJson(const QByteArray &json);
/// The JSON sharedContentFromJson() reads: sharedContentFromJson(sharedContentToJson(x)) equals x
/// for a share it made. Empty strings, a size of -1 and an empty source are left out.
QJsonObject sharedContentToJson(const SharedContent &share);

/// The browser version's share target launch (wasm/manifest.webmanifest "share_target", GET): the
/// query "share-title=…&share-text=…&share-url=…" as subject, text and url. Read as
/// application/x-www-form-urlencoded: "+" is a space, %XX are UTF-8 bytes (a "%" not followed by
/// two hex digits stays as it is, invalid UTF-8 becomes U+FFFD), a leading "?" is skipped. Unknown
/// keys are ignored, keys are case-sensitive, a repeated key keeps its first value, each value is
/// cut to SharedTextMaxChars. No files, source "". Never throws.
SharedContent sharedContentFromQuery(QStringView query);

/// A launch's command line split at the first "--open": `options` before it (the program name
/// first), `files` after it — every argument after "--open" is a file, also one that starts with
/// "-". Without "--open" `files` is empty.
struct OpenArguments {
    QStringList options;
    QStringList files;
};
OpenArguments splitOpenArguments(const QStringList &arguments);

/// Files opened with Brulion (`arguments` as a launcher passes them: paths absolute or relative to
/// `workingDirectory`, or file:// URLs) as a share with source "open": absolute clean paths ("/"
/// separators), the file name as its name, no mime or size (the app reads the files), the same
/// path once, at most SharedEntriesMax entries, files after the first SharedFilesMax "tooMany" (as
/// sharedContentFromJson). A URL that is no local file (smb://, https://) is an entry with error
/// "unreadable" named after its last path segment; empty arguments are skipped. Pure: no file is
/// looked at.
SharedContent openedFilesShare(const QStringList &arguments, const QString &workingDirectory);

/// A newer "open" share joined to one still waiting in the share dialog (Windows starts Brulion once
/// per selected file; two "Otwórz za pomocą" in a row): the waiting files, then the newer ones not
/// listed yet (the same path; an entry without a path: the same name and error), the caps applied
/// again as in sharedContentFromJson. Subject, text and url from `newer` when it has any, else from
/// `waiting`. Source "open", no dir.
SharedContent mergeOpenedShares(const SharedContent &waiting, const SharedContent &newer);

/// The page of the notebook at `vaultRoot` that `file` is ("Projekty/Plan.md"), "" when it is none:
/// outside the folder (also "/v2/a.md" for the root "/v"), the folder itself, not Markdown
/// (paths::isMarkdown) or ignored (paths::isIgnored: .trash, .obsidian, logseq… — the notebook's
/// own rule, so ".szkice/Pomysł.md" is a page). Both paths absolute and clean, "/" separators;
/// symbolic links are compared as written (the caller tries them resolved and as given).
/// `rootCase`: how the folder part is compared (Qt::CaseInsensitive where the file system ignores
/// letter case, Windows); the page keeps the letters of `file`.
QString pageForOpenedFile(const QString &file, const QString &vaultRoot,
                          Qt::CaseSensitivity rootCase = Qt::CaseSensitive);

/// One capture text from a share (port of the previous app's share target): a link becomes
/// "[Title](url)" (brackets dropped from the title) or "<url>" without a title, a link inside
/// the text is taken out of it as the link (Android puts it there), parts already contained in
/// another part are dropped, the rest of the text follows after " — " with its lines kept.
/// "" when there is nothing. "Wideo" + "Zobacz to https://yt.be/abc" ->
/// "[Wideo](https://yt.be/abc) — Zobacz to".
QString composeSharedText(const QString &subject, const QString &text, const QString &url = QString());

/// Why a shared file cannot be saved.
enum class SharedFileProblem { None, NotText, TooLarge, Binary };

/// A file named `name` of type `mime` and `size` bytes (-1 unknown): a text file by its
/// extension (.md, .markdown, .txt, …) or by a "text/" type, at most SharedFileMaxBytes.
SharedFileProblem checkSharedFile(const QString &name, const QString &mime, qint64 size);
/// Bytes that are not text: a NUL byte outside UTF-16 (which has a byte-order mark).
bool looksBinary(const QByteArray &bytes);
/// The error code of `problem` as in SharedFile::error ("" for None).
QString sharedFileError(SharedFileProblem problem);

/// A shared file's bytes as text: UTF-8 (BOM skipped), UTF-16 with BOM, else windows-1250/1252
/// (text::decodeText). Line endings are kept.
QString decodeSharedBytes(const QByteArray &bytes);

/// Title of a new page from a share: the file's name without its text extension ("Lista
/// zakupów.md" -> "Lista zakupów", "notatka.TXT" -> "notatka"), else the text's first line
/// (after a frontmatter; heading, list and checkbox markers, a link's target, bare links and
/// emphasis dropped), cut on a word to at most 60 characters; sanitised for a file name (no "/": one
/// page, no folders). `fallback` when neither gives a name.
QString sharedPageTitle(const QString &fileName, const QString &text, const QString &fallback);

/// Content of a new page made from a shared file's text: the text as it is (a .txt is not
/// re-formatted; line endings and every byte kept) with a line break at the end when it has
/// none (in the text's own line ending). "" for blank text.
QString sharedPageContent(const QString &text);

/// The journal block of a shared file (several files to the journal: one block each): its
/// title in bold as the block's first line ("- **Lista zakupów**"), the file's lines below as the
/// block's continuation lines (indented by two spaces, their own indentation kept: a list in
/// the file stays nested), a frontmatter dropped, runs of blank lines kept as one. Lines joined
/// with "\n" (notebook::appendBlock writes the note's own line ending). "- **title**" alone
/// for an empty file.
QString sharedFileBlock(const QString &title, const QString &text);

} // namespace brulion::notebook
