// brulion-searchbench — search quality and speed of the semantic search prototype against the
// full-text SearchIndex, on a folder of Markdown notes and a file of queries.
//
//   brulion-searchbench --corpus <dir> --queries <file.jsonl> --model <model.gguf> --out <runs.json>
//                       [--calibration <calib.json>] [--vault-calibration] [--cache <vectors.bin>]
//                       [--threads N] [--limit 100] [--alpha 0.8]
//
// Without --model only the full-text run is made. Every *.md file below <dir> is a note (id = path relative to <dir>, title = file name without
// .md). Queries: one JSON object per line with "query" (other keys are ignored). The output holds,
// per system ("bm25", "semantic", "hybrid"), the ranked [id, score] lists of every query, plus
// timings; tools/…/eval scripts compute the metrics. --cache keeps chunk vectors between runs
// (keyed by model file name and chunk text).
#include "index/searchindex.h"
#include "semantic/embedder.h"
#include "semantic/semantic.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

using namespace brulion;
using namespace brulion::semantic;
using namespace Qt::StringLiterals;

namespace {

QTextStream &err()
{
    static QTextStream s(stderr);
    return s;
}

QByteArray chunkKey(const QString &model, const QString &text)
{
    return QCryptographicHash::hash((model + u'\x1f' + text).toUtf8(), QCryptographicHash::Sha1);
}

QHash<QByteArray, Vector> loadCache(const QString &path)
{
    QHash<QByteArray, Vector> cache;
    QFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::ReadOnly))
        return cache;
    QDataStream in(&f);
    in >> cache;
    return in.status() == QDataStream::Ok ? cache : QHash<QByteArray, Vector>{};
}

void saveCache(const QString &path, const QHash<QByteArray, Vector> &cache)
{
    QFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::WriteOnly))
        return;
    QDataStream out(&f);
    out << cache;
}

QJsonArray toJson(const QList<ScoredId> &hits)
{
    QJsonArray a;
    for (const ScoredId &h : hits)
        a.append(QJsonArray{h.id, h.score});
    return a;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser p;
    p.addHelpOption();
    p.addOptions({
        {u"corpus"_s, u"Folder of Markdown notes"_s, u"dir"_s},
        {u"queries"_s, u"JSON lines with a \"query\" key"_s, u"file"_s},
        {u"model"_s, u"GGUF embedding model"_s, u"file"_s},
        {u"out"_s, u"Output JSON"_s, u"file"_s},
        {u"calibration"_s, u"Calibration JSON (queryMean, passageMean, direction)"_s, u"file"_s},
        {u"vault-calibration"_s, u"Estimate passageMean and direction from the corpus itself"_s},
        {u"cache"_s, u"Chunk vector cache"_s, u"file"_s},
        {u"threads"_s, u"CPU threads (0 = half the cores)"_s, u"n"_s, u"0"_s},
        {u"cpu"_s, u"Run the model on the CPU even when a GPU is available"_s},
        {u"any-gpu"_s, u"Also use an integrated GPU (by default only a discrete one)"_s},
        {u"batch-tokens"_s, u"Tokens per llama.cpp call"_s, u"n"_s, u"1024"_s},
        {u"batch-texts"_s, u"Texts per llama.cpp call"_s, u"n"_s, u"32"_s},
        {u"limit"_s, u"Hits per query"_s, u"n"_s, u"100"_s},
        {u"alpha"_s, u"Semantic weight of the hybrid"_s, u"a"_s, QString::number(DefaultSemanticWeight)},
    });
    p.process(app);
    for (const QString &o : {u"corpus"_s, u"queries"_s, u"out"_s}) {
        if (!p.isSet(o)) {
            err() << "missing --" << o << Qt::endl;
            return 2;
        }
    }
    const int limit = p.value(u"limit"_s).toInt();
    const double alpha = p.value(u"alpha"_s).toDouble();

    // Notes.
    const QDir root(p.value(u"corpus"_s));
    QStringList ids, titles, contents;
    QDirIterator it(root.path(), {u"*.md"_s}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        ids.append(root.relativeFilePath(path));
        titles.append(QFileInfo(path).completeBaseName());
        contents.append(QString::fromUtf8(f.readAll()));
    }
    // Queries.
    QStringList queries;
    {
        QFile f(p.value(u"queries"_s));
        if (!f.open(QIODevice::ReadOnly)) {
            err() << "cannot read the queries" << Qt::endl;
            return 2;
        }
        for (const QByteArray &line : f.readAll().split('\n')) {
            const QJsonObject o = QJsonDocument::fromJson(line).object();
            if (o.contains(u"query"_s))
                queries.append(o.value(u"query"_s).toString());
        }
    }
    err() << ids.size() << " notes, " << queries.size() << " queries" << Qt::endl;

    QJsonObject stats;
    QElapsedTimer t;

    // Full text.
    t.start();
    SearchIndex search;
    for (qsizetype i = 0; i < ids.size(); ++i)
        search.set(ids.at(i), titles.at(i), {}, contents.at(i));
    search.squeeze();
    stats.insert(u"bm25IndexMs"_s, double(t.elapsed()));

    if (!p.isSet(u"model"_s)) { // full text only
        QJsonObject bm25Run;
        t.start();
        for (const QString &query : std::as_const(queries)) {
            QList<ScoredId> lexical;
            for (const SearchIndex::Hit &h : search.search(query, limit))
                lexical.append({h.id, h.score});
            bm25Run.insert(query, toJson(lexical));
        }
        stats.insert(u"queryBm25Ms"_s, double(t.elapsed()) / qMax<qsizetype>(1, queries.size()));
        stats.insert(u"notes"_s, double(ids.size()));
        QFile f(p.value(u"out"_s));
        if (!f.open(QIODevice::WriteOnly))
            return 1;
        f.write(QJsonDocument(QJsonObject{{u"bm25"_s, bm25Run}, {u"stats"_s, stats}}).toJson(QJsonDocument::Compact));
        return 0;
    }

    // Chunks and their vectors.
    QString error;
    Embedder::Options options;
    options.threads = p.value(u"threads"_s).toInt();
    options.device = p.isSet(u"cpu"_s)       ? Embedder::Device::Cpu
                     : p.isSet(u"any-gpu"_s) ? Embedder::Device::AnyGpu
                                             : Embedder::Device::Auto;
    options.batchTokens = p.value(u"batch-tokens"_s).toInt();
    options.maxSequences = p.value(u"batch-texts"_s).toInt();
    t.start();
    auto embedder = Embedder::open(p.value(u"model"_s), options, &error);
    if (!embedder) {
        err() << error << Qt::endl;
        return 1;
    }
    stats.insert(u"modelLoadMs"_s, double(t.elapsed()));
    stats.insert(u"device"_s, embedder->device());
    err() << "model on " << embedder->device() << Qt::endl;
    const QString modelKey = QFileInfo(p.value(u"model"_s)).fileName();
    QHash<QByteArray, Vector> cache = loadCache(p.value(u"cache"_s));

    QList<QList<Chunk>> chunks(ids.size());
    QStringList missingTexts;
    QList<QByteArray> missingKeys;
    qint64 chunkCount = 0;
    for (qsizetype i = 0; i < ids.size(); ++i) {
        chunks[i] = chunkNote(titles.at(i), contents.at(i));
        for (const Chunk &c : std::as_const(chunks.at(i))) {
            ++chunkCount;
            const QByteArray key = chunkKey(modelKey, c.text);
            if (!cache.contains(key) && !missingKeys.contains(key)) {
                missingKeys.append(key);
                missingTexts.append(c.text);
            }
        }
    }
    err() << chunkCount << " chunks, " << missingTexts.size() << " to embed" << Qt::endl;
    t.start();
    constexpr qsizetype step = 512;
    for (qsizetype b = 0; b < missingTexts.size(); b += step) {
        const QList<Vector> vs = embedder->embedPassages(missingTexts.mid(b, step));
        if (vs.size() != qMin(step, missingTexts.size() - b)) {
            err() << "embedding failed" << Qt::endl;
            return 1;
        }
        for (qsizetype k = 0; k < vs.size(); ++k)
            cache.insert(missingKeys.at(b + k), vs.at(k));
        err() << "\r  " << qMin(b + step, missingTexts.size()) << "/" << missingTexts.size() << Qt::flush;
    }
    err() << Qt::endl;
    const double embedMs = double(t.elapsed());
    stats.insert(u"chunks"_s, double(chunkCount));
    stats.insert(u"embeddedChunks"_s, double(missingTexts.size()));
    stats.insert(u"embedMs"_s, embedMs);
    stats.insert(u"embedTokens"_s, double(embedder->tokenCount()));
    saveCache(p.value(u"cache"_s), cache);

    t.start();
    VectorIndex vectors;
    for (qsizetype i = 0; i < ids.size(); ++i) {
        QList<Vector> vs;
        for (const Chunk &c : std::as_const(chunks.at(i)))
            vs.append(cache.value(chunkKey(modelKey, c.text)));
        vectors.set(ids.at(i), chunks.at(i), vs);
    }
    Calibration calibration;
    if (p.isSet(u"calibration"_s)) {
        QFile f(p.value(u"calibration"_s));
        if (f.open(QIODevice::ReadOnly))
            calibration = Calibration::fromJson(QJsonDocument::fromJson(f.readAll()).object());
    }
    if (p.isSet(u"vault-calibration"_s)) {
        const Calibration own = vectors.estimateCalibration(calibration.queryMean);
        if (own.direction.isEmpty())
            err() << "vault calibration: too few chunks of one language, keeping the given direction" << Qt::endl;
        else
            calibration.direction = own.direction;
        calibration.passageMean = own.passageMean;
    }
    vectors.setCalibration(calibration);
    stats.insert(u"vectorIndexMs"_s, double(t.elapsed()));

    // Queries.
    QJsonObject bm25Run, semRun, hybridRun;
    qint64 bmNs = 0, qEmbedNs = 0, vecNs = 0, fuseNs = 0;
    QElapsedTimer q;
    for (const QString &query : std::as_const(queries)) {
        q.start();
        QList<ScoredId> lexical;
        for (const SearchIndex::Hit &h : search.search(query, limit))
            lexical.append({h.id, h.score});
        bmNs += q.nsecsElapsed();
        q.start();
        const QList<Vector> qv = embedder->embedQueries({query});
        qEmbedNs += q.nsecsElapsed();
        q.start();
        QList<ScoredId> sem;
        if (!qv.isEmpty()) {
            for (const VectorIndex::Hit &h : vectors.search(qv.first(), limit, calibration.appliesTo(query)))
                sem.append({h.id, h.score});
        }
        vecNs += q.nsecsElapsed();
        q.start();
        const QList<ScoredId> hybrid = fuseConvex(lexical, sem, alpha, limit);
        fuseNs += q.nsecsElapsed();
        bm25Run.insert(query, toJson(lexical));
        semRun.insert(query, toJson(sem));
        hybridRun.insert(query, toJson(hybrid));
    }
    const double nq = qMax<qsizetype>(1, queries.size());
    stats.insert(u"queryBm25Ms"_s, double(bmNs) / 1e6 / nq);
    stats.insert(u"queryEmbedMs"_s, double(qEmbedNs) / 1e6 / nq);
    stats.insert(u"queryVectorMs"_s, double(vecNs) / 1e6 / nq);
    stats.insert(u"queryFuseMs"_s, double(fuseNs) / 1e6 / nq);
    stats.insert(u"notes"_s, double(ids.size()));
    stats.insert(u"threads"_s, options.threads);
    stats.insert(u"batchTokens"_s, options.batchTokens);
    stats.insert(u"batchTexts"_s, options.maxSequences);

    QJsonObject out{{u"bm25"_s, bm25Run}, {u"semantic"_s, semRun}, {u"hybrid"_s, hybridRun}, {u"stats"_s, stats},
                    {u"calibration"_s, calibration.toJson()}};
    QFile f(p.value(u"out"_s));
    if (!f.open(QIODevice::WriteOnly)) {
        err() << "cannot write " << f.fileName() << Qt::endl;
        return 1;
    }
    f.write(QJsonDocument(out).toJson(QJsonDocument::Compact));
    err() << QJsonDocument(stats).toJson(QJsonDocument::Indented);
    return 0;
}
