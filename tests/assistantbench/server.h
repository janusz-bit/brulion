#pragma once
// The model's arithmetic on a llama-server (llama.cpp's own server — on a GPU, many conversations
// at once) for the bench's long runs: the attempts training data is made of (docs/FINETUNE.md).
// Everything else stays the app's: ai::answerLocally() writes the prompt (local::prompt), cuts a
// request that is too long (local::shorten) and reads the answer (local::reply); this runner only
// turns the prompt's pieces into tokens and lets the server write the answer — as the app's runner
// (ai/llamarunner.cpp) does on the device:
//
//   * a control piece is its one token; a text is tokenized with no control token read in it, cut
//     after the first letter of every place that spells one (llama.cpp still reads its
//     user-defined tokens, the tool tags, inside plain text);
//   * the same sampling: penalties for repeating over the last 64 tokens, top-k, temperature, the
//     request's seed — nothing else;
//   * the answer ends with the model's end of turn or a control token that starts another turn
//     (Prompt::stops), as there; its tokens are pieces of text and control tokens again.
//
// The server holds the whole context for every conversation (-c <slots × context>, -np <slots>):
//
//   llama-server -m model.gguf -c 131072 -np 8 -ngl 99 -fa on --special --no-context-shift --port 8091
//   assistantbench::ServerRunner runner(QUrl(u"http://127.0.0.1:8091"_s), vocabulary, 16384);
//
// `--special` is needed: with it the server writes control tokens as their text, so that a turn
// ends at Prompt::stops. One runner is used from one thread at a time (each its own connection).

#include "ai/localrunner.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

#include <memory>
#include <optional>

class QNetworkAccessManager;

namespace brulion::assistantbench {

/// A model's vocabulary as llama.cpp has it from the model's file (GGUF: tokenizer.ggml.tokens,
/// tokenizer.ggml.token_type), for a byte-level BPE tokenizer ("gpt2", as LFM3's).
struct Vocabulary {
    /// The control tokens — control or user-defined (token_type 3, 4) — whose text has two bytes
    /// or more: text → token. What llama.cpp's runner keeps apart from plain text.
    QHash<QString, int> controls;
    /// What each token writes: its bytes (a control token: its text; an unused one: nothing).
    QList<QByteArray> bytes;
};

/// nullopt with `error` for a file that is not a model's, or one of another kind of tokenizer.
std::optional<Vocabulary> readVocabulary(const QString &ggufFile, QString *error);

/// A prompt or an answer as the model read or wrote it: its pieces and the tokens of each.
struct Tokenized {
    QList<ai::local::Piece> pieces;
    QList<QList<int>> tokens; ///< of each piece
    QList<int> all() const;
};

class ServerRunner : public ai::LocalRunner {
public:
    ServerRunner(QUrl url, std::shared_ptr<const Vocabulary> vocabulary, int contextTokens);
    ~ServerRunner() override;

    int contextTokens() const override { return m_contextTokens; }
    bool sees() const override { return false; }
    QString device() const override;
    Result generate(const ai::local::Prompt &prompt, const Params &params) override;

    /// Pieces as the runner gives them to the model: a control piece its one token, a text its
    /// tokens with no control token read in it. nullopt with `error` (a picture, a control token
    /// the vocabulary lacks, the server not answering).
    std::optional<Tokenized> tokenize(const QList<ai::local::Piece> &pieces, QString *error);

    /// Of the last generate(), when the model answered (status Ok; else empty): the prompt and the
    /// answer as tokens — the answer without the token that ended it.
    const Tokenized &lastPrompt() const { return m_lastPrompt; }
    const Tokenized &lastAnswer() const { return m_lastAnswer; }

private:
    std::optional<QJsonObject> post(const QString &path, const QJsonObject &body, int timeoutMs, QString *error);
    bool tokenizeText(const QString &text, QList<int> *out, QString *error);

    QUrl m_url;
    std::shared_ptr<const Vocabulary> m_vocabulary;
    QHash<int, QString> m_controlTexts;   ///< token → text
    QList<QByteArray> m_controlSpellings; ///< their texts, UTF-8
    int m_contextTokens;
    std::unique_ptr<QNetworkAccessManager> m_network;
    QHash<QString, QList<int>> m_known; ///< texts tokenized before (the system text, the tools)
    Tokenized m_lastPrompt;
    Tokenized m_lastAnswer;
};

} // namespace brulion::assistantbench
