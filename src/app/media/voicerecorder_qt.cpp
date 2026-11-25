// VoiceRecorder over Qt Multimedia (BRULION_MULTIMEDIA): the samples from QAudioSource (the
// system's default input) — the meter's levels and the duration from them —, written by
// QMediaRecorder through QAudioBufferInput as AAC/M4A or Opus/Ogg where the backend encodes them
// (FFmpeg's), else — a backend without buffer input: Qt's native Android one — QMediaRecorder over
// QAudioInput, the system's own recorder (AAC/M4A; no samples pass here, so no meter), else as a
// WAV file written here. The permission through QMicrophonePermission.
#include "voicerecorder_p.h"

#include "media/media.h"
#include "media/pcm.h"

#include <QAudioBuffer>
#include <QAudioBufferInput>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioInput>
#include <QAudioSource>
#include <QCoreApplication>
#include <QFile>
#include <QLoggingCategory>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QPermissions>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <deque>

using namespace Qt::StringLiterals;

namespace brulion::media {

Q_LOGGING_CATEGORY(lcRecorder, "brulion.media.recorder")

namespace {

struct Encoding {
    QMediaFormat::FileFormat file;
    QMediaFormat::AudioCodec codec;
    const char *extension;
};

class QtRecorderBackend final : public QObject, public VoiceRecorder::Backend {
public:
    explicit QtRecorderBackend(VoiceRecorder *owner) : VoiceRecorder::Backend(owner) { }
    ~QtRecorderBackend() override { abort(); }

    bool available() const override { return true; }

    void requestPermission(std::function<void(bool)> done) override
    {
#if QT_CONFIG(permissions)
        QMicrophonePermission microphone;
        switch (qApp->checkPermission(microphone)) {
        case Qt::PermissionStatus::Granted: done(true); return;
        case Qt::PermissionStatus::Denied: done(false); return;
        case Qt::PermissionStatus::Undetermined: break;
        }
        qApp->requestPermission(microphone, this,
                                [done](const QPermission &p) { done(p.status() == Qt::PermissionStatus::Granted); });
#else
        done(true);
#endif
    }

    QString open(bool wavOnly, int feedRate, int feedChannels) override
    {
        abort();
        if (feedRate > 0) {
            m_format.setSampleRate(feedRate);
            m_format.setChannelCount(feedChannels);
            m_format.setSampleFormat(QAudioFormat::Int16);
            setDevice(u"test"_s);
        } else {
            const QAudioDevice input = QMediaDevices::defaultAudioInput();
            if (input.isNull())
                return VoiceRecorder::tr("This device has no microphone.");
            QAudioFormat f = input.preferredFormat();
            // One channel of 16-bit samples where the microphone gives them (speech, and half the
            // bytes of stereo); else what it prefers.
            QAudioFormat mono = f;
            mono.setChannelCount(1);
            mono.setSampleFormat(QAudioFormat::Int16);
            if (input.isFormatSupported(mono))
                f = mono;
            m_format = f;
            setDevice(input.description());
            m_source = std::make_unique<QAudioSource>(input, f);
            m_source->setBufferSize(f.bytesForDuration(200'000));
        }
        // The encoder: the first format of these the backend writes, unless it does not start.
        const Encoding encodings[] = {
                {QMediaFormat::Mpeg4Audio, QMediaFormat::AudioCodec::AAC, "m4a"},
                {QMediaFormat::Ogg, QMediaFormat::AudioCodec::Opus, "ogg"},
        };
        if (!wavOnly) {
            for (const Encoding &e : encodings) {
                QMediaFormat format(e.file);
                format.setAudioCodec(e.codec);
                if (!format.isSupported(QMediaFormat::Encode))
                    continue;
                if (openEncoder(format, QString::fromLatin1(e.extension)))
                    break;
            }
        }
        // No buffer input (Qt's native Android backend): the system's own recorder, from the
        // microphone straight into the file.
        if (!m_recorder && !wavOnly && feedRate <= 0) {
            for (const Encoding &e : encodings) {
                QMediaFormat format(e.file);
                format.setAudioCodec(e.codec);
                if (format.isSupported(QMediaFormat::Encode) && openDirect(format, QString::fromLatin1(e.extension))) {
                    m_source.reset();
                    return {};
                }
            }
        }
        if (!m_recorder && !openWav())
            return VoiceRecorder::tr("The recording cannot be written: %1").arg(m_wav.errorString());
        if (m_source) {
            m_io = m_source->start();
            if (!m_io || m_source->error() != QtAudio::NoError) {
                abort();
                return VoiceRecorder::tr("The microphone cannot be opened.");
            }
            QObject::connect(m_io, &QIODevice::readyRead, this, [this] { take(m_io->readAll()); });
        }
        return {};
    }

    void pause() override
    {
        m_paused = true;
        if (m_direct && m_recorder)
            m_recorder->pause();
        if (m_source)
            m_source->suspend();
        pushLevel(0);
    }

    void resume() override
    {
        m_paused = false;
        if (m_direct && m_recorder)
            m_recorder->record();
        if (m_source)
            m_source->resume();
    }

    void feed(const QByteArray &pcm16) override { take(pcm16); }

    void finish(std::function<void(bool, const QString &)> done) override
    {
        if (m_source) {
            // What it still holds, then no more.
            if (m_io && !m_paused)
                take(m_io->readAll());
            m_source->stop();
            m_source.reset();
            m_io = nullptr;
        }
        if (m_recorder) {
            m_done = std::move(done);
            m_finishing = true;
            if (m_direct) {
                m_recorder->stop();
                return;
            }
            sendPending();
            if (m_pending.empty())
                m_recorder->stop();
            // (Stopped: recorderStateChanged → finished() below.)
            return;
        }
        if (m_wav.isOpen()) {
            // The sizes the header left at 0.
            const qint64 data = m_wav.size() - 44;
            m_wav.seek(0);
            const bool ok = m_wav.write(wavHeader(m_format.sampleRate(), m_format.channelCount(), data)) == 44;
            m_wav.close();
            done(ok, ok ? QString() : m_wav.errorString());
            return;
        }
        done(false, VoiceRecorder::tr("Nothing was recorded."));
    }

    void abort() override
    {
        if (m_source) {
            m_source->stop();
            m_source.reset();
        }
        m_io = nullptr;
        m_done = nullptr;
        m_finishing = false;
        m_pending.clear();
        if (m_recorder) {
            QObject::disconnect(m_recorder.get(), nullptr, this, nullptr);
            m_recorder->stop();
            m_recorder.reset();
        }
        m_input.reset();
        m_audioInput.reset();
        m_session.reset();
        if (m_wav.isOpen())
            m_wav.close();
        m_paused = false;
        m_direct = false;
        m_startUs = 0;
    }

private:
    bool openEncoder(const QMediaFormat &format, const QString &extension)
    {
        const QString path = newFile(extension);
        m_session = std::make_unique<QMediaCaptureSession>();
        m_input = std::make_unique<QAudioBufferInput>(m_format);
        m_recorder = std::make_unique<QMediaRecorder>();
        m_session->setAudioBufferInput(m_input.get());
        m_session->setRecorder(m_recorder.get());
        m_recorder->setMediaFormat(format);
        m_recorder->setQuality(QMediaRecorder::NormalQuality);
        m_recorder->setEncodingMode(QMediaRecorder::ConstantQualityEncoding);
        m_recorder->setAudioChannelCount(m_format.channelCount());
        m_recorder->setAudioSampleRate(m_format.sampleRate());
        m_recorder->setOutputLocation(QUrl::fromLocalFile(path));
        QObject::connect(m_input.get(), &QAudioBufferInput::readyToSendAudioBuffer, this, [this] { sendPending(); });
        QObject::connect(m_recorder.get(), &QMediaRecorder::errorOccurred, this,
                         [this](QMediaRecorder::Error, const QString &text) {
                             qCWarning(lcRecorder).noquote() << u"the encoder failed: "_s + text;
                             if (m_done)
                                 complete(false, text);
                         });
        QObject::connect(m_recorder.get(), &QMediaRecorder::recorderStateChanged, this,
                         [this](QMediaRecorder::RecorderState state) {
                             if (state == QMediaRecorder::StoppedState && m_finishing)
                                 complete(m_recorder->error() == QMediaRecorder::NoError, m_recorder->errorString());
                         });
        m_recorder->record();
        if (m_recorder->error() != QMediaRecorder::NoError) {
            qCInfo(lcRecorder).noquote() << u"%1 cannot be recorded here (%2): WAV instead"_s.arg(
                    extension, m_recorder->errorString());
            QObject::disconnect(m_recorder.get(), nullptr, this, nullptr);
            m_recorder.reset();
            m_input.reset();
            m_session.reset();
            QFile::remove(path);
            return false;
        }
        setFile(path);
        setFormat(extension);
        return true;
    }

    /// The system's recorder over the default microphone: QAudioInput → QMediaRecorder.
    bool openDirect(const QMediaFormat &format, const QString &extension)
    {
        const QString path = newFile(extension);
        m_session = std::make_unique<QMediaCaptureSession>();
        m_audioInput = std::make_unique<QAudioInput>();
        m_recorder = std::make_unique<QMediaRecorder>();
        m_session->setAudioInput(m_audioInput.get());
        m_session->setRecorder(m_recorder.get());
        m_recorder->setMediaFormat(format);
        m_recorder->setQuality(QMediaRecorder::NormalQuality);
        m_recorder->setAudioChannelCount(1);
        m_recorder->setOutputLocation(QUrl::fromLocalFile(path));
        QObject::connect(m_recorder.get(), &QMediaRecorder::durationChanged, this,
                         [this](qint64 ms) { setDuration(ms); });
        QObject::connect(m_recorder.get(), &QMediaRecorder::errorOccurred, this,
                         [this](QMediaRecorder::Error, const QString &text) {
                             qCWarning(lcRecorder).noquote() << u"the system's recorder failed: "_s + text;
                             if (m_done)
                                 complete(false, text);
                         });
        QObject::connect(m_recorder.get(), &QMediaRecorder::recorderStateChanged, this,
                         [this](QMediaRecorder::RecorderState state) {
                             if (state == QMediaRecorder::StoppedState && m_finishing)
                                 complete(m_recorder->error() == QMediaRecorder::NoError, m_recorder->errorString());
                         });
        m_recorder->record();
        if (m_recorder->error() != QMediaRecorder::NoError) {
            QObject::disconnect(m_recorder.get(), nullptr, this, nullptr);
            m_recorder.reset();
            m_audioInput.reset();
            m_session.reset();
            QFile::remove(path);
            return false;
        }
        m_direct = true;
        setFile(path);
        setFormat(extension);
        setMetered(false);
        return true;
    }

    bool openWav()
    {
        const QString path = newFile(u"wav"_s);
        m_wav.setFileName(path);
        if (!m_wav.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        m_wav.write(wavHeader(m_format.sampleRate(), m_format.channelCount(), 0));
        setFile(path);
        setFormat(u"wav"_s);
        // (WAV holds 16-bit samples: others are converted as they come.)
        return true;
    }

    /// Samples from the microphone (or the feed), in m_format.
    void take(const QByteArray &bytes)
    {
        if (bytes.isEmpty() || m_paused || m_format.bytesPerFrame() <= 0)
            return;
        const qint64 frames = bytes.size() / m_format.bytesPerFrame();
        if (frames <= 0)
            return;
        const QByteArray whole = bytes.left(frames * m_format.bytesPerFrame());
        // The level of this piece.
        const qsizetype samples = frames * m_format.channelCount();
        double level = 0;
        QByteArray pcm16;
        if (m_format.sampleFormat() == QAudioFormat::Int16) {
            level = meterLevel(reinterpret_cast<const qint16 *>(whole.constData()), samples);
            pcm16 = whole;
        } else {
            QList<float> f(samples);
            for (qsizetype i = 0; i < samples; ++i)
                f[i] = m_format.normalizedSampleValue(whole.constData() + i * m_format.bytesPerSample());
            level = meterLevel(f.constData(), samples);
            if (!m_recorder)
                pcm16 = toPcm16(f.constData(), samples);
        }
        pushLevel(level);
        if (listened())
            hearMono(whole, frames);
        if (m_recorder) {
            m_pending.emplace_back(whole, m_format, m_startUs);
            m_startUs += m_format.durationForFrames(qint32(frames));
            sendPending();
        } else if (m_wav.isOpen()) {
            m_wav.write(pcm16);
        }
        addFrames(frames, m_format.sampleRate());
    }

    /// `frames` frames of m_format as one channel of floats, for the sample listener.
    void hearMono(const QByteArray &bytes, qint64 frames)
    {
        const int channels = std::max(1, m_format.channelCount());
        const int bytesPerSample = m_format.bytesPerSample();
        QList<float> mono(frames);
        const bool int16 = m_format.sampleFormat() == QAudioFormat::Int16;
        for (qint64 f = 0; f < frames; ++f) {
            float sum = 0;
            for (int c = 0; c < channels; ++c) {
                const char *at = bytes.constData() + (f * channels + c) * bytesPerSample;
                sum += int16 ? float(qFromLittleEndian<qint16>(at)) / 32768.0f : m_format.normalizedSampleValue(at);
            }
            mono[f] = sum / float(channels);
        }
        hear(mono.constData(), mono.size(), m_format.sampleRate());
    }

    void sendPending()
    {
        while (m_input && !m_pending.empty()) {
            if (!m_input->sendAudioBuffer(m_pending.front()))
                return; // (readyToSendAudioBuffer: again)
            m_pending.pop_front();
        }
        if (m_finishing && m_pending.empty() && m_recorder
            && m_recorder->recorderState() == QMediaRecorder::RecordingState)
            m_recorder->stop();
    }

    void complete(bool ok, const QString &error)
    {
        auto done = std::exchange(m_done, nullptr);
        m_finishing = false;
        if (m_recorder)
            QObject::disconnect(m_recorder.get(), nullptr, this, nullptr);
        // (Deleted later: this runs inside the recorder's own signal.)
        QMediaRecorder *mediaRecorder = m_recorder.release();
        QAudioBufferInput *input = m_input.release();
        QMediaCaptureSession *session = m_session.release();
        QAudioInput *audioInput = m_audioInput.release();
        m_direct = false;
        QTimer::singleShot(0, [mediaRecorder, input, session, audioInput] {
            delete mediaRecorder;
            delete input;
            delete session;
            delete audioInput;
        });
        if (done)
            done(ok, ok ? QString() : error);
    }

    QAudioFormat m_format;
    std::unique_ptr<QAudioSource> m_source;
    QIODevice *m_io = nullptr;
    std::unique_ptr<QMediaCaptureSession> m_session;
    std::unique_ptr<QAudioBufferInput> m_input;
    std::unique_ptr<QAudioInput> m_audioInput; ///< the system's recorder's microphone (openDirect)
    std::unique_ptr<QMediaRecorder> m_recorder;
    bool m_direct = false; ///< the system's recorder writes the file (no samples here)
    std::deque<QAudioBuffer> m_pending;
    qint64 m_startUs = 0;
    QFile m_wav;
    bool m_paused = false;
    bool m_finishing = false;
    std::function<void(bool, const QString &)> m_done;
};

} // namespace

std::unique_ptr<VoiceRecorder::Backend> makeRecorderBackend(VoiceRecorder *owner)
{
    return std::make_unique<QtRecorderBackend>(owner);
}

} // namespace brulion::media
