#include "writing/writing.h"

#include <QRegularExpression>

#include <algorithm>
#include <vector>

using namespace Qt::StringLiterals;

namespace brulion::writing {

namespace {

const QString kSystem = u"You are the writing assistant of the notes app Brulion. You get a part of the user's note "
                        "and what to do with it. Reply with the resulting text alone: no preamble, no explanation, "
                        "no quotes or code fence around it. Keep the note's Markdown as it is: links [[…]], tags #…, "
                        "tasks - [ ] and - [x], due dates 📅 …, reminders ⏰ …, the list's markers and indentation, "
                        "line breaks where they belong. Keep the text's language unless you are asked to translate "
                        "it. Never add facts the text does not give."_s;

/// At most `max` characters of `text`: its end (`fromEnd`) or its start, cut at a word, never in
/// the middle of a letter beyond the basic plane.
QString around(QStringView text, int max, bool fromEnd)
{
    if (text.size() <= max)
        return text.toString();
    if (fromEnd) {
        qsizetype from = text.size() - max;
        if (text.at(from).isLowSurrogate())
            ++from;
        const qsizetype space = text.indexOf(u' ', from);
        if (space >= 0 && space < from + 80)
            from = space + 1;
        return u"…"_s + text.mid(from).toString();
    }
    qsizetype to = max;
    if (text.at(to - 1).isHighSurrogate())
        --to;
    const qsizetype space = text.lastIndexOf(u' ', to);
    if (space > to - 80 && space > 0)
        to = space;
    return text.left(to).toString() + u'…';
}

bool startsList(QStringView s)
{
    static const QRegularExpression marker(u"^[ \\t]*(?:[-*+]|\\d{1,9}[.)])[ \\t]+"_s);
    return marker.matchView(s).hasMatch();
}

bool startsBlock(QStringView s)
{
    static const QRegularExpression block(u"^(?:[ \\t]*(?:[-*+]|\\d{1,9}[.)])[ \\t]+|#{1,6}[ \\t]|>|```|~~~|\\|)"_s);
    return block.matchView(s).hasMatch();
}

/// The whitespace at the start / end of `s`.
qsizetype leadingSpace(QStringView s)
{
    qsizetype n = 0;
    while (n < s.size() && s.at(n).isSpace())
        ++n;
    return n;
}

qsizetype trailingSpace(QStringView s)
{
    qsizetype n = 0;
    while (n < s.size() && s.at(s.size() - 1 - n).isSpace())
        ++n;
    return n;
}

/// The quotes a whole answer may come in: opening, closing.
const QList<std::pair<QChar, QChar>> &quotes()
{
    static const QList<std::pair<QChar, QChar>> pairs{
            {u'"', u'"'}, {u'\'', u'\''}, {QChar(0x201E), QChar(0x201D)}, {QChar(0x201C), QChar(0x201D)},
            {QChar(0x00AB), QChar(0x00BB)}, {QChar(0x201A), QChar(0x2019)}, {QChar(0x2018), QChar(0x2019)}};
    return pairs;
}

QStringList tokens(QStringView text)
{
    static const QRegularExpression token(u"\\w+|\\s+|[^\\w\\s]"_s, QRegularExpression::UseUnicodePropertiesOption);
    QStringList out;
    for (auto it = token.globalMatchView(text); it.hasNext();)
        out << it.next().captured();
    return out;
}

} // namespace

QString actionName(Action action)
{
    switch (action) {
    case Action::Continue: return u"continue"_s;
    case Action::Summarize: return u"summarize"_s;
    case Action::Rewrite: break;
    }
    return u"rewrite"_s;
}

Action actionFrom(QStringView name)
{
    if (name == u"continue")
        return Action::Continue;
    if (name == u"summarize")
        return Action::Summarize;
    return Action::Rewrite;
}

QStringList presets()
{
    return {u"shorter"_s, u"clearer"_s, u"grammar"_s, u"formal"_s, u"casual"_s, u"english"_s, u"polish"_s};
}

QString presetInstruction(QStringView preset)
{
    if (preset == u"shorter")
        return u"Make it shorter: say the same in fewer words."_s;
    if (preset == u"clearer")
        return u"Make it clearer and easier to read, without changing what it says."_s;
    if (preset == u"grammar")
        return u"Fix its spelling, grammar and punctuation; change nothing else."_s;
    if (preset == u"formal")
        return u"Make its tone more formal."_s;
    if (preset == u"casual")
        return u"Make its tone more casual and friendly."_s;
    if (preset == u"english")
        return u"Translate it into English."_s;
    if (preset == u"polish")
        return u"Translate it into Polish."_s;
    return {};
}

QString problem(const Input &input)
{
    if (input.text.trimmed().isEmpty())
        return input.action == Action::Continue ? u"there is no text before the caret to continue"_s
                                                : u"there is no text to work on"_s;
    if (input.action != Action::Continue && input.text.size() > MaxText)
        return u"the text is too long (%1 characters; at most %2)"_s.arg(input.text.size()).arg(MaxText);
    if (input.action == Action::Rewrite && input.instruction.trimmed().isEmpty())
        return u"say how to rewrite it"_s;
    return {};
}

ai::Request request(const Input &input)
{
    ai::Request r;
    r.system = kSystem;
    r.effort = u"low"_s;
    QStringList parts;
    switch (input.action) {
    case Action::Rewrite:
        parts << u"Rewrite the text below: "_s + input.instruction.trimmed();
        break;
    case Action::Continue:
        parts << u"Continue the text below where it ends: write what comes next — a sentence or two, or the next "
                 "items when it ends in a list —, in its language, tone and format. Reply with the continuation "
                 "alone, without repeating the text."_s;
        break;
    case Action::Summarize:
        parts << u"Sum the text below up in a short paragraph (at most three sentences), in its language. Reply "
                 "with the summary alone."_s;
        break;
    }
    if (!input.page.trimmed().isEmpty())
        parts << u"The note: "_s + input.page.trimmed();
    // (Continue: the text is what comes before the caret; nothing more before it.)
    const QString text = input.action == Action::Continue ? around(input.text, MaxText, true) : input.text;
    if (input.action != Action::Continue && !input.before.trimmed().isEmpty())
        parts << u"Before it in the note (context, not to repeat):\n<before>\n"_s
                         + around(input.before, MaxContext, true).trimmed() + u"\n</before>"_s;
    parts << u"<text>\n"_s + text + u"\n</text>"_s;
    if (!input.after.trimmed().isEmpty())
        parts << u"After it in the note (context, not to repeat):\n<after>\n"_s
                         + around(input.after, MaxContext, false).trimmed() + u"\n</after>"_s;
    if (input.inline_)
        parts << (input.text.contains(u'\n')
                          ? u"The text is a part of one item of the note: give back text for its place, without a "
                            "list marker or heading of its own."_s
                          : u"The text is a part of one line of the note: give back one line for its place, "
                            "without a list marker or heading of its own."_s);
    switch (input.action) {
    case Action::Rewrite:
        parts << u"Reply with the rewritten text alone."_s;
        r.maxTokens = std::clamp(int(input.text.size()) + 600, 1000, 16000);
        break;
    case Action::Continue:
        r.maxTokens = 800;
        break;
    case Action::Summarize:
        r.maxTokens = 600;
        break;
    }
    r.messages << ai::textMessage(u"user"_s, parts.join(u"\n\n"_s));
    return r;
}

QString clean(QStringView answer, const Input &input, bool partial)
{
    QString s = answer.toString();
    s.replace(u"\r\n"_s, u"\n"_s);
    s = s.trimmed();
    // A fence around all of it (```markdown … ```); still coming: its opening alone.
    static const QRegularExpression open(u"^(`{3,}|~{3,})[\\w+-]*[ \\t]*\\n"_s);
    if (const auto m = open.match(s); m.hasMatch() && !input.text.trimmed().startsWith(m.captured(1))) {
        const QString fence = m.captured(1);
        QString inner = s.mid(m.capturedLength());
        if (inner.endsWith(fence) && (inner.size() == fence.size() || inner.at(inner.size() - fence.size() - 1) == u'\n'))
            s = inner.chopped(fence.size()).trimmed();
        else if (partial)
            s = inner.trimmed();
    }
    // A preamble ("Here is the rewritten text:", "Oto poprawiony tekst:") before the text.
    static const QRegularExpression preamble(
            u"^(?:here is|here's|here are|sure|certainly|of course|oto|poniżej|proszę|jasne|oczywiście)\\b[^\\n]{0,120}:[ \\t]*\\n+"_s,
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    if (const auto m = preamble.match(s); m.hasMatch() && m.capturedLength() < s.size()
        && !preamble.match(input.text.trimmed() + u'\n').hasMatch())
        s = s.mid(m.capturedLength()).trimmed();
    // Quotes around all of it that the text did not have.
    const QString original = input.text.trimmed();
    for (const auto &[first, last] : quotes()) {
        if (s.size() >= 2 && s.front() == first && s.back() == last
            && !(original.size() >= 1 && original.front() == first)) {
            const QString inner = s.mid(1, s.size() - 2);
            if (!inner.contains(first) && !inner.contains(last)) // ("a" and "b": no quote around all)
                s = inner.trimmed();
            break;
        }
    }
    if (input.inline_) {
        // A part of one block's text: no list marker of its own; one line for one line.
        static const QRegularExpression marker(u"^(?:[-*+]|\\d{1,9}[.)])[ \\t]+(?:\\[[ xX/-]\\][ \\t]+)?"_s);
        if (!startsList(original) && startsList(s))
            s.remove(marker);
        if (!input.text.contains(u'\n'))
            s = s.simplified();
    }
    if (input.action == Action::Continue && !s.isEmpty()) {
        // The end of the text written again before what comes next.
        const QString text = input.text.trimmed();
        const qsizetype most = std::min<qsizetype>({s.size(), text.size(), 2000});
        for (qsizetype k = most; k >= 12; --k) {
            if (text.endsWith(QStringView(s).left(k))) {
                s = s.mid(k).trimmed();
                break;
            }
        }
    }
    return s;
}

Edit edit(QStringView text, qsizetype from, qsizetype to, QStringView result, Action action)
{
    from = std::clamp<qsizetype>(from, 0, text.size());
    to = std::clamp<qsizetype>(to, 0, text.size());
    if (from > to)
        std::swap(from, to);
    const QString inserted = result.trimmed().toString();
    switch (action) {
    case Action::Rewrite: {
        const QStringView selected = text.mid(from, to - from);
        const qsizetype lead = leadingSpace(selected);
        if (lead == selected.size())
            return {from, to, inserted};
        return {from + lead, to - trailingSpace(selected), inserted};
    }
    case Action::Continue: {
        if (inserted.isEmpty())
            return {to, to, QString()};
        const QStringView before = text.left(to);
        const QStringView rest = text.mid(to);
        QString out;
        if (before.isEmpty() || before.endsWith(u'\n')) {
            // (Its own line already.)
        } else if (startsBlock(inserted)) {
            out = u"\n"_s;
        } else if (!before.back().isSpace()) {
            static const QRegularExpression punctuation(u"^[,.;:!?)\\]…]"_s);
            if (!punctuation.match(inserted).hasMatch())
                out = u" "_s;
        }
        out += inserted;
        if (!rest.isEmpty() && !rest.front().isSpace())
            out += u' ';
        return {to, to, out};
    }
    case Action::Summarize: {
        // After the paragraph the selection ends in.
        qsizetype at = to;
        if (at > 0 && at < text.size() && text.at(at - 1) != u'\n') {
            const qsizetype blank = text.indexOf(u"\n\n", at);
            const qsizetype crBlank = text.indexOf(u"\n\r\n", at);
            qsizetype end = blank < 0 ? crBlank : crBlank < 0 ? blank : std::min(blank, crBlank);
            at = end < 0 ? text.size() : end;
        }
        // Back before the line breaks there: the summary goes after the text, before the blank.
        while (at > from && (text.at(at - 1) == u'\n' || text.at(at - 1) == u'\r'))
            --at;
        const QStringView before = text.left(at);
        const QStringView rest = text.mid(at);
        QString out;
        if (!before.trimmed().isEmpty())
            out = u"\n\n"_s;
        out += inserted;
        // What follows: a blank line between (the line breaks there now count).
        qsizetype breaks = 0;
        for (qsizetype i = 0; i < rest.size() && (rest.at(i) == u'\n' || rest.at(i) == u'\r'); ++i)
            breaks += rest.at(i) == u'\n' ? 1 : 0;
        if (!rest.trimmed().isEmpty()) {
            for (qsizetype i = breaks; i < 2; ++i)
                out += u'\n';
        } else if (breaks == 0 && !rest.isEmpty()) {
            out += u'\n';
        }
        return {at, at, out};
    }
    }
    return {from, to, inserted};
}

QString apply(QStringView text, const Edit &edit)
{
    const qsizetype from = std::clamp<qsizetype>(edit.from, 0, text.size());
    const qsizetype to = std::clamp<qsizetype>(edit.to, from, text.size());
    return text.left(from).toString() + edit.text + text.mid(to).toString();
}

QList<Change> diff(QStringView before, QStringView after)
{
    const QStringList a = tokens(before);
    const QStringList b = tokens(after);
    // The same start and end need no comparing.
    qsizetype head = 0;
    while (head < a.size() && head < b.size() && a.at(head) == b.at(head))
        ++head;
    qsizetype tail = 0;
    while (tail < a.size() - head && tail < b.size() - head && a.at(a.size() - 1 - tail) == b.at(b.size() - 1 - tail))
        ++tail;
    const qsizetype n = a.size() - head - tail;
    const qsizetype m = b.size() - head - tail;
    QList<Change> raw;
    const auto add = [&raw](Change::Kind kind, const QString &text) {
        if (text.isEmpty())
            return;
        if (!raw.isEmpty() && raw.last().kind == kind)
            raw.last().text += text;
        else
            raw.append({kind, text});
    };
    for (qsizetype i = 0; i < head; ++i)
        add(Change::Same, a.at(i));
    if (n * m > 4'000'000) {
        for (qsizetype i = 0; i < n; ++i)
            add(Change::Removed, a.at(head + i));
        for (qsizetype j = 0; j < m; ++j)
            add(Change::Added, b.at(head + j));
    } else {
        // The longest common subsequence of the middles, from the end.
        std::vector<int> lcs(size_t((n + 1) * (m + 1)), 0);
        const auto at = [&](qsizetype i, qsizetype j) -> int & { return lcs[size_t(i * (m + 1) + j)]; };
        for (qsizetype i = n - 1; i >= 0; --i) {
            for (qsizetype j = m - 1; j >= 0; --j)
                at(i, j) = a.at(head + i) == b.at(head + j) ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));
        }
        qsizetype i = 0, j = 0;
        while (i < n || j < m) {
            if (i < n && j < m && a.at(head + i) == b.at(head + j)) {
                add(Change::Same, a.at(head + i));
                ++i;
                ++j;
            } else if (j < m && (i == n || at(i, j + 1) >= at(i + 1, j))) {
                add(Change::Added, b.at(head + j));
                ++j;
            } else {
                add(Change::Removed, a.at(head + i));
                ++i;
            }
        }
    }
    for (qsizetype i = a.size() - tail; i < a.size(); ++i)
        add(Change::Same, a.at(i));
    // Whitespace alone kept between two changes is part of the change (a sentence rewritten reads
    // as one removal and one addition, not word against word), and in each run of changes the
    // removed text comes before the added.
    QList<Change> out;
    for (qsizetype k = 0; k < raw.size();) {
        if (raw.at(k).kind == Change::Same) {
            out.append(raw.at(k));
            ++k;
            continue;
        }
        QString removed, added;
        qsizetype e = k;
        while (e < raw.size()) {
            const Change &c = raw.at(e);
            if (c.kind == Change::Same) {
                const bool between = e + 1 < raw.size() && raw.at(e + 1).kind != Change::Same;
                if (!between || !c.text.trimmed().isEmpty())
                    break;
                removed += c.text;
                added += c.text;
            } else {
                (c.kind == Change::Removed ? removed : added) += c.text;
            }
            ++e;
        }
        if (!removed.isEmpty())
            out.append({Change::Removed, removed});
        if (!added.isEmpty())
            out.append({Change::Added, added});
        k = e;
    }
    return out;
}

} // namespace brulion::writing
