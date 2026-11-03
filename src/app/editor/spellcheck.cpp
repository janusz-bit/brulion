#include "spellcheck.h"

#include "spellbackends.h"

#include "spell/languages.h"
#include "spell/spellwords.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLocale>
#include <QPointer>
#include <QThread>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::editor {

/// The checker on its own thread: every call reaches it as a queued functor.
class SpellWorker : public QObject {
public:
    explicit SpellWorker(SpellCheck::BackendFactory factory) : m_factory(std::move(factory)) { }

    void configure(const QStringList &languages, const QStringList &personal, const QStringList &ignored)
    {
        if (languages != m_languages || !m_configured) {
            std::vector<std::unique_ptr<spell::SpellBackend>> backends;
            for (const QString &language : languages) {
                if (auto backend = m_factory ? m_factory(language) : nullptr)
                    backends.push_back(std::move(backend));
            }
            checker.setBackends(std::move(backends));
            m_languages = languages;
            m_configured = true;
        }
        checker.setPersonalWords(personal);
        checker.setIgnoredWords(ignored);
    }

    spell::SpellChecker checker;

private:
    SpellCheck::BackendFactory m_factory;
    QStringList m_languages;
    bool m_configured = false;
};

namespace {

QStringList sortedUnique(QStringList words)
{
    words.removeAll(QString());
    std::sort(words.begin(), words.end(), [](const QString &a, const QString &b) {
        const int c = QString::compare(a, b, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a < b;
    });
    words.erase(std::unique(words.begin(), words.end()), words.end());
    return words;
}

} // namespace

SpellCheck::SpellCheck(QStringList availableLanguages, BackendFactory factory, QString engine, QObject *parent)
    : QObject(parent)
    , m_factory(std::move(factory))
    , m_engine(std::move(engine))
    , m_systemLanguage(QLocale::system().name())
{
    for (const QString &l : availableLanguages) {
        const QString p = spell::primaryLanguage(l);
        if (!p.isEmpty() && !m_available.contains(p))
            m_available.append(p);
    }
    // The words of one highlighting pass (a block, a page) go out together.
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(0);
    connect(&m_flushTimer, &QTimer::timeout, this, &SpellCheck::flush);
    updateLanguages();
}

SpellCheck::~SpellCheck()
{
    stopWorker();
}

void SpellCheck::stopWorker()
{
    if (!m_thread)
        return;
    // The worker goes with its thread (finished → deleteLater); answers still on their way are
    // dropped by their generation.
    m_thread->quit();
    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;
    m_worker = nullptr;
}

void SpellCheck::useWordLists(const QVariantMap &lists)
{
    stopWorker();
    QHash<QString, QStringList> words;
    QStringList available;
    for (auto it = lists.cbegin(); it != lists.cend(); ++it) {
        const QString language = spell::primaryLanguage(it.key());
        if (language.isEmpty())
            continue;
        words.insert(language, it.value().toStringList());
        available.append(language);
    }
    m_factory = [words](const QString &language) -> std::unique_ptr<spell::SpellBackend> {
        if (!words.contains(language))
            return nullptr;
        return std::make_unique<spell::WordListBackend>(language, words.value(language));
    };
    m_available = available;
    m_engine = u"wordlist"_s;
    m_storeWords = false;
    m_personal.clear();
    m_ignored.clear();
    m_verdicts.clear();
    m_inFlight.clear();
    m_toSend.clear();
    m_suggestions.clear();
    ++m_generation;
    Q_EMIT availabilityChanged();
    Q_EMIT personalWordsChanged();
    Q_EMIT ignoredWordsChanged();
    // Everything is set up again for the new backends.
    const bool wasActive = active();
    m_languages.clear();
    if (wasActive)
        Q_EMIT activeChanged();
    updateLanguages();
    if (!active())
        Q_EMIT invalidated();
}

SpellCheck *SpellCheck::instance()
{
    static QPointer<SpellCheck> spell;
    if (!spell)
        spell = new SpellCheck(platform::spellLanguages(), &platform::createSpellBackend, platform::spellEngine(),
                               QCoreApplication::instance());
    return spell;
}

void SpellCheck::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    const bool wasActive = active();
    m_enabled = on;
    Q_EMIT enabledChanged();
    if (wasActive != active()) {
        Q_EMIT activeChanged();
        if (active())
            configure(true);
        else
            Q_EMIT invalidated();
    }
}

void SpellCheck::setLanguageSetting(const QString &setting)
{
    if (m_languageSetting == setting)
        return;
    m_languageSetting = setting;
    updateLanguages();
}

void SpellCheck::setUiLanguage(const QString &language)
{
    if (m_uiLanguage == language)
        return;
    m_uiLanguage = language;
    updateLanguages();
}

void SpellCheck::setSystemLanguage(const QString &language)
{
    if (m_systemLanguage == language)
        return;
    m_systemLanguage = language;
    updateLanguages();
}

void SpellCheck::updateLanguages()
{
    const QStringList languages =
            spell::resolveLanguages(m_languageSetting, m_uiLanguage, m_systemLanguage, m_available);
    if (languages == m_languages) {
        Q_EMIT languagesChanged(); // the setting changed, the result did not
        return;
    }
    const bool wasActive = active();
    m_languages = languages;
    Q_EMIT languagesChanged();
    if (wasActive != active())
        Q_EMIT activeChanged();
    if (active())
        configure(true);
    else if (wasActive)
        Q_EMIT invalidated();
}

void SpellCheck::setPersonalWords(const QStringList &input)
{
    QStringList words;
    for (const QString &w : input)
        words.append(dictionaryForm(w));
    words = sortedUnique(words);
    if (words == m_personal)
        return;
    // A word taken out may make words wrong that were right: everything is asked again.
    const bool removed = std::any_of(m_personal.cbegin(), m_personal.cend(),
                                     [&words](const QString &w) { return !words.contains(w); });
    m_personal = words;
    Q_EMIT personalWordsChanged();
    if (active())
        configure(removed);
}

QString SpellCheck::dictionaryForm(const QString &word) const
{
    QString w = spell::normalizedWord(word.trimmed());
    while (w.endsWith(u'.'))
        w.chop(1);
    return w;
}

void SpellCheck::addToDictionary(const QString &word)
{
    const QString w = dictionaryForm(word);
    if (w.isEmpty() || m_personal.contains(w))
        return;
    setPersonalWords(m_personal + QStringList{w});
    if (m_storeWords)
        Q_EMIT personalWordsEdited();
}

void SpellCheck::removeFromDictionary(const QString &word)
{
    const QString w = dictionaryForm(word);
    if (!m_personal.contains(w))
        return;
    QStringList words = m_personal;
    words.removeAll(w);
    setPersonalWords(words);
    if (m_storeWords)
        Q_EMIT personalWordsEdited();
}

void SpellCheck::ignore(const QString &word)
{
    const QString w = dictionaryForm(word);
    if (w.isEmpty() || m_ignored.contains(w))
        return;
    m_ignored.append(w);
    Q_EMIT ignoredWordsChanged();
    if (active())
        configure(false);
}

SpellCheck::Verdict SpellCheck::verdict(const QString &word)
{
    if (!active() || word.isEmpty())
        return Verdict::Correct;
    if (const auto it = m_verdicts.constFind(word); it != m_verdicts.constEnd())
        return it.value() ? Verdict::Correct : Verdict::Wrong;
    if (!m_inFlight.contains(word)) {
        m_inFlight.insert(word);
        m_toSend.append(word);
        if (!m_flushTimer.isActive())
            m_flushTimer.start();
    }
    return Verdict::Pending;
}

void SpellCheck::ensureWorker()
{
    if (m_thread)
        return;
    m_thread = new QThread;
    m_thread->setObjectName(u"SpellCheck"_s);
    m_worker = new SpellWorker(m_factory);
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start(QThread::LowPriority);
}

void SpellCheck::configure(bool forget)
{
    ensureWorker();
    ++m_generation;
    const QStringList languages = m_languages;
    const QStringList personal = m_personal;
    const QStringList ignored = m_ignored;
    SpellWorker *worker = m_worker;
    QMetaObject::invokeMethod(
            worker, [worker, languages, personal, ignored] { worker->configure(languages, personal, ignored); },
            Qt::QueuedConnection);
    // What was on its way is asked again under the new settings (answers of the old ones are
    // dropped by their generation); without `forget`, so are the words marked wrong — they stay
    // marked until then.
    QStringList again(m_inFlight.cbegin(), m_inFlight.cend());
    if (forget) {
        m_verdicts.clear();
    } else {
        for (auto it = m_verdicts.cbegin(); it != m_verdicts.cend(); ++it) {
            if (!it.value() && !m_inFlight.contains(it.key()))
                again.append(it.key());
        }
    }
    m_suggestions.clear();
    m_toSend = again;
    m_inFlight = QSet<QString>(again.cbegin(), again.cend());
    if (!m_toSend.isEmpty())
        m_flushTimer.start();
    if (forget)
        Q_EMIT invalidated();
}

void SpellCheck::flush()
{
    if (m_toSend.isEmpty() || !active())
        return;
    if (!m_thread)
        configure(false);
    const int generation = m_generation;
    SpellWorker *worker = m_worker;
    for (qsizetype i = 0; i < m_toSend.size(); i += BatchSize) {
        const QStringList batch = m_toSend.mid(i, BatchSize);
        QMetaObject::invokeMethod(
                worker,
                [this, worker, generation, batch] {
                    const QList<bool> right = worker->checker.checkAll(batch);
                    QMetaObject::invokeMethod(
                            this, [this, generation, batch, right] { onChecked(generation, batch, right); },
                            Qt::QueuedConnection);
                },
                Qt::QueuedConnection);
    }
    m_toSend.clear();
}

void SpellCheck::onChecked(int generation, const QStringList &words, const QList<bool> &right)
{
    if (generation != m_generation)
        return; // asked again under the current settings (configure())
    if (m_verdicts.size() + words.size() > MaxCachedWords)
        m_verdicts.clear();
    bool changed = false;
    for (qsizetype i = 0; i < words.size(); ++i) {
        m_inFlight.remove(words[i]);
        const bool r = right.value(i, true);
        const auto it = m_verdicts.find(words[i]);
        if (it == m_verdicts.end()) {
            m_verdicts.insert(words[i], r);
        } else if (it.value() != r) {
            it.value() = r;
            changed = true;
        }
    }
    Q_EMIT checked(changed);
}

void SpellCheck::requestSuggestions(const QString &word)
{
    if (!active() || word.isEmpty()) {
        Q_EMIT suggestionsReady(word, {});
        return;
    }
    if (!m_thread)
        configure(false);
    const int generation = m_generation;
    SpellWorker *worker = m_worker;
    QMetaObject::invokeMethod(
            worker,
            [this, worker, generation, word] {
                const QStringList list = worker->checker.suggestions(word, MaxSuggestions);
                QMetaObject::invokeMethod(
                        this, [this, generation, word, list] { onSuggested(generation, word, list); },
                        Qt::QueuedConnection);
            },
            Qt::QueuedConnection);
}

void SpellCheck::onSuggested(int generation, const QString &word, const QStringList &suggestions)
{
    if (generation == m_generation) {
        if (m_suggestions.size() > 100)
            m_suggestions.clear();
        m_suggestions.insert(word, suggestions);
    }
    Q_EMIT suggestionsReady(word, suggestions);
}

bool SpellCheck::waitUntilIdle(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (pendingCount() > 0 || m_flushTimer.isActive()) {
        if (timer.elapsed() > ms)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return true;
}

} // namespace brulion::editor
