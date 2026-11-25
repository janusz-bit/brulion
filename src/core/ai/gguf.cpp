#include "ai/gguf.h"

#include <QFile>
#include <QHash>
#include <QIODevice>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

using namespace Qt::StringLiterals;

namespace brulion::ai {

namespace {

/// GGUF's value types.
enum Type : quint32 {
    U8 = 0,
    I8 = 1,
    U16 = 2,
    I16 = 3,
    U32 = 4,
    I32 = 5,
    F32 = 6,
    Bool = 7,
    String = 8,
    Array = 9,
    U64 = 10,
    I64 = 11,
    F64 = 12,
};

/// The bytes of a scalar of `type` (0: not a scalar).
int scalarBytes(quint32 type)
{
    switch (type) {
    case U8:
    case I8:
    case Bool: return 1;
    case U16:
    case I16: return 2;
    case U32:
    case I32:
    case F32: return 4;
    case U64:
    case I64:
    case F64: return 8;
    default: return 0;
    }
}

/// A key's text is at most this long (llama.cpp's are some tens of bytes).
constexpr qint64 MaxKeyBytes = 64 * 1024;
/// A text value kept (a longer one is skipped): a model's name, its licence …
constexpr qint64 MaxTextBytes = 64 * 1024;
/// A list of numbers kept per layer (head counts): at most so many layers.
constexpr qint64 MaxLayers = 4096;
/// GGML's most dimensions of a tensor.
constexpr quint32 MaxDims = 4;
/// A vocabulary's token longer than this is no tag of a chat format.
constexpr qint64 MaxTagBytes = 64;
/// Lists in lists: deeper than this is not a model's header.
constexpr int MaxDepth = 3;

class Reader {
public:
    Reader(QIODevice *device, qint64 size) : m_device(device), m_size(size) { }

    QString error() const { return m_error; }
    qint64 left() const { return m_size - m_pos; }

    bool fail(const QString &why)
    {
        if (m_error.isEmpty())
            m_error = why;
        return false;
    }

    bool read(char *out, qint64 n)
    {
        if (n < 0 || n > left())
            return fail(u"the file ends early: it is cut off or no model"_s);
        if (n > 0 && m_device->read(out, n) != n)
            return fail(u"the file cannot be read"_s);
        m_pos += n;
        return true;
    }

    bool skip(qint64 n)
    {
        if (n < 0 || n > left())
            return fail(u"the file ends early: it is cut off or no model"_s);
        while (n > 0) {
            const qint64 skipped = m_device->skip(n);
            if (skipped <= 0)
                return fail(u"the file cannot be read"_s);
            n -= skipped;
            m_pos += skipped;
        }
        return true;
    }

    template<typename T>
    bool number(T *out)
    {
        std::array<uchar, sizeof(T)> bytes{};
        if (!read(reinterpret_cast<char *>(bytes.data()), qint64(bytes.size())))
            return false;
        *out = qFromLittleEndian<T>(bytes.data());
        return true;
    }

    /// A string's length, checked against what is left of the file.
    bool length(quint64 *out)
    {
        if (!number(out))
            return false;
        if (*out > quint64(left()))
            return fail(u"a text in the file's header runs past its end"_s);
        return true;
    }

    /// A string: its bytes when at most `max`, else skipped (`kept` false).
    bool string(QByteArray *out, qint64 max, bool *kept = nullptr)
    {
        quint64 n = 0;
        if (!length(&n))
            return false;
        if (qint64(n) > max) {
            out->clear();
            if (kept)
                *kept = false;
            return skip(qint64(n));
        }
        out->resize(qsizetype(n));
        if (kept)
            *kept = true;
        return read(out->data(), qint64(n));
    }

    /// A scalar as a whole number (floats as theirs, rounded down; a truth value as 0 / 1).
    bool scalar(quint32 type, qint64 *out)
    {
        switch (type) {
        case U8: {
            quint8 v = 0;
            if (!number(&v))
                return false;
            *out = v;
            return true;
        }
        case I8: {
            qint8 v = 0;
            if (!number(&v))
                return false;
            *out = v;
            return true;
        }
        case Bool: {
            quint8 v = 0;
            if (!number(&v))
                return false;
            *out = v != 0;
            return true;
        }
        case U16: {
            quint16 v = 0;
            if (!number(&v))
                return false;
            *out = v;
            return true;
        }
        case I16: {
            qint16 v = 0;
            if (!number(&v))
                return false;
            *out = v;
            return true;
        }
        case U32: {
            quint32 v = 0;
            if (!number(&v))
                return false;
            *out = v;
            return true;
        }
        case I32: {
            qint32 v = 0;
            if (!number(&v))
                return false;
            *out = v;
            return true;
        }
        case U64: {
            quint64 v = 0;
            if (!number(&v))
                return false;
            *out = v > quint64(std::numeric_limits<qint64>::max()) ? std::numeric_limits<qint64>::max() : qint64(v);
            return true;
        }
        case I64: return number(out);
        case F32: {
            quint32 bits = 0;
            if (!number(&bits))
                return false;
            float f = 0;
            std::memcpy(&f, &bits, sizeof f);
            *out = std::isfinite(f) && std::abs(f) < 9.0e18f ? qint64(f) : 0;
            return true;
        }
        case F64: {
            quint64 bits = 0;
            if (!number(&bits))
                return false;
            double d = 0;
            std::memcpy(&d, &bits, sizeof d);
            *out = std::isfinite(d) && std::abs(d) < 9.0e18 ? qint64(d) : 0;
            return true;
        }
        default: return fail(u"the file's header has a value of an unknown type (%1)"_s.arg(type));
        }
    }

    /// Skips a value of `type` (lists in lists to MaxDepth).
    bool skipValue(quint32 type, int depth = 0)
    {
        if (const int bytes = scalarBytes(type))
            return skip(bytes);
        if (type == String) {
            quint64 n = 0;
            return length(&n) && skip(qint64(n));
        }
        if (type != Array)
            return fail(u"the file's header has a value of an unknown type (%1)"_s.arg(type));
        if (depth >= MaxDepth)
            return fail(u"the file's header nests lists too deeply"_s);
        quint32 element = 0;
        quint64 count = 0;
        if (!number(&element) || !number(&count))
            return false;
        if (const int bytes = scalarBytes(element)) {
            if (count > quint64(left() / bytes))
                return fail(u"a list in the file's header runs past its end"_s);
            return skip(qint64(count) * bytes);
        }
        // (Each element takes at least 8 bytes: a string's length, a list's type and count.)
        if (count > quint64(left() / 8))
            return fail(u"a list in the file's header runs past its end"_s);
        for (quint64 i = 0; i < count; ++i) {
            if (!skipValue(element, depth + 1))
                return false;
        }
        return true;
    }

private:
    QIODevice *m_device;
    qint64 m_size;
    qint64 m_pos = 0;
    QString m_error;
};

QString text(const QByteArray &utf8)
{
    return QString::fromUtf8(utf8);
}

/// The ggml type names (ggml.h), by number.
QString ggmlTypeName(int type)
{
    static const QHash<int, QString> names{
            {0, u"F32"_s},      {1, u"F16"_s},      {2, u"Q4_0"_s},   {3, u"Q4_1"_s},    {6, u"Q5_0"_s},
            {7, u"Q5_1"_s},     {8, u"Q8_0"_s},     {9, u"Q8_1"_s},   {10, u"Q2_K"_s},   {11, u"Q3_K"_s},
            {12, u"Q4_K"_s},    {13, u"Q5_K"_s},    {14, u"Q6_K"_s},  {15, u"Q8_K"_s},   {16, u"IQ2_XXS"_s},
            {17, u"IQ2_XS"_s},  {18, u"IQ3_XXS"_s}, {19, u"IQ1_S"_s}, {20, u"IQ4_NL"_s}, {21, u"IQ3_S"_s},
            {22, u"IQ2_S"_s},   {23, u"IQ4_XS"_s},  {29, u"IQ1_M"_s}, {30, u"BF16"_s},   {34, u"TQ1_0"_s},
            {35, u"TQ2_0"_s},   {39, u"MXFP4"_s},   {40, u"NVFP4"_s}, {41, u"Q1_0"_s},   {42, u"Q2_0"_s},
            {43, u"F8_E4M3"_s},
    };
    return names.value(type);
}

/// llama.cpp's file types (llama.h, llama_ftype), by number.
QString fileTypeName(int type)
{
    static const QHash<int, QString> names{
            {0, u"F32"_s},      {1, u"F16"_s},      {2, u"Q4_0"_s},       {3, u"Q4_1"_s},    {7, u"Q8_0"_s},
            {8, u"Q5_0"_s},     {9, u"Q5_1"_s},     {10, u"Q2_K"_s},      {11, u"Q3_K_S"_s}, {12, u"Q3_K_M"_s},
            {13, u"Q3_K_L"_s},  {14, u"Q4_K_S"_s},  {15, u"Q4_K_M"_s},    {16, u"Q5_K_S"_s}, {17, u"Q5_K_M"_s},
            {18, u"Q6_K"_s},    {19, u"IQ2_XXS"_s}, {20, u"IQ2_XS"_s},    {21, u"Q2_K_S"_s}, {22, u"IQ3_XS"_s},
            {23, u"IQ3_XXS"_s}, {24, u"IQ1_S"_s},   {25, u"IQ4_NL"_s},    {26, u"IQ3_S"_s},  {27, u"IQ3_M"_s},
            {28, u"IQ2_S"_s},   {29, u"IQ2_M"_s},   {30, u"IQ4_XS"_s},    {31, u"IQ1_M"_s},  {32, u"BF16"_s},
            {36, u"TQ1_0"_s},   {37, u"TQ2_0"_s},   {38, u"MXFP4_MOE"_s}, {39, u"NVFP4"_s},  {40, u"Q1_0"_s},
            {41, u"Q2_0"_s},    {42, u"F8_E4M3"_s},
    };
    return names.value(type);
}

/// The metadata keys whose numbers are kept: "<architecture>.<suffix>".
const QStringList &architectureKeys()
{
    static const QStringList keys{u"context_length"_s,          u"block_count"_s,
                                  u"embedding_length"_s,        u"attention.head_count"_s,
                                  u"attention.head_count_kv"_s, u"attention.key_length"_s,
                                  u"attention.value_length"_s};
    return keys;
}

/// The key's suffix after its architecture's name ("qwen3.context_length" → "context_length"),
/// when it is one architectureKeys() has; "" otherwise.
QString architectureSuffix(const QString &key)
{
    const qsizetype dot = key.indexOf(u'.');
    if (dot <= 0)
        return QString();
    const QString suffix = key.mid(dot + 1);
    return architectureKeys().contains(suffix) ? suffix : QString();
}

} // namespace

const QStringList &knownTags()
{
    static const QStringList tags{
            u"<|im_start|>"_s,
            u"<|im_end|>"_s,
            u"<|im_sep|>"_s,
            u"<|start_header_id|>"_s,
            u"<|end_header_id|>"_s,
            u"<|eot_id|>"_s,
            u"<start_of_turn>"_s,
            u"<end_of_turn>"_s,
            u"<|system|>"_s,
            u"<|user|>"_s,
            u"<|assistant|>"_s,
            u"<|end|>"_s,
            u"[INST]"_s,
            u"[/INST]"_s,
            u"[SYSTEM_PROMPT]"_s,
            u"[/SYSTEM_PROMPT]"_s,
            u"<｜User｜>"_s,
            u"<｜Assistant｜>"_s,
            u"<｜end▁of▁sentence｜>"_s,
            u"<tool_call>"_s,
            u"</tool_call>"_s,
            u"<tool_response>"_s,
            u"</tool_response>"_s,
            u"<think>"_s,
            u"</think>"_s,
            u"<|startoftext|>"_s,
            u"<function="_s,
            u"<parameter="_s,
    };
    return tags;
}

GgufInfo readGguf(QIODevice *device, qint64 fileBytes)
{
    GgufInfo info;
    if (!device || !device->isReadable()) {
        info.error = u"the file cannot be read"_s;
        return info;
    }
    info.fileBytes = fileBytes > 0 ? fileBytes : device->size();
    Reader in(device, info.fileBytes);
    const auto failed = [&](const QString &why = {}) {
        info.ok = false;
        info.error = why.isEmpty() ? in.error() : why;
        return info;
    };

    char magic[4] = {};
    if (!in.read(magic, 4) || std::memcmp(magic, "GGUF", 4) != 0)
        return failed(u"not a GGUF file (a model for llama.cpp ends in .gguf)"_s);
    quint32 version = 0;
    if (!in.number(&version))
        return failed();
    if (version == 0x02000000 || version == 0x03000000)
        return failed(u"a GGUF file of the other byte order (big-endian): not one this device reads"_s);
    if (version < 2 || version > 3)
        return failed(version == 1 ? u"a GGUF file of the first version, which llama.cpp reads no longer"_s
                                   : u"a GGUF file of an unknown version (%1)"_s.arg(version));
    info.version = version;
    quint64 tensors = 0;
    quint64 entries = 0;
    if (!in.number(&tensors) || !in.number(&entries))
        return failed();
    if (tensors > quint64(MaxTensors) || entries > quint64(MaxMetadataEntries))
        return failed(
                u"the file's header claims more than a model has (%1 tensors, %2 entries)"_s.arg(tensors).arg(entries));
    info.tensorCount = qint64(tensors);

    QHash<QString, qint64> numbers;         // "<architecture>.<suffix>" → its value
    QHash<QString, QList<qint64>> perLayer; // the same, given per layer
    QSet<QString> tags;
    const QStringList &wanted = knownTags();
    for (quint64 e = 0; e < entries; ++e) {
        QByteArray keyBytes;
        bool kept = false;
        if (!in.string(&keyBytes, MaxKeyBytes, &kept))
            return failed();
        if (!kept)
            return failed(u"the file's header has a key longer than any model's"_s);
        const QString key = text(keyBytes);
        quint32 type = 0;
        if (!in.number(&type))
            return failed();
        const QString suffix = architectureSuffix(key);
        if (type == String) {
            const bool chat = key == u"tokenizer.chat_template";
            QByteArray value;
            if (!in.string(&value, chat ? MaxTemplateBytes : MaxTextBytes))
                return failed();
            if (key == u"general.architecture")
                info.architecture = text(value);
            else if (key == u"general.type")
                info.type = text(value);
            else if (key == u"general.name")
                info.name = text(value);
            else if (key == u"general.basename")
                info.basename = text(value);
            else if (key == u"general.size_label")
                info.sizeLabel = text(value);
            else if (key == u"general.license")
                info.license = text(value);
            else if (chat)
                info.chatTemplate = text(value);
        } else if (scalarBytes(type)) {
            qint64 value = 0;
            if (!in.scalar(type, &value))
                return failed();
            if (key == u"general.file_type")
                info.fileType = value >= 0 && value < 1024 ? int(value) : -1;
            else if (key == u"clip.has_vision_encoder")
                info.visionEncoder = value != 0;
            else if (!suffix.isEmpty())
                numbers.insert(key, value);
        } else if (type == Array) {
            quint32 element = 0;
            quint64 count = 0;
            if (!in.number(&element) || !in.number(&count))
                return failed();
            if (key == u"tokenizer.ggml.tokens" && element == String) {
                if (count > quint64(in.left() / 8))
                    return failed(u"a list in the file's header runs past its end"_s);
                info.vocabularySize = qint64(count);
                for (quint64 i = 0; i < count; ++i) {
                    QByteArray token;
                    bool small = false;
                    if (!in.string(&token, MaxTagBytes, &small))
                        return failed();
                    if (small && !token.isEmpty() && (token.at(0) == '<' || token.at(0) == '[')) {
                        const QString tag = text(token);
                        if (wanted.contains(tag))
                            tags.insert(tag);
                    }
                }
            } else if (!suffix.isEmpty() && scalarBytes(element) && count <= quint64(MaxLayers)) {
                QList<qint64> values;
                for (quint64 i = 0; i < count; ++i) {
                    qint64 v = 0;
                    if (!in.scalar(element, &v))
                        return failed();
                    values << v;
                }
                perLayer.insert(key, values);
            } else {
                // (Put back what was read of the list's head, then skip it whole.)
                if (const int bytes = scalarBytes(element)) {
                    if (count > quint64(in.left() / bytes))
                        return failed(u"a list in the file's header runs past its end"_s);
                    if (!in.skip(qint64(count) * bytes))
                        return failed();
                } else {
                    if (count > quint64(in.left() / 8))
                        return failed(u"a list in the file's header runs past its end"_s);
                    for (quint64 i = 0; i < count; ++i) {
                        if (!in.skipValue(element, 1))
                            return failed();
                    }
                }
            }
        } else {
            return failed(u"the file's header has a value of an unknown type (%1)"_s.arg(type));
        }
    }
    for (const QString &tag : wanted) {
        if (tags.contains(tag))
            info.tags << tag;
    }

    // Its architecture's numbers.
    const QString arch = info.architecture + u'.';
    const auto value = [&](const QString &suffix) -> qint64 { return numbers.value(arch + suffix, 0); };
    const auto most = [&](const QString &suffix) -> qint64 {
        const QList<qint64> list = perLayer.value(arch + suffix);
        return list.isEmpty() ? value(suffix) : *std::ranges::max_element(list);
    };
    const auto clampInt = [](qint64 v) { return int(std::clamp<qint64>(v, 0, std::numeric_limits<int>::max())); };
    info.contextLength = std::max<qint64>(0, value(u"context_length"_s));
    info.blockCount = clampInt(value(u"block_count"_s));
    info.embeddingLength = clampInt(value(u"embedding_length"_s));
    info.headCount = clampInt(most(u"attention.head_count"_s));
    info.keyLength = clampInt(value(u"attention.key_length"_s));
    info.valueLength = clampInt(value(u"attention.value_length"_s));
    if (const QList<qint64> kv = perLayer.value(arch + u"attention.head_count_kv"_s); !kv.isEmpty()) {
        for (const qint64 heads : kv)
            info.kvHeads += std::clamp<qint64>(heads, 0, 1 << 16);
    } else {
        const qint64 heads = numbers.contains(arch + u"attention.head_count_kv"_s) ? value(u"attention.head_count_kv"_s)
                                                                                   : info.headCount;
        info.kvHeads = std::clamp<qint64>(heads, 0, 1 << 16) * info.blockCount;
    }

    // The tensors: what they hold together, and in which type most of it is.
    QHash<int, qint64> byType;
    constexpr qint64 MostElements = qint64(1) << 53;
    for (quint64 t = 0; t < tensors; ++t) {
        QByteArray name;
        if (!in.string(&name, MaxKeyBytes))
            return failed();
        quint32 dims = 0;
        if (!in.number(&dims))
            return failed();
        if (dims == 0 || dims > MaxDims)
            return failed(u"a tensor in the file has %1 dimensions: not a model"_s.arg(dims));
        qint64 elements = 1;
        for (quint32 d = 0; d < dims; ++d) {
            quint64 size = 0;
            if (!in.number(&size))
                return failed();
            if (size == 0 || size > quint64(MostElements) || elements > MostElements / qint64(size))
                return failed(u"a tensor in the file is of an impossible size"_s);
            elements *= qint64(size);
        }
        quint32 type = 0;
        quint64 offset = 0;
        if (!in.number(&type) || !in.number(&offset))
            return failed();
        if (offset >= quint64(info.fileBytes))
            return failed(u"a tensor in the file lies past its end: it is cut off"_s);
        info.parameters = std::min(MostElements, info.parameters + elements);
        byType[int(type)] += elements;
    }
    qint64 best = 0;
    for (auto it = byType.cbegin(); it != byType.cend(); ++it) {
        // (The 1-D tensors — norms, in 32 bits — are a small part.)
        if (it.value() > best) {
            best = it.value();
            info.commonTensorType = it.key();
        }
    }
    info.ok = true;
    return info;
}

GgufInfo readGgufFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        GgufInfo info;
        info.error =
                file.exists() ? u"the file cannot be read: %1"_s.arg(file.errorString()) : u"there is no such file"_s;
        return info;
    }
    return readGguf(&file, file.size());
}

QString quantizationName(const GgufInfo &info)
{
    const QString named = fileTypeName(info.fileType);
    if (!named.isEmpty())
        return named;
    return info.commonTensorType >= 0 ? ggmlTypeName(info.commonTensorType) : QString();
}

QString sizeName(const GgufInfo &info)
{
    if (!info.sizeLabel.isEmpty())
        return info.sizeLabel;
    if (info.parameters <= 0)
        return QString();
    const double p = double(info.parameters);
    if (p >= 1.0e9)
        return QString::number(p / 1.0e9, 'f', 1) + u'B';
    if (p >= 1.0e6)
        return QString::number(std::round(p / 1.0e6), 'f', 0) + u'M';
    return QString::number(info.parameters);
}

qint64 memoryNeeded(const GgufInfo &info, int contextTokens)
{
    if (info.fileBytes <= 0)
        return 0;
    qint64 cells = 0;
    if (info.kvHeads > 0 && contextTokens > 0) {
        const qint64 headDim =
                info.headCount > 0 && info.embeddingLength > 0 ? info.embeddingLength / info.headCount : 0;
        const qint64 keys = info.keyLength > 0 ? info.keyLength : headDim;
        const qint64 values = info.valueLength > 0 ? info.valueLength : headDim;
        // (16 bits each: two bytes.)
        cells = info.kvHeads * (keys + values) * 2 * qint64(contextTokens);
    }
    // The weights (laid out anew for the processor, the file's pages let go), the cells and some
    // 300 MB of working buffers.
    return info.fileBytes + cells + 300LL * 1024 * 1024;
}

qint64 memTotalFrom(const QByteArray &meminfo)
{
    for (const QByteArray &line : meminfo.split('\n')) {
        if (!line.startsWith("MemTotal:"))
            continue;
        const QList<QByteArray> parts = line.mid(9).simplified().split(' ');
        bool ok = false;
        const qint64 value = parts.value(0).toLongLong(&ok);
        if (!ok || value <= 0)
            return 0;
        const QByteArray unit = parts.value(1).toLower();
        if (unit == "kb")
            return value * 1024;
        if (unit == "mb")
            return value * 1024 * 1024;
        return unit.isEmpty() ? value : 0;
    }
    return 0;
}

qint64 deviceMemory(const QString &meminfo)
{
    QFile file(meminfo);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    return memTotalFrom(file.read(64 * 1024));
}

} // namespace brulion::ai
