#pragma once
// Extensions written in the notebook (docs/EXTENSIONS.md "Extensions in the notebook"): a page
// tagged `rozszerzenie` (`extension`) whose ```luau blocks are its code — the notebook's own code,
// edited, synced, versioned and linked like any note (Emacs' init file and TiddlyWiki's code
// tiddlers in one). Pure functions over a page's text and its parsed note.
//
//   if (isExtensionPage(note)) {
//       const PageExtension e = readPageExtension(path, note, content);
//       engine.load(e.extension());           // errors name the page's lines: "Licznik słów:14: …"
//   }
//
// The code is every fenced block whose language is `luau` or `lua`, in order (also inside a list
// item, as Logseq writes them). `source` keeps each code line at its own line and blanks every
// other line, so the line numbers of errors and tracebacks are the page's. `hash` covers the code
// and the permissions only: prose around the code changes nothing that needs approving again.
//
// Page properties (frontmatter or `key:: value` lines; Polish or English keys and values):
//   tags:: rozszerzenie                  (or extension) — makes the page an extension
//   uprawnienia:: zapis, ai              (permissions:: write, ai) — what it may do beyond reading
//   opis:: Liczy słowa dzisiejszej notatki   (description::) — shown in Settings → Extensions
//   zastępuje:: silownia                 (overrides::) — the built-in extension it replaces
//
// The id is the title folded: lower case, no diacritics, words joined by "-" ("Licznik słów" →
// "licznik-slow"). A page that overrides a built-in extension (`zastępuje::`, or a title naming
// it: "Siłownia", "Gym") is that extension's source in the notebook: it runs instead of the
// built-in code (TiddlyWiki's shadow tiddlers), with the built-in's bundled data.

#include "extensions/engine.h"
#include "types.h"

#include <QString>
#include <QStringList>

namespace brulion::extensions {

/// The tags that make a page an extension (page-level: frontmatter `tags:` or a `tags::` line).
inline const QStringList &extensionTags()
{
    static const QStringList tags{QStringLiteral("rozszerzenie"), QStringLiteral("extension")};
    return tags;
}

/// Is the page tagged as an extension at page level?
bool isExtensionPage(const ParsedNote &note);

/// The id of an extension page titled `title`: "Licznik słów" → "licznik-slow" (a-z, 0-9 and "-",
/// at most 64 characters; "extension" when nothing is left).
QString extensionId(const QString &title);

/// The code of a page: `source` (the page's lines, everything but the code blanked), `code` (the
/// code blocks' text, each block's lines joined by "\n", blocks separated by an empty line).
struct ExtensionCode
{
    QString source;
    QString code;
    int firstLine = -1; ///< the first code line (0-based), -1: no code
    int blocks = 0; ///< ```luau blocks found
    bool unclosed = false; ///< the last one has no closing fence (it runs to the page's end)
};

ExtensionCode extensionCode(QStringView content);

/// The ```luau block that contains `line` (0-based; a fence line counts) as code padded to its own
/// lines like `source`, or "" when the line is in no such block. "Run Luau block" in the editor.
QString codeBlockAt(QStringView content, int line);

/// Permission names as the engine knows them ("write", "ai"), from a page property's words in
/// either language ("zapis", "write", "ai"); unknown words go to `unknown`.
QStringList permissionsFrom(const QStringList &words, QStringList *unknown = nullptr);

struct PageExtension
{
    QString id; ///< "licznik-slow"
    QString title; ///< "Licznik słów"
    VaultPath path;
    QString source; ///< ExtensionCode::source
    QString code;
    QStringList permissions; ///< canonical, sorted: "ai", "write"
    QString description;
    QString overrides; ///< `zastępuje::` / `overrides::`: a built-in extension's id, "" = none
    QString hash; ///< hex SHA-256 of the permissions and the code: what an approval covers
    int firstLine = -1;
    int blocks = 0;
    QStringList problems; ///< "unknown permission: sieć", "no ```luau block", "unclosed ```luau block"

    /// For Engine::load(): id, source, permissions, the title as the name in errors.
    Extension extension() const;
};

PageExtension readPageExtension(const VaultPath &path, const ParsedNote &note, QStringView content);

/// The hash of an extension's permissions and code (PageExtension::hash; built-in sources too).
QString extensionHash(const QStringList &permissions, const QString &code);

/// The text of a new extension page in `language` ("pl" | "en"): the tag, a description and a
/// small example (a command, a view, a hook), every line explained.
QString newExtensionPage(const QString &language);

/// A built-in extension's source as a page of the notebook (its override: `zastępuje:: <id>`): the
/// properties, a few words on what the page is, then the code in ```luau blocks, one per section
/// of the source ("-- Parsing ----" comment lines start a section, each under its own heading).
QString builtinSourcePage(const Extension &builtin, const QString &language);

} // namespace brulion::extensions
