// Quick capture lines (notebook::buildCaptureLine): Polish and English phrases become tasks with a
// reminder (⏰, a time given) or a due date (📅), notes about the past stay notes, an existing
// token or checkbox is kept, further lines are continuation lines. The app's quick capture and
// `brulion-cli capture` both write these.
#include "notebook/capture.h"

#include <QTest>

using namespace Qt::StringLiterals;
using brulion::notebook::buildCaptureLine;
using brulion::notebook::captureKindName;
using brulion::notebook::CaptureLine;

class TestNotebookCapture : public QObject {
    Q_OBJECT

    const QDateTime m_now{QDate(2026, 10, 14), QTime(10, 30)}; // a Wednesday

private Q_SLOTS:
    void captureLine_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("block");
        QTest::addColumn<QString>("kind");

        QTest::newRow("reminder") << u"Kupić mleko jutro o 18"_s << u"- [ ] Kupić mleko ⏰ 2026-10-15 18:00"_s
                                  << u"reminder"_s;
        QTest::newRow("weekday due") << u"Oddać książkę w piątek"_s << u"- [ ] Oddać książkę 📅 2026-10-16"_s
                                     << u"due"_s;
        QTest::newRow("in two hours") << u"Zadzwonić do Łukasza za 2 godziny"_s
                                      << u"- [ ] Zadzwonić do Łukasza ⏰ 2026-10-14 12:30"_s << u"reminder"_s;
        QTest::newRow("tomorrow at 9") << u"Zadzwonić do Łukasza jutro o 9"_s
                                       << u"- [ ] Zadzwonić do Łukasza ⏰ 2026-10-15 09:00"_s << u"reminder"_s;
        QTest::newRow("explicit date and time") << u"Odebrać garnitur 20.10 o 17:30"_s
                                                << u"- [ ] Odebrać garnitur ⏰ 2026-10-20 17:30"_s << u"reminder"_s;
        QTest::newRow("existing checkbox") << u"- [ ] Spotkanie z zespołem w poniedziałek o 9:30"_s
                                           << u"- [ ] Spotkanie z zespołem ⏰ 2026-10-19 09:30"_s << u"reminder"_s;
        QTest::newRow("logseq keyword") << u"TODO Wysłać fakturę 15 października"_s
                                        << u"- TODO Wysłać fakturę 📅 2026-10-15"_s << u"due"_s;
        QTest::newRow("numbered") << u"1. Przeczytać rozdział jutro"_s << u"1. [ ] Przeczytać rozdział 📅 2026-10-15"_s
                                  << u"due"_s;
        QTest::newRow("english") << u"Call mom tomorrow at 6pm"_s << u"- [ ] Call mom ⏰ 2026-10-15 18:00"_s
                                 << u"reminder"_s;
        QTest::newRow("capitals") << u"ŻÓŁW jutro 9:00"_s << u"- [ ] ŻÓŁW ⏰ 2026-10-15 09:00"_s << u"reminder"_s;
        QTest::newRow("typed without diacritics") << u"Basen w srode"_s << u"- [ ] Basen 📅 2026-10-21"_s << u"due"_s;
        QTest::newRow("only a date") << u"jutro o 9"_s << u"- [ ] jutro o 9 ⏰ 2026-10-15 09:00"_s << u"reminder"_s;
        QTest::newRow("past is a note") << u"Wczoraj byłem u dentysty"_s << u"- Wczoraj byłem u dentysty"_s
                                        << u"note"_s;
        QTest::newRow("past, lower case")
                << u"wczoraj byłem u lekarza"_s << u"- wczoraj byłem u lekarza"_s << u"note"_s;
        QTest::newRow("plain note") << u"Pomysł: żółta ściana w salonie"_s << u"- Pomysł: żółta ściana w salonie"_s
                                    << u"note"_s;
        QTest::newRow("amount is no time") << u"Podwyżka o 5 zł"_s << u"- Podwyżka o 5 zł"_s << u"note"_s;
        QTest::newRow("token kept") << u"Kupić mleko ⏰ 2026-10-20 10:00"_s << u"- Kupić mleko ⏰ 2026-10-20 10:00"_s
                                    << u"note"_s;
        QTest::newRow("due token kept") << u"Kupić mleko 📅 2026-10-20 jutro"_s
                                        << u"- Kupić mleko 📅 2026-10-20 jutro"_s << u"note"_s;
        QTest::newRow("more lines") << u"\nLista zakupów jutro\r\nmleko\n\n  chleb  \n\n"_s
                                    << u"- [ ] Lista zakupów 📅 2026-10-15\n  mleko\n\n  chleb"_s << u"due"_s;
        QTest::newRow("empty") << u"  \n \n"_s << QString() << QString();
        QTest::newRow("nothing") << QString() << QString() << QString();
    }

    void captureLine()
    {
        QFETCH(QString, input);
        QFETCH(QString, block);
        QFETCH(QString, kind);
        const CaptureLine line = buildCaptureLine(input, m_now);
        QCOMPARE(line.block, block);
        QCOMPARE(captureKindName(line.kind), kind);
    }

    void whenAndText()
    {
        const CaptureLine line = buildCaptureLine(u"Kupić mleko jutro o 18"_s, m_now);
        QCOMPARE(line.text, u"Kupić mleko"_s);
        QCOMPARE(line.when.toString(), u"2026-10-15 18:00"_s);
        // English input understood with the English locale ("10/11" = October 11).
        const CaptureLine en =
                buildCaptureLine(u"Dentist 10/11 3pm"_s, m_now, Qt::Monday, brulion::dates::UiLocale::English);
        // (passed this year: next year)
        QCOMPARE(en.block, u"- [ ] Dentist ⏰ 2027-10-11 15:00"_s);
        // Without a clock nothing is parsed (an invalid now).
        QCOMPARE(buildCaptureLine(u"jutro o 9"_s, QDateTime()).kind, CaptureLine::Kind::Note);
    }

    void hostileInput()
    {
        // Very long text and control characters never crash; the first line decides.
        const QString longLine = QString(20000, u'ą') + u" jutro o 9"_s;
        const CaptureLine line = buildCaptureLine(longLine, m_now);
        QCOMPARE(line.kind, CaptureLine::Kind::Reminder);
        QVERIFY(line.block.endsWith(u"⏰ 2026-10-15 09:00"_s));
        const CaptureLine nul = buildCaptureLine(u"a\0b jutro"_s, m_now);
        QVERIFY(nul.kind != CaptureLine::Kind::Empty);
    }
};

QTEST_GUILESS_MAIN(TestNotebookCapture)
#include "tst_notebook_capture.moc"
