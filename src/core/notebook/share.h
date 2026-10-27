// What the app does with text and files shared to Brulion from another app ("Udostępnij →
// Brulion" on Android, docs/ANDROID.md; the `--share` developer option elsewhere): the shared
// content as the platform hands it over (JSON), one capture text from a shared title, text and
// link, the title and the content of a new page made from a shared file, and the journal block of
// a shared file. Pure functions: nothing here reads the clock or writes a file.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace brulion::notebook {

/// Largest shared file Brulion saves (bytes); bigger ones are refused (SharedFileProblem::TooLarge).
inline constexpr qint64 SharedFileMaxBytes = 2 * 1024 * 1024;
/// Most files one share saves; the rest are listed as refused ("tooMany").
inline constexpr int SharedFilesMax = 20;

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

    /// Nothing to save: no text, no link and no file.
    bool isEmpty() const;
};

/// `{"subject", "text", "url", "dir", "files": [{"path", "name", "mime", "size", "error"}]}`;
/// missing keys and values of the wrong type count as absent, a file without a path is
/// "unreadable", readable files after the first SharedFilesMax are "tooMany" (at most 100
/// entries are kept), and anything that is not a JSON object is an empty share. Never throws.
SharedContent sharedContentFromJson(const QJsonObject &object);
SharedContent sharedContentFromJson(const QByteArray &json);

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
