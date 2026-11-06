// Chunks of a note for the embedding model. Part of the semantic module (semantic/semantic.h).
#pragma once

#include <QList>
#include <QString>
#include <QStringView>

namespace brulion::semantic {

struct Chunk {
    /// "<title>\n<text>": the title gives every chunk its context.
    QString text;
    /// 0-based line of the note where the chunk starts (the frontmatter counts).
    int line = 0;
};

/// Default chunk size in words: about 200 tokens of Polish text, well inside the model's 512.
inline constexpr int DefaultChunkWords = 120;

/// Cut a note into chunks of at most `maxWords` words. Parts end at blank lines and before
/// headings ('#') and top-level bullets ("- "); consecutive parts are joined while they fit, and a
/// part longer than `maxWords` is split by words. YAML frontmatter is skipped. Never empty: a note
/// without text gives one chunk with the title alone.
QList<Chunk> chunkNote(QStringView title, QStringView content, int maxWords = DefaultChunkWords);

/// Whether a text reads as English rather than Polish (stop words and Polish letters counted);
/// used to estimate the language direction of Calibration.
bool looksEnglish(QStringView text);

} // namespace brulion::semantic
