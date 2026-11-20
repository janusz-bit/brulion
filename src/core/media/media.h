#pragma once
// media — audio and video attachments (docs/SYNTAX.md §2 "Audio and video"), the part without a
// player, built and tested everywhere: which files play (util/paths.h: isAudioPath, isVideoPath),
// the time a link or an embed starts at (W3C Media Fragments' `#t=`), the player's times as text
// and the names of voice notes (this header); where a transcript goes under its recording
// (transcript.h); sound as numbers — WAV files, 16 kHz mono for speech recognition, the level
// meter (pcm.h). Pure functions: the players and the recorder are the app's (src/app/media).

#include <QDateTime>
#include <QString>
#include <QStringView>

namespace brulion::media {

enum class Kind {
    None,
    Audio, ///< paths::isAudioPath(): a player bar
    Video, ///< paths::isVideoPath(): a picture with the bar under it (sound alone shows as audio)
};

/// The kind of player a file gets, by its name (any folder, any case).
Kind kindOf(QStringView path);

/// A span of time of a recording, in milliseconds; -1: open (from the start / to the end).
struct TimeRange {
    qint64 startMs = -1;
    qint64 endMs = -1;
    bool isValid() const { return startMs >= 0 || endMs >= 0; }
    friend bool operator==(const TimeRange &, const TimeRange &) = default;
};

/// The temporal fragment of a link or an embed (`![[wykład.mp3#t=1:23]]`, `[[film.mp4#t=83]]`,
/// `![](a.ogg#t=10,20)`): `fragment` is the text after '#', '&'-separated key=value pairs, the key
/// `t` (any case) in W3C Media Fragments URI 1.0's normal play time — seconds ("83", "83.5"),
/// "mm:ss" or "hh:mm:ss" with optional fractions, an optional "npt:" prefix, a start and/or an end
/// ("10,20", ",20") — and, as YouTube and Obsidian plugins write it, "1h2m3s" / "90s". Other keys
/// are ignored; the last valid `t=` counts. Invalid or missing: {} (an end before the start, SMPTE
/// or clock time, a minute or second field of 60 or more after a colon, more than 24 h).
TimeRange parseTimeFragment(QStringView fragment);

/// The fragment for a time range: "t=83", "t=83.5" (tenths only when not whole), "t=10,20",
/// "t=,20" — seconds, what every player and browser reads. "" for an invalid range.
QString timeFragment(const TimeRange &range);

/// A position as the player shows it: "0:07", "1:23", "12:05", "1:02:03" — hours when the longer
/// of `ms` and `totalMs` is an hour or more (so "0:59:58 / 1:00:00" line up). Negative: "0:00".
QString formatTime(qint64 ms, qint64 totalMs = -1);

/// A voice note's file name: "<prefix> yyyy-MM-dd HH.mm.<extension>" (as drawings are named;
/// `prefix` is the app's translated "Recording" — "Nagranie 2026-11-20 18.30.m4a"). The extension
/// without its dot, lower-cased; the prefix's characters a file name cannot hold dropped
/// (paths::sanitizeFileName). A free name ("… 2.m4a") is the attachment store's job.
QString recordingName(const QString &prefix, const QDateTime &when, const QString &extension);

/// The longest voice note the recorder makes before it stops by itself (3 hours: a lecture).
inline constexpr qint64 MaxRecordingMs = 3LL * 60 * 60 * 1000;

} // namespace brulion::media
