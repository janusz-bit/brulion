// The index cache's codec pieces (indexcache.cpp), for its tests: the golden digest of the
// record and search encodings, round trips, hostile input. Internal.
#pragma once

#include "index/indexcache.h"

#include <QByteArray>
#include <QByteArrayView>

#include <optional>

namespace brulion::indexcache::detail {

/// The body of a note's record; nullopt when the note holds a value the format cannot carry
/// (a frontmatter QVariant of another type, nesting deeper than 32).
std::optional<QByteArray> encodeNote(const ParsedNote &note, const QString &content);
/// decodeNote() of a record body (no checksum): nullopt when malformed.
std::optional<ParsedNote> decodeNote(QByteArrayView body, const VaultPath &path, const QString &content,
                                     const VaultConfig &config);
/// The body of the search section.
QByteArray encodeSearch(const SearchIndex &search);
/// decodeSearch() of a body (no checksum): nullopt when malformed.
std::optional<SearchIndex> decodeSearch(QByteArrayView body);

} // namespace brulion::indexcache::detail
