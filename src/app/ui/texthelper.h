// TextHelper — text utilities for QML: diacritic folding (core text::foldKey), a fuzzy matcher
// for the command palette / quick switcher, and safe rich-text markup of matches.
// QML singleton `TextHelper` in module Brulion (registered by qmlforeign.h; the class itself is
// QML-free, library brulion_ui_helpers).
//
// Fuzzy matching: every non-space character of the query must appear in the text in order
// (case- and diacritic-insensitive: "zolw" matches "Żółw"). The best alignment is chosen by
// dynamic programming with fzf-like scoring: bonuses for word starts, camelCase humps,
// consecutive runs and a match at the very beginning; penalties for gaps. Score 0 = no match.
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace brulion::ui {

/// Result of fuzzyMatch(): score (0 = no match) and the matched UTF-16 offsets in the
/// original text, ascending.
struct FuzzyMatch {
    int score = 0;
    QList<int> positions;
};

/// The matcher itself (pure function, unit-tested).
FuzzyMatch fuzzyMatch(QStringView query, QStringView text);

class TextHelper : public QObject {
    Q_OBJECT

public:
    explicit TextHelper(QObject *parent = nullptr);

    /// Search key: lower-case without diacritics ("Zażółć" -> "zazolc").
    Q_INVOKABLE QString fold(const QString &text) const;
    /// Fuzzy score of `query` against `text`; 0 when it does not match. An empty query scores 1.
    Q_INVOKABLE int fuzzyScore(const QString &query, const QString &text) const;
    /// { score, positions } (see FuzzyMatch).
    Q_INVOKABLE QVariantMap fuzzyMatch(const QString &query, const QString &text) const;
    /// Rank `texts` against `query`: [{ index, score, positions }] best first (ties keep the
    /// input order), at most `limit` entries (0 = all). An empty query returns every index in
    /// order with score 0 and no positions. `secondary` (optional, same length) is matched too
    /// and counts at 1/2 when it beats the primary text (e.g. a page's folder path).
    Q_INVOKABLE QVariantList fuzzyRank(const QString &query, const QStringList &texts, int limit = 0,
                                       const QStringList &secondary = {}) const;
    /// Escaped rich text with the characters at `positions` wrapped in
    /// <b><font color="…">…</font></b> (colour optional). Safe for Text.StyledText.
    Q_INVOKABLE QString markMatches(const QString &text, const QVariantList &positions,
                                    const QString &color = {}) const;
    /// HTML-escape (&, <, >, ") for rich-text Text items.
    Q_INVOKABLE QString escapeHtml(const QString &text) const;
};

} // namespace brulion::ui
