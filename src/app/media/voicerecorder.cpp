#include "voicerecorder.h"

#include "voicerecorder_p.h"

#include "media/media.h"
#include "media/pcm.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTimer>

#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace brulion::media {

namespace {

std::function<QDateTime()> &clock()
{
    static std::function<QDateTime()> c = [] { return QDateTime::currentDateTime(); };
    return c;
}

/// Removes a cancelled recording's file. On Windows the encoder (Media Foundation, FFmpeg) may
/// still hold the file a moment after it was stopped, and an open file cannot be removed there:
/// tried again every 100 ms for up to 5 s.
void removeWhenFree(const QString &path, int attemptsLeft = 50)
{
    if (QFile::remove(path) || !QFile::exists(path) || attemptsLeft <= 0)
        return;
    QTimer::singleShot(100, QCoreApplication::instance(),
                       [path, attemptsLeft] { removeWhenFree(path, attemptsLeft - 1); });
}

} // namespace

bool VoiceRecorder::Backend::full() const
{
    return recorder->m_durationMs >= MaxRecordingMs;
}

VoiceRecorder::VoiceRecorder(QObject *parent) : QObject(parent), m_backend(makeRecorderBackend(this))
{
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    m_folder = (cache.isEmpty() ? QDir::tempPath() : cache) + u"/recordings"_s;
    if (qEnvironmentVariableIsSet(TestMicrophoneVariable) && available()) {
        useFeed(48000, 1);
        m_testVoice = new QTimer(this);
        m_testVoice->setInterval(100);
        connect(m_testVoice, &QTimer::timeout, this, &VoiceRecorder::feedTestVoice);
    }
}

void VoiceRecorder::feedTestVoice()
{
    if (m_state != u"recording")
        return;
    // A tone that swells and fades about once a second, like words.
    constexpr int Frames = 4800;
    QList<float> piece(Frames);
    for (int i = 0; i < Frames; ++i) {
        const double t = double(m_testFrames + i) / 48000.0;
        const double envelope = 0.15 + 0.6 * std::pow(std::sin(std::numbers::pi * t * 1.3), 2);
        piece[i] = float(envelope * std::sin(2 * std::numbers::pi * 220.0 * t));
    }
    m_testFrames += Frames;
    feed(toPcm16(piece.constData(), piece.size()));
}

VoiceRecorder::~VoiceRecorder()
{
    // A recording left unfinished (the dialog closed, the app quit): its file goes.
    if (m_state == u"recording" || m_state == u"paused" || m_state == u"saving") {
        m_backend->abort();
        if (!m_filePath.isEmpty())
            QFile::remove(m_filePath);
    }
}

bool VoiceRecorder::available() const
{
    return m_backend->available();
}

void VoiceRecorder::setNamePrefix(const QString &prefix)
{
    if (prefix == m_namePrefix)
        return;
    m_namePrefix = prefix;
    Q_EMIT namePrefixChanged();
}

void VoiceRecorder::setClock(std::function<QDateTime()> c)
{
    clock() = c ? std::move(c) : std::function<QDateTime()>([] { return QDateTime::currentDateTime(); });
}

void VoiceRecorder::useFeed(int rate, int channels)
{
    m_feedRate = rate;
    m_feedChannels = channels;
}

void VoiceRecorder::feed(const QByteArray &pcm16)
{
    if (m_state == u"recording" || m_state == u"paused")
        m_backend->feed(pcm16);
}

void VoiceRecorder::setState(const QString &state, const QString &error)
{
    if (state == m_state && error == m_error)
        return;
    m_state = state;
    m_error = error;
    Q_EMIT stateChanged();
}

void VoiceRecorder::start()
{
    if (m_state == u"asking" || m_state == u"recording" || m_state == u"paused" || m_state == u"saving")
        return;
    if (!available()) {
        setState(u"failed"_s, tr("This version of Brulion cannot record."));
        Q_EMIT failed(m_error);
        return;
    }
    m_levels.clear();
    m_durationMs = 0;
    m_frames = 0;
    m_filePath.clear();
    m_format.clear();
    m_metered = true;
    Q_EMIT levelChanged();
    Q_EMIT durationChanged();
    if (m_feedRate > 0) {
        begin();
        return;
    }
    setState(u"asking"_s);
    m_backend->requestPermission([this](bool granted) {
        if (m_state != u"asking")
            return; // (cancelled meanwhile)
        if (!granted) {
            setState(u"denied"_s, tr("Brulion may not use the microphone."));
            Q_EMIT failed(m_error);
            return;
        }
        begin();
    });
}

void VoiceRecorder::begin()
{
    const QString problem = m_backend->open(m_wavOnly, m_feedRate, m_feedChannels);
    if (!problem.isEmpty()) {
        if (!m_filePath.isEmpty())
            QFile::remove(m_filePath);
        m_filePath.clear();
        setState(u"failed"_s, problem);
        Q_EMIT failed(problem);
        return;
    }
    setState(u"recording"_s);
    if (m_testVoice) {
        m_testFrames = 0;
        m_testVoice->start();
    }
}

QString VoiceRecorder::newFile(const QString &extension)
{
    QDir().mkpath(m_folder);
    const QString name = recordingName(m_namePrefix, clock()(), extension);
    // (A file of the same minute still there: the next free name.)
    QString path = m_folder + u'/' + name;
    for (int i = 2; QFile::exists(path); ++i)
        path = m_folder + u'/' + name.left(name.size() - extension.size() - 1) + u' ' + QString::number(i) + u'.'
               + extension;
    return path;
}

void VoiceRecorder::pause()
{
    if (m_state != u"recording")
        return;
    m_backend->pause();
    setState(u"paused"_s);
}

void VoiceRecorder::resume()
{
    if (m_state != u"paused")
        return;
    m_backend->resume();
    setState(u"recording"_s);
}

void VoiceRecorder::stop()
{
    if (m_state != u"recording" && m_state != u"paused")
        return;
    setState(u"saving"_s);
    if (m_testVoice)
        m_testVoice->stop();
    m_backend->finish([this](bool ok, const QString &error) { saved(ok, error); });
}

void VoiceRecorder::saved(bool ok, const QString &error)
{
    if (m_state != u"saving")
        return;
    if (!ok || m_durationMs <= 0) {
        if (!m_filePath.isEmpty())
            QFile::remove(m_filePath);
        m_filePath.clear();
        const QString why = !ok ? error : tr("Nothing was recorded.");
        setState(u"failed"_s, why);
        Q_EMIT failed(why);
        return;
    }
    setState(u"done"_s);
    Q_EMIT finished(m_filePath);
}

void VoiceRecorder::cancel()
{
    if (m_testVoice)
        m_testVoice->stop();
    if (m_state == u"recording" || m_state == u"paused" || m_state == u"saving")
        m_backend->abort();
    if (!m_filePath.isEmpty() && m_state != u"done")
        removeWhenFree(m_filePath);
    m_filePath.clear();
    m_levels.clear();
    m_durationMs = 0;
    Q_EMIT levelChanged();
    Q_EMIT durationChanged();
    setState(u"idle"_s);
}

void VoiceRecorder::pushLevel(double level)
{
    m_levels.append(level);
    while (m_levels.size() > LevelCount)
        m_levels.removeFirst();
    Q_EMIT levelChanged();
}

void VoiceRecorder::setDuration(qint64 ms)
{
    if (ms == m_durationMs)
        return;
    m_durationMs = ms;
    Q_EMIT durationChanged();
    if (m_durationMs >= MaxRecordingMs && m_state == u"recording")
        stop();
}

void VoiceRecorder::hearSamples(const float *samples, qsizetype count, int rate)
{
    // (A copy: the listener may set another one, or none, meanwhile.)
    if (const SampleListener listener = m_listener; listener && count > 0 && rate > 0)
        listener(samples, count, rate);
}

void VoiceRecorder::addFrames(qint64 frames, int rate)
{
    if (rate <= 0)
        return;
    m_frames += frames;
    const qint64 ms = m_frames * 1000 / rate;
    if (ms / 100 != m_durationMs / 100 || ms < m_durationMs) {
        m_durationMs = ms;
        Q_EMIT durationChanged();
    } else {
        m_durationMs = ms;
    }
    if (m_durationMs >= MaxRecordingMs && m_state == u"recording")
        stop();
}

} // namespace brulion::media
