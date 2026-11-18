#include "server.h"

#include <QDataStream>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::assistantbench {

namespace {

/// GGUF's value types (ggml/include/gguf.h).
enum GgufType : quint32 {
    U8 = 0,
    I8,
    U16,
    I16,
    U32,
    I32,
    F32,
    Bool,
    String,
    Array,
    U64,
    I64,
    F64,
};

/// Reads a GGUF file's header: its key-value pairs, a few of them kept.
class GgufReader {
public:
    explicit GgufReader(QIODevice *device) : m_in(device) { m_in.setByteOrder(QDataStream::LittleEndian); }

    bool ok() const { return m_in.status() == QDataStream::Ok; }

    QByteArray string()
    {
        quint64 size = 0;
        m_in >> size;
        if (!ok() || size > (quint64(1) << 30)) {
            m_in.setStatus(QDataStream::ReadCorruptData);
            return {};
        }
        QByteArray bytes(qsizetype(size), Qt::Uninitialized);
        if (m_in.readRawData(bytes.data(), int(size)) != int(size))
            m_in.setStatus(QDataStream::ReadPastEnd);
        return bytes;
    }

    /// Skips a value of `type`.
    void skip(quint32 type)
    {
        switch (type) {
        case U8:
        case I8:
        case Bool: m_in.skipRawData(1); break;
        case U16:
        case I16: m_in.skipRawData(2); break;
        case U32:
        case I32:
        case F32: m_in.skipRawData(4); break;
        case U64:
        case I64:
        case F64: m_in.skipRawData(8); break;
        case String: string(); break;
        case Array: {
            quint32 element = 0;
            quint64 count = 0;
            m_in >> element >> count;
            for (quint64 i = 0; i < count && ok(); ++i)
                skip(element);
            break;
        }
        default: m_in.setStatus(QDataStream::ReadCorruptData);
        }
    }

    QDataStream &in() { return m_in; }

private:
    QDataStream m_in;
};

/// A byte-level BPE token's text (GPT-2's way: every byte a letter of its own) as its bytes.
QByteArray bytesOfBpe(const QByteArray &text)
{
    // GPT-2's bytes_to_unicode(): the printable bytes stand for themselves, the others (0–32,
    // 127–160, 173) are the letters from U+0100 on, in order.
    static const QHash<char32_t, uchar> byteOf = [] {
        QHash<char32_t, uchar> map;
        char32_t next = 256;
        for (int b = 0; b < 256; ++b) {
            const bool printable = (b >= 33 && b <= 126) || (b >= 161 && b <= 172) || (b >= 174 && b <= 255);
            map.insert(printable ? char32_t(b) : next++, uchar(b));
        }
        return map;
    }();
    QByteArray bytes;
    for (const char32_t letter : QString::fromUtf8(text).toUcs4())
        bytes.append(char(byteOf.value(letter, uchar('?'))));
    return bytes;
}

} // namespace

std::optional<Vocabulary> readVocabulary(const QString &ggufFile, QString *error)
{
    const auto fail = [&](const QString &why) -> std::optional<Vocabulary> {
        if (error)
            *error = u"%1: %2"_s.arg(ggufFile, why);
        return std::nullopt;
    };
    QFile file(ggufFile);
    if (!file.open(QIODevice::ReadOnly))
        return fail(file.errorString());
    GgufReader reader(&file);
    quint32 magic = 0;
    quint32 version = 0;
    quint64 tensors = 0;
    quint64 pairs = 0;
    reader.in() >> magic >> version >> tensors >> pairs;
    if (!reader.ok() || magic != 0x46554747 /* "GGUF" */ || version < 2)
        return fail(u"not a GGUF file (version 2 or later)"_s);

    QByteArray model;
    QList<QByteArray> texts;
    QList<qint32> types;
    for (quint64 i = 0; i < pairs && reader.ok() && (model.isEmpty() || texts.isEmpty() || types.isEmpty()); ++i) {
        const QByteArray key = reader.string();
        quint32 type = 0;
        reader.in() >> type;
        if (key == "tokenizer.ggml.model" && type == String) {
            model = reader.string();
        } else if ((key == "tokenizer.ggml.tokens" || key == "tokenizer.ggml.token_type") && type == Array) {
            quint32 element = 0;
            quint64 count = 0;
            reader.in() >> element >> count;
            if (count > 10'000'000)
                return fail(u"a vocabulary of %1 tokens"_s.arg(count));
            if (key == "tokenizer.ggml.tokens" && element == String) {
                texts.reserve(qsizetype(count));
                for (quint64 n = 0; n < count && reader.ok(); ++n)
                    texts.append(reader.string());
            } else if (key == "tokenizer.ggml.token_type" && element == I32) {
                types.reserve(qsizetype(count));
                for (quint64 n = 0; n < count && reader.ok(); ++n) {
                    qint32 t = 0;
                    reader.in() >> t;
                    types.append(t);
                }
            } else {
                for (quint64 n = 0; n < count && reader.ok(); ++n)
                    reader.skip(element);
            }
        } else {
            reader.skip(type);
        }
    }
    if (!reader.ok())
        return fail(u"its header ends too early or is damaged"_s);
    if (texts.isEmpty() || texts.size() != types.size())
        return fail(u"no vocabulary (tokenizer.ggml.tokens, tokenizer.ggml.token_type)"_s);
    if (model != "gpt2")
        return fail(u"a tokenizer of the kind \"%1\" (only byte-level BPE, \"gpt2\")"_s.arg(QString::fromUtf8(model)));

    Vocabulary vocabulary;
    vocabulary.bytes.reserve(texts.size());
    for (qsizetype id = 0; id < texts.size(); ++id) {
        // llama_token_type: 1 normal, 3 control, 4 user-defined, 5 unused, 6 byte.
        const qint32 kind = types.at(id);
        const QByteArray &text = texts.at(id);
        if ((kind == 3 || kind == 4) && text.size() >= 2)
            vocabulary.controls.insert(QString::fromUtf8(text), int(id));
        if (kind == 3 || kind == 4)
            vocabulary.bytes.append(text);
        else if (kind == 1 || kind == 6)
            vocabulary.bytes.append(bytesOfBpe(text));
        else
            vocabulary.bytes.append(QByteArray());
    }
    if (vocabulary.controls.isEmpty())
        return fail(u"no control tokens in its vocabulary"_s);
    return vocabulary;
}

QList<int> Tokenized::all() const
{
    QList<int> out;
    for (const QList<int> &t : tokens)
        out += t;
    return out;
}

ServerRunner::ServerRunner(QUrl url, std::shared_ptr<const Vocabulary> vocabulary, int contextTokens)
    : m_url(std::move(url)), m_vocabulary(std::move(vocabulary)), m_contextTokens(contextTokens),
      m_network(std::make_unique<QNetworkAccessManager>())
{
    for (auto it = m_vocabulary->controls.cbegin(); it != m_vocabulary->controls.cend(); ++it) {
        m_controlTexts.insert(it.value(), it.key());
        m_controlSpellings.append(it.key().toUtf8());
    }
}

ServerRunner::~ServerRunner() = default;

QString ServerRunner::device() const
{
    return u"llama-server at %1"_s.arg(m_url.toString());
}

std::optional<QJsonObject> ServerRunner::post(const QString &path, const QJsonObject &body, int timeoutMs,
                                              QString *error)
{
    QUrl url = m_url;
    url.setPath(path);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
    request.setTransferTimeout(timeoutMs);
    const std::unique_ptr<QNetworkReply> reply(
            m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact)));
    QEventLoop loop;
    QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (!reply->isFinished())
        loop.exec();
    const QByteArray data = reply->readAll();
    const QJsonObject object = QJsonDocument::fromJson(data).object();
    if (reply->error() != QNetworkReply::NoError) {
        const QString said = object.value(u"error"_s).toObject().value(u"message"_s).toString();
        *error = u"llama-server %1: %2"_s.arg(path, said.isEmpty() ? reply->errorString() : said);
        return std::nullopt;
    }
    if (object.isEmpty()) {
        *error = u"llama-server %1: not an answer of JSON"_s.arg(path);
        return std::nullopt;
    }
    return object;
}

bool ServerRunner::tokenizeText(const QString &text, QList<int> *out, QString *error)
{
    if (const auto it = m_known.constFind(text); it != m_known.constEnd()) {
        *out += it.value();
        return true;
    }
    // Cut after the first letter of every place that spells a control token (ai/llamarunner.cpp,
    // tokenizeText()).
    const QByteArray utf8 = text.toUtf8();
    QList<qsizetype> cuts;
    for (const QByteArray &control : std::as_const(m_controlSpellings)) {
        qsizetype first = 1;
        while (first < control.size() && (uchar(control.at(first)) & 0xC0) == 0x80)
            ++first;
        for (qsizetype at = utf8.indexOf(control); at >= 0; at = utf8.indexOf(control, at + 1))
            cuts.append(at + first);
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    cuts.append(utf8.size());

    QList<int> tokens;
    qsizetype from = 0;
    for (const qsizetype to : std::as_const(cuts)) {
        if (to <= from)
            continue;
        const std::optional<QJsonObject> answer =
                post(u"/tokenize"_s,
                     QJsonObject{{u"content"_s, QString::fromUtf8(utf8.mid(from, to - from))},
                                 {u"add_special"_s, false},
                                 {u"parse_special"_s, false}},
                     60000, error);
        if (!answer)
            return false;
        for (const QJsonValue v : answer->value(u"tokens"_s).toArray())
            tokens.append(v.toInt());
        from = to;
    }
    if (m_known.size() > 20000)
        m_known.clear();
    m_known.insert(text, tokens);
    *out += tokens;
    return true;
}

std::optional<Tokenized> ServerRunner::tokenize(const QList<ai::local::Piece> &pieces, QString *error)
{
    Tokenized out;
    for (const ai::local::Piece &piece : pieces) {
        QList<int> tokens;
        if (!piece.image.isEmpty()) {
            *error = u"the bench's llama-server reads no pictures"_s;
            return std::nullopt;
        }
        if (piece.special) {
            const int token = m_vocabulary->controls.value(piece.text, -1);
            if (token < 0) {
                *error = u"the model has no token \"%1\": its file is not of the model chosen"_s.arg(piece.text);
                return std::nullopt;
            }
            tokens.append(token);
        } else if (!tokenizeText(piece.text, &tokens, error)) {
            return std::nullopt;
        }
        out.pieces.append(piece);
        out.tokens.append(tokens);
    }
    return out;
}

ai::LocalRunner::Result ServerRunner::generate(const ai::local::Prompt &prompt, const Params &params)
{
    Result r;
    m_lastPrompt = {};
    m_lastAnswer = {};

    const std::optional<Tokenized> read = tokenize(prompt.pieces, &r.error);
    if (!read)
        return r;
    const Tokenized &asked = *read;
    QJsonArray promptTokens;
    for (const int t : asked.all())
        promptTokens.append(t);
    r.promptTokens = int(promptTokens.size());
    const int reserve = std::clamp(params.reserveTokens, 16, m_contextTokens / 2);
    r.promptLimit = m_contextTokens - reserve;
    if (promptTokens.isEmpty()) {
        r.error = u"not a request the model can answer"_s;
        return r;
    }
    if (r.promptTokens > r.promptLimit) {
        r.status = TooLong;
        r.error = u"the request is too long for the model on this device (%1 tokens; at most %2)"_s.arg(r.promptTokens)
                          .arg(r.promptLimit);
        return r;
    }

    const int room = m_contextTokens - r.promptTokens;
    const int most = params.maxTokens > 0 ? std::min(params.maxTokens, room) : room;
    const QJsonObject body{
            {u"prompt"_s, promptTokens},
            {u"n_predict"_s, most},
            // The app's sampling (ai/llamarunner.cpp): nothing else of the server's.
            {u"samplers"_s, QJsonArray{u"penalties"_s, u"top_k"_s, u"temperature"_s}},
            {u"repeat_last_n"_s, 64},
            {u"repeat_penalty"_s, double(params.repeatPenalty)},
            {u"presence_penalty"_s, 0.0},
            {u"frequency_penalty"_s, 0.0},
            {u"top_k"_s, params.topK},
            {u"temperature"_s, double(params.temperature)},
            {u"top_p"_s, 1.0},
            {u"min_p"_s, 0.0},
            {u"typical_p"_s, 1.0},
            {u"xtc_probability"_s, 0.0},
            {u"dry_multiplier"_s, 0.0},
            {u"top_n_sigma"_s, -1.0},
            {u"seed"_s, qint64(params.seed)},
            // The same samplers computed on the GPU, beside the model: three times as many tokens
            // a second with 32 conversations at once (their random numbers are other than the
            // processor's).
            {u"backend_sampling"_s, true},
            {u"stop"_s, QJsonArray::fromStringList(prompt.stops)},
            {u"cache_prompt"_s, true},
            {u"return_tokens"_s, true}, // (its bytes are the vocabulary's)
            {u"stream"_s, false},
    };
    // (The server answers when the whole answer is written: as long as the app gives a model.)
    const std::optional<QJsonObject> answer =
            post(u"/completion"_s, body, params.timeoutMs > 0 ? params.timeoutMs : ai::TimeoutMs, &r.error);
    if (!answer)
        return r;

    Tokenized written;
    QByteArray words; // the text piece being written (a letter may span tokens)
    QList<int> wordTokens;
    const auto flushWords = [&] {
        if (!words.isEmpty()) {
            written.pieces.append(ai::local::Piece{QString::fromUtf8(words), false, {}});
            written.tokens.append(wordTokens);
        }
        words.clear();
        wordTokens.clear();
    };
    const QString stopType = answer->value(u"stop_type"_s).toString();
    const QJsonArray tokens = answer->value(u"tokens"_s).toArray();
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        const int id = tokens.at(i).toInt();
        if (id < 0 || id >= m_vocabulary->bytes.size()) {
            r.error = u"llama-server wrote the token %1, not of this model's vocabulary (--model)"_s.arg(id);
            return r;
        }
        if (const auto control = m_controlTexts.constFind(id); control != m_controlTexts.constEnd()) {
            if (prompt.stops.contains(control.value()))
                break; // it began another turn: its own is over
            // The end of its turn (the server lists it last) is not a piece of the answer.
            if (stopType == u"eos" && i == tokens.size() - 1)
                break;
            flushWords();
            written.pieces.append(ai::local::Piece{control.value(), true, {}});
            written.tokens.append(QList<int>{id});
        } else {
            words += m_vocabulary->bytes.at(id);
            wordTokens.append(id);
        }
        ++r.outputTokens;
    }
    flushWords();

    r.status = Ok;
    r.pieces = written.pieces;
    r.complete = stopType == u"eos" || stopType == u"word";
    const QJsonObject timings = answer->value(u"timings"_s).toObject();
    r.reusedTokens = timings.value(u"cache_n"_s).toInt();
    r.promptMs = qint64(timings.value(u"prompt_ms"_s).toDouble());
    r.outputMs = qint64(timings.value(u"predicted_ms"_s).toDouble());
    m_lastPrompt = asked;
    m_lastAnswer = written;
    return r;
}

} // namespace brulion::assistantbench
