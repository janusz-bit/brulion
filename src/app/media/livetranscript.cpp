#include "livetranscript.h"

#include "transcriber.h"
#include "voicerecorder.h"

#include <QLoggingCategory>

using namespace Qt::StringLiterals;

namespace brulion::media {

Q_LOGGING_CATEGORY(lcLive, "brulion.speech.live")

LiveTranscript::LiveTranscript(QObject *parent) : QObject(parent) { }

LiveTranscript::~LiveTranscript()
{
    if (m_recorder)
        m_recorder->setSampleListener({});
    if (m_inFlight && m_transcriber)
        m_transcriber->cancelLive();
}

void LiveTranscript::setRecorder(VoiceRecorder *recorder)
{
    if (recorder == m_recorder)
        return;
    if (m_recorder) {
        disconnect(m_recorder, nullptr, this, nullptr);
        m_recorder->setSampleListener({});
    }
    end();
    m_recorder = recorder;
    if (recorder) {
        connect(recorder, &VoiceRecorder::stateChanged, this, &LiveTranscript::onRecorderState);
        connect(recorder, &QObject::destroyed, this, [this] { end(); });
        listen();
        onRecorderState();
    }
    Q_EMIT recorderChanged();
}

void LiveTranscript::setTranscriber(Transcriber *transcriber)
{
    if (transcriber == m_transcriber)
        return;
    if (m_transcriber)
        disconnect(m_transcriber, nullptr, this, nullptr);
    m_transcriber = transcriber;
    if (transcriber) {
        connect(transcriber, &Transcriber::liveReadyChanged, this, [this] {
            if (m_recording && m_status != u"slow")
                setStatus(canHear() ? (m_windows > 0 ? u"hearing"_s : u"waiting"_s) : u"off"_s);
        });
    }
    Q_EMIT transcriberChanged();
}

void LiveTranscript::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    Q_EMIT enabledChanged();
    if (m_recording && m_status != u"slow")
        setStatus(canHear() ? (m_windows > 0 ? u"hearing"_s : u"waiting"_s) : u"off"_s);
}

QString LiveTranscript::text() const
{
    return m_live ? m_live->text() : QString();
}

QString LiveTranscript::committed() const
{
    return m_live ? m_live->committed() : QString();
}

QString LiveTranscript::tentative() const
{
    return m_live ? m_live->tentative() : QString();
}

QString LiveTranscript::tail() const
{
    return speech::textTail(text(), m_tailChars);
}

void LiveTranscript::setTailChars(int chars)
{
    if (chars == m_tailChars)
        return;
    m_tailChars = chars;
    Q_EMIT textChanged();
}

void LiveTranscript::setStatus(const QString &status)
{
    if (status == m_status)
        return;
    m_status = status;
    Q_EMIT statusChanged();
}

bool LiveTranscript::canHear() const
{
    return m_enabled && m_transcriber && m_transcriber->liveReady() && m_recorder && m_recorder->metered();
}

void LiveTranscript::listen()
{
    // (The recorder calls it on the GUI thread, as the microphone's samples come.)
    m_recorder->setSampleListener(
            [this](const float *samples, qsizetype count, int rate) { onSamples(samples, count, rate); });
}

void LiveTranscript::onRecorderState()
{
    const QString state = m_recorder ? m_recorder->state() : QString();
    const bool recording = state == u"recording" || state == u"paused";
    if (recording && !m_recording)
        start();
    else if (!recording && m_recording)
        end();
}

void LiveTranscript::start()
{
    ++m_generation;
    m_recording = true;
    // (Made again once the first samples say their rate.)
    m_live = std::make_unique<speech::LiveWindows>(16000, m_rules);
    m_windows = 0;
    m_late = 0;
    m_inFlight = false;
    m_language.clear();
    if (m_transcriber && m_transcriber->language() != u"auto")
        m_language = m_transcriber->language();
    else
        m_language = speech::knownLanguage(m_preferred) == u"auto" ? QString() : speech::knownLanguage(m_preferred);
    m_rate = 0;
    setStatus(canHear() ? u"waiting"_s : u"off"_s);
    Q_EMIT textChanged();
}

void LiveTranscript::end()
{
    if (!m_recording)
        return;
    m_recording = false;
    // The recording ended: a window still being heard is not needed (the whole recording is
    // heard next); what was heard stays shown until the next recording starts.
    ++m_generation;
    if (m_inFlight && m_transcriber)
        m_transcriber->cancelLive();
    m_inFlight = false;
    if (m_status != u"slow")
        setStatus(u"off"_s);
}

void LiveTranscript::onSamples(const float *samples, qsizetype count, int rate)
{
    if (!m_recording || !m_live || rate <= 0)
        return;
    if (rate != m_rate) {
        // (The first samples, or a microphone that changed its rate: from here on.)
        m_rate = rate;
        if (m_live->recordedMs() == 0)
            m_live = std::make_unique<speech::LiveWindows>(rate, m_rules);
    }
    m_live->append(samples, count);
    hearNext();
}

void LiveTranscript::hearNext()
{
    if (!m_recording || !m_live || m_inFlight || m_status == u"slow" || !canHear() || !m_live->due())
        return;
    // A transcription runs or waits (it goes first), or another recorder's window is heard.
    if (m_transcriber->busy() || m_transcriber->hearingLive())
        return;
    const speech::LiveWindows::Window window = m_live->take();
    if (window.isNull())
        return;
    const QString language = m_language.isEmpty() ? u"auto"_s : m_language;
    const quint64 generation = m_generation;
    const int rate = m_live->sampleRate();
    const bool asked = m_transcriber->hearLive(
            window.samples, rate, language, this, [this, generation, window](const Transcriber::LiveHeard &heard) {
                onHeard(generation, window, heard.ok, heard.modelId, heard.segments, heard.language);
            });
    if (!asked)
        return;
    m_inFlight = true;
    if (m_windows == 0)
        setStatus(u"hearing"_s);
}

void LiveTranscript::onHeard(quint64 generation, const speech::LiveWindows::Window &window, bool ok,
                             const QString &modelId, const QList<speech::Segment> &segments, const QString &language)
{
    if (generation != m_generation || !m_live)
        return;
    m_inFlight = false;
    if (ok) {
        m_live->heard(window, segments);
        ++m_windows;
        // An "auto" language is the first window's that had words: the next ones are heard in it.
        if (m_language.isEmpty() && !language.isEmpty() && !speech::spokenSegments(segments).isEmpty())
            m_language = language;
        setStatus(u"hearing"_s);
        // (The language too.)
        Q_EMIT statusChanged();
        Q_EMIT textChanged();
        // How far behind the recording it is now.
        const qint64 behind = m_live->recordedMs() - m_live->msOf(window.to);
        if (behind > m_lateMs && ++m_late >= 2) {
            qCInfo(lcLive).noquote() << u"text while recording is %1 s behind: given up for this recording"_s.arg(
                    behind / 1000);
            setStatus(u"slow"_s);
            if (m_transcriber)
                m_transcriber->markLiveSlow(modelId);
            return;
        }
    }
    hearNext();
}

} // namespace brulion::media
