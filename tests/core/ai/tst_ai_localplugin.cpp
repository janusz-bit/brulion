// The plugin with the model runner (src/core/ai/localrunner.h: brulion_localai, loaded the way
// the app loads it), in builds that have it: it is found and loaded with llama.cpp's libraries
// from its own folder, a missing model is an error and not a crash; with BRULION_LOCAL_MODEL set
// (tst_ai_llama.cpp) a model answers through it.

#include "ai/localrunner.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using ai::LocalRunner;

class TestAiLocalPlugin : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void isFound()
    {
        const QString dir = QStringLiteral(BRULION_LOCAL_AI_OUTPUT_DIR);
        QVERIFY(ai::hasLocalRunner({dir}));
        QTemporaryDir empty;
        QVERIFY(!ai::hasLocalRunner({empty.path()}));
        QVERIFY(!ai::hasLocalRunner({}));
        QVERIFY(ai::hasLocalRunner({empty.path(), dir}));
        // By its name alone (Android): nothing to look at, so it is taken to be there.
        QVERIFY(ai::hasLocalRunner({QString()}));

        // A folder without it: an error that says so, nothing loaded.
        QString error;
        LocalRunner::Options options;
        options.modelPath = u"/nonexistent/model.gguf"_s;
        QVERIFY(!ai::loadLocalRunner({empty.path()}, options, &error));
        QVERIFY2(error.contains(u"runs no model"), qPrintable(error));
    }

    void loadsAndOpens()
    {
        // The plugin loads llama.cpp from next to itself and the processor plugin this machine can
        // run; the model's file is missing.
        const QString dir = QStringLiteral(BRULION_LOCAL_AI_OUTPUT_DIR);
        QString error;
        LocalRunner::Options options;
        options.modelPath = u"/nonexistent/model.gguf"_s;
        QVERIFY(!ai::loadLocalRunner({dir}, options, &error));
        QVERIFY2(error.contains(u"/nonexistent/model.gguf") || error.contains(u"too old"), qPrintable(error));
        // No model at all: only the check that the plugin and llama.cpp load here (brulion-cli
        // version --plugins) — nothing opened, no error.
        options.modelPath.clear();
        error = u"(not cleared)"_s;
        QVERIFY(!ai::loadLocalRunner({dir}, options, &error));
        QVERIFY2(error.isEmpty() || error.contains(u"too old"), qPrintable(error));

        const QString model = qEnvironmentVariable("BRULION_LOCAL_MODEL");
        if (model.isEmpty())
            QSKIP("BRULION_LOCAL_MODEL is not set: no model to run");
        options.modelPath = model;
        options.contextTokens = 2048;
        const std::unique_ptr<LocalRunner> runner = ai::loadLocalRunner({dir}, options, &error);
        QVERIFY2(runner, qPrintable(error));
        ai::Request request;
        request.effort = u"low"_s;
        request.messages << ai::textMessage(u"user"_s, u"Say OK."_s);
        LocalRunner::Params params;
        params.maxTokens = 32;
        params.reserveTokens = 64;
        const LocalRunner::Result r = runner->generate(ai::local::prompt(u"lfm3"_s, request), params);
        QVERIFY2(r.status == LocalRunner::Ok, qPrintable(r.error));
        QVERIFY2(ai::local::reply(u"lfm3"_s, r.pieces, request, r.complete).text.contains(u"OK", Qt::CaseInsensitive),
                 qPrintable(ai::local::reply(u"lfm3"_s, r.pieces, request, r.complete).text));
    }
};

QTEST_GUILESS_MAIN(TestAiLocalPlugin)
#include "tst_ai_localplugin.moc"
