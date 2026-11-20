// What records for VoiceRecorder (voicerecorder.h): Qt Multimedia (voicerecorder_qt.cpp) or nothing
// (voicerecorder_none.cpp). Private to the recorder.
#pragma once

#include "voicerecorder.h"

#include <QByteArray>
#include <QString>

#include <functional>
#include <memory>

namespace brulion::media {

struct VoiceRecorder::Backend {
    explicit Backend(VoiceRecorder *owner) : recorder(owner) { }
    virtual ~Backend() = default;
    virtual bool available() const = 0;
    /// The microphone may be used: `done(true)`; asked first where the system asks.
    virtual void requestPermission(std::function<void(bool granted)> done) = 0;
    /// Opens the microphone (`feedRate` > 0: the test feed instead) and the file, and starts:
    /// "" on success, else why not.
    virtual QString open(bool wavOnly, int feedRate, int feedChannels) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void feed(const QByteArray &pcm16) = 0;
    /// Ends the recording; `done(ok, error)` once the file is whole (possibly later).
    virtual void finish(std::function<void(bool ok, const QString &error)> done) = 0;
    /// Stops everything at once; the file is the recorder's to delete.
    virtual void abort() = 0;

    // The recorder's own, for the backend.
    QString newFile(const QString &extension) { return recorder->newFile(extension); }
    void pushLevel(double level) { recorder->pushLevel(level); }
    void addFrames(qint64 frames, int rate) { recorder->addFrames(frames, rate); }
    void setFormat(const QString &format) { recorder->m_format = format; }
    void setDevice(const QString &device) { recorder->m_device = device; }
    void setFile(const QString &path) { recorder->m_filePath = path; }
    void setMetered(bool metered) { recorder->m_metered = metered; }
    void setDuration(qint64 ms) { recorder->setDuration(ms); }
    QString file() const { return recorder->m_filePath; }
    /// The recording reached media::MaxRecordingMs.
    bool full() const;
    void stopByItself() { recorder->stop(); }

    VoiceRecorder *recorder;
};

std::unique_ptr<VoiceRecorder::Backend> makeRecorderBackend(VoiceRecorder *owner);

} // namespace brulion::media
