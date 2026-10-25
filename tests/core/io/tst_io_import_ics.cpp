// iCalendar importer: events on journal pages, to-dos as tasks, time zones, recurrence, alarms.
#include "iotest.h"

#include "io/importer.h"
#include "centraleuropeantime.h"

#include <QTest>

using namespace brulion;
using namespace brulion::io;
using namespace Qt::StringLiterals;

namespace {

QByteArray calendar(const QString &body)
{
    return QString(u"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//PL\r\n"_s + body + u"END:VCALENDAR\r\n"_s)
            .replace(u"\n"_s, u"\r\n"_s)
            .replace(u"\r\r\n"_s, u"\r\n"_s)
            .toUtf8();
}

const QString Events =
        u"X-WR-CALNAME:Praca\n"
        u"BEGIN:VTIMEZONE\nTZID:Europe/Warsaw\nEND:VTIMEZONE\n"
        u"BEGIN:VEVENT\nUID:e1\nSUMMARY:Spotkanie zespołu\n"
        u"DTSTART;TZID=Europe/Warsaw:20261014T100000\nDTEND;TZID=Europe/Warsaw:20261014T113000\n"
        u"RRULE:FREQ=WEEKLY;BYDAY=WE\nLOCATION:Sala 5\\, piętro 2\n"
        u"DESCRIPTION:Agenda:\\n- budżet\\n# nie nagłówek\n"
        u"BEGIN:VALARM\nACTION:DISPLAY\nTRIGGER:-PT15M\nEND:VALARM\nEND:VEVENT\n"
        u"BEGIN:VEVENT\nSUMMARY:Lot do Krakowa\nDTSTART:20261014T060000Z\nDURATION:PT1H30M\n"
        u"BEGIN:VALARM\nACTION:AUDIO\nTRIGGER;VALUE=DATE-TIME:20261014T040000Z\nEND:VALARM\nEND:VEVENT\n"
        u"BEGIN:VEVENT\nSUMMARY:Urlop\nDTSTART;VALUE=DATE:20261020\nDTEND;VALUE=DATE:20261023\n"
        u"BEGIN:VALARM\nTRIGGER:-PT15H\nACTION:DISPLAY\nEND:VALARM\nEND:VEVENT\n"
        u"BEGIN:VEVENT\nSUMMARY:Odwołane\nSTATUS:CANCELLED\nDTSTART:20261014T120000Z\nEND:VEVENT\n"
        u"BEGIN:VEVENT\nSUMMARY:Wyjątek serii\nRECURRENCE-ID;TZID=Europe/Warsaw:20261021T100000\n"
        u"DTSTART;TZID=Europe/Warsaw:20261021T110000\nEND:VEVENT\n"
        u"BEGIN:VEVENT\nSUMMARY:Bez daty\nEND:VEVENT\n"
        u"BEGIN:VEVENT\nSUMMARY:Co drugi wtorek\nDTSTART;VALUE=DATE:20261013\nRRULE:FREQ=MONTHLY;BYDAY=2TU\n"
        u"END:VEVENT\n"
        u"BEGIN:VEVENT\nSUMMARY:Z końcem\nDTSTART:20261014T150000\nRRULE:FREQ=DAILY;COUNT=3\nEND:VEVENT\n"
        u"BEGIN:VTODO\nSUMMARY:Raport kwartalny\nDUE;VALUE=DATE:20261031\nDTSTART;VALUE=DATE:20261025\n"
        u"PRIORITY:1\nCATEGORIES:praca,Finanse firmy\nSTATUS:NEEDS-ACTION\nRRULE:FREQ=MONTHLY;BYMONTHDAY=-1\n"
        u"BEGIN:VALARM\nTRIGGER;RELATED=END:-PT2H\nACTION:DISPLAY\nEND:VALARM\nEND:VTODO\n"
        u"BEGIN:VTODO\nSUMMARY:Zrobione\nSTATUS:COMPLETED\nCOMPLETED:20261010T120000Z\n"
        u"DUE:20261009T080000Z\nBEGIN:VALARM\nTRIGGER:-PT1H\nEND:VALARM\nEND:VTODO\n"
        u"BEGIN:VTODO\nSUMMARY:W toku\nSTATUS:IN-PROCESS\nPRIORITY:5\nEND:VTODO\n"_s;

} // namespace

class TestIoImportIcs : public QObject {
    Q_OBJECT

public:
    /// A system zone with DST, so tests catch date arithmetic done in the system zone instead of
    /// ImportOptions::timeZone.
    static void initMain() { brulion::test::useCentralEuropeanTime(); }

private Q_SLOTS:
    void eventsAndTodos()
    {
        const ImportResult r =
                runImport(*importerById(u"ics"), {iotest::bytes(u"praca.ics"_s, calendar(Events))}, iotest::options());
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Praca.md"_s, u"journals/2026-10-13.md"_s,
                                                  u"journals/2026-10-14.md"_s, u"journals/2026-10-20.md"_s}));
        QCOMPARE(iotest::textAt(r, u"journals/2026-10-14.md"_s),
                 u"- 08:00–09:30 Lot do Krakowa ⏰! 2026-10-14 06:00\n"
                 u"- 10:00–11:30 Spotkanie zespołu 🔁 every week on Wednesday ⏰ 2026-10-14 09:45\n"
                 u"  location:: Sala 5, piętro 2\n  Agenda:\n  \\- budżet\n  \\# nie nagłówek\n"
                 u"- 15:00 Z końcem 🔁 every day\n"_s);
        QCOMPARE(iotest::textAt(r, u"journals/2026-10-20.md"_s), u"- Urlop (→ 2026-10-22) ⏰ 2026-10-19 09:00\n"_s);
        QCOMPARE(iotest::textAt(r, u"journals/2026-10-13.md"_s), u"- Co drugi wtorek\n"_s);
        QCOMPARE(iotest::textAt(r, u"Praca.md"_s), u"- [ ] Raport kwartalny #praca #Finanse-firmy ⏫ ⏳ 2026-10-25 📅 "
                                                   u"2026-10-31 🔁 every month on the last "
                                                   u"⏰ 2026-10-30 22:00\n"
                                                   u"- [x] Zrobione 📅 2026-10-09 10:00 ✅ 2026-10-10\n"
                                                   u"- [/] W toku 🔼\n"_s);
        for (const ImportedFile &f : r.files)
            QCOMPARE(f.append, f.path.startsWith(u"journals/"_s));
        QCOMPARE(r.warnings.size(), 3); // no start date, simplified rule, unsupported rule
    }

    void journalSettingsAndTargetFolder()
    {
        ImportOptions o = iotest::options(u"Kalendarze"_s);
        o.config.journalsFolder = u"Dziennik"_s;
        o.config.journalFileFormat = u"dd.MM.yyyy"_s;
        const QByteArray data = calendar(u"BEGIN:VEVENT\nSUMMARY:A\nDTSTART;VALUE=DATE:20261014\nEND:VEVENT\n"
                                         u"BEGIN:VEVENT\nSUMMARY:B\nDTSTART;VALUE=DATE:20261014\nEND:VEVENT\n"
                                         u"BEGIN:VTODO\nSUMMARY:Zadanie\nEND:VTODO\n"_s);
        const ImportResult r = runImport(*importerById(u"ics"), {iotest::bytes(u"dom.ics"_s, data)}, o);
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Dziennik/14.10.2026.md"_s, u"Kalendarze/dom.md"_s}));
        QCOMPARE(iotest::textAt(r, u"Dziennik/14.10.2026.md"_s), u"- A\n- B\n"_s);
        QCOMPARE(iotest::textAt(r, u"Kalendarze/dom.md"_s), u"- [ ] Zadanie\n"_s);
    }

    void unknownZoneAndMalformed()
    {
        const QByteArray data =
                calendar(u"garbage line without colon\n"
                         u"BEGIN:VEVENT\nSUMMARY:Mars\nDTSTART;TZID=Mars/Olympus:20261014T100000\nEND:VEVENT\n"
                         u"BEGIN:VEVENT\nSUMMARY:Zła data\nDTSTART:2026-13-45\nEND:VEVENT\n"_s);
        const ImportResult r = runImport(*importerById(u"ics"), {iotest::bytes(u"x.ics"_s, data)}, iotest::options());
        QCOMPARE(iotest::textAt(r, u"journals/2026-10-14.md"_s), u"- 10:00 Mars\n"_s);
        QCOMPARE(r.warnings.size(), 3); // malformed line, missing start, unknown zone
        const ImportResult empty =
                runImport(*importerById(u"ics"), {iotest::bytes(u"y.ics"_s, "\xFF\xFE garbage")}, iotest::options());
        QVERIFY(empty.files.isEmpty());
    }

    void untitledTodo()
    {
        const QByteArray data = calendar(u"BEGIN:VTODO\nDUE:20261014T120000\nEND:VTODO\n"_s);
        const ImportResult r = runImport(*importerById(u"ics"), {iotest::bytes(u"z.ics"_s, data)}, iotest::options());
        QCOMPARE(iotest::textAt(r, u"z.md"_s), u"- [ ] (no title) 📅 2026-10-14 12:00\n"_s);
    }

    void dollarsShowAsWritten()
    {
        // A calendar's texts hold no formulas: their dollars that would pair (also across the
        // description's lines) are escaped, the others stay as written (SYNTAX §11).
        const QByteArray data = calendar(u"BEGIN:VEVENT\nSUMMARY:Kino $x$\nDTSTART;VALUE=DATE:20261014\n"
                                         u"LOCATION:Sala $5\nDESCRIPTION:Bilet $10\\nparking 5$\\n$3 i $4\nEND:VEVENT\n"
                                         u"BEGIN:VTODO\nSUMMARY:Zapłacić $5\\, nie 10$\nEND:VTODO\n"_s);
        const ImportResult r =
                runImport(*importerById(u"ics"), {iotest::bytes(u"kino.ics"_s, data)}, iotest::options());
        const QString day = iotest::textAt(r, u"journals/2026-10-14.md"_s);
        // The location (a property) stays as written; its dollar would pair with the text's.
        QCOMPARE(day, u"- Kino \\$x$\n  location:: Sala $5\n  Bilet \\$10\n  parking 5\\$\n  $3 i $4\n"_s);
        for (const bool exported : {false, true}) {
            const QString shown = iotest::shownText(day, exported);
            for (const QString &line : {u"Kino $x$"_s, u"Bilet $10"_s, u"parking 5$"_s, u"$3 i $4"_s})
                QVERIFY2(shown.contains(line), qPrintable(shown));
        }
        const QString tasks = iotest::textAt(r, u"kino.md"_s);
        QCOMPARE(tasks, u"- [ ] Zapłacić \\$5, nie 10$\n"_s);
        QVERIFY(iotest::shownText(tasks).contains(u"Zapłacić $5, nie 10$"_s));
    }

    void summaryEscaping()
    {
        const QByteArray data = calendar(u"BEGIN:VEVENT\nSUMMARY:# [[Nie link]] *gwiazdki*\\nDruga linia\n"
                                         u"DTSTART;VALUE=DATE:20261014\nEND:VEVENT\n"_s);
        const ImportResult r = runImport(*importerById(u"ics"), {iotest::bytes(u"x.ics"_s, data)}, iotest::options());
        // The summary starts the bullet's content: block syntax there is escaped too.
        QCOMPARE(iotest::textAt(r, u"journals/2026-10-14.md"_s),
                 u"- \\# \\[\\[Nie link\\]\\] \\*gwiazdki\\* Druga linia\n"_s);
        const QByteArray list = calendar(u"BEGIN:VEVENT\nSUMMARY:- nie lista\nDTSTART;VALUE=DATE:20261015\nEND:VEVENT\n"
                                         u"BEGIN:VEVENT\nSUMMARY:1. nie numer\nDTSTART:20261015T080000\nEND:VEVENT\n"_s);
        const ImportResult l = runImport(*importerById(u"ics"), {iotest::bytes(u"l.ics"_s, list)}, iotest::options());
        QCOMPARE(iotest::textAt(l, u"journals/2026-10-15.md"_s), u"- \\- nie lista\n- 08:00 1. nie numer\n"_s);
    }

    void byteOrderMark()
    {
        // Some Windows tools write a UTF-8 BOM before BEGIN:VCALENDAR.
        const QByteArray data =
                "\xEF\xBB\xBF"
                + calendar(u"BEGIN:VEVENT\nSUMMARY:Zebranie\nDTSTART;VALUE=DATE:20261014\nEND:VEVENT\n"_s);
        const ImportResult r = runImport(*importerById(u"ics"), {iotest::bytes(u"bom.ics"_s, data)}, iotest::options());
        QCOMPARE(iotest::textAt(r, u"journals/2026-10-14.md"_s), u"- Zebranie\n"_s);
        QVERIFY(r.warnings.isEmpty());
        QCOMPARE(detectImporter({iotest::bytes(u"eksport"_s, data)}), importerById(u"ics"));
    }

    void relativeTimesAcrossDst()
    {
        // Durations and alarm offsets are computed in the vault's wall-clock zone, never in the
        // system zone (Europe/Warsaw here, DST change on 2026-03-29 at 01:00 UTC). Days are
        // nominal (-P1D: same time the day before), hours exact (RFC 5545 3.3.6).
        const QByteArray data =
                calendar(u"BEGIN:VEVENT\nSUMMARY:Dyżur\nDTSTART:20260329T003000Z\nDURATION:PT2H\nEND:VEVENT\n"
                         u"BEGIN:VEVENT\nSUMMARY:Wizyta\nDTSTART;TZID=Europe/Warsaw:20260329T100000\n"
                         u"BEGIN:VALARM\nACTION:DISPLAY\nTRIGGER:-P1D\nEND:VALARM\n"
                         u"BEGIN:VALARM\nACTION:DISPLAY\nTRIGGER:-PT24H\nEND:VALARM\nEND:VEVENT\n"_s);
        ImportOptions utc = iotest::options();
        utc.timeZone = QTimeZone(QTimeZone::UTC);
        const ImportResult a = runImport(*importerById(u"ics"), {iotest::bytes(u"d.ics"_s, data)}, utc);
        QCOMPARE(iotest::textAt(a, u"journals/2026-03-29.md"_s),
                 u"- 00:30–02:30 Dyżur\n- 08:00 Wizyta ⏰ 2026-03-28 08:00\n"_s);
        const ImportResult w = runImport(*importerById(u"ics"), {iotest::bytes(u"d.ics"_s, data)}, iotest::options());
        QCOMPARE(iotest::textAt(w, u"journals/2026-03-29.md"_s),
                 u"- 01:30–04:30 Dyżur\n- 10:00 Wizyta ⏰ 2026-03-28 10:00 ⏰ 2026-03-28 09:00\n"_s);
    }
};

QTEST_MAIN(TestIoImportIcs)
#include "tst_io_import_ics.moc"
