// VoiceRecorder in a build without Qt Multimedia: nothing records (`available` false; the editor
// offers no "Record voice note").
#include "voicerecorder_p.h"

namespace brulion::media {

namespace {

struct NoRecorderBackend final : VoiceRecorder::Backend {
    using VoiceRecorder::Backend::Backend;
    bool available() const override { return false; }
    void requestPermission(std::function<void(bool)> done) override { done(false); }
    QString open(bool, int, int) override { return VoiceRecorder::tr("This version of Brulion cannot record."); }
    void pause() override { }
    void resume() override { }
    void feed(const QByteArray &) override { }
    void finish(std::function<void(bool, const QString &)> done) override
    {
        done(false, VoiceRecorder::tr("This version of Brulion cannot record."));
    }
    void abort() override { }
};

} // namespace

std::unique_ptr<VoiceRecorder::Backend> makeRecorderBackend(VoiceRecorder *owner)
{
    return std::make_unique<NoRecorderBackend>(owner);
}

} // namespace brulion::media
