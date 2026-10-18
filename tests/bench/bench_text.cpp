// Benchmarks of the text module: diacritic folding (foldKey — search index, suggestions,
// natural dates, unlinked references), folding with an offset map, line splitting and content
// hashing, on Polish and ASCII text of 10 KB … 1 MB (the rows expose super-linear scaling).
#include "benchdata.h"

#include "text/text.h"

#include <QTest>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

QString polishText(int kb)
{
    bench::Rng r{quint64(kb)};
    QString s;
    while (s.size() < qsizetype(kb) * 1000) { // kb thousand UTF-16 code units
        s += bench::sentence(r);
        s += r.chance(20) ? u"\n"_s : u" "_s;
    }
    bench::reportInput(u"polishText %1K"_s.arg(kb), s);
    return s;
}

QString asciiText(int kb)
{
    QString s = polishText(kb);
    return text::foldKey(s); // the same text without diacritics
}

void addRows()
{
    QTest::addColumn<QString>("text");
    for (int kb : bench::sizes({10, 100, 1000}, {10, 100, 300})) {
        QTest::newRow(qPrintable(u"polish %1 K chars"_s.arg(kb))) << polishText(kb);
        QTest::newRow(qPrintable(u"ascii %1 K chars"_s.arg(kb))) << asciiText(kb);
    }
}

} // namespace

class BenchText : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void foldKey_data() { addRows(); }
    void foldKey()
    {
        QFETCH(QString, text);
        QBENCHMARK {
            const QString f = text::foldKey(text);
            Q_UNUSED(f);
        }
    }

    void foldKeyWithMap_data() { addRows(); }
    void foldKeyWithMap()
    {
        QFETCH(QString, text);
        QBENCHMARK {
            const text::FoldedText f = text::foldKeyWithMap(text);
            Q_UNUSED(f);
        }
    }

    void normalizedNfc_data() { addRows(); }
    void normalizedNfc()
    {
        QFETCH(QString, text);
        QBENCHMARK {
            const QString f = text.normalized(QString::NormalizationForm_C);
            Q_UNUSED(f);
        }
    }

    void toLower_data() { addRows(); }
    void toLower()
    {
        QFETCH(QString, text);
        QBENCHMARK {
            const QString f = text.toLower();
            Q_UNUSED(f);
        }
    }

    void splitLines_data() { addRows(); }
    void splitLines()
    {
        QFETCH(QString, text);
        QBENCHMARK {
            const auto lines = text::splitLines(text);
            Q_UNUSED(lines);
        }
    }

    void contentHash_data() { addRows(); }
    void contentHash()
    {
        QFETCH(QString, text);
        QBENCHMARK {
            const QString h = text::contentHash(text);
            Q_UNUSED(h);
        }
    }
};

QTEST_GUILESS_MAIN(BenchText)
#include "bench_text.moc"
