// Keeping a notebook's VectorIndex up to date, as pure functions. Part of the semantic module
// (semantic/semantic.h).
//
//   noteChunks(title, content)   what a note is embedded from
//   noteKey(title, content)      the version of a note its vectors are kept under
//   diffIndex(notes, index)      which notes to embed (new or changed) and which to drop (gone)
//
// The app (shell/semanticsearch.h) and brulion-cli (search --semantic) agree on all three: a note
// whose key the index keeps is current, any other is embedded again (the app) or searched by its
// words alone (the CLI).
#pragma once

#include "semantic/chunker.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <utility>

namespace brulion::semantic {

class VectorIndex;

/// The most chunks of one note embedded (some 30,000 words): a huge pasted log or book in a note
/// is searched by meaning through its beginning, and does not hold the rest of the notebook up.
inline constexpr int MaxChunksPerNote = 256;

/// The chunks a note is embedded from: chunkNote() of its text with every encrypted fragment
/// blanked first (markdown/encrypted.h — ciphertext means nothing, and the device's cache never
/// keeps anything of what it hides), at most MaxChunksPerNote.
QList<Chunk> noteChunks(QStringView title, QStringView content);

/// The version of a note its vectors are kept under: FNV-1a 64 over its title and its text (the
/// title begins every chunk) — never 0, which VectorIndex::key() gives for an unknown note.
quint64 noteKey(QStringView title, QStringView content);

/// The text of a note found by meaning alone, for its search result: from `line` (where its
/// closest chunk starts) on, at least `chars` characters of it when the note has them — blank lines,
/// code fences and the markers that begin a line (headings, list bullets, task boxes, quotes)
/// left out, the lines joined by spaces. Encrypted fragments are blanked.
QString passageExcerpt(QStringView content, int line, qsizetype chars = 260);

/// What brings an index up to date with the notebook's notes.
struct IndexDiff {
    /// Notes to embed — new ones and those whose key changed —, in the order `notes` gives them.
    QStringList embed;
    /// Notes the index has that the notebook no longer has, sorted.
    QStringList remove;
};

/// `notes`: every note of the notebook (its id — the vault path — and noteKey()), in the order to
/// embed them (the app: the most recently changed first).
IndexDiff diffIndex(const QList<std::pair<QString, quint64>> &notes, const VectorIndex &index);

} // namespace brulion::semantic
