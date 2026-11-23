#pragma once
// The tool calls of an AI agent's turn in words (docs/UI.md "AI agents (ACP)"; T3 Code's work log):
// the summary line of ChatActivity, which opens the list of calls, and the answer a turn of tool
// calls without words leaves in its conversation.
//
//   toolSummary({{"read", "completed"}, {"read", "completed"}, {"execute", "failed"}})
//   // "Read 2 files · 1 step failed" — „Przeczytano 2 pliki · 1 krok się nie powiódł”
//   toolsAnswer(steps, {"Read Dom.md", "Edit Dom.md"})
//   // "Read 1 file · edited 1 file\n\n- Read Dom.md\n- Edit Dom.md"
//
// A part per kind of call (ACP's ToolKind: read, edit, delete, move, search, execute, fetch; think
// and the rest are "other steps"), in the order the turn first did them, the first three named and
// the rest counted as other steps; the failed ones counted apart, not as done. Plural forms through
// the translation (`%n`, context "ChatTools": Polish has three). Single-threaded (the GUI thread).

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace brulion::shell {

/// A tool call as the summary counts it: its ACP kind and status ("pending", "in_progress",
/// "completed", "failed").
struct ToolStep {
    QString kind;
    QString status;
};

/// The calls in words: "Read 3 files · ran 2 commands · edited 1 file"; "" for none.
QString toolSummary(const QList<ToolStep> &steps);
/// The same of ChatTurn.tools' rows ({kind, status, …}).
QString toolSummary(const QVariantList &tools);
/// The answer of a turn of tool calls without words: the summary and the titles as a list ("Done."
/// without any calls).
QString toolsAnswer(const QList<ToolStep> &steps, const QStringList &titles);

} // namespace brulion::shell
