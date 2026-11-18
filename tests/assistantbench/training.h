#pragma once
// Training data from the bench's runs (docs/FINETUNE.md "Training data"): what the model read and
// wrote in a run the checks passed, token for token as the app's runner has it — the model learns
// its own answers to the prompts the app writes, nothing re-tokenized on the way.

#include "server.h"

#include "ai/ai.h"

#include <QJsonArray>
#include <QList>
#include <QString>

#include <optional>

namespace brulion::assistantbench {

/// One request of a run as the model read and answered it.
struct Turn {
    Tokenized prompt;
    Tokenized answer;      ///< without the token that ended it
    bool complete = false; ///< it ended its turn itself (false: out of tokens)
    bool learnt = true;    ///< false: only read (the model's own steps before a teacher's correction)
};

/// A run's turns as training sequences. One sequence for as long as each prompt goes on from the
/// one before — that prompt, the answer to it and the end of the turn, token for token: the model
/// on the device keeps them so and reads only the rest; a prompt that does not (an earlier turn
/// written in other tokens, a request cut to fit) starts another sequence. A sequence is a JSON
/// array of segments {text, special, train, tokens}, in order: `train` marks the model's own
/// answers, each followed by the end of its turn (`endText`, the token `endToken`), the only parts
/// to learn; an answer cut short (not complete) or not to be learnt is kept as something read.
QList<QJsonArray> trainingSequences(const QList<Turn> &turns, const QString &endText, int endToken);

/// A reply (a task's solution, played) as the model of `format` would have written it after
/// prompt(format, request): the pieces of the assistant's turn as prompt() writes it back into the
/// conversation — its thoughts, its words, its tool calls — up to `endText`, the end of the turn,
/// which is not among them. For a teacher's turns: the training data of a task the model does not
/// do by itself (docs/FINETUNE.md). nullopt with `error` when those pieces do not follow the
/// prompt, or reply() reads them back as another answer.
std::optional<QList<ai::local::Piece>> writtenAnswer(const QString &format, const ai::Request &request,
                                                      const ai::Reply &reply, const QString &endText, QString *error);

} // namespace brulion::assistantbench
