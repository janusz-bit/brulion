# Audio and video in notes, voice notes (library brulion_media, src/app/media):
#   tst_media_playback    the app's one player (MediaPlayback) over a fake engine: one file at a
#                         time, the place each was left, time fragments and their end, the surfaces
#                         of a video (the last one wins, the one before takes it back, a video
#                         pauses unseen), the views count, info() and posters, speed and volume;
#   tst_media_engine      Qt Multimedia's engine, prober, decoder and recorder on generated files
#                         (a tone as WAV — and MP3, M4A, Ogg, FLAC, WebM, MP4 written by this
#                         build's own recorder where it encodes): probed, played to the end
#                         without a sound card, decoded to 16 kHz; recorded from a fed tone as
#                         M4A / Ogg / WAV, paused, cancelled — skipped where the build has no Qt
#                         Multimedia;
#   tst_media_transcriber the speech queue (Transcriber) with a fake recognizer and decoder: the
#                         model files, the queue in order, cancel, the transcript placed, errors;
#   tst_media_dictation   the AI chat's dictation (Dictation) with a fed recorder and the fake
#                         recognizer: not ready without a model, the text heard in its language,
#                         its limit, cancelled, nothing recorded, the recordings deleted.
brulion_add_test(tst_media_playback SOURCES tst_media_playback.cpp LIBS brulion_media)
brulion_add_test(tst_media_engine SOURCES tst_media_engine.cpp LIBS brulion_media Qt6::Gui)
set_tests_properties(tst_media_engine PROPERTIES TIMEOUT 300)
# Silent: no sound card is reached (the player's clock runs without one).
set_property(TEST tst_media_engine APPEND PROPERTY ENVIRONMENT "PULSE_SERVER=unix:/nonexistent")
brulion_add_test(tst_media_transcriber SOURCES tst_media_transcriber.cpp LIBS brulion_media)
brulion_add_test(tst_media_dictation SOURCES tst_media_dictation.cpp LIBS brulion_media)
# Silent: no sound card is reached (the recorder is fed its samples).
set_property(TEST tst_media_dictation APPEND PROPERTY ENVIRONMENT "PULSE_SERVER=unix:/nonexistent")
