// Code mode's texts written by the AI (docs/CODE.md, docs/CODE-GIT.md), as T3 Code writes them: a
// commit message from the changes, a pull request's title and description from the branch's
// commits and its diff against the base, a new worktree's branch name from the thread's first
// message, and a thread's title from its messages ("Regenerate title"). GUI-free and pure: the
// questions as ai::Request, the answers read into what the app uses. The app sends the requests
// (AiAsk, WorkspaceExtensions::sendAi: the AI of Settings → Extensions → AI) and checks what the
// readers give back against its own rules (a branch name's prefix and git's rules: CodeProjects).
//
//   const ai::Request request = codeai::pullRequest({base, head, commits, stat, patch});
//   const codeai::PullRequestText text = codeai::readPullRequest(reply.text);   // title "" = none
//
// What goes to the model is bounded (bounded(): a long diff cut, never between the halves of a
// character), and what comes back is data: every reader takes one line or field, never more.
#pragma once

#include "ai/ai.h"
#include "chat/chat.h"

#include <QString>
#include <QStringView>

namespace brulion::codeai {

/// `text` cut to at most `max` characters (never between the two halves of a surrogate pair), and
/// then "\n[truncated]" when it was longer.
QString bounded(QStringView text, qsizetype max);

// ---- A commit message ------------------------------------------------------------------------

/// The most characters of a diff a question sends (a commit's changes, a branch's diff).
inline constexpr qsizetype MaxPatch = 60000;

/// The commit message for `patch` (the checked files' changes as CodeGit::requestPatch gives
/// them) on `branch` ("" a detached HEAD): the subject line, a blank line and a body when needed,
/// plain text in the language of the changes.
ai::Request commitMessage(QStringView branch, QStringView patch);

// ---- A pull request --------------------------------------------------------------------------

/// What a branch has that its base has not.
struct BranchRange {
    QString base;    ///< the base compared with ("origin/main")
    QString head;    ///< the branch
    QString commits; ///< `git log base..HEAD` (a line per commit, its message's body under it)
    QString stat;    ///< `git diff --stat base...HEAD`
    QString patch;   ///< `git diff base...HEAD`
};
inline constexpr qsizetype MaxCommits = 12000;
inline constexpr qsizetype MaxStat = 6000;

/// The title and description of a pull request of `range`: the title on the first line, then the
/// description in Markdown ("## Summary", "## Testing") — the commits and the diff as data.
ai::Request pullRequest(const BranchRange &range);

struct PullRequestText {
    QString title; ///< one line; "" when the answer gives none
    QString body;  ///< Markdown, trimmed ("" none)
    friend bool operator==(const PullRequestText &, const PullRequestText &) = default;
};
/// The answer to pullRequest(): its first line the title (a heading's "#", a "Title:" label,
/// quotes and a trailing period taken off), the rest the description; a JSON object {title, body}
/// read as such; a fence around all of it taken off.
PullRequestText readPullRequest(QStringView answer);

// ---- A branch name ---------------------------------------------------------------------------

/// The longest name readBranchName() takes (more is a sentence, not a name).
inline constexpr qsizetype MaxBranchChars = 64;
/// The most characters of the thread's first message a question sends.
inline constexpr qsizetype MaxMessage = 8000;

/// A short branch name for the work `message` asks for (a new worktree thread's first message):
/// a few lowercase words joined by hyphens, without a prefix — the app adds its own.
ai::Request branchName(QStringView message);

/// The name the answer to branchName() gives: its first line or a JSON object's `branch`, with a
/// fence, quotes, a label ("Branch: …") and a prefix ("feature/…") taken off; "" when it gives none
/// or more than a name (over MaxBranchChars). Not yet a git branch name: the app makes one of it.
QString readBranchName(QStringView answer);

// ---- A thread's title ------------------------------------------------------------------------

inline constexpr qsizetype MaxTitleContext = 8000;
inline constexpr qsizetype MaxTitleMessage = 2000;
/// The longest title readTitle() gives (a conversation page's name: chat::pageTitle's).
inline constexpr qsizetype MaxTitleChars = 60;

/// The thread's messages for its title, at most MaxTitleContext characters: "USER:" and
/// "ASSISTANT:" sections in their order — its first user message, then the newest user messages,
/// then the newest answers, each at most MaxTitleMessage (a longer one keeps its start and end) —,
/// pictures left out, "[Earlier content truncated]" first when something was left out or cut; ""
/// for a conversation without words.
QString titleContext(const chat::Conversation &conversation);

/// A title for the thread whose messages are `context` (titleContext()), named `previousTitle`
/// now, of the project `project` (which the title leaves out): kept when it is accurate, else a
/// better one — the subject and the outcome, in the language of the user's messages.
ai::Request threadTitle(QStringView previousTitle, QStringView project, QStringView context);

/// The title the answer to threadTitle() gives: its first line or a JSON object's `title`, with a
/// fence, a heading's "#", a label, quotes and trailing punctuation taken off, its spaces made
/// single, at most MaxTitleChars (cut at a word); "" none.
QString readTitle(QStringView answer);

} // namespace brulion::codeai
