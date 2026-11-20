# media — audio and video in notes (docs/SYNTAX.md §2 "Audio and video", docs/UI.md "Audio and
# video", "Voice notes"), the part without a player: the time an embed or a link starts at (`#t=`),
# the player's times as text, voice notes' names (media.h), where a transcript goes under its
# recording (transcript.h), WAV files, 16 kHz mono for speech recognition and the recorder's meter
# (pcm.h). Pure functions; the players and the recorder are the app's (src/app/media), speech
# recognition the speech module's.
brulion_add_core_module(media
    SOURCES
        media.h media.cpp
        pcm.h pcm.cpp
        transcript.h transcript.cpp
    DEPS util markdown)
