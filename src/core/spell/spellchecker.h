// SpellChecker — whether a word is spelled right in any of the chosen languages, and what to write
// instead (docs/UI.md "Spell checking"). Part of the core module "spell" — see spell/spell.h.
//
//   SpellChecker checker;
//   checker.setBackends(std::move(backends));      // one per language, the first one's suggestions first
//   checker.setPersonalWords({u"Brulion"_s});      // the personal word list (a device setting)
//   checker.isCorrect(u"zażółć"_s);                // NFC, typographic apostrophes as ASCII
//   checker.suggestions(u"zażułć"_s);              // merged, at most `max`
//
// The rules (tested in tests/core/spell):
//  * a word is right when one backend knows it — Polish and English at once: "Spotkanie z team
//    leaderem" needs no setting;
//  * the personal list and the ignored words match exactly, capitalised ("Kot" for "kot": the
//    start of a sentence) and in capitals ("KOT"); "Brulion" in the list does not make
//    "brulion" right;
//  * a hyphenated word is right when the whole is known, or every part is (a part in capitals —
//    an acronym, "PDF-y" — counts as right);
//  * a word followed by '.' ("np.", "e.g.": spellwords.h WordSpan::dot) is right when the word
//    alone or with its dot is known;
//  * without backends every word is right (nothing to check against).
// Not thread-safe; backends may be slow (Hunspell's suggestions, Android's checker answers over
// IPC): the app runs one SpellChecker on a worker thread (src/app/editor/spellcheck.h).
#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace brulion::spell {

/// One language's dictionary: Hunspell, Android's spell checker (src/platform/spellbackends.h)
/// or a word list (tests).
class SpellBackend {
public:
    virtual ~SpellBackend();
    /// The language it checks: a primary language subtag ("pl", "en").
    virtual QString language() const = 0;
    /// True when `word` (normalizedWord()) is spelled right.
    virtual bool check(const QString &word) = 0;
    /// Up to `max` corrections of `word`, the best first.
    virtual QStringList suggest(const QString &word, int max) = 0;
    /// check() of every word, in order (one request for backends that pay per call: Android).
    virtual QList<bool> checkAll(const QStringList &words);
};

/// A backend knowing a fixed list of words — for tests and the editor playground. Matching
/// follows Hunspell's letter-case rules (as SpellChecker's personal list: exact, capitalised, in
/// capitals); suggestions are the known words nearest by edit distance (at most 2 edits; a
/// transposition is one).
class WordListBackend final : public SpellBackend {
public:
    WordListBackend(QString language, const QStringList &words);
    QString language() const override { return m_language; }
    bool check(const QString &word) override;
    QStringList suggest(const QString &word, int max) override;
    /// Words check()ed or suggest()ed so far (tests: what reached the backend).
    QStringList asked() const { return m_asked; }

private:
    QString m_language;
    QSet<QString> m_words;
    QStringList m_ordered;
    QStringList m_asked;
};

/// True when `word` matches one of `list` by the personal list's letter-case rules: exactly,
/// capitalised ("Kot" for "kot") or in capitals ("KOT" for "kot" and for "Kot").
bool matchesWordList(const QSet<QString> &list, const QString &word);

/// Damerau–Levenshtein distance (optimal string alignment) of `a` and `b`, at most `limit` + 1.
int editDistance(const QString &a, const QString &b, int limit);

class SpellChecker {
public:
    SpellChecker();
    ~SpellChecker();
    SpellChecker(const SpellChecker &) = delete;
    SpellChecker &operator=(const SpellChecker &) = delete;

    void setBackends(std::vector<std::unique_ptr<SpellBackend>> backends);
    /// The languages of the backends, in order.
    QStringList languages() const;
    bool hasBackends() const { return !m_backends.empty(); }

    /// The personal word list (replaces the previous one).
    void setPersonalWords(const QStringList &words);
    /// Words ignored until the app quits ("Ignore").
    void setIgnoredWords(const QStringList &words);

    /// Whether `word` is spelled right (the rules above). `word` as checkedText() gives it.
    bool isCorrect(const QString &word);
    /// isCorrect() of every word; words the backends decide are asked in one checkAll() each.
    QList<bool> checkAll(const QStringList &words);
    /// Corrections of `word`: the backends' suggestions taken in turn (the first backend's best,
    /// the second's best, …), without duplicates and without the word itself, at most `max`.
    QStringList suggestions(const QString &word, int max = 6);

private:
    bool listed(const QString &word) const;
    bool knownByBackends(const QString &word);
    bool partsCorrect(const QString &word);

    std::vector<std::unique_ptr<SpellBackend>> m_backends;
    QSet<QString> m_personal;
    QSet<QString> m_ignored;
};

} // namespace brulion::spell
