#include "speech/live.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::speech {

namespace {

/// `word` without the punctuation around it, lower-cased: how two hearings of it compare.
QString bare(QStringView word)
{
    qsizetype from = 0;
    qsizetype to = word.size();
    while (from < to && !word[from].isLetterOrNumber())
        ++from;
    while (to > from && !word[to - 1].isLetterOrNumber())
        --to;
    return word.sliced(from, to - from).toString().toLower();
}

QString joined(const QString &a, const QString &b)
{
    if (a.isEmpty())
        return b;
    if (b.isEmpty())
        return a;
    return a + u' ' + b;
}

} // namespace

LiveWindows::LiveWindows(int sampleRate, Rules rules) : m_rate(std::max(1, sampleRate)), m_rules(rules) { }

void LiveWindows::append(const float *samples, qsizetype count)
{
    if (!samples || count <= 0)
        return;
    const qsizetype at = m_samples.size();
    m_samples.resize(at + count);
    std::copy(samples, samples + count, m_samples.begin() + at);
    m_total += count;
    // A recognizer far behind: what waits longest is never heard live (the transcript, heard
    // whole when the recording ends, has it).
    const qint64 most = samplesOf(m_rules.bufferedMs);
    if (m_total - m_start > most) {
        m_start = m_total - samplesOf(m_rules.longestMs);
        m_lastTake = std::max(m_lastTake, m_start);
        m_tentative.clear();
        dropBefore(m_start);
    }
}

bool LiveWindows::due() const
{
    return m_total - m_lastTake >= samplesOf(m_rules.stepMs) && m_total - m_start >= samplesOf(m_rules.shortestMs);
}

LiveWindows::Window LiveWindows::take()
{
    Window w;
    if (m_total - m_start < samplesOf(m_rules.shortestMs))
        return w;
    w.from = m_start;
    w.to = std::min(m_total, m_start + samplesOf(m_rules.longestMs));
    w.closing = w.to - w.from >= samplesOf(m_rules.lengthMs);
    const qint64 offset = w.from - m_bufferStart;
    w.samples = m_samples.mid(offset, w.to - w.from);
    m_lastTake = w.to;
    return w;
}

void LiveWindows::heard(const Window &window, const QList<Segment> &segments)
{
    if (window.isNull() || window.from != m_start)
        return;
    const qint64 fromMs = msOf(window.from);
    const qint64 lengthMs = msOf(window.to - window.from);
    QList<Segment> spoken;
    for (const Segment &s : spokenSegments(segments)) {
        // (Times past the window's end: Whisper's guess at the last one's end.)
        const qint64 start = std::clamp<qint64>(s.startMs, 0, lengthMs);
        const qint64 end = std::clamp<qint64>(s.endMs, start, lengthMs);
        const QString text = withoutLoops(s.text);
        // (A segment that only says again what the one before said: stuck in a loop.)
        if (!spoken.isEmpty() && bare(spoken.last().text) == bare(text) && !bare(text).isEmpty() && spoken.size() >= 2
            && bare(spoken.at(spoken.size() - 2).text) == bare(text))
            continue;
        spoken.append(Segment{fromMs + start, fromMs + end, text});
    }
    if (!window.closing) {
        QStringList texts;
        for (const Segment &s : std::as_const(spoken))
            texts.append(s.text);
        m_tentative = withoutRepeat(m_committed, withoutLoops(texts.join(u' ')));
        return;
    }
    m_tentative.clear();
    // The last segment reaching the window's end may hold a word cut in two: heard again, whole,
    // at the start of the next window — unless it is the only one (no progress then).
    const qint64 endMs = msOf(window.to);
    const qint64 cutMs = 1000;
    if (spoken.size() >= 2 && spoken.last().endMs >= endMs - cutMs && spoken.last().startMs > fromMs + cutMs) {
        const Segment last = spoken.takeLast();
        keep(spoken);
        m_start = std::clamp(samplesOf(last.startMs), window.from + 1, window.to);
    } else {
        keep(spoken);
        m_start = std::max(window.from + 1, window.to - samplesOf(m_rules.overlapMs));
    }
    m_lastTake = std::max(m_lastTake, m_start);
    dropBefore(m_start);
}

void LiveWindows::keep(const QList<Segment> &segments)
{
    for (Segment s : segments) {
        s.text = withoutRepeat(m_committed, s.text);
        if (s.text.isEmpty())
            continue;
        m_committed = joined(m_committed, s.text);
        m_segments.append(s);
    }
}

void LiveWindows::dropBefore(qint64 sample)
{
    const qint64 drop = std::clamp<qint64>(sample - m_bufferStart, 0, m_samples.size());
    if (drop <= 0)
        return;
    m_samples.remove(0, drop);
    m_bufferStart += drop;
}

QString LiveWindows::text() const
{
    return joined(m_committed, m_tentative);
}

void LiveWindows::reset()
{
    m_samples.clear();
    m_bufferStart = 0;
    m_total = 0;
    m_start = 0;
    m_lastTake = 0;
    m_committed.clear();
    m_tentative.clear();
    m_segments.clear();
}

QString textTail(const QString &text, int maxChars)
{
    const QString t = text.trimmed();
    if (maxChars <= 0)
        return {};
    if (t.size() <= maxChars)
        return t;
    qsizetype from = t.size() - maxChars;
    // From the next word on (a word longer than the whole tail: cut inside it).
    const qsizetype space = t.indexOf(u' ', from);
    if (space >= 0 && space + 1 < t.size())
        from = space + 1;
    // (Not inside a surrogate pair.)
    if (from > 0 && from < t.size() && t[from].isLowSurrogate())
        ++from;
    return u"…"_s + t.mid(from);
}

QString withoutLoops(const QString &text)
{
    constexpr qsizetype LongestPhrase = 8;
    constexpr int Loop = 3;
    const QStringList words = text.split(u' ', Qt::SkipEmptyParts);
    QStringList bares;
    bares.reserve(words.size());
    for (const QString &w : words)
        bares.append(bare(w));
    QStringList out;
    qsizetype i = 0;
    while (i < words.size()) {
        qsizetype skip = 0;
        for (qsizetype n = 1; n <= LongestPhrase && i + n * Loop <= words.size(); ++n) {
            int times = 1;
            while (i + (times + 1) * n <= words.size()) {
                bool same = true;
                for (qsizetype k = 0; k < n && same; ++k)
                    same = !bares.at(i + k).isEmpty() && bares.at(i + k) == bares.at(i + times * n + k);
                if (!same)
                    break;
                ++times;
            }
            if (times >= Loop) {
                skip = n * times;
                // Once, as it was first said — ending as it was last (its full stop).
                for (qsizetype k = 0; k + 1 < n; ++k)
                    out.append(words.at(i + k));
                out.append(words.at(i + skip - 1));
                break;
            }
        }
        if (skip > 0) {
            i += skip;
        } else {
            out.append(words.at(i));
            ++i;
        }
    }
    return out.join(u' ');
}

QString withoutRepeat(const QString &before, const QString &next)
{
    const QString n = next.trimmed();
    if (before.isEmpty() || n.isEmpty())
        return n;
    const qsizetype lastSpace = before.lastIndexOf(u' ');
    const QString last = bare(QStringView(before).sliced(lastSpace + 1));
    const qsizetype firstSpace = n.indexOf(u' ');
    const QStringView firstWord = firstSpace < 0 ? QStringView(n) : QStringView(n).first(firstSpace);
    if (last.isEmpty() || bare(firstWord) != last)
        return n;
    return firstSpace < 0 ? QString() : n.mid(firstSpace + 1).trimmed();
}

} // namespace brulion::speech
