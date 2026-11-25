#pragma once
// What a GGUF file says of itself (a model of the user's own: Settings → Extensions → AI → Add a
// model file…, ai/ownmodels.h): its architecture, name, size, quantization, context length, chat
// template and the tags of its vocabulary — read from its header with bounds checks, nothing
// trusted. A file may come from anywhere: a length or a count larger than what is left of the file
// ends the reading with an error, never a large allocation; a text longer than the limits below is
// skipped (its letters not kept). Only the header is read — the weights after it never.
//
//   const ai::GgufInfo info = ai::readGgufFile(path);
//   if (!info.ok) … info.error …
//   info.architecture, info.name, info.parameters, ai::quantizationName(info), info.contextLength

#include <QList>
#include <QString>
#include <QStringList>

class QIODevice;

namespace brulion::ai {

struct GgufInfo {
    bool ok = false;
    QString error; ///< why it is no GGUF this app can read ("not a GGUF file", "the file ends early" …)
    quint32 version = 0;
    QString architecture;     ///< general.architecture: "llama", "qwen3", "clip" (a projector) …
    QString type;             ///< general.type: "model", "adapter", "mmproj" ("": not said)
    QString name;             ///< general.name ("": not said)
    QString basename;         ///< general.basename
    QString sizeLabel;        ///< general.size_label: "4B", "8x7B"
    QString license;          ///< general.license
    QString chatTemplate;     ///< tokenizer.chat_template ("": none, or longer than MaxTemplateBytes)
    int fileType = -1;        ///< general.file_type (llama.cpp's llama_ftype; -1: not said)
    qint64 contextLength = 0; ///< <architecture>.context_length
    int blockCount = 0;       ///< <architecture>.block_count
    int embeddingLength = 0;  ///< <architecture>.embedding_length
    int headCount = 0;        ///< <architecture>.attention.head_count (per layer: the largest)
    /// <architecture>.attention.head_count_kv summed over the layers (one number: times the
    /// layers; a list per layer, as hybrid models give it: 0 for a layer without attention).
    qint64 kvHeads = 0;
    int keyLength = 0;         ///< <architecture>.attention.key_length (0: embedding / heads)
    int valueLength = 0;       ///< <architecture>.attention.value_length
    qint64 vocabularySize = 0; ///< tokenizer.ggml.tokens
    /// The control tags of chat formats its vocabulary has ("<|im_start|>", "<tool_call>",
    /// "<think>" …: knownTags()).
    QStringList tags;
    bool visionEncoder = false; ///< clip.has_vision_encoder: a projector that sees pictures
    qint64 tensorCount = 0;
    qint64 parameters = 0;     ///< the tensors' elements together
    int commonTensorType = -1; ///< the ggml type most of the parameters are in (when file_type is not said)
    qint64 fileBytes = 0;
};

/// At most this much of a chat template is kept (some are tens of kilobytes).
inline constexpr qint64 MaxTemplateBytes = 1024 * 1024;
/// A file whose header claims more than these is refused.
inline constexpr qint64 MaxMetadataEntries = 1 << 16;
inline constexpr qint64 MaxTensors = 1 << 20;

/// Reads the header from `device` (at its start, opened for reading) of a file of `fileBytes`
/// bytes (0: the device's size).
GgufInfo readGguf(QIODevice *device, qint64 fileBytes = 0);
GgufInfo readGgufFile(const QString &path);

/// The tags of chat formats looked for in a vocabulary (GgufInfo::tags).
const QStringList &knownTags();

/// Its quantization as people name it: "Q4_K_M", "Q8_0", "F16" … — from general.file_type, else the
/// type of most of its parameters; "" when neither says.
QString quantizationName(const GgufInfo &info);
/// Its size as its maker names it ("4B") or from the parameters counted ("4.0B", "350M"); "".
QString sizeName(const GgufInfo &info);

/// The memory the model takes on the processor holding `contextTokens` at once: its weights (the
/// file's size), the context's cells (its attention's keys and values in 16 bits for every layer
/// and token) and some working buffers. 0 when the file says too little to tell.
qint64 memoryNeeded(const GgufInfo &info, int contextTokens);

/// The device's memory from Linux's /proc/meminfo ("MemTotal:  3775512 kB" → bytes); 0 where there
/// is none (another system, an unreadable file).
qint64 deviceMemory(const QString &meminfo = QStringLiteral("/proc/meminfo"));
/// The same, from the file's text.
qint64 memTotalFrom(const QByteArray &meminfo);

} // namespace brulion::ai
