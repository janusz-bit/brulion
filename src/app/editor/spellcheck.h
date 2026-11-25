// SpellCheck — spell checking for the editor (docs/UI.md "Spell checking"; QML singleton
// `SpellCheck` of the module Brulion, registered in editorforeign.h). Never blocks the UI thread:
// the checker (spell::SpellChecker over the platform's backends, src/platform/spellbackends.h)
// runs on a worker thread of its own, made when the first word is asked about; the UI thread
// only reads verdicts it already has. The worker goes again, with its dictionaries (Hunspell's
// Polish and English ones hold about 20 MB), once nothing was asked for `idleMs` or the app is
// away (unload(), docs/PERFORMANCE.md "Memory: only what is in use"): the verdicts stay, and the
// next unknown word brings the worker back (the dictionaries are read again: its verdict comes a
// moment later).
//
//   SpellCheck::Verdict v = spell->verdict(u"zażółć"_s);   // Pending the first time: queued
//   // … checked(changed) — the verdicts of queued words are in: re-highlight what was pending
//   spell->requestSuggestions(word);                      // … suggestionsReady(word, list)
//   spell->addToDictionary(word); spell->ignore(word);    // the words already marked are asked again
//
// Settings come from outside (main.cpp binds them to AppSettings: `spelling/enabled`,
// `spelling/languages`, `spelling/words`): `enabled`, `languageSetting` ("auto", "pl", "en",
// "pl,en"; spell/languages.h), `uiLanguage` and `personalWords`; addToDictionary() /
// removeFromDictionary() change `personalWords` and emit personalWordsEdited() for the app to
// store them. Ignored words last until the app quits.
//
// `available`: this build and device can check at least one language (Hunspell found a
// dictionary, Android's checker is on); the settings hide spell checking otherwise. `active`:
// available, enabled and with languages — what the editor's highlighters follow.
//
// Verdicts are cached by word (at most MaxCachedWords, then forgotten): typing a word again asks
// nothing. A change of languages or a word taken out of the dictionary forgets them all
// (invalidated(): re-highlight everything); adding or ignoring a word only asks again about the
// words marked wrong (they stay marked until the answer comes: no flicker).
//
// The rendered blocks of a page (`wholePage`, docs/UI.md "Spell checking"): wrongWords(text) gives
// the misspelled words of a block's text from the verdicts known now, cached by the text (at most
// MaxCachedTexts) until a verdict changes or is forgotten; `revision` changes whenever verdicts
// arrive or go, so QML bindings over it follow.
#pragma once

#include "spell/spellchecker.h"
#include "spell/spellwords.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <memory>

class QThread;

namespace brulion::editor {

class SpellWorker;

class SpellCheck : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged FINAL)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages NOTIFY availabilityChanged FINAL)
    /// "hunspell", "android", "wordlist" (useWordLists()) or "" (no spell checking in this build).
    Q_PROPERTY(QString engine READ engine NOTIFY availabilityChanged FINAL)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(QString languageSetting READ languageSetting WRITE setLanguageSetting NOTIFY languagesChanged FINAL)
    /// The UI language ("pl", "en"): "auto" checks it first.
    Q_PROPERTY(QString uiLanguage READ uiLanguage WRITE setUiLanguage NOTIFY languagesChanged FINAL)
    /// The system's locale name ("pl_PL"); QLocale::system() unless set (tests).
    Q_PROPERTY(QString systemLanguage READ systemLanguage WRITE setSystemLanguage NOTIFY languagesChanged FINAL)
    /// The languages checked now, in order.
    Q_PROPERTY(QStringList languages READ languages NOTIFY languagesChanged FINAL)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged FINAL)
    Q_PROPERTY(QStringList personalWords READ personalWords WRITE setPersonalWords NOTIFY personalWordsChanged FINAL)
    Q_PROPERTY(QStringList ignoredWords READ ignoredWords NOTIFY ignoredWordsChanged FINAL)
    /// Misspelled words are underlined in the rendered blocks too, not only in the edited one.
    Q_PROPERTY(bool wholePage READ wholePage WRITE setWholePage NOTIFY wholePageChanged FINAL)
    /// Changes whenever verdicts arrive or are forgotten (wrongWords() may give another answer).
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged FINAL)

public:
    enum class Verdict { Pending, Correct, Wrong };
    using BackendFactory = std::function<std::unique_ptr<spell::SpellBackend>(const QString &language)>;

    /// Words whose verdicts are kept; more and the cache starts again.
    static constexpr int MaxCachedWords = 20000;
    /// Words sent to the worker at once.
    static constexpr int BatchSize = 200;
    /// Suggestions offered for a word.
    static constexpr int MaxSuggestions = 6;
    /// Texts whose misspelled words wrongWords() keeps; more and the cache starts again.
    static constexpr int MaxCachedTexts = 2000;
    /// The worker and its dictionaries go when nothing was asked for this long.
    static constexpr int DefaultIdleMs = 90 * 1000;

    /// `available`: the languages backends can be made for; `factory` makes them (on the worker
    /// thread).
    SpellCheck(const QStringList &availableLanguages, BackendFactory factory, QString engine = {},
               QObject *parent = nullptr);
    ~SpellCheck() override;

    /// The app's: the platform's backends (made on first use; a child of the application).
    static SpellCheck *instance();

    bool available() const { return !m_available.isEmpty(); }
    QStringList availableLanguages() const { return m_available; }
    QString engine() const { return m_engine; }
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    QString languageSetting() const { return m_languageSetting; }
    void setLanguageSetting(const QString &setting);
    QString uiLanguage() const { return m_uiLanguage; }
    void setUiLanguage(const QString &language);
    QString systemLanguage() const { return m_systemLanguage; }
    void setSystemLanguage(const QString &language);
    QStringList languages() const { return m_languages; }
    bool active() const { return m_enabled && !m_languages.isEmpty(); }
    QStringList personalWords() const { return m_personal; }
    void setPersonalWords(const QStringList &words);
    QStringList ignoredWords() const { return m_ignored; }
    bool wholePage() const { return m_wholePage; }
    void setWholePage(bool on);
    int revision() const { return m_revision; }

    /// The verdict about `word` (as spell::checkedText() gives it). Pending: not known yet — it
    /// is queued (unless already) and checked() follows. Always Correct while not active.
    Verdict verdict(const QString &word);
    /// The verdict about `word` if known, else Pending — without queuing it.
    Verdict knownVerdict(const QString &word) const;

    /// The misspelled words of `text` (a block's text, its lines checked as spell::findWords()
    /// with `lineStart`; positions in `text`), from the verdicts known now: the words not known
    /// yet are queued and `*pending` set (checked() follows). Cached by the text until a verdict
    /// changes or is forgotten. Empty while not active.
    QList<spell::WordSpan> wrongWords(const QString &text, bool *pending = nullptr);

    /// Ask for corrections of `word`: suggestionsReady() follows (also with an empty list).
    Q_INVOKABLE void requestSuggestions(const QString &word);
    /// The last suggestions given for `word`, empty when none came yet.
    Q_INVOKABLE QStringList suggestionsFor(const QString &word) const { return m_suggestions.value(word); }
    /// The word as the dictionary keeps it (spell::normalizedWord, without a trailing '.').
    Q_INVOKABLE QString dictionaryForm(const QString &word) const;
    /// Add `word` to the personal dictionary (sorted, no duplicates); personalWordsEdited().
    Q_INVOKABLE void addToDictionary(const QString &word);
    Q_INVOKABLE void removeFromDictionary(const QString &word);
    /// Ignore `word` everywhere until the app quits.
    Q_INVOKABLE void ignore(const QString &word);

    /// For the editor playground and its self-tests (qml/dev/SpellingSelfTest.qml): word lists
    /// ({"pl": [...], "en": [...]}, spell::WordListBackend) instead of the platform's backends —
    /// the same results on every machine. The personal dictionary and the ignored words start
    /// empty, and words added from now on are not stored (no personalWordsEdited()).
    Q_INVOKABLE void useWordLists(const QVariantMap &lists);

    /// The worker and its dictionaries go when nothing was asked for this long (0: never).
    int idleMs() const { return m_idleMs; }
    void setIdleMs(int ms);
    /// The worker exists: its dictionaries are in memory.
    bool loaded() const { return m_thread != nullptr; }
    /// Let the worker and its dictionaries go now, unless words are on their way (the verdicts
    /// known stay; the next unknown word brings it back). True: it went.
    bool unload();

    /// Nothing queued or in flight (tests).
    Q_INVOKABLE bool idle() const { return m_inFlight.isEmpty() && !m_flushTimer.isActive(); }
    /// Process events until nothing is queued or in flight (tests), at most `ms`.
    bool waitUntilIdle(int ms = 5000);
    /// Words queued or in flight.
    int pendingCount() const { return int(m_inFlight.size()); }

Q_SIGNALS:
    void availabilityChanged();
    void enabledChanged();
    void languagesChanged();
    void activeChanged();
    void personalWordsChanged();
    void ignoredWordsChanged();
    /// The user added or removed a word: store personalWords.
    void personalWordsEdited();
    /// Verdicts of queued words arrived; `changed`: some word known before changed its verdict
    /// (asked again after addToDictionary() / ignore()) — lines marked before may need it.
    void checked(bool changed);
    /// Every verdict is gone (languages, the dictionary, enabled): re-highlight everything.
    void invalidated();
    void suggestionsReady(const QString &word, const QStringList &suggestions);
    void wholePageChanged();
    void revisionChanged();
    /// The worker and its dictionaries went (unload(): their memory is free).
    void unloaded();

private:
    /// What wrongWords() found in a text, and under which verdicts.
    struct TextWords {
        QList<spell::WordSpan> wrong;
        bool pending = false;
        int epoch = 0;
    };
    /// Verdicts may differ now: wrongWords()' cache is stale, bindings follow `revision`.
    void verdictsChanged(bool differ);

    void updateLanguages();
    void ensureWorker();
    void sendSettings();
    void stopWorker();
    void used();
    void configure(bool forget);
    void flush();
    void onChecked(int generation, const QStringList &words, const QList<bool> &right);
    void onSuggested(int generation, const QString &word, const QStringList &suggestions);

    QStringList m_available;
    BackendFactory m_factory;
    QString m_engine;
    bool m_enabled = true;
    QString m_languageSetting = QStringLiteral("auto");
    QString m_uiLanguage;
    QString m_systemLanguage;
    QStringList m_languages;
    QStringList m_personal;
    QStringList m_ignored;

    QHash<QString, bool> m_verdicts;
    QSet<QString> m_inFlight;
    QStringList m_toSend;
    QHash<QString, QStringList> m_suggestions;
    int m_generation = 0;
    bool m_storeWords = true;
    bool m_wholePage = true;
    int m_revision = 0;
    /// Bumped whenever a known verdict may change (wrongWords()' cache is valid only for its own).
    int m_epoch = 0;
    QHash<QString, TextWords> m_textWords;
    QTimer m_flushTimer;
    QTimer m_idleTimer; ///< unload() when it fires
    int m_idleMs = DefaultIdleMs;
    std::unique_ptr<QThread> m_thread;
    SpellWorker *m_worker = nullptr; ///< deleted on m_thread when it finishes
};

} // namespace brulion::editor
