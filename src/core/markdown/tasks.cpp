#include "markdown/tasks.h"

#include "markdown/lines_p.h"

#include <array>

namespace brulion::markdown {

using namespace detail;
using namespace Qt::StringLiterals;

namespace {

struct Keyword {
    QStringView word;
    TaskStatus status;
};

// Longer spellings first so that a prefix never shadows a longer keyword.
constexpr std::array<Keyword, 10> Keywords = {{
        {u"IN-PROGRESS", TaskStatus::Doing},
        {u"CANCELLED", TaskStatus::Cancelled},
        {u"CANCELED", TaskStatus::Cancelled},
        {u"WAITING", TaskStatus::Deferred},
        {u"LATER", TaskStatus::Todo},
        {u"DOING", TaskStatus::Doing},
        {u"TODO", TaskStatus::Todo},
        {u"DONE", TaskStatus::Done},
        {u"WAIT", TaskStatus::Deferred},
        {u"NOW", TaskStatus::Doing},
}};

} // namespace

TaskStatus statusFromCheckbox(QChar ch)
{
    switch (ch.unicode()) {
    case u'x':
    case u'X': return TaskStatus::Done;
    case u'/': return TaskStatus::Doing;
    case u'-': return TaskStatus::Cancelled;
    case u'>': return TaskStatus::Deferred;
    default: return TaskStatus::Todo;
    }
}

std::optional<TaskStatus> statusFromKeyword(QStringView word)
{
    for (const Keyword &k : Keywords) {
        if (word == k.word)
            return k.status;
    }
    return std::nullopt;
}

QChar statusChar(TaskStatus status)
{
    switch (status) {
    case TaskStatus::Todo: return u' ';
    case TaskStatus::Doing: return u'/';
    case TaskStatus::Done: return u'x';
    case TaskStatus::Cancelled: return u'-';
    case TaskStatus::Deferred: return u'>';
    }
    return u' ';
}

QChar checkboxCharFor(TaskStatus status, std::optional<QChar> previous)
{
    if (previous && statusFromCheckbox(*previous) == status)
        return *previous;
    return statusChar(status);
}

QString keywordFor(TaskStatus status, QStringView previous)
{
    if (!previous.isEmpty() && statusFromKeyword(previous) == status)
        return previous.toString();
    const bool nowLater = previous == u"LATER" || previous == u"NOW";
    switch (status) {
    case TaskStatus::Todo: return nowLater ? u"LATER"_s : u"TODO"_s;
    case TaskStatus::Doing: return nowLater ? u"NOW"_s : u"DOING"_s;
    case TaskStatus::Done: return u"DONE"_s;
    case TaskStatus::Cancelled: return previous == u"CANCELLED" ? u"CANCELLED"_s : u"CANCELED"_s;
    case TaskStatus::Deferred: return previous == u"WAIT" ? u"WAIT"_s : u"WAITING"_s;
    }
    return u"TODO"_s;
}

std::optional<TaskMarker> matchTaskMarker(QStringView text, QStringView masked, qsizetype start)
{
    if (start < 0 || start > masked.size())
        return std::nullopt;
    const QStringView rest = masked.sliced(start);
    TaskMarker out;
    qsizetype len = 0;
    // \[([^\]\r\n])\](?=[ \t]|$)
    if (rest.size() >= 3 && rest[0] == u'[' && rest[1] != u']' && !isLineBreak(rest[1]) && rest[2] == u']'
        && (rest.size() == 3 || isSpaceOrTab(rest[3]))) {
        out.kind = TaskMarker::Kind::Checkbox;
        out.marker = QString(charAt(text, start + 1));
        out.status = statusFromCheckbox(charAt(text, start + 1));
        len = 3;
    } else {
        const Keyword *found = nullptr;
        for (const Keyword &k : Keywords) {
            if (rest.startsWith(k.word) && (rest.size() == k.word.size() || isSpaceOrTab(rest[k.word.size()]))) {
                found = &k;
                break;
            }
        }
        if (!found)
            return std::nullopt;
        out.kind = TaskMarker::Kind::Keyword;
        out.marker = found->word.toString();
        out.status = found->status;
        len = found->word.size();
    }
    out.from = start;
    out.to = start + len;
    // The whitespace of the text: in `masked` a code span, formula or comment right after the
    // marker is blanked too, and would be taken for it ("- [ ] `npm i` w katalogu" lost its code
    // in the index, in rendered task lines, in Logseq exports and to "no status").
    qsizetype content = out.to;
    while (isSpaceOrTab(charAt(text, content)))
        ++content;
    out.contentStart = content;
    return out;
}

} // namespace brulion::markdown
