// Helpers shared by the command implementations of brulion-cli (commands_*.cpp).
#pragma once

#include "cli_p.h"
#include "notebook/pages.h"

#include <QHash>

namespace brulion::cli {

// ---- Building the registry
// Summaries and help are English texts marked for translation in the context "brulion::cli"
// (QT_TRANSLATE_NOOP, cliText()); see Command.
Arg positional(const QString &name, ArgType type, const char *help, bool required = true, bool rest = false);
Arg option(const QString &name, ArgType type, const char *help, bool required = false);
Arg flag(const QString &name, const char *help);
Arg choice(const QString &name, const QStringList &values, const char *help, bool positional = false,
           bool required = false);
/// `arg` that `--stdin` can provide.
Arg stdinable(Arg arg);

// ---- Results
/// A page / folder operation's problem as an error (codes: not_found, name_required, into_itself,
/// hidden_name, not_a_folder, target_exists, io_error).
Error problemError(const notebook::PathProblem &problem);
Result failWith(const Error &error);

// ---- Notes
/// The block `block` (blockFromText()) placed under a heading ("## Zakupy", "Zakupy") or a block
/// ("^id", "((id))") of `content`: as the last item of the heading's section, or the last child
/// of the block. `insertedLine` receives the new block's first line. nullopt + call.error when
/// the heading or block is not there.
std::optional<QString> insertUnder(Call &call, const QString &content, const QString &under, const QString &block,
                                   int *insertedLine);
/// Write options for an edit of `path` that may create it: a missing daily note starts from the
/// journal template filled in for its day — as ensureJournal(), and so the app, creates it — in a
/// real run and a dry run alike (the dry run's diff is what gets written). Other pages must exist.
notebook::Notebook::WriteOptions creatingJournal(Call &call, const VaultPath &path);
/// The first line of `block` appended at the end of `after` (appendBlock()).
int appendedLine(const QString &after, const QString &block);
/// The text of `content`'s 0-based line `line` ("" when out of range).
QString lineText(const QString &content, int line);
/// Links and tags naming `path` (the merged backlinks of the app's "Odnośniki"): [{kind: "link" |
/// "tag", path, title, line, text, blockId?, journal, tag?, ref?}].
QJsonArray backlinksJson(Call &call, const VaultPath &path);
/// "ref" (line ref) added to every item of a query result's items/groups that has a path and line.
void addRefs(Call &call, QJsonObject &queryResult);
/// The page text cache of addRefs() and friends: a note's current text by path.
QString noteText(Call &call, const VaultPath &path);
/// One line of a task, as the app writes it: "- [ ] text 🔺 🛫 … ⏳ … 📅 … ⏰ … 🔁 …".
QString cleanItemText(const QString &text);

} // namespace brulion::cli
