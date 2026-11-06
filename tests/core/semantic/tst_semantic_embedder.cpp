// Embedder against a real model — skipped unless BRULION_SEMANTIC_MODEL names a GGUF file of
// multilingual-e5-small or mmlw-e5-small (scripts/semantic/convert_e5_gguf.py).
#include "semantic/embedder.h"

#include <QTest>

#include <cmath>

using namespace brulion::semantic;
using namespace Qt::StringLiterals;

class TestSemanticEmbedder : public QObject {
    Q_OBJECT
    std::unique_ptr<Embedder> m_embedder;

private Q_SLOTS:
    void initTestCase()
    {
        const QString path = qEnvironmentVariable("BRULION_SEMANTIC_MODEL");
        if (path.isEmpty())
            QSKIP("BRULION_SEMANTIC_MODEL is not set");
        QString error;
        Embedder::Options o;
        o.batchTokens = 1024;
        o.maxSequences = 8;
        m_embedder = Embedder::open(path, o, &error);
        QVERIFY2(m_embedder, qPrintable(error));
    }

    void missingFile()
    {
        QString error;
        QVERIFY(!Embedder::open(u"/nie/ma/takiego/modelu.gguf"_s, {}, &error));
        QVERIFY(!error.isEmpty());
    }

    void unitVectors()
    {
        const QList<Vector> vs = m_embedder->embedPassages({u"Zażółć gęślą jaźń"_s, QString(), u"🙂 ∑ 漢字"_s});
        QCOMPARE(vs.size(), 3);
        for (const Vector &v : vs) {
            QCOMPARE(v.size(), m_embedder->dimension());
            QVERIFY(std::abs(std::sqrt(dot(v, v)) - 1) < 1e-4f);
        }
    }

    void meaningOrdersNeighbours()
    {
        const QList<Vector> q = m_embedder->embedQueries({u"wymiana opon zimowych"_s});
        const QList<Vector> p = m_embedder->embedPassages(
            {u"Założyłem opony na zimę w serwisie, koszt 180 zł"_s, u"Przepis na szarlotkę z kruszonką"_s});
        QVERIFY(dot(q.first(), p.at(0)) > dot(q.first(), p.at(1)));
    }

    void batchingDoesNotChangeVectors()
    {
        QStringList texts;
        for (int i = 0; i < 20; ++i)
            texts.append(u"Notatka numer %1 o ogrodzie, pomidorach i podlewaniu"_s.arg(i));
        const QList<Vector> together = m_embedder->embedPassages(texts);
        QCOMPARE(together.size(), 20);
        for (int i : {0, 7, 19}) {
            const Vector alone = m_embedder->embedPassages({texts.at(i)}).first();
            QVERIFY2(dot(alone, together.at(i)) > 0.9999f, qPrintable(QString::number(dot(alone, together.at(i)))));
        }
    }

    void longTextIsCut()
    {
        QString text;
        for (int i = 0; i < 3000; ++i)
            text += u"słowo "_s;
        const qint64 before = m_embedder->tokenCount();
        const QList<Vector> vs = m_embedder->embedPassages({text});
        QCOMPARE(vs.size(), 1);
        QVERIFY(m_embedder->tokenCount() - before <= 512);
    }
};

QTEST_GUILESS_MAIN(TestSemanticEmbedder)
#include "tst_semantic_embedder.moc"
