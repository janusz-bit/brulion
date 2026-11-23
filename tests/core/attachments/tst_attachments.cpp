// The attachments module (docs/UI.md "Searching attachments"): the device cache's record of a file
// (round trips, one page read alone, malformed and hostile bytes), the manifest, the OCR text helpers,
// the attachments' full-text search (one hit per file at its best page, diacritics, prefixes, names,
// OCR pages, renames, kinds) with excerpts and the viewer's search text, and the search field's
// `in:` filters.
#include "attachments/attachments.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QTest>

using namespace brulion;
using namespace brulion::attachments;
using namespace Qt::StringLiterals;

namespace {

Page textPage(const QString &text, PageSource source = PageSource::TextLayer)
{
    Page p;
    p.source = source;
    p.text = text;
    return p;
}

/// An OCR page of `words` on one line each, boxes in a column.
Page ocrPage(const QStringList &words, const QString &languages = u"pol+eng"_s)
{
    QList<Word> boxes;
    QList<int> lines;
    QList<int> blocks;
    for (qsizetype i = 0; i < words.size(); ++i) {
        Word w;
        w.box = QRectF(0.1, 0.05 + 0.1 * qreal(i), 0.5, 0.05);
        boxes.append(w);
        lines.append(int(i));
        blocks.append(0);
    }
    Page p;
    p.source = PageSource::Ocr;
    p.ocrLanguages = languages;
    p.text = joinOcrWords(words, lines, blocks, boxes);
    p.words = boxes;
    return p;
}

Record lecture()
{
    Record r;
    r.kind = Kind::Pdf;
    r.extractor = 3;
    r.pageCount = 4;
    r.pages = {textPage(u"Wykład 3 — Zażółć gęślą jaźń. Wstęp do algebry liniowej."_s),
               textPage(u"Macierze i wyznaczniki. Żółw idzie powoli przez łąkę."_s),
               textPage(u"12"_s, PageSource::NeedsOcr), ocrPage({u"Skan"_s, u"notatek:"_s, u"źdźbło"_s, u"trawy"_s})};
    return r;
}

QByteArray sha(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

QStringList hitPaths(const QList<AttachmentSearch::Hit> &hits)
{
    QStringList out;
    for (const AttachmentSearch::Hit &h : hits)
        out.append(h.path);
    return out;
}

} // namespace

class TestAttachments : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- record.h ----
    void recordRoundTrip()
    {
        const Record r = lecture();
        const QByteArray bytes = encodeRecord(r);
        const std::optional<Record> back = decodeRecord(bytes);
        QVERIFY(back);
        QCOMPARE(back->kind, Kind::Pdf);
        QCOMPARE(back->extractor, 3u);
        QCOMPARE(back->pageCount, 4);
        QCOMPARE(back->pages.size(), 4);
        QCOMPARE(back->pages.at(0).text, r.pages.at(0).text);
        QCOMPARE(back->pages.at(2).source, PageSource::NeedsOcr);
        QCOMPARE(back->pages.at(3).source, PageSource::Ocr);
        QCOMPARE(back->pages.at(3).ocrLanguages, u"pol+eng"_s);
        QCOMPARE(back->pages.at(3).words.size(), 4);
        // Boxes in steps of 1/65535.
        const QRectF box = back->pages.at(3).words.at(2).box;
        QVERIFY(qAbs(box.left() - 0.1) < 1e-4 && qAbs(box.top() - 0.25) < 1e-4 && qAbs(box.width() - 0.5) < 1e-4);
        QCOMPARE(back->pages.at(3).words.at(2).start, r.pages.at(3).words.at(2).start);
        // The same bytes again.
        QCOMPARE(encodeRecord(*back), bytes);
    }

    void recordFlags()
    {
        Record locked;
        locked.locked = true;
        const std::optional<Record> back = decodeRecord(encodeRecord(locked));
        QVERIFY(back && back->locked && !back->unreadable && back->pages.isEmpty() && back->pageCount == 0);
        Record picture;
        picture.kind = Kind::Picture;
        picture.unreadable = true;
        picture.pageCount = 1;
        picture.pages = {Page()};
        const std::optional<Record> p = decodeRecord(encodeRecord(picture));
        QVERIFY(p && p->kind == Kind::Picture && p->unreadable && p->pages.size() == 1);
        QCOMPARE(p->pages.at(0).source, PageSource::Unread);
    }

    void onePage()
    {
        const QByteArray bytes = encodeRecord(lecture());
        const std::optional<Page> second = decodeRecordPage(bytes, 2);
        QVERIFY(second);
        QVERIFY(second->text.contains(u"Żółw"_s));
        QVERIFY(!decodeRecordPage(bytes, 0));
        QVERIFY(!decodeRecordPage(bytes, 5));
        // From a device: the header and that page only.
        QBuffer buffer;
        buffer.setData(bytes);
        QVERIFY(buffer.open(QIODevice::ReadOnly));
        const std::optional<Page> fourth = readRecordPage(buffer, 4);
        QVERIFY(fourth);
        QCOMPARE(fourth->words.size(), 4);
        const std::optional<RecordInfo> info = readRecordInfo(buffer);
        QVERIFY(info);
        QCOMPARE(info->sources, (QList<PageSource>{PageSource::TextLayer, PageSource::TextLayer, PageSource::NeedsOcr,
                                                   PageSource::Ocr}));
        QCOMPARE(decodeRecordInfo(bytes)->pageCount, 4);
    }

    void malformedRecords_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        const QByteArray good = encodeRecord(lecture());
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("magic only") << QByteArray("BRAT");
        QTest::newRow("wrong magic") << QByteArray("BRAX") + good.mid(4);
        QTest::newRow("cut header") << good.left(30);
        QTest::newRow("cut page") << good.left(good.size() - 3);
        QByteArray format = good;
        format[4] = char(99);
        QTest::newRow("other format") << format;
        QByteArray table = good;
        table[30] = char(table.at(30) ^ 0x40); // inside the page table: the header's checksum fails
        QTest::newRow("page table") << table;
        QByteArray text = good;
        text[text.size() - 20] = char(text.at(text.size() - 20) ^ 0x01); // a page's bytes
        QTest::newRow("page bytes") << text;
        QByteArray huge = good;
        huge[20] = char(0xFF); // storedPages
        huge[21] = char(0xFF);
        QTest::newRow("huge page count") << huge;
        QTest::newRow("garbage") << QByteArray(4096, char(0x5A));
    }
    void malformedRecords()
    {
        QFETCH(QByteArray, bytes);
        QVERIFY(!decodeRecord(bytes));
        QVERIFY(!decodeRecordInfo(bytes) || !decodeRecord(bytes));
        QBuffer buffer;
        buffer.setData(bytes);
        QVERIFY(buffer.open(QIODevice::ReadOnly));
        // (No page of a broken record is trusted beyond its own checksum.)
        const std::optional<Page> page = readRecordPage(buffer, 4);
        QVERIFY(!page || page->words.size() == 4);
    }

    void randomBytesNeverCrash()
    {
        QRandomGenerator rng(20261120);
        const QByteArray good = encodeRecord(lecture());
        for (int round = 0; round < 300; ++round) {
            QByteArray bytes = good;
            for (int k = 0; k < 4; ++k)
                bytes[rng.bounded(int(bytes.size()))] = char(rng.bounded(256));
            if (rng.bounded(4) == 0)
                bytes.truncate(rng.bounded(int(bytes.size())));
            const std::optional<Record> r = decodeRecord(bytes);
            if (r) {
                for (const Page &p : r->pages) {
                    for (const Word &w : p.words)
                        QVERIFY(w.start >= 0 && qsizetype(w.start) + w.length <= p.text.size());
                }
            }
            (void)decodeRecordPage(bytes, 1 + rng.bounded(5));
        }
    }

    void capsOnWrite()
    {
        Record r;
        r.pageCount = 1;
        Page p = textPage(QString(MaxPageText + 500, u'ą'));
        // A word beyond the text is left out, the others kept.
        p.words = {Word{0, 3, QRectF(0, 0, 0.1, 0.1)}, Word{qint32(MaxPageText + 10), 4, QRectF()}};
        r.pages = {p};
        const std::optional<Record> back = decodeRecord(encodeRecord(r));
        QVERIFY(back);
        QCOMPARE(back->pages.at(0).text.size(), MaxPageText);
        QCOMPARE(back->pages.at(0).words.size(), 1);
    }

    void ocrText()
    {
        QList<Word> words(5);
        for (qsizetype i = 0; i < words.size(); ++i)
            words[i].box = QRectF(0.1 * qreal(i), 0.2, 0.08, 0.03);
        const QString text = joinOcrWords({u"Zażółć"_s, u"gęślą"_s, u"jaźń"_s, u"Druga"_s, u"linia"_s}, {0, 0, 0, 1, 1},
                                          {0, 0, 0, 0, 0}, words);
        QCOMPARE(text, u"Zażółć gęślą jaźń\nDruga linia"_s);
        QCOMPARE(words.at(1).start, 7);
        QCOMPARE(words.at(1).length, 5);
        const QString blocks = joinOcrWords({u"a"_s, u"b"_s}, {0, 1}, {0, 1}, words);
        QCOMPARE(blocks, u"a\n\nb"_s);
        // Character boxes: a word's box shared by its letters, nothing between words.
        Page page;
        page.source = PageSource::Ocr;
        QList<Word> two(2);
        two[0].box = QRectF(0.0, 0.5, 0.4, 0.1);
        two[1].box = QRectF(0.5, 0.5, 0.2, 0.1);
        page.text = joinOcrWords({u"żółw"_s, u"ab"_s}, {0, 0}, {0, 0}, two);
        page.words = two;
        const QList<QRectF> boxes = ocrCharBoxes(page);
        QCOMPARE(boxes.size(), page.text.size());
        QVERIFY(qAbs(boxes.at(1).left() - 0.1) < 1e-9 && qAbs(boxes.at(1).width() - 0.1) < 1e-9);
        QVERIFY(boxes.at(4).isNull()); // the space
        QVERIFY(qAbs(boxes.at(6).left() - 0.6) < 1e-9);
        QVERIFY(ocrCharBoxes(textPage(u"bez słów"_s)).isEmpty());
    }

    // ---- manifest.h ----
    void manifestRoundTrip()
    {
        Manifest m;
        m.insert(u"assets/Wykład – ćwiczenia.pdf"_s, ManifestEntry{123456, 1763600000123, sha("a")});
        m.insert(u"zdjęcia/tablica.jpg"_s, ManifestEntry{42, -5, sha("b")});
        const QByteArray bytes = encodeManifest(m);
        const std::optional<Manifest> back = decodeManifest(bytes);
        QVERIFY(back);
        QCOMPARE(*back, m);
        QCOMPARE(encodeManifest(*back), bytes);
        // An entry without a whole hash is not written.
        Manifest bad = m;
        bad.insert(u"x.pdf"_s, ManifestEntry{1, 1, QByteArray("short")});
        QCOMPARE(decodeManifest(encodeManifest(bad))->size(), 2);
        QVERIFY(decodeManifest(encodeManifest({})) && decodeManifest(encodeManifest({}))->isEmpty());
    }

    void malformedManifest()
    {
        QByteArray bytes = encodeManifest({{u"a.pdf"_s, ManifestEntry{1, 2, sha("x")}}});
        QVERIFY(decodeManifest(bytes));
        QVERIFY(!decodeManifest(bytes.left(bytes.size() - 1)));
        QByteArray flipped = bytes;
        flipped[14] = char(flipped.at(14) ^ 1);
        QVERIFY(!decodeManifest(flipped));
        QVERIFY(!decodeManifest(QByteArray("BRAM")));
        QVERIFY(!decodeManifest(QByteArray(100, 'x')));
    }

    void hashing()
    {
        QByteArray data(3 * 1024 * 1024 + 17, 'z');
        data[12345] = char(0xC5);
        QBuffer buffer(&data);
        QVERIFY(buffer.open(QIODevice::ReadOnly));
        QCOMPARE(hashFile(buffer).value_or(QByteArray()), sha(data));
        QVERIFY(buffer.seek(0));
        int asked = 0;
        QVERIFY(!hashFile(buffer, [&asked] { return ++asked > 1; }));
        QCOMPARE(recordFileName(sha("a")).size(), 64 + 4);
        QVERIFY(recordFileName(sha("a")).endsWith(u".bat"_s));
    }

    // ---- attachmentsearch.h ----
    void searchPages()
    {
        AttachmentSearch s;
        s.set(AttachmentSearch::prepare(u"assets/Wykład 3.pdf"_s, lecture()));
        Record photo;
        photo.kind = Kind::Picture;
        photo.pageCount = 1;
        photo.pages = {ocrPage({u"Tablica"_s, u"żółw"_s, u"zielony"_s})};
        s.set(AttachmentSearch::prepare(u"zdjęcia/tablica.jpg"_s, photo));
        QCOMPARE(s.fileCount(), 2);
        QCOMPARE(s.pageCount(), 5);
        QCOMPARE(s.indexedPages(u"assets/Wykład 3.pdf"_s), 4);

        // Diacritics optional; one hit per file at its best page.
        QList<AttachmentSearch::Hit> hits = s.search(u"zolw"_s, 10);
        QCOMPARE(hits.size(), 2);
        const auto pdf = std::ranges::find_if(hits, [](const auto &h) { return h.kind == Kind::Pdf; });
        QVERIFY(pdf != hits.cend());
        QCOMPARE(pdf->page, 2);
        QCOMPARE(pdf->matchedPages, 1);
        QCOMPARE(pdf->pageCount, 4);
        QVERIFY(!pdf->ocr && !pdf->nameOnly);
        const auto picture = std::ranges::find_if(hits, [](const auto &h) { return h.kind == Kind::Picture; });
        QVERIFY(picture->ocr);
        QCOMPARE(picture->page, 1);

        // An OCR page of the PDF.
        hits = s.search(u"zdzblo"_s, 10);
        QCOMPARE(hitPaths(hits), QStringList{u"assets/Wykład 3.pdf"_s});
        QCOMPARE(hits.at(0).page, 4);
        QVERIFY(hits.at(0).ocr);

        // The last word as a prefix.
        hits = s.search(u"wyzn"_s, 10);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.at(0).page, 2);

        // The file's name.
        hits = s.search(u"wyklad"_s, 10);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.at(0).page, 1); // the name and page 1 ("Wykład 3 — …")
        hits = s.search(u"tablica"_s, 10);
        QCOMPARE(hitPaths(hits), QStringList{u"zdjęcia/tablica.jpg"_s});

        // Kinds.
        QCOMPARE(hitPaths(s.search(u"zolw"_s, 10, Scope::Pictures)), QStringList{u"zdjęcia/tablica.jpg"_s});
        QCOMPARE(hitPaths(s.search(u"zolw"_s, 10, Scope::Pdf)), QStringList{u"assets/Wykład 3.pdf"_s});
        QVERIFY(s.search(u"zolw"_s, 10, Scope::Pages).isEmpty());
        // The limit counts files.
        QCOMPARE(s.search(u"zolw"_s, 1).size(), 1);
        QVERIFY(s.search(u"   "_s, 10).isEmpty());
        QVERIFY(s.search(u"nieistniejące"_s, 10).isEmpty());
    }

    void nameOnlyHit()
    {
        AttachmentSearch s;
        Record r;
        r.pageCount = 2;
        r.pages = {textPage(u"Pierwsza strona tekstu."_s), textPage(u"Druga strona: notatki z laboratorium."_s)};
        s.set(AttachmentSearch::prepare(u"Raport końcowy.pdf"_s, r));
        const QList<AttachmentSearch::Hit> name = s.search(u"raport"_s, 5);
        QCOMPARE(name.size(), 1);
        QVERIFY(name.at(0).nameOnly);
        QCOMPARE(name.at(0).matchedPages, 0);
        QCOMPARE(name.at(0).page, 1);
        // The name and a page: the page's words show.
        const QList<AttachmentSearch::Hit> both = s.search(u"raport laboratorium"_s, 5);
        QCOMPARE(both.size(), 1);
        QCOMPARE(both.at(0).page, 2);
        QVERIFY(!both.at(0).nameOnly);
    }

    void manyPagesOneHit()
    {
        AttachmentSearch s;
        Record r;
        r.pageCount = 300;
        for (int i = 1; i <= 300; ++i)
            r.pages.append(textPage(i % 3 == 0 ? u"Strona %1: całka oznaczona i pole pod wykresem."_s.arg(i)
                                               : u"Strona %1: inne zagadnienia."_s.arg(i)));
        s.set(AttachmentSearch::prepare(u"skrypt.pdf"_s, r));
        const QList<AttachmentSearch::Hit> hits = s.search(u"calka"_s, 10);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.at(0).matchedPages, 100);
        QVERIFY(hits.at(0).page % 3 == 0);
    }

    void setPageAndRename()
    {
        AttachmentSearch s;
        Record r;
        r.pageCount = 2;
        r.pages = {textPage(u"Spis treści i wstęp do kursu"_s), textPage(u"7"_s, PageSource::NeedsOcr)};
        s.set(AttachmentSearch::prepare(u"kurs.pdf"_s, r));
        QVERIFY(s.search(u"pochodna"_s, 5).isEmpty());
        // OCR read page 2.
        s.setPage(u"kurs.pdf"_s, AttachmentSearch::preparePage(2, ocrPage({u"Pochodna"_s, u"funkcji"_s})));
        QList<AttachmentSearch::Hit> hits = s.search(u"pochodna"_s, 5);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.at(0).page, 2);
        QVERIFY(hits.at(0).ocr);
        // A page with no words any more leaves.
        s.setPage(u"kurs.pdf"_s, AttachmentSearch::preparePage(2, textPage(u""_s)));
        QVERIFY(s.search(u"pochodna"_s, 5).isEmpty());
        QCOMPARE(s.indexedPages(u"kurs.pdf"_s), 1);
        // Unknown files take no pages.
        s.setPage(u"inny.pdf"_s, AttachmentSearch::preparePage(1, textPage(u"pochodna"_s)));
        QVERIFY(!s.contains(u"inny.pdf"_s));

        // A rename keeps the pages; the name follows.
        const quint64 before = s.revision();
        s.rename(u"kurs.pdf"_s, u"Archiwum/Analiza matematyczna.pdf"_s);
        QVERIFY(s.revision() != before);
        QVERIFY(!s.contains(u"kurs.pdf"_s));
        hits = s.search(u"wstep"_s, 5);
        QCOMPARE(hitPaths(hits), QStringList{u"Archiwum/Analiza matematyczna.pdf"_s});
        QCOMPARE(hitPaths(s.search(u"analiza"_s, 5)), QStringList{u"Archiwum/Analiza matematyczna.pdf"_s});
        QVERIFY(s.search(u"kurs"_s, 5).size() == 1); // (still in its text: "kursu")
        s.remove(u"Archiwum/Analiza matematyczna.pdf"_s);
        QCOMPARE(s.fileCount(), 0);
        QVERIFY(s.search(u"wstep"_s, 5).isEmpty());
    }

    void setReplaces()
    {
        AttachmentSearch s;
        Record a;
        a.pageCount = 1;
        a.pages = {textPage(u"stara treść o kotach"_s)};
        s.set(AttachmentSearch::prepare(u"a.pdf"_s, a));
        a.pages = {textPage(u"nowa treść o psach"_s)};
        s.set(AttachmentSearch::prepare(u"a.pdf"_s, a));
        QVERIFY(s.search(u"kotach"_s, 5).isEmpty());
        QCOMPARE(s.search(u"psach"_s, 5).size(), 1);
        QCOMPARE(s.fileCount(), 1);
    }

    void excerptsAndFind()
    {
        AttachmentSearch s;
        s.set(AttachmentSearch::prepare(u"assets/Wykład 3.pdf"_s, lecture()));
        const Record r = lecture();
        const auto pageText = [&r](const QString &, int page) -> std::optional<Page> {
            if (page < 1 || page > r.pages.size())
                return std::nullopt;
            return r.pages.at(page - 1);
        };
        QList<AttachmentSearch::Hit> hits = s.search(u"zolw"_s, 5, AttachmentSearch::AllKinds, pageText);
        QCOMPARE(hits.size(), 1);
        QVERIFY2(hits.at(0).excerpt.contains(u"Żółw idzie powoli"_s), qPrintable(hits.at(0).excerpt));
        QCOMPARE(hits.at(0).find, u"Żółw"_s);
        // A phrase on the page: searched as typed.
        hits = s.search(u"zolw idzie"_s, 5, AttachmentSearch::AllKinds, pageText);
        QCOMPARE(hits.at(0).find, u"zolw idzie"_s);
        // Words apart: the first one the page holds.
        hits = s.search(u"lake zolw"_s, 5, AttachmentSearch::AllKinds, pageText);
        QCOMPARE(hits.at(0).find, u"łąkę"_s);

        // The excerpt's window: words cut at spaces, "…" where cut, white space squashed.
        QString long_;
        for (int i = 0; i < 40; ++i)
            long_ += u"słowo%1 "_s.arg(i);
        long_ += u"\n\n  szukany   żółw  "_s;
        for (int i = 0; i < 40; ++i)
            long_ += u"dalej%1 "_s.arg(i);
        const QString e = AttachmentSearch::excerpt(long_, {u"zolw"_s}, 80);
        QVERIFY2(e.startsWith(u"… "_s) && e.endsWith(u" …"_s), qPrintable(e));
        QVERIFY2(e.contains(u"szukany żółw dalej0"_s), qPrintable(e));
        QVERIFY(e.size() <= 80 + 4);
        // Most terms together win.
        const QString twoText = u"kot tutaj. "_s + QString(300, u'x') + u" kot i pies razem"_s;
        const QString two = AttachmentSearch::excerpt(twoText, {u"kot"_s, u"pies"_s}, 60);
        QVERIFY2(two.contains(u"kot i pies"_s), qPrintable(two));
        // Whole tokens only: "kot" is no hit in "kotlet".
        const QString tokenText = u"kotlet "_s + QString(200, u'y') + u" mój kot"_s;
        const QString token = AttachmentSearch::excerpt(tokenText, {u"kot"_s}, 40);
        QVERIFY2(token.contains(u"mój kot"_s), qPrintable(token));
        // No term: the start.
        QCOMPARE(AttachmentSearch::excerpt(u"  Krótki\ttekst  "_s, {}), u"Krótki tekst"_s);
        QCOMPARE(AttachmentSearch::excerpt(u""_s, {u"x"_s}), QString());
        // Soft hyphens dropped.
        QCOMPARE(AttachmentSearch::excerpt(u"gęś­lą"_s, {}), u"gęślą"_s);
    }

    void findTextFallbacks()
    {
        QCOMPARE(AttachmentSearch::findText(u"ZAŻÓŁĆ GĘŚLĄ jaźń"_s, u"gesla"_s, {u"gesla"_s}), u"GĘŚLĄ"_s);
        QCOMPARE(AttachmentSearch::findText(u"ZAŻÓŁĆ GĘŚLĄ jaźń"_s, u"gesla jazn"_s, {u"gesla"_s, u"jazn"_s}),
                 u"gesla jazn"_s);
        QCOMPARE(AttachmentSearch::findText(u"Ala ma kota"_s, u"psa"_s, {u"kotem"_s}), u"kotem"_s);
        QCOMPARE(AttachmentSearch::findText(u"Ala ma kota"_s, u"  ala   ma "_s, {}), u"ala ma"_s);
        // A ligature folds to more units: the page's own spelling still comes back.
        QCOMPARE(AttachmentSearch::findText(u"ﬁlm o żubrach"_s, u"zubr"_s, {u"zubrach"_s}), u"żubrach"_s);
    }

    void hostileQuery()
    {
        AttachmentSearch s;
        s.set(AttachmentSearch::prepare(u"a.pdf"_s, lecture()));
        QString paste;
        for (int i = 0; i < 200; ++i)
            paste += u"wyraz%1 "_s.arg(i);
        (void)s.search(paste, 10);
        (void)s.search(QString(10000, u'ż'), 10);
        QVERIFY(s.search(u"zolw"_s, 0).isEmpty());
    }

    // ---- searchfilters.h ----
    void filters_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("scopes");
        QTest::addColumn<bool>("filtered");
        const int all = int(AllScopes.toInt());
        const int att = int(AttachmentScopes.toInt());
        QTest::newRow("none") << u"żółw zielony"_s << u"żółw zielony"_s << all << false;
        QTest::newRow("in:attachments") << u"in:attachments żółw"_s << u"żółw"_s << att << true;
        QTest::newRow("type:attachments") << u"żółw type:attachments"_s << u"żółw"_s << att << true;
        QTest::newRow("w:załączniki") << u"w:załączniki żółw"_s << u"żółw"_s << att << true;
        QTest::newRow("w:zalaczniki") << u"W:ZALACZNIKI żółw"_s << u"żółw"_s << att << true;
        QTest::newRow("in:pdf") << u"in:pdf całka"_s << u"całka"_s << int(Scope::Pdf) << true;
        QTest::newRow("in:pictures") << u"in:pictures tablica"_s << u"tablica"_s << int(Scope::Pictures) << true;
        QTest::newRow("typ:zdjęcie") << u"typ:zdjęcie tablica"_s << u"tablica"_s << int(Scope::Pictures) << true;
        QTest::newRow("in:documents") << u"in:documents umowa"_s << u"umowa"_s << int(Scope::Documents) << true;
        QTest::newRow("w:dokumenty") << u"W:Dokumenty umowa"_s << u"umowa"_s << int(Scope::Documents) << true;
        QTest::newRow("type:docs") << u"umowa type:docs"_s << u"umowa"_s << int(Scope::Documents) << true;
        QTest::newRow("pdf + documents") << u"in:pdf in:dokument x"_s << u"x"_s
                                         << int((Scope::Pdf | Scope::Documents).toInt()) << true;
        QTest::newRow("pdf + pictures") << u"in:pdf in:obrazy x"_s << u"x"_s
                                       << int((Scope::Pdf | Scope::Pictures).toInt()) << true;
        QTest::newRow("all three") << u"in:pdf in:obrazy in:docs x"_s << u"x"_s << att << true;
        QTest::newRow("in:notes") << u"in:notes x"_s << u"x"_s << int(NoteScopes.toInt()) << true;
        QTest::newRow("in:dziennik") << u"w:dziennik x"_s << u"x"_s << int(Scope::Journals) << true;
        QTest::newRow("in:strony") << u"w:strony x"_s << u"x"_s << int(Scope::Pages) << true;
        QTest::newRow("unknown value") << u"in:xyz kot"_s << u"in:xyz kot"_s << all << false;
        QTest::newRow("a time") << u"spotkanie 12:30"_s << u"spotkanie 12:30"_s << all << false;
        QTest::newRow("an address") << u"https://example.org"_s << u"https://example.org"_s << all << false;
        QTest::newRow("empty value") << u"in: kot"_s << u"in: kot"_s << all << false;
        QTest::newRow("only a filter") << u"in:pdf"_s << QString() << int(Scope::Pdf) << true;
        QTest::newRow("spaces") << u"  in:pdf   całka   oznaczona  "_s << u"całka oznaczona"_s << int(Scope::Pdf)
                                << true;
        QTest::newRow("empty") << QString() << QString() << all << false;
    }
    void filters()
    {
        QFETCH(QString, query);
        QFETCH(QString, text);
        QFETCH(int, scopes);
        QFETCH(bool, filtered);
        const SearchFilters f = parseSearchFilters(query);
        QCOMPARE(f.text, text);
        QCOMPARE(int(f.scopes.toInt()), scopes);
        QCOMPARE(f.filtered, filtered);
    }
};

QTEST_GUILESS_MAIN(TestAttachments)
#include "tst_attachments.moc"
