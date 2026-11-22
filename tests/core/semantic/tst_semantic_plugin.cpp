// The plugin of search by meaning (src/core/semantic/semanticrunner.h: brulion_semantic, loaded the
// way the app and brulion-cli load it), in builds that have it: it is found and loaded with
// llama.cpp from its own folder, a missing or broken model is an error and not a crash. With a model:
//
//   BRULION_SEMANTIC_MODEL=~/.cache/brulion-models/multilingual-e5-small-q8_0.gguf ctest -R tst_semantic_plugin
//
// it embeds passages and queries — the meaning orders a query's neighbours, a Polish and an English
// text of one meaning meet —, and a query is embedded while passages are (two threads, two model
// contexts) with the same vectors as alone.

#include "semantic/semantic.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <cmath>

using namespace Qt::StringLiterals;
using namespace brulion;
using semantic::SemanticRunner;

class TestSemanticPlugin : public QObject {
    Q_OBJECT

    std::unique_ptr<SemanticRunner> m_runner;

private Q_SLOTS:
    void isFound()
    {
        const QString dir = QStringLiteral(BRULION_LOCAL_AI_OUTPUT_DIR);
        QVERIFY(semantic::hasSemanticRunner({dir}));
        QTemporaryDir empty;
        QVERIFY(!semantic::hasSemanticRunner({empty.path()}));
        QVERIFY(!semantic::hasSemanticRunner({}));
        QVERIFY(semantic::hasSemanticRunner({empty.path(), dir}));
        // By its name alone (Android): nothing to look at, so it is taken to be there.
        QVERIFY(semantic::hasSemanticRunner({QString()}));

        QString error;
        SemanticRunner::Options options;
        options.modelPath = u"/nonexistent/e5.gguf"_s;
        QVERIFY(!semantic::loadSemanticRunner({empty.path()}, options, &error));
        QVERIFY2(error.contains(u"searches nothing by meaning"), qPrintable(error));
    }

    void missingAndBrokenModel()
    {
        const QString dir = QStringLiteral(BRULION_LOCAL_AI_OUTPUT_DIR);
        QString error;
        SemanticRunner::Options options;
        options.modelPath = u"/nonexistent/e5.gguf"_s;
        QVERIFY(!semantic::loadSemanticRunner({dir}, options, &error));
        QVERIFY2(error.contains(u"cannot load") || error.contains(u"too old"), qPrintable(error));

        QTemporaryDir tmp;
        const QString broken = tmp.filePath(u"zły model ąę.gguf"_s);
        QFile f(broken);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("GGUF");
        f.write(QByteArray(4096, 'x'));
        f.close();
        options.modelPath = broken;
        error.clear();
        QVERIFY(!semantic::loadSemanticRunner({dir}, options, &error));
        QVERIFY(!error.isEmpty());
    }

    void realModel()
    {
        const QString model = qEnvironmentVariable("BRULION_SEMANTIC_MODEL");
        if (model.isEmpty())
            QSKIP("BRULION_SEMANTIC_MODEL is not set");
        QString error;
        SemanticRunner::Options options;
        options.modelPath = model;
        m_runner = semantic::loadSemanticRunner({QStringLiteral(BRULION_LOCAL_AI_OUTPUT_DIR)}, options, &error);
        QVERIFY2(m_runner, qPrintable(error));
        QCOMPARE(m_runner->dimension(), 384);
        QVERIFY2(m_runner->device().contains(u"thread"_s), qPrintable(m_runner->device()));

        const QList<semantic::Chunk> chunks = semantic::noteChunks(u"Szarlotka babci"_s,
                                                                   u"- jabłka, kruszonka, cynamon\n- piec 45 minut\n"_s);
        const QList<semantic::Vector> passages = m_runner->embedPassages(
                {chunks.first().text, u"Wymiana opon zimowych w serwisie, 180 zł"_s,
                 u"Thai green curry with coconut milk"_s});
        QCOMPARE(passages.size(), 3);
        for (const semantic::Vector &v : passages)
            QVERIFY(std::abs(std::sqrt(semantic::dot(v, v)) - 1) < 1e-4f);
        // A query finds its meaning, in either language.
        const semantic::Vector cake = m_runner->embedQuery(u"apple cake"_s);
        QVERIFY(semantic::dot(cake, passages.at(0)) > semantic::dot(cake, passages.at(1)));
        const semantic::Vector tyres = m_runner->embedQuery(u"kupiłem opony"_s);
        QVERIFY(semantic::dot(tyres, passages.at(1)) > semantic::dot(tyres, passages.at(0)));
        QVERIFY(semantic::dot(tyres, passages.at(1)) > semantic::dot(tyres, passages.at(2)));
    }

    void queryWhilePassages()
    {
        if (!m_runner)
            QSKIP("BRULION_SEMANTIC_MODEL is not set");
        const semantic::Vector alone = m_runner->embedQuery(u"przepis na chleb"_s);
        QStringList many;
        for (int i = 0; i < 64; ++i)
            many.append(u"Notatka %1: zakupy, ogród, pomidory i podlewanie w lipcu"_s.arg(i));
        QList<semantic::Vector> passages;
        QThread *thread = QThread::create([&] { passages = m_runner->embedPassages(many); });
        thread->start();
        QList<semantic::Vector> queries;
        while (!thread->isFinished() || queries.size() < 3)
            queries.append(m_runner->embedQuery(u"przepis na chleb"_s));
        thread->wait();
        delete thread;
        QCOMPARE(passages.size(), 64);
        for (const semantic::Vector &q : std::as_const(queries))
            QVERIFY(semantic::dot(q, alone) > 0.9999f);
    }
};

QTEST_GUILESS_MAIN(TestSemanticPlugin)
#include "tst_semantic_plugin.moc"
