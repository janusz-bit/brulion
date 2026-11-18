#pragma once
// A model on the device itself (the provider "local", docs/EXTENSIONS.md "AI"): the same b.ai
// requests answered by a small model that runs inside the app — no key, no server, no network once
// its file is downloaded. This header is the part without a model runner, built and tested
// everywhere: which models there are (their files, sizes, licenses), how a request is written for
// one (its chat format, as pieces of text and control tokens) and how its answer is read (the
// text, the tools it calls). The runner (ai/localrunner.h: llama.cpp, library brulion_ai_llama,
// only in builds that have it) turns the pieces into tokens and back.
//
//   const ai::LocalModel *model = ai::localModel(settings.model);
//   ai::local::Prompt prompt = ai::local::prompt(model->format, request);   // → the runner
//   ai::Reply reply = ai::local::reply(model->format, generated, request, /*complete*/ true);
//
// Notes come from anywhere (imports, sync), and a request carries them: what a note says must
// never read as the conversation's structure. So a prompt is pieces, not one text: a control token
// (the end of a turn, the start of a tool call) is a piece of its own, marked `special`, and
// everything else — the user's words, a note, a tool's result — is text in which the same letters
// are only letters. The runner keeps the two apart, in the prompt and in what the model writes.
// A picture is a piece too (a model with a projector sees it; ai::Image: written by the app).

#include "ai/ai.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace brulion::ai {

/// A file of a model, downloaded into the app's models folder.
struct LocalFile {
    QString name;      ///< its name there ("": no such file)
    QUrl url;          ///< where it comes from: one revision, so the file never changes
    QByteArray sha256; ///< of the whole file, hex — checked after the download
    qint64 bytes = 0;
    friend bool operator==(const LocalFile &, const LocalFile &) = default;
};

/// A model the app can download and run on the device. A file may be another model's too (a model
/// adapted to the app and the same model as its maker gives it): it is downloaded once and stays
/// while either of them is on the device (LocalModelFiles::remove). Two models share a file only
/// as the identical LocalFile — name, url, checksum and size (tst_ai_local checks the models the
/// app knows): a name is a file's place in the models folder, so two different files never have one.
struct LocalModel {
    QString id;          ///< the settings' and a reply's name for it: "lfm3-vl-2b"
    QString name;        ///< "LFM3-VL 2B"
    /// What the settings' chooser shows of it, short enough for all the models side by side on a
    /// narrow phone ("": its name).
    QString label;
    /// Who made it ("Liquid AI"); of an adapted model, who trained the adapter ("Brulion") — the
    /// model under it is `baseName` by `baseMaker`, whose licence it keeps.
    QString maker;
    QString format;      ///< how its prompts are written (local::prompt): "lfm3"
    LocalFile file;      ///< the model itself (GGUF)
    /// What was learnt on top of it: a LoRA adapter (GGUF) the runner adds to the weights of
    /// `file`, which stays as it is (a 4-bit file trained for that precision keeps it); none: the
    /// model as its maker gives it.
    LocalFile adapter;
    /// An adapted model's own: the model `file` is, by name ("LFM3 1.7B"), and who made it
    /// ("Liquid AI"). Empty for a model as its maker gives it.
    QString baseName;
    QString baseMaker;
    LocalFile projector; ///< what lets it see pictures (GGUF); none: it reads text only
    QString license;     ///< the license's name (not a free-software one: the user downloads the files)
    QUrl licenseUrl;
    QUrl pageUrl; ///< the maker's page about the model (of an adapted one: about the adapter)
    /// It may think before it answers, when the request lets it (local::thinks: an effort other
    /// than "low"); false: it always answers at once — it was trained to, and every request it
    /// gets is taken as one of the effort "low".
    bool thinks = true;

    bool adapted() const { return !adapter.name.isEmpty(); }
    bool sees() const { return !projector.name.isEmpty(); }
    /// Its files, the model first.
    QList<LocalFile> files() const
    {
        QList<LocalFile> list{file};
        if (adapted())
            list.append(adapter);
        if (sees())
            list.append(projector);
        return list;
    }
    qint64 bytes() const { return file.bytes + adapter.bytes + projector.bytes; }

    /// The maker's sampling: a low temperature for answers that follow the notes.
    float temperature = 0.1f;
    int topK = 50;
    float repeatPenalty = 1.1f;
    /// Where the sampling starts: the same request gets the same answer.
    quint32 seed = 0xB2011017;
    /// The most tokens of one answer, thoughts included: a small model that loses its thread
    /// writes on and on, and on a phone every hundred tokens are seconds.
    int answerTokens = 2048;
};

/// The models the app knows, the default first.
const QList<LocalModel> &localModels();
/// The model `id` names ("": the default); nullptr for an unknown one.
const LocalModel *localModel(const QString &id);

/// What answers a request of the provider "local": the app's model runner (src/app/shell/localai.h)
/// in builds that have one. `done` is called once, from the event loop; `partial` (when given)
/// gets the answer while the model writes it, from the event loop; `stopped` set (Pending::stop,
/// or the backend's own: a model removed) ends the request — a model writing it stops, one still
/// waiting is never asked.
class LocalBackend {
public:
    virtual ~LocalBackend() = default;
    virtual void send(const Settings &settings, const Request &request, std::function<void(const Reply &)> done,
                      PartialCallback partial, std::shared_ptr<std::atomic<bool>> stopped) = 0;
};

namespace local {

/// A run of a prompt or of an answer: one of the model's control tokens (`special`: exactly one
/// token, e.g. "<|im_end|>"), plain text, in which nothing is a control token, or a picture (in a
/// prompt: `image` has its bytes and the text is empty).
struct Piece {
    QString text;
    bool special = false;
    QByteArray image;
    friend bool operator==(const Piece &, const Piece &) = default;
};

struct Prompt {
    QList<Piece> pieces;
    /// Pieces [0, stable) are the system text and the tools: the same for every turn of a
    /// conversation and for the next question, so a runner keeps the model's state after them.
    int stable = 0;
    /// The model thinks before it answers: its answer begins inside its thoughts.
    bool thinking = false;
    /// Control tokens that end the answer besides the model's own end of turn: a model that
    /// starts another turn by itself has left its own.
    QStringList stops;
};

/// Is `format` one this build can write ("lfm3")?
bool knowsFormat(const QString &format);

/// Does the model think before answering this request? Unless its effort is "low".
bool thinks(const Request &request);

/// `request` as the model's prompt, ending where its answer starts. Texts go in unchanged: fitting
/// them into the model's context is shorten()'s job. Pictures become pieces before their
/// message's text (a model that cannot see them: the caller refuses the request first).
Prompt prompt(const QString &format, const Request &request);

/// Does `request` carry a picture?
bool hasImages(const Request &request);

/// The model's answer from what it generated after prompt(…) of the same request, without the
/// token that ended its turn. `complete`: it ended the turn itself (false: it ran out of tokens,
/// so a half-written tool call is dropped and the stop reason is "length"). The reply's raw is an
/// OpenAI-style assistant message — content, reasoning_content, tool_calls with the arguments in
/// the order and the words the model wrote — which prompt() writes back exactly.
Reply reply(const QString &format, const QList<Piece> &generated, const Request &request, bool complete);

/// What the model has written so far of its answer (after prompt(…) of `request`): its words
/// and its thoughts as reply() would read them, without a tool call it is writing.
Partial partial(const QString &format, const QList<Piece> &generated, const Request &request);

/// Takes about `chars` characters out of `request` so that it fits a small model's context: from
/// its longest texts first — a tool's result loses its end, a message its middle (its start and
/// its question stay) — each marked as cut. Returns how many characters went (0: nothing left to
/// cut; the system text, the tools and the model's own turns are never cut).
int shorten(Request *request, int chars);

} // namespace local

} // namespace brulion::ai
