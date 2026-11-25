#include "ai/ownmodels.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::ai {

namespace {

const QString kPrefix = u"own-"_s;

/// Architectures of models that embed text or speech, not chat: no answers from them.
bool embeddingOnly(const GgufInfo &info)
{
    static const QStringList archs{u"bert"_s,         u"nomic-bert"_s, u"nomic-bert-moe"_s,  u"jina-bert-v2"_s,
                                   u"jina-bert-v3"_s, u"t5encoder"_s,  u"modern-bert"_s,     u"neo-bert"_s,
                                   u"eurobert"_s,     u"whisper"_s,    u"wavtokenizer-dec"_s};
    return archs.contains(info.architecture.toLower()) || info.type == u"embedding";
}

} // namespace

QString ownModelId(const QString &path)
{
    const QByteArray hash = QCryptographicHash::hash(QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toUtf8(),
                                                     QCryptographicHash::Sha1);
    return kPrefix + QString::fromLatin1(hash.toHex().left(12));
}

bool isOwnModelId(const QString &id)
{
    if (!id.startsWith(kPrefix) || id.size() != kPrefix.size() + 12)
        return false;
    return std::ranges::all_of(QStringView(id).mid(kPrefix.size()),
                               [](QChar c) { return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f'); });
}

bool isProjector(const GgufInfo &info, QString *error)
{
    const auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (!info.ok)
        return fail(info.error);
    if (info.architecture != u"clip" && info.type != u"mmproj")
        return fail(u"not a vision projector (an mmproj-….gguf file): it is a model of its own"_s);
    if (!info.visionEncoder)
        return fail(u"a projector without a vision encoder (one for sound?): it sees no pictures"_s);
    return true;
}

std::optional<OwnModel> ownModelFrom(const QString &path, const GgufInfo &info, QString *error, const QString &name)
{
    const auto fail = [&](const QString &why) -> std::optional<OwnModel> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    if (!info.ok)
        return fail(info.error);
    if (info.type == u"adapter")
        return fail(u"a LoRA adapter, not a model: it needs the model it was trained on"_s);
    if (info.architecture == u"clip" || info.type == u"mmproj")
        return fail(u"a vision projector, not a model: add it with its model, under Pictures"_s);
    if (info.architecture.isEmpty())
        return fail(u"the file does not say which kind of model it is (general.architecture)"_s);
    if (embeddingOnly(info))
        return fail(u"a model that embeds text or sound (%1), not one that writes answers"_s.arg(info.architecture));
    if (info.tensorCount == 0)
        return fail(u"the file holds no weights"_s);
    const QString format = local::detectFormat(info.chatTemplate, info.architecture, info.tags);
    if (format.isEmpty())
        return fail(info.chatTemplate.isEmpty()
                            ? u"the file has no chat template, and its vocabulary is of no chat format Brulion writes"_s
                            : u"its chat format is none Brulion writes yet (ChatML, Llama 3, Gemma, Phi-3 and 4, "
                              u"Mistral, DeepSeek, LFM3)"_s);
    OwnModel model;
    model.id = ownModelId(path);
    model.path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    model.format = format;
    model.bytes = info.fileBytes;
    model.parameters = info.parameters;
    model.contextLength = int(std::min<qint64>(info.contextLength, 1 << 30));
    model.architecture = info.architecture;
    model.quantization = quantizationName(info);
    model.sizeLabel = sizeName(info);
    QString named = name.simplified();
    if (named.isEmpty())
        named = info.name.simplified();
    if (named.isEmpty())
        named = info.basename.simplified();
    if (named.isEmpty())
        named = QFileInfo(path).completeBaseName();
    model.name = named.left(80);
    return model;
}

LocalModel localModelOf(const OwnModel &own)
{
    LocalModel model;
    model.id = own.id;
    model.name = own.name;
    model.format = own.format;
    model.own = true;
    model.file = {QFileInfo(own.path).fileName(), QUrl::fromLocalFile(own.path), {}, own.bytes, own.path};
    if (!own.projector.isEmpty())
        model.projector = {QFileInfo(own.projector).fileName(),
                           QUrl::fromLocalFile(own.projector),
                           {},
                           own.projectorBytes,
                           own.projector};
    model.thinks = own.format.endsWith(u"+think"_s);
    model.parameters = own.parameters;
    model.trainedContext = own.contextLength;
    return model;
}

QList<OwnModel> readOwnModels(const QByteArray &json)
{
    QList<OwnModel> list;
    for (const QJsonValue value : QJsonDocument::fromJson(json).object().value(u"models"_s).toArray()) {
        const QJsonObject o = value.toObject();
        OwnModel m;
        m.id = o.value(u"id"_s).toString();
        m.name = o.value(u"name"_s).toString().simplified().left(80);
        m.path = o.value(u"path"_s).toString();
        m.projector = o.value(u"projector"_s).toString();
        m.copied = o.value(u"copied"_s).toBool();
        m.format = o.value(u"format"_s).toString();
        m.bytes = qint64(o.value(u"bytes"_s).toDouble());
        m.projectorBytes = qint64(o.value(u"projectorBytes"_s).toDouble());
        m.parameters = qint64(o.value(u"parameters"_s).toDouble());
        m.contextLength = o.value(u"contextLength"_s).toInt();
        m.architecture = o.value(u"architecture"_s).toString();
        m.quantization = o.value(u"quantization"_s).toString();
        m.sizeLabel = o.value(u"sizeLabel"_s).toString();
        m.memory = qint64(o.value(u"memory"_s).toDouble());
        if (!isOwnModelId(m.id) || m.path.isEmpty() || !local::knowsFormat(m.format))
            continue;
        if (std::ranges::any_of(list, [&](const OwnModel &other) { return other.id == m.id; }))
            continue;
        if (m.name.isEmpty())
            m.name = QFileInfo(m.path).completeBaseName();
        list << m;
    }
    return list;
}

QByteArray writeOwnModels(const QList<OwnModel> &models)
{
    QJsonArray list;
    for (const OwnModel &m : models) {
        QJsonObject o{{u"id"_s, m.id},
                      {u"name"_s, m.name},
                      {u"path"_s, m.path},
                      {u"format"_s, m.format},
                      {u"bytes"_s, double(m.bytes)},
                      {u"parameters"_s, double(m.parameters)},
                      {u"contextLength"_s, m.contextLength},
                      {u"architecture"_s, m.architecture},
                      {u"quantization"_s, m.quantization},
                      {u"sizeLabel"_s, m.sizeLabel},
                      {u"memory"_s, double(m.memory)}};
        if (!m.projector.isEmpty()) {
            o.insert(u"projector"_s, m.projector);
            o.insert(u"projectorBytes"_s, double(m.projectorBytes));
        }
        if (m.copied)
            o.insert(u"copied"_s, true);
        list.append(o);
    }
    return QJsonDocument(QJsonObject{{u"version"_s, 1}, {u"models"_s, list}}).toJson(QJsonDocument::Indented);
}

} // namespace brulion::ai
