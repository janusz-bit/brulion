#pragma once
// Models of the user's own on the device (Settings → Extensions → AI → Add a model file…,
// docs/EXTENSIONS.md "AI"): a GGUF file the user picked — on a computer used where it is, on
// Android copied into the app's models folder — and optionally its vision projector
// (mmproj-*.gguf). What the app keeps of one (the list in the models folder, own-models.json:
// the device's, never the notebook's) and what makes a file one: its header read (ai/gguf.h), its
// chat format told from it (local::detectFormat), a file that is no model refused with the reason.
//
//   const ai::GgufInfo info = ai::readGgufFile(path);
//   QString why;
//   std::optional<ai::OwnModel> own = ai::ownModelFrom(path, info, &why);
//   ai::LocalModel model = ai::localModelOf(*own);      // as the models the app knows
//   QByteArray json = ai::writeOwnModels(list);  …  ai::readOwnModels(json)

#include "ai/gguf.h"
#include "ai/local.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace brulion::ai {

struct OwnModel {
    QString id;        ///< "own-" and 12 hex digits of its file's path (ownModelId)
    QString name;      ///< what the settings call it: the user's, else its GGUF's general.name
    QString path;      ///< the model's GGUF
    QString projector; ///< its vision projector's GGUF ("": it reads text only)
    /// The app's own copy (Android: in the models folder, copied from the picked document): its
    /// files go when it is removed. A file used where it is never does.
    bool copied = false;
    QString format;   ///< local::detectFormat's
    qint64 bytes = 0; ///< the model's file
    qint64 projectorBytes = 0;
    qint64 parameters = 0;
    int contextLength = 0; ///< what it was trained to hold at once (0: not said)
    QString architecture;
    QString quantization; ///< "Q4_K_M"
    QString sizeLabel;    ///< "4B"
    /// The memory it takes holding the app's context (ai::memoryNeeded; 0: not known).
    qint64 memory = 0;
    friend bool operator==(const OwnModel &, const OwnModel &) = default;
};

/// The id of a model of the user's own at `path`: "own-" and the first 12 hex digits of its path's
/// SHA-1 — the same file is the same model.
QString ownModelId(const QString &path);
bool isOwnModelId(const QString &id);

/// The file at `path` (its header `info`) as a model of the user's own: its format, name, size …;
/// nothing, with `error`, when it is no model the app can run — no GGUF, a LoRA adapter, a vision
/// projector, an embedding model, a chat format the app does not write. `name` "" is its
/// general.name (else its basename, else the file's name).
std::optional<OwnModel> ownModelFrom(const QString &path, const GgufInfo &info, QString *error,
                                     const QString &name = {});
/// Is `info` a vision projector (mmproj: architecture "clip" with a vision encoder)? `error`: why
/// not.
bool isProjector(const GgufInfo &info, QString *error);

/// As the models the app knows (LocalModel::own, its file's path), for LocalAi.
LocalModel localModelOf(const OwnModel &model);

/// The list as kept (JSON); entries without an id, a path or a format the app writes are left out.
QList<OwnModel> readOwnModels(const QByteArray &json);
QByteArray writeOwnModels(const QList<OwnModel> &models);

/// The file the app keeps them in, in the models folder.
inline constexpr char OwnModelsFile[] = "own-models.json";

} // namespace brulion::ai
