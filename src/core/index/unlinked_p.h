// Unlinked references: plain-text mentions of a page's title or aliases in other notes — whole
// words (Unicode boundaries), case-insensitive, never inside code, links, URLs, tags or
// frontmatter. Internal to the index module.
#pragma once

#include "types.h"

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace brulion::index_detail {

/// A compiled page name to look for.
struct MentionMatcher {
    QString firstWord;     ///< lower-cased NFC first word (cheap pre-filter on lower-cased content)
    QRegularExpression re; ///< the name, case-insensitive, any whitespace run between words
    qsizetype length = 0;  ///< name length (longer names win ties)
};

/// Matchers for a page's names (title + aliases): NFC, deduplicated case-insensitively,
/// longest first (so "Dom Jana" wins over "Dom" at the same position); names shorter than two
/// characters are dropped.
QList<MentionMatcher> mentionMatchers(const QStringList &names);

/// One mention: 0-based line and an excerpt around it.
struct Mention {
    int line = 0;
    QString excerpt;
};

/// Mentions of `matchers` in one note, at most `max`, one per line. `content` must be the text
/// `note` was parsed from (link offsets are used for masking).
QList<Mention> findMentions(const QString &content, const ParsedNote &note, const QList<MentionMatcher> &matchers,
                            qsizetype max);

} // namespace brulion::index_detail
