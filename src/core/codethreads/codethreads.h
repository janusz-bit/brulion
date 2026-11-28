// codethreads — a Code mode thread's place in the sidebar, as T3 Code settles and snoozes threads
// (docs/CODE.md "Settling and snoozing"). Pure: a thread page's frontmatter in, its state out, and
// edits of it as content in → content out.
//
// A thread page (docs/SYNTAX.md §14, docs/CODE.md "Threads") says it in its frontmatter, the keys the
// same in every language (as `project:`), the times local wall-clock time ("yyyy-MM-dd HH:mm"):
//
//   ---
//   project: sklep
//   settled: 2026-11-28 10:15        ← the task is done for now: the "Settled" section
//   snoozed: 2026-11-28 18:00        ← hidden in "Snoozed" until then
//   auto-settle: never               ← never settled by itself
//   ---
//
// The page is the notebook's, synced: every device of the user's shows the thread where the others
// put it. A new message of the thread takes `settled:` and `snoozed:` away (reactivated()); a time of
// `snoozed:` that passed is no snooze.
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::codethreads {

/// What a thread page's frontmatter says of its place.
struct State {
    /// When it was settled (invalid: it is not).
    QDateTime settled;
    /// Until when it is snoozed (invalid: it is not; a time that passed is kept as written).
    QDateTime snoozedUntil;
    /// `auto-settle: never`: inactivity and a merged pull request never settle it.
    bool neverAutoSettle = false;
    friend bool operator==(const State &, const State &) = default;
};

/// The frontmatter keys.
inline constexpr QStringView SettledKey = u"settled";
inline constexpr QStringView SnoozedKey = u"snoozed";
inline constexpr QStringView AutoSettleKey = u"auto-settle";

/// The state `content`'s frontmatter gives (none for a page without one; a time that does not read
/// is none).
State readState(QStringView content);

/// `content` settled at `at` (its snooze taken away) / not settled. The rest byte for byte.
QString settle(const QString &content, const QDateTime &at);
QString unsettle(const QString &content);
/// `content` snoozed until `until` (not settled any more) / woken. An invalid `until` wakes it.
QString snooze(const QString &content, const QDateTime &until);
QString wake(const QString &content);
/// `content` with `auto-settle: never` (`never`) or without it.
QString setNeverAutoSettle(const QString &content, bool never);
/// `content` after a new message: neither settled nor snoozed (T3 Code: "any new message
/// reactivates it"). Unchanged when it was neither.
QString reactivated(const QString &content);

/// The sidebar's sections, top to bottom.
enum class Section { Pinned, Active, Snoozed, Settled, Archived };

/// What the app knows of a thread beyond its page.
struct Facts {
    bool pinned = false;
    bool archived = false;
    /// It waits for the user (a question, an approval) or its turn failed: a snooze ends early.
    bool needsUser = false;
};

/// Where the sidebar lists a thread at `now`: archived ones last; a snoozed one (until a time still
/// ahead) in Snoozed unless it needs the user; a settled one in Settled; pinned ones on top; the rest
/// Active.
Section sectionOf(const State &state, const Facts &facts, const QDateTime &now);
/// "pinned", "active", "snoozed", "settled", "archived" (the model's `section` role).
QString sectionName(Section section);

/// The time a snooze preset ends, from `now`: "1h", "3h" (hours later), "evening" (18:00 today, else
/// tomorrow), "tomorrow" (9:00 tomorrow), "monday" (9:00 next Monday); else the text read as a date
/// in words or digits in Polish or English ("jutro o 18", "in 2 hours", "2026-12-01 9:00"; a day
/// without a time is 9:00 of it). None when it names no time ahead of `now`.
std::optional<QDateTime> snoozeEnd(QStringView preset, const QDateTime &now);

/// A thread settles by itself (T3 Code: after `days` of inactivity, default 7; 0 never) when its last
/// activity is at least `days` before `now`, it is not settled, archived or `auto-settle: never`, and
/// nothing holds it (`busy`: a turn runs, it asks, a message is queued, a sub-conversation will wake
/// it, it is shown).
bool settlesFromInactivity(const State &state, const Facts &facts, const QDateTime &lastActivity,
                           const QDateTime &now, int days, bool busy);

/// What a thread's pull request is, as far as settling and cleaning up go.
enum class PullRequest { None, Open, Merged, Closed };
/// gitcli's PullRequestInfo::state ("open", "draft", "merged", "closed"; "" none).
PullRequest pullRequestOf(QStringView state);

/// A thread settles by itself when its pull request was merged or closed (T3 Code's second rule),
/// unless it is settled, archived, `auto-settle: never` or held (`busy`, as settlesFromInactivity()).
bool settlesFromPullRequest(const State &state, const Facts &facts, PullRequest pr, bool busy);

/// When a settled thread's worktree is removed by itself (Settings → Code mode → "Remove the worktrees
/// of settled threads"): never (`Never`), once its pull request is merged (`Merged`), or also `days`
/// after it settled.
struct Cleanup {
    enum class When { Never, Merged, Days };
    When when = When::Never;
    int days = 0;
};
/// The setting's value read: "never", "merged", or the days ("7", "30"); anything else never.
Cleanup cleanupOf(QStringView setting);

/// The worktree of a settled thread goes (`git worktree remove`, without --force: its branch and
/// commits stay) when the cleanup says so, it has no uncommitted change (`changes` 0 — untracked
/// files count), and no thread that is not settled works in it (`othersActive`).
bool removesWorktree(const State &state, PullRequest pr, const Cleanup &cleanup, const QDateTime &now,
                     int changes, bool othersActive);

/// The local "yyyy-MM-dd HH:mm" of `time` (what the keys hold).
QString formatTime(const QDateTime &time);
/// A key's value read: "yyyy-MM-dd HH:mm", "yyyy-MM-ddTHH:mm[:ss]", a bare date (00:00); quotes
/// around it allowed. Invalid for anything else.
QDateTime parseTime(QStringView value);

} // namespace brulion::codethreads
