// QML registration of the media area's C++ (library brulion_media, media/CMakeLists.txt) in the
// app's QML module "Brulion": MediaPlayback is a singleton (the app's one player), VoiceRecorder a
// creatable type (qml/media/VoiceRecorderDialog.qml), and so is LiveTranscript (its text while
// recording). The Transcriber is AppController.speech.
#pragma once

#include "livetranscript.h"
#include "mediaplayback.h"
#include "transcriber.h"
#include "voicerecorder.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace brulion::media {

struct MediaPlaybackForeign {
    Q_GADGET
    QML_FOREIGN(brulion::media::MediaPlayback)
    QML_NAMED_ELEMENT(MediaPlayback)
    QML_SINGLETON
public:
    /// The app's one player (MediaPlayback::instance(): the C++ side and the self-tests steer the
    /// same one), not a player of its own.
    static MediaPlayback *create(QQmlEngine *engine, QJSEngine *js) { return MediaPlayback::create(engine, js); }
};

struct VoiceRecorderForeign {
    Q_GADGET
    QML_FOREIGN(brulion::media::VoiceRecorder)
    QML_NAMED_ELEMENT(VoiceRecorder)
};

struct LiveTranscriptForeign {
    Q_GADGET
    QML_FOREIGN(brulion::media::LiveTranscript)
    QML_NAMED_ELEMENT(LiveTranscript)
};

struct TranscriberForeign {
    Q_GADGET
    QML_FOREIGN(brulion::media::Transcriber)
    QML_NAMED_ELEMENT(Transcriber)
    QML_UNCREATABLE("AppController.speech")
};

} // namespace brulion::media
