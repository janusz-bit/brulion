#include "dictation.h"

#include "livetranscript.h"
#include "transcriber.h"
#include "voicerecorder.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::media {

Dictation::Dictation(Transcriber *transcriber, QObject *parent)
    : SpeechRecognizer(parent), m_transcriber(transcriber), m_recorder(new VoiceRecorder(this))
{
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    setFolder((cache.isEmpty() ? QDir::tempPath() : cache) + u"/dictation"_s);
    // WAV: no encoder needed, and the recognizer reads it as it is.
    m_recorder->setWavOnly(true);
    m_recorder->setNamePrefix(u"Dictation"_s);
    connect(m_recorder, &VoiceRecorder::stateChanged, this, &Dictation::onRecorderState);
    connect(m_recorder, &VoiceRecorder::durationChanged, this, [this] {
        Q_EMIT elapsedChanged();
        if (m_state == State::Recording && qint64(m_recorder->duration()) >= m_maxMs)
            stop();
    });
    // The text so far while it records, in the field at the caret (replaced by the full pass's).
    m_live = new LiveTranscript(this);
    m_live->setRecorder(m_recorder);
    m_live->setTranscriber(transcriber);
    connect(m_live, &LiveTranscript::textChanged, this, [this] {
        if (m_state == State::Recording && !m_live->text().isEmpty())
            Q_EMIT partial(m_live->text());
    });
    if (transcriber) {
        connect(transcriber, &Transcriber::heard, this, &Dictation::onHeard);
        connect(transcriber, &Transcriber::modelsChanged, this, &SpeechRecognizer::readyChanged);
    }
}

Dictation::~Dictation()
{
    // A recording being heard: not any more, and its file goes (the recorder removes its own
    // unfinished one).
    if (m_state == State::Recognizing && !m_file.isEmpty()) {
        if (m_transcriber)
            m_transcriber->cancel(m_file);
        QFile::remove(m_file);
    }
}

void Dictation::setFolder(const QString &folder)
{
    m_folder = folder;
    m_recorder->setFolder(folder);
}

bool Dictation::ready() const
{
    return m_transcriber && m_transcriber->ready() && m_recorder->available();
}

bool Dictation::listening() const
{
    return m_state == State::Recording;
}

qint64 Dictation::elapsedMs() const
{
    return m_state == State::Recording || m_state == State::Saving ? qint64(m_recorder->duration()) : 0;
}

void Dictation::setState(State state)
{
    if (state == m_state)
        return;
    m_state = state;
    Q_EMIT listeningChanged();
    Q_EMIT elapsedChanged();
}

void Dictation::removeLeftovers() const
{
    const QDir dir(m_folder);
    for (const QString &name : dir.entryList(QDir::Files | QDir::Hidden))
        QFile::remove(dir.filePath(name));
}

bool Dictation::start(const QString &language)
{
    if (m_state != State::Idle)
        return false;
    if (!m_recorder->available()) {
        Q_EMIT failed(tr("This version of Brulion cannot record."));
        return false;
    }
    if (!ready()) {
        Q_EMIT failed(tr("No speech model is on this device: download one in Settings → Extensions → Speech."));
        return false;
    }
    removeLeftovers();
    // "pl-PL" → "pl": short dictations are heard better in a language named than recognized.
    m_language = language.section(u'-', 0, 0).trimmed().toLower();
    m_live->setPreferredLanguage(m_language);
    setState(State::Recording);
    m_recorder->start();
    // (A recorder that failed at once has said why: idle again.)
    return m_state != State::Idle;
}

void Dictation::stop()
{
    if (m_state != State::Recording)
        return;
    // Still asking for the microphone: nothing was recorded.
    if (m_recorder->state() == u"asking") {
        cancel();
        return;
    }
    m_recorder->stop();
}

void Dictation::cancel()
{
    switch (m_state) {
    case State::Idle: return;
    case State::Recording:
    case State::Saving:
        setState(State::Idle);
        m_recorder->cancel(); // (its file removed)
        break;
    case State::Recognizing: {
        const QString file = std::exchange(m_file, {});
        setState(State::Idle);
        // Its file goes when the recognizer has let it go (onHeard()).
        if (m_transcriber && m_transcriber->isQueued(file))
            m_transcriber->cancel(file);
        else
            QFile::remove(file);
        break;
    }
    }
    Q_EMIT failed(QString());
}

void Dictation::end()
{
    setState(State::Idle);
}

void Dictation::onRecorderState()
{
    const QString state = m_recorder->state();
    if (m_state == State::Recording && (state == u"denied" || state == u"failed")) {
        const QString why = m_recorder->error();
        end();
        Q_EMIT failed(why);
        return;
    }
    if (m_state == State::Recording && state == u"saving")
        setState(State::Saving);
    if ((m_state == State::Recording || m_state == State::Saving) && state == u"failed") {
        // (Nothing recorded, or the file could not be written.)
        const QString why = m_recorder->error();
        end();
        Q_EMIT failed(why);
        return;
    }
    if ((m_state == State::Recording || m_state == State::Saving) && state == u"done") {
        m_file = m_recorder->filePath();
        setState(State::Recognizing);
        // The language it started with, unless the speech settings name one.
        const QString language = m_transcriber && m_transcriber->language() == u"auto" ? m_language : QString();
        if (!m_transcriber || !m_transcriber->transcribeFile(m_file, language)) {
            QFile::remove(std::exchange(m_file, {}));
            end();
            Q_EMIT failed(tr("The recording could not be heard: is a speech model on this device?"));
        }
    }
}

void Dictation::onHeard(const QString &file, bool ok, const QString &text, const QString &error)
{
    if (!file.startsWith(m_folder + u'/'))
        return; // (not a dictation's)
    QFile::remove(file);
    if (file != m_file || m_state != State::Recognizing)
        return; // (cancelled meanwhile)
    m_file.clear();
    end();
    if (ok)
        Q_EMIT recognized(text.simplified());
    else
        Q_EMIT failed(error);
}

} // namespace brulion::media
