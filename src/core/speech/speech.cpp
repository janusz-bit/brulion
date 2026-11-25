#include "speech/speech.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace brulion::speech {

namespace {

// whisper.cpp's conversions of OpenAI's Whisper models (ggerganov/whisper.cpp on Hugging Face, MIT
// like the models), pinned to one revision of the repository (the files behind a revision never
// change), with the SHA-256 each download is checked against — Hugging Face's own LFS object ids.
const QString kRepository = u"https://huggingface.co/ggerganov/whisper.cpp"_s;
const QString kRevision = u"5359861c739e955e79d9a303bcbc70fb988958b1"_s;

ai::LocalModel whisper(const QString &id, const QString &name, const QString &label, const QString &file,
                       const QByteArray &sha256, qint64 bytes)
{
    ai::LocalModel m;
    m.id = id;
    m.name = name;
    m.label = label;
    m.maker = u"OpenAI"_s;
    m.format = u"whisper"_s;
    m.file = {file, QUrl(u"%1/resolve/%2/%3"_s.arg(kRepository, kRevision, file)), sha256, bytes};
    m.license = u"MIT"_s;
    m.licenseUrl = QUrl(u"https://github.com/openai/whisper/blob/main/LICENSE"_s);
    m.pageUrl = QUrl(u"https://github.com/openai/whisper"_s);
    m.thinks = false;
    return m;
}

QList<ai::LocalModel> knownModels()
{
    // Small (8-bit): Polish heard well enough to edit, a minute of speech in seconds on a phone —
    // the default.
    return {
            whisper(u"whisper-small"_s, u"Whisper Small"_s, u"Small"_s, u"ggml-small-q8_0.bin"_s,
                    "49c8fb02b65e6049d5fa6c04f81f53b867b5ec9540406812c643f177317f779f", 264464607),
            // Base (8-bit): fast on any device, rough in Polish.
            whisper(u"whisper-base"_s, u"Whisper Base"_s, u"Base"_s, u"ggml-base-q8_0.bin"_s,
                    "c577b9a86e7e048a0b7eada054f4dd79a56bbfa911fbdacf900ac5b567cbb7d9", 81768585),
            // Large v3 Turbo (5-bit): the most accurate, also with names and numbers; slower.
            whisper(u"whisper-large-v3-turbo"_s, u"Whisper Large v3 Turbo"_s, u"Large Turbo"_s,
                    u"ggml-large-v3-turbo-q5_0.bin"_s,
                    "394221709cd5ad1f40c46e6031ca61bce88931e6e088c188294c6d5a55ffa7e2", 574041195),
    };
}

} // namespace

const QList<ai::LocalModel> &speechModels()
{
    static const QList<ai::LocalModel> models = knownModels();
    return models;
}

const ai::LocalModel *speechModel(const QString &id)
{
    const QList<ai::LocalModel> &models = speechModels();
    if (id.isEmpty())
        return models.isEmpty() ? nullptr : &models.first();
    for (const ai::LocalModel &model : models) {
        if (model.id == id)
            return &model;
    }
    return nullptr;
}

const QStringList &speechLanguages()
{
    static const QStringList languages{u"auto"_s, u"pl"_s, u"en"_s};
    return languages;
}

QString knownLanguage(const QString &language)
{
    return speechLanguages().contains(language) ? language : speechLanguages().first();
}

QList<Segment> spokenSegments(const QList<Segment> &segments)
{
    // A segment that is only a note in brackets (or between asterisks), or a credit of subtitles
    // Whisper learnt from films and writes over silence.
    static const QRegularExpression note(u"^\\s*(\\[[^\\]]*\\]|\\([^)]*\\)|\\*[^*]*\\*|\\x{266A}+|\\.+)\\s*$"_s);
    static const QRegularExpression credit(
            u"^\\s*(napisy|tłumaczenie|subtitles|transcription|transcribed|translated)\\b.*"
            u"\\b(by|przez|wykonane|wykonał|wykonała|zrobione|amara)\\b"_s,
            QRegularExpression::CaseInsensitiveOption);
    QList<Segment> out;
    for (const Segment &s : segments) {
        const QString text = s.text.trimmed();
        if (text.isEmpty() || note.match(text).hasMatch() || credit.match(text).hasMatch())
            continue;
        out.append(Segment{s.startMs, s.endMs, text});
    }
    return out;
}

QStringList spokenTexts(const QList<Segment> &segments)
{
    QStringList out;
    for (const Segment &s : spokenSegments(segments))
        out.append(s.text);
    return out;
}

} // namespace brulion::speech
