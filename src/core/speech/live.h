#pragma once
// Text while it is being recorded (docs/UI.md "Voice notes" → "Text while recording"): the
// recording so far heard again and again in short windows, as whisper.cpp's stream example does —
// a window grows by a step of a few seconds each time it is heard, its text replacing the one
// before (tentative), until it is long enough; then its text is kept (committed) and the next
// window starts where it ended. Pure: the caller feeds the samples as they are recorded, takes a
// window when one is due(), has the recognizer hear it (off the GUI thread: the app's speech
// worker) and hands back what was heard.
//
//   speech::LiveWindows live(48000);
//   live.append(samples);                       // as the microphone gives them
//   if (live.due() && !busy) {
//       const speech::LiveWindows::Window w = live.take();
//       … hear w.samples (at live.sampleRate()), then: live.heard(w, segments);
//   }
//   show(live.text());
//
// A kept window keeps all its segments but a last one that reaches its end (a word may be cut
// there): the next window starts at that segment and hears it whole; a window of one segment is
// kept whole and the next one starts a moment before its end (the overlap catches a word cut in
// two). A word the next kept text repeats at the seam is dropped.

#include "speech/speech.h"

#include <QList>
#include <QString>

namespace brulion::speech {

/// When LiveWindows hears a window again, keeps it, gives up on what waits.
struct LiveRules {
    qint64 stepMs = 3000;      ///< a window is heard again once this much more is recorded
    qint64 lengthMs = 10000;   ///< a window this long is kept: the next one starts after it
    qint64 longestMs = 25000;  ///< a window never holds more (a slow recognizer fell behind)
    qint64 overlapMs = 200;    ///< what the next window hears again of a window kept whole
    qint64 shortestMs = 1000;  ///< less than this is not worth hearing
    qint64 bufferedMs = 60000; ///< more waiting than this: the oldest is never heard live
};

class LiveWindows {
public:
    using Rules = LiveRules;

    /// A stretch of the recording to hear: samples [from, to) of it.
    struct Window {
        qint64 from = 0;
        qint64 to = 0;
        QList<float> samples;
        /// Its text is kept once heard (it is long enough): the next window starts after it.
        bool closing = false;
        bool isNull() const { return to <= from; }
    };

    /// `sampleRate`: of the samples append() is given (one channel).
    explicit LiveWindows(int sampleRate, Rules rules = {});

    int sampleRate() const { return m_rate; }
    const Rules &rules() const { return m_rules; }

    /// Samples just recorded (one channel, floats in [-1, 1]).
    void append(const float *samples, qsizetype count);
    void append(const QList<float> &samples) { append(samples.constData(), samples.size()); }

    /// How much is recorded, in milliseconds.
    qint64 recordedMs() const { return msOf(m_total); }
    /// The recording reached where the next window should be heard (a step more since the last
    /// one was taken, and enough to hear).
    bool due() const;
    /// The window to hear now: from where the text kept so far ends to what is recorded (at most
    /// Rules::longestMs). Null when nothing is there to hear.
    Window take();
    /// What the recognizer heard in `window` (take()'s; the segments' times from the window's
    /// start). A window the recording has moved past meanwhile (not the current one) is ignored.
    void heard(const Window &window, const QList<Segment> &segments);

    /// The text kept so far.
    QString committed() const { return m_committed; }
    /// What the window heard last says, after the kept text (replaced by the next one).
    QString tentative() const { return m_tentative; }
    /// Both, as one text.
    QString text() const;
    /// The kept text's segments, their times in the recording.
    const QList<Segment> &committedSegments() const { return m_segments; }
    /// Starts again: nothing recorded, nothing heard.
    void reset();

    /// Milliseconds of `samples` samples at this rate.
    qint64 msOf(qint64 samples) const { return m_rate > 0 ? samples * 1000 / m_rate : 0; }
    qint64 samplesOf(qint64 ms) const { return ms * m_rate / 1000; }

private:
    void keep(const QList<Segment> &segments);
    void dropBefore(qint64 sample);

    int m_rate = 16000;
    Rules m_rules;
    QList<float> m_samples; ///< from m_bufferStart on
    qint64 m_bufferStart = 0;
    qint64 m_total = 0;    ///< samples recorded
    qint64 m_start = 0;    ///< where the current window starts (everything before is kept text)
    qint64 m_lastTake = 0; ///< the end of the last window taken
    QString m_committed;
    QString m_tentative;
    QList<Segment> m_segments;
};

/// The last `maxChars` characters of `text` from the start of a word, "…" before them when it was
/// cut — what a small field shows of a text that grows at its end. All of it when it fits.
QString textTail(const QString &text, int maxChars);

/// `text` with a phrase of up to eight words said three or more times in a row once — what Whisper
/// writes when greedy decoding gets stuck in a loop ("Dzień dobry. Dzień dobry. Dzień dobry. …")
/// —, the words compared without case and punctuation, white space between words made single.
/// Twice is kept (people say "nie, nie").
QString withoutLoops(const QString &text);

/// `next` without a first word that repeats the last word of `before` (any case, punctuation aside)
/// — what two windows heard twice at their seam.
QString withoutRepeat(const QString &before, const QString &next);

} // namespace brulion::speech
