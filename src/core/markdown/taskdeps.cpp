#include "markdown/taskdeps.h"

#include "markdown/inline.h"
#include "markdown/parse.h"
#include "markdown/tokens.h"
#include "text/text.h"

#include <QRandomGenerator>

#include <algorithm>
#include <optional>

namespace brulion::markdown {

using namespace Qt::StringLiterals;

namespace {

bool isIdChar(QChar c)
{
    const char16_t u = c.unicode();
    return (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') || (u >= u'0' && u <= u'9') || u == u'_' || u == u'-';
}

/// Tokens that Obsidian Tasks writes after the description (Brulion's reminders too): a new 🆔
/// or ⛔ goes before the first of them.
bool isLayoutToken(TokenKind kind)
{
    switch (kind) {
    case TokenKind::LogseqPriority:
    case TokenKind::Planning: return false;
    default: return true;
    }
}

/// The task's first line in the file's lines, nullopt when `line` holds no task.
std::optional<int> taskFirstLine(const QString &content, int line)
{
    const ParsedNote note = parseNote(VaultPath(), content);
    const int b = blockAtLine(note, line);
    if (b < 0 || note.blocks[b].kind != BlockKind::ListItem || !note.blocks[b].task)
        return std::nullopt;
    return note.blocks[b].line;
}

/// Rewrite the task's first line with `edit` (line in, line out).
template<typename Edit>
QString editFirstLine(const QString &content, int line, Edit edit)
{
    const auto first = taskFirstLine(content, line);
    if (!first)
        return content;
    QList<text::Line> lines = text::splitLines(content);
    if (*first >= lines.size())
        return content;
    const QString before = lines[*first].text;
    const QString after = edit(before);
    if (after == before)
        return content;
    lines[*first].text = after;
    return text::joinLines(lines);
}

/// Where a new token goes in `line` (tokens found in its masked text): before the first layout
/// token other than `skip` ("🆔 x " inserted there), else at the end before a block id and
/// trailing blanks (" 🆔 x" there). `atToken` tells which.
qsizetype insertionPoint(const QString &line, const QList<Token> &tokens, TokenKind skip, bool *atToken)
{
    for (const Token &t : tokens) {
        if (isLayoutToken(t.kind) && t.kind != skip) {
            *atToken = true;
            return t.from;
        }
    }
    *atToken = false;
    qsizetype at = line.size();
    if (const auto id = blockIdAtEnd(line))
        at = id->from;
    while (at > 0 && (line[at - 1] == u' ' || line[at - 1] == u'\t'))
        --at;
    return at;
}

QString insertToken(const QString &line, const QList<Token> &tokens, TokenKind skip, const QString &token)
{
    bool atToken = false;
    const qsizetype at = insertionPoint(line, tokens, skip, &atToken);
    if (atToken)
        return line.first(at) + token + u' ' + line.sliced(at);
    return line.first(at) + u' ' + token + line.sliced(at);
}

/// `line` without [from, to) and the blanks before it (one adjacent space).
QString removeRange(const QString &line, qsizetype from, qsizetype to)
{
    qsizetype f = from;
    while (f > 0 && (line[f - 1] == u' ' || line[f - 1] == u'\t'))
        --f;
    if (f == 0) {
        // At the start: the blanks after it go instead.
        f = from;
        while (to < line.size() && (line[to] == u' ' || line[to] == u'\t'))
            ++to;
    }
    return line.first(f) + line.sliced(to);
}

QStringList idsOf(QStringView value)
{
    QStringList out;
    for (QStringView part : value.split(u',')) {
        part = part.trimmed();
        if (!part.isEmpty() && !out.contains(part.toString()))
            out.append(part.toString());
    }
    return out;
}

QList<Token> lineTokens(const QString &line)
{
    return findTokens(maskInlineCode(line));
}

} // namespace

bool isValidTaskId(QStringView id)
{
    return !id.isEmpty() && std::ranges::all_of(id, isIdChar);
}

QString generateTaskId(const std::function<bool(const QString &)> &isTaken)
{
    static constexpr char16_t Alphabet[] = u"abcdefghijklmnopqrstuvwxyz0123456789";
    for (int attempt = 0;; ++attempt) {
        const int length = 6 + attempt / 100; // longer ids when the short ones run out
        QString id;
        for (int k = 0; k < length; ++k)
            id += QChar(Alphabet[QRandomGenerator::global()->bounded(36)]);
        if (!isTaken || !isTaken(id))
            return id;
    }
}

QString setTaskId(const QString &content, int line, const QString &id)
{
    if (!id.isEmpty() && !isValidTaskId(id))
        return content;
    return editFirstLine(content, line, [&](const QString &text) -> QString {
        const QList<Token> tokens = lineTokens(text);
        for (const Token &t : tokens) {
            if (t.kind != TokenKind::TaskId)
                continue;
            if (id.isEmpty())
                return removeRange(text, t.from, t.to);
            return text.first(t.valueFrom) + id + text.sliced(t.valueTo);
        }
        if (id.isEmpty())
            return text;
        return insertToken(text, tokens, TokenKind::TaskId, tokens::TaskId.toString() + u' ' + id);
    });
}

QString addTaskDependency(const QString &content, int line, const QString &id)
{
    if (!isValidTaskId(id))
        return content;
    return editFirstLine(content, line, [&](const QString &text) -> QString {
        const QList<Token> tokens = lineTokens(text);
        const Token *deps = nullptr;
        const Token *own = nullptr;
        for (const Token &t : tokens) {
            if (t.kind == TokenKind::DependsOn) {
                if (idsOf(t.value).contains(id))
                    return text;
                if (!deps)
                    deps = &t;
            } else if (t.kind == TokenKind::TaskId && !own) {
                own = &t;
            }
        }
        if (deps)
            return text.first(deps->valueTo) + u',' + id + text.sliced(deps->valueTo);
        const QString token = tokens::DependsOn.toString() + u' ' + id;
        if (own)
            return text.first(own->to) + u' ' + token + text.sliced(own->to);
        return insertToken(text, tokens, TokenKind::DependsOn, token);
    });
}

QString removeTaskDependency(const QString &content, int line, const QString &id)
{
    return editFirstLine(content, line, [&](const QString &text) -> QString {
        const QList<Token> tokens = lineTokens(text);
        QString out = text;
        // Back to front: earlier offsets stay valid.
        for (qsizetype k = tokens.size() - 1; k >= 0; --k) {
            const Token &t = tokens[k];
            if (t.kind != TokenKind::DependsOn)
                continue;
            QStringList ids = idsOf(t.value);
            if (!ids.removeAll(id))
                continue;
            if (ids.isEmpty())
                out = removeRange(out, t.from, t.to);
            else
                out = out.first(t.valueFrom) + ids.join(u',') + out.sliced(t.valueTo);
        }
        return out;
    });
}

SubtaskProgress subtaskProgress(const ParsedNote &note, int blockIndex)
{
    SubtaskProgress out;
    if (blockIndex < 0 || blockIndex >= note.blocks.size())
        return out;
    QList<int> stack = note.blocks[blockIndex].children;
    while (!stack.isEmpty()) {
        const int i = stack.takeLast();
        if (i < 0 || i >= note.blocks.size())
            continue;
        const Block &b = note.blocks[i];
        if (b.task) {
            if (b.task->status == TaskStatus::Done)
                ++out.done;
            if (b.task->status != TaskStatus::Cancelled)
                ++out.total;
        }
        stack.append(b.children);
    }
    return out;
}

} // namespace brulion::markdown
