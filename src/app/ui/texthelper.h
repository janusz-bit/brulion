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

/// One entry of fuzzyRankList(): the index in `texts`, its score and matched positions.
struct FuzzyRanked {
    int index = 0;
    int score = 0;
    QList<int> positions;
};
/// TextHelper::markMatches() with the positions as a list (pure function).
QString markMatchesHtml(const QString &text, const QList<int> &positions, const QString &color);
/// TextHelper::fuzzyRank() as a list (pure function).
QList<FuzzyRanked> fuzzyRankList(const QString &query, const QStringList &texts, int limit,
                                 const QStringList &secondary);

class TextHelper : public QObject {
    Q_OBJECT

public:
    explicit TextHelper(QObject *parent = nullptr);

    /// BKbd's key caps of a shortcut: `sequence` split on "+", "++" giving a "+" key ("Ctrl++" ->
    /// ["Ctrl", "+"], "Ctrl+Shift+F" -> ["Ctrl", "Shift", "F"]); empty parts dropped.
    Q_INVOKABLE QStringList shortcutKeys(const QString &sequence) const;
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
    /// fuzzyRank()'s texts themselves, best first (all of them in order for an empty query).
    Q_INVOKABLE QStringList fuzzyRankTexts(const QString &query, const QStringList &texts, int limit = 0) const;
    Q_INVOKABLE QString markMatches(const QString &text, const QVariantList &positions,
                                    const QString &color = {}) const;
    /// HTML-escape (&, <, >, ") for rich-text Text items.
    Q_INVOKABLE QString escapeHtml(const QString &text) const;
    /// Rendered rich text (markdown::InlineRenderer) with the links to `href` underlined — the
    /// hover and press feedback of rendered text (docs/UI.md "Links in rendered text",
    /// InlineRenderer::withHoveredLink()); `html` unchanged for "" and an href not in it.
    Q_INVOKABLE QString hoveredLinkHtml(const QString &html, const QString &href) const;
    /// Rendered rich text with the find bar's matches tinted `color`
    /// (InlineRenderer::withFindHighlight(); docs/UI.md "Find and replace"). `html` unchanged for "".
    Q_INVOKABLE QString findHighlightedHtml(const QString &html, const QString &query, bool matchCase,
                                            bool matchDiacritics, bool wholeWord, const QString &color) const;
    /// The editor's "Highlight" on a block's text (markdown::setHighlight(), docs/UI.md "Coloured
    /// highlights"): `color` a palette name ("default", "red", …) or "" to remove the highlight
    /// around the selection [from, to). { text, from, to (the highlighted text), changed }.
    Q_INVOKABLE QVariantMap setHighlight(const QString &text, int from, int to, const QString &color) const;
    /// The page icon a typed or pasted text names (markdown::parsePageIcon(): an emoji, Iconize's
    /// `LiBookOpen`): { kind: "emoji" | "app" | "", name }.
    Q_INVOKABLE QVariantMap pageIcon(const QString &text) const;
    /// The highlight palette for menus: [{ name, tint (what the app draws), written (what the
    /// note gets) }], "default" first.
    Q_INVOKABLE QVariantList highlightColors() const;
};

} // namespace brulion::ui
