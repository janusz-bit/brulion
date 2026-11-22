// A brulion:// request worked out for a notebook and carried out (automation/plan.h): pages named
// by path, name, alias, ISO date or a Polish phrase; daily notes made from the journal template;
// appending and prepending under headings and blocks; tasks and quick capture with dates; new
// pages from templates, never over an existing one; the routes of open, daily, search and view;
// commands of extensions; unknown parameters refused; the outcome's JSON and callback parameters;
// the notebook chosen by name or folder.
#include "automation/chatlink.h"
#include "automation/plan.h"

#include "index/vaultindex.h"
#include "notebook/session.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::automation;

namespace {

const QDateTime kNow(QDate(2026, 11, 20), QTime(10, 0)); // a Friday

void put(const QString &root, const QString &path, const QByteArray &bytes)
{
    const QString file = root + u'/' + path;
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(bytes);
}

QByteArray read(const QString &root, const QString &path)
{
    QFile f(root + u'/' + path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

Request request(const QString &uri)
{
    const ParsedUri parsed = parseUri(uri);
    if (!parsed.request)
        qWarning() << "not an address:" << uri << uriErrorCode(parsed.error);
    return parsed.request.value_or(Request());
}

} // namespace

class TestAutomationPlan : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_root;
    std::unique_ptr<notebook::Notebook> m_notebook;

    Context context() const { return {kNow, dates::UiLocale::Polish}; }

    Plan planOf(const QString &uri)
    {
        return plan(request(uri), m_notebook->vault(), m_notebook->index(), context());
    }

    Outcome run(const QString &uri)
    {
        NotebookWriter writer(*m_notebook);
        const Request r = request(uri);
        return execute(plan(r, m_notebook->vault(), m_notebook->index(), context()), writer, r.id);
    }

private Q_SLOTS:
    void init()
    {
        QVERIFY(m_dir.isValid());
        m_root = m_dir.filePath(u"Notatnik %1"_s.arg(QDateTime::currentMSecsSinceEpoch()));
        QDir().mkpath(m_root);
        put(m_root, u"Zakupy.md"_s, "# Zakupy\n\n## Dziś\n- chleb\n\n## Jutro\n- ser\n");
        put(m_root, u"Projekty/Dom i ogród.md"_s, "---\naliases: [Ogród]\n---\n- Taras ^taras\n  - deski\n");
        put(m_root, u"Plan/Poniedziałek.md"_s, "- plan\n");
        put(m_root, u"Windows.md"_s, "# Windows\r\n\r\n- a\r\n");
        put(m_root, u"templates/journal.md"_s, "## Plan dnia\n- \n");
        put(m_root, u"templates/Spotkanie.md"_s, "# {{title}}\n\nData: {{date}}\n\n## Ustalenia\n-\n");
        put(m_root, u"journals/2026-11-19.md"_s, "- wczorajszy wpis\n");
        put(m_root, u"assets/foto.png"_s, "\x89PNG");
        put(m_root, u".trash/stara.md"_s, "- w koszu\n");
        m_notebook = std::make_unique<notebook::Notebook>();
        notebook::Notebook::Options options;
        options.stateDir = m_dir.filePath(u"state"_s);
        options.clock = [] { return kNow; };
        QString error;
        QVERIFY2(m_notebook->open(m_root, options, &error), qPrintable(error));
        QVERIFY(m_notebook->index().note(u"Zakupy.md"_s));
    }

    void cleanup() { m_notebook.reset(); }

    void opens_data()
    {
        QTest::addColumn<QString>("uri");
        QTest::addColumn<QJsonObject>("route");
        QTest::newRow("name") << u"brulion://open?page=zakupy"_s
                              << QJsonObject{{u"view"_s, u"page"_s}, {u"path"_s, u"Zakupy.md"_s}};
        QTest::newRow("path without .md") << u"brulion://open/Projekty/Dom%20i%20ogr%C3%B3d"_s
                                          << QJsonObject{{u"view"_s, u"page"_s}, {u"path"_s, u"Projekty/Dom i ogród.md"_s}};
        QTest::newRow("alias") << u"brulion://open?page=Ogr%C3%B3d"_s
                               << QJsonObject{{u"view"_s, u"page"_s}, {u"path"_s, u"Projekty/Dom i ogród.md"_s}};
        QTest::newRow("heading in the name") << u"brulion://open?page=Zakupy%23Jutro"_s
                                             << QJsonObject{{u"view"_s, u"page"_s},
                                                            {u"path"_s, u"Zakupy.md"_s},
                                                            {u"heading"_s, u"Jutro"_s}};
        QTest::newRow("raw hash") << u"brulion://open?page=Zakupy#Jutro"_s
                                  << QJsonObject{{u"view"_s, u"page"_s}, {u"path"_s, u"Zakupy.md"_s}, {u"heading"_s, u"Jutro"_s}};
        QTest::newRow("block") << u"brulion://open?page=Ogr%C3%B3d&block=%5Etaras"_s
                               << QJsonObject{{u"view"_s, u"page"_s},
                                              {u"path"_s, u"Projekty/Dom i ogród.md"_s},
                                              {u"blockId"_s, u"taras"_s}};
        QTest::newRow("line") << u"brulion://open?page=Zakupy&line=3"_s
                              << QJsonObject{{u"view"_s, u"page"_s}, {u"path"_s, u"Zakupy.md"_s}, {u"line"_s, 2}};
        QTest::newRow("a page named like a day") << u"brulion://open?page=poniedzia%C5%82ek"_s
                                                 << QJsonObject{{u"view"_s, u"page"_s}, {u"path"_s, u"Plan/Poniedziałek.md"_s}};
        QTest::newRow("a day") << u"brulion://open?page=wczoraj"_s
                               << QJsonObject{{u"view"_s, u"journal"_s},
                                              {u"date"_s, u"2026-11-19"_s},
                                              {u"path"_s, u"journals/2026-11-19.md"_s}};
        QTest::newRow("a day without a note") << u"brulion://open?page=2026-12-24"_s
                                              << QJsonObject{{u"view"_s, u"journal"_s},
                                                             {u"date"_s, u"2026-12-24"_s},
                                                             {u"path"_s, u"journals/2026-12-24.md"_s}};
        QTest::newRow("daily") << u"brulion://daily"_s
                               << QJsonObject{{u"view"_s, u"journal"_s}, {u"date"_s, u"2026-11-20"_s}};
        QTest::newRow("daily, phrase") << u"brulion://daily?date=przedwczoraj"_s
                                       << QJsonObject{{u"view"_s, u"journal"_s}, {u"date"_s, u"2026-11-18"_s}};
        QTest::newRow("search") << u"brulion://search?query=%C5%BC%C3%B3%C5%82w"_s
                                << QJsonObject{{u"view"_s, u"search"_s}, {u"query"_s, u"żółw"_s}};
        QTest::newRow("search, empty") << u"brulion://search"_s
                                       << QJsonObject{{u"view"_s, u"search"_s}, {u"query"_s, QString()}};
        QTest::newRow("view") << u"brulion://view?name=Calendar&date=jutro&mode=week"_s
                              << QJsonObject{{u"view"_s, u"calendar"_s}, {u"date"_s, u"2026-11-21"_s}, {u"mode"_s, u"week"_s}};
        QTest::newRow("view alias") << u"brulion://view?name=pages&folder=Projekty"_s
                                    << QJsonObject{{u"view"_s, u"allPages"_s}, {u"path"_s, u"Projekty"_s}};
        QTest::newRow("settings section") << u"brulion://view?name=settings&section=automation"_s
                                          << QJsonObject{{u"view"_s, u"settings"_s}, {u"section"_s, u"automation"_s}};
        QTest::newRow("capture dialog") << u"brulion://capture"_s << QJsonObject{{u"view"_s, u"capture"_s}};
        // A prompt link: a new conversation's draft, never sent (no `prompt`, which sends at once).
        const QJsonObject chat{{u"view"_s, u"chat"_s}, {u"new"_s, true}, {u"fromLink"_s, true}};
        QJsonObject draft = chat;
        draft.insert(u"draft"_s, u"Co mam jutro do zrobienia?"_s);
        QTest::newRow("chat") << u"brulion://chat?prompt=Co%20mam%20jutro%20do%20zrobienia%3F"_s << draft;
        QTest::newRow("chat, empty") << u"brulion://chat"_s << chat;
        QJsonObject about = draft;
        about.insert(u"page"_s, u"Dom i ogród"_s);
        about.insert(u"partner"_s, u"acp:claude"_s);
        QTest::newRow("chat about a page, with a partner")
                << u"brulion://chat?prompt=Co+mam+jutro+do+zrobienia%3F&page=Ogr%C3%B3d&partner=acp%3Aclaude"_s << about;
        QJsonObject missing = draft;
        missing.insert(u"pageMissing"_s, u"Nieistniejąca"_s);
        QTest::newRow("chat about a page not here")
                << u"brulion://chat?prompt=Co+mam+jutro+do+zrobienia%3F&page=Nieistniej%C4%85ca"_s << missing;
        QJsonObject hidden = chat;
        hidden.insert(u"draft"_s, u"Podsumuj tę stronę"_s);
        hidden.insert(u"hidden"_s, 4);
        // A zero-width space, a right-to-left override and two tag characters ("hi" no one sees).
        QTest::newRow("chat, hidden characters")
                << u"brulion://chat?prompt=Podsumuj%E2%80%8B%20t%C4%99%20%E2%80%AEstron%C4%99%F3%A0%81%A8%F3%A0%81%A9"_s
                << hidden;
        QJsonObject capped = chat;
        capped.insert(u"draft"_s, QString(MaxPromptLength, u'ż'));
        capped.insert(u"capped"_s, true);
        QTest::newRow("chat, too long") << u"brulion://chat?prompt="_s + QString(MaxPromptLength + 100, u'ż') << capped;
    }

    void opens()
    {
        QFETCH(QString, uri);
        QFETCH(QJsonObject, route);
        const Plan p = planOf(uri);
        QVERIFY2(p.ok(), qPrintable(p.error + u": "_s + p.detail));
        QCOMPARE(p.access, Access::Open);
        QCOMPARE(p.route, route);
        // Opening writes nothing — not even the day's note.
        const Outcome o = run(uri);
        QVERIFY(o.ok);
        QCOMPARE(o.route, route);
        QVERIFY(!QFile::exists(m_root + u"/journals/2026-12-24.md"_s));
        QVERIFY(!QFile::exists(m_root + u"/journals/2026-11-20.md"_s));
    }

    void refuses_data()
    {
        QTest::addColumn<QString>("uri");
        QTest::addColumn<QString>("error");
        QTest::newRow("no page") << u"brulion://open"_s << u"missing_parameter"_s;
        QTest::newRow("missing page") << u"brulion://open?page=Nieistniej%C4%85ca"_s << u"page_not_found"_s;
        QTest::newRow("an attachment") << u"brulion://open?page=assets/foto.png"_s << u"not_a_page"_s;
        QTest::newRow("outside") << u"brulion://open?page=../sekret"_s << u"page_not_found"_s;
        QTest::newRow("absolute") << u"brulion://open?page=/etc/passwd"_s << u"page_not_found"_s;
        QTest::newRow("a hidden folder") << u"brulion://append?page=.trash/stara&text=x"_s << u"not_a_page"_s;
        QTest::newRow("a hidden new page") << u"brulion://append?page=.obsidian/x&create=yes&text=x"_s
                                           << u"not_a_page"_s;
        QTest::newRow("new page in a hidden folder") << u"brulion://new?title=x&folder=.git"_s << u"not_a_page"_s;
        QTest::newRow("bad line") << u"brulion://open?page=Zakupy&line=zero"_s << u"missing_parameter"_s;
        QTest::newRow("typo") << u"brulion://append?page=Zakupy&heding=Dzi%C5%9B&text=a"_s << u"unknown_parameter"_s;
        QTest::newRow("daily, no date") << u"brulion://daily?date=kiedy%C5%9B"_s << u"date_not_understood"_s;
        QTest::newRow("unknown view") << u"brulion://view?name=kosmos"_s << u"unknown_view"_s;
        QTest::newRow("view's parameter") << u"brulion://view?name=graph&date=dzi%C5%9B"_s << u"unknown_parameter"_s;
        QTest::newRow("append without text") << u"brulion://append?page=Zakupy"_s << u"missing_text"_s;
        QTest::newRow("append to a missing page") << u"brulion://append?page=Brak&text=a"_s << u"page_not_found"_s;
        QTest::newRow("page and date") << u"brulion://append?page=Zakupy&date=dzi%C5%9B&text=a"_s
                                       << u"unknown_parameter"_s;
        QTest::newRow("heading and block") << u"brulion://append?page=Zakupy&heading=a&block=b&text=a"_s
                                           << u"unknown_parameter"_s;
        QTest::newRow("task without text") << u"brulion://task?due=jutro"_s << u"missing_text"_s;
        QTest::newRow("task, bad date") << u"brulion://task?text=a&due=nigdy"_s << u"date_not_understood"_s;
        QTest::newRow("task, remind and alarm") << u"brulion://task?text=a&remind=jutro&alarm=pojutrze"_s
                                                << u"unknown_parameter"_s;
        QTest::newRow("new without a title") << u"brulion://new?text=a"_s << u"missing_title"_s;
        QTest::newRow("new over a page") << u"brulion://new?title=Zakupy&text=a"_s << u"exists"_s;
        QTest::newRow("new over a page, other case") << u"brulion://new?title=ZAKUPY"_s << u"exists"_s;
        QTest::newRow("new, no template") << u"brulion://new?title=X&template=Brak"_s << u"template_not_found"_s;
        QTest::newRow("new, bad mode") << u"brulion://new?title=X&mode=tabela"_s << u"unknown_parameter"_s;
        QTest::newRow("run without a command") << u"brulion://run?extension=silownia"_s << u"missing_parameter"_s;
        QTest::newRow("run, bad args") << u"brulion://run?extension=a&command=b&args=%5B1%5D"_s << u"bad_args"_s;
        QTest::newRow("chat, a typo") << u"brulion://chat?promt=Cze%C5%9B%C4%87"_s << u"unknown_parameter"_s;
    }

    void refuses()
    {
        QFETCH(QString, uri);
        QFETCH(QString, error);
        const QByteArray before = read(m_root, u"Zakupy.md"_s);
        const Plan p = planOf(uri);
        QCOMPARE(p.error, error);
        const Outcome o = run(uri);
        QVERIFY(!o.ok);
        QCOMPARE(o.error, error);
        QCOMPARE(read(m_root, u"Zakupy.md"_s), before);
    }

    void writes_data()
    {
        QTest::addColumn<QString>("uri");
        QTest::addColumn<QString>("path");
        QTest::addColumn<QByteArray>("content");
        QTest::addColumn<int>("line");
        QTest::addColumn<QString>("kind");

        QTest::newRow("append to today's note, from the template")
                << u"brulion://append?text=Kupi%C4%87+%C5%BC%C3%B3%C5%82ty+ser"_s << u"journals/2026-11-20.md"_s
                << QByteArray("## Plan dnia\n- Kupić żółty ser\n") << 1 << u"note"_s;
        QTest::newRow("append under a heading") << u"brulion://append?page=Zakupy&heading=Dzi%C5%9B&text=mleko"_s
                                                << u"Zakupy.md"_s
                                                << QByteArray("# Zakupy\n\n## Dziś\n- chleb\n- mleko\n\n## Jutro\n- ser\n")
                                                << 4 << u"note"_s;
        QTest::newRow("prepend under a heading in the name")
                << u"brulion://prepend?page=Zakupy%23Jutro&text=jajka"_s << u"Zakupy.md"_s
                << QByteArray("# Zakupy\n\n## Dziś\n- chleb\n\n## Jutro\n- jajka\n- ser\n") << 6 << u"note"_s;
        QTest::newRow("prepend to a page") << u"brulion://prepend?page=Zakupy&text=najpierw"_s << u"Zakupy.md"_s
                                           << QByteArray("# Zakupy\n\n- najpierw\n\n## Dziś\n- chleb\n\n## Jutro\n- ser\n")
                                           << 2 << u"note"_s;
        QTest::newRow("CRLF kept") << u"brulion://append?page=Windows&text=b%0Ac"_s << u"Windows.md"_s
                                   << QByteArray("# Windows\r\n\r\n- a\r\n- b\r\n  c\r\n") << 3 << u"note"_s;
        QTest::newRow("a block's child") << u"brulion://append?page=Ogr%C3%B3d&block=taras&text=szlifowanie"_s
                                         << u"Projekty/Dom i ogród.md"_s
                                         << QByteArray("---\naliases: [Ogród]\n---\n- Taras ^taras\n  - deski\n  - szlifowanie\n")
                                         << 5 << u"note"_s;
        QTest::newRow("a day by phrase") << u"brulion://append?date=wczoraj&text=dopisek"_s << u"journals/2026-11-19.md"_s
                                         << QByteArray("- wczorajszy wpis\n- dopisek\n") << 1 << u"note"_s;
        QTest::newRow("a new page with create") << u"brulion://append?page=Pomys%C5%82y&create=yes&text=pierwszy"_s
                                                << u"Pomysły.md"_s << QByteArray("- pierwszy\n") << 0 << u"note"_s;
        QTest::newRow("task with a reminder") << u"brulion://task?text=Zadzwoni%C4%87+do+mamy&remind=jutro+o+18"_s
                                              << u"journals/2026-11-20.md"_s
                                              << QByteArray("## Plan dnia\n- [ ] Zadzwonić do mamy ⏰ 2026-11-21 18:00\n")
                                              << 1 << u"reminder"_s;
        QTest::newRow("task, date from the text, on a page")
                << u"brulion://task?page=Zakupy&heading=Jutro&text=Odebra%C4%87+paczk%C4%99+w+poniedzia%C5%82ek"_s
                << u"Zakupy.md"_s
                << QByteArray("# Zakupy\n\n## Dziś\n- chleb\n\n## Jutro\n- ser\n- [ ] Odebrać paczkę 📅 2026-11-23\n") << 7
                << u"due"_s;
        QTest::newRow("task, alarm") << u"brulion://task?text=Poci%C4%85g&alarm=jutro+5:40"_s
                                     << u"journals/2026-11-20.md"_s
                                     << QByteArray("## Plan dnia\n- [ ] Pociąg ⏰! 2026-11-21 05:40\n") << 1 << u"alarm"_s;
        QTest::newRow("capture") << u"brulion://capture?text=Kupi%C4%87+mleko+jutro+o+18"_s << u"journals/2026-11-20.md"_s
                                 << QByteArray("## Plan dnia\n- [ ] Kupić mleko ⏰ 2026-11-21 18:00\n") << 1
                                 << u"reminder"_s;
        QTest::newRow("capture, a note") << u"brulion://capture?text=Pomys%C5%82+na+prezent"_s
                                         << u"journals/2026-11-20.md"_s << QByteArray("## Plan dnia\n- Pomysł na prezent\n")
                                         << 1 << u"note"_s;
        QTest::newRow("new page") << u"brulion://new?title=Notatka+z+podr%C3%B3%C5%BCy&text=Krak%C3%B3w"_s
                                  << u"Notatka z podróży.md"_s << QByteArray("Kraków\n") << 0 << u"page"_s;
        QTest::newRow("new page in a folder") << u"brulion://new?title=Lista&folder=Projekty"_s
                                              << u"Projekty/Lista.md"_s << QByteArray() << 0 << u"page"_s;
        QTest::newRow("new page from a template")
                << u"brulion://new?title=Spotkanie+z+Ann%C4%85&template=spotkanie&text=-+Bud%C5%BCet"_s
                << u"Spotkanie z Anną.md"_s
                << QByteArray("# Spotkanie z Anną\n\nData: 2026-11-20\n\n## Ustalenia\n-\n\n- Budżet\n") << 0 << u"page"_s;
        QTest::newRow("new page, outline") << u"brulion://new?title=Konspekt&mode=outline"_s << u"Konspekt.md"_s
                                           << QByteArray("---\nbrulion: outline\n---\n") << 0 << u"page"_s;
    }

    void writes()
    {
        QFETCH(QString, uri);
        QFETCH(QString, path);
        QFETCH(QByteArray, content);
        QFETCH(int, line);
        QFETCH(QString, kind);
        const Outcome o = run(uri);
        QVERIFY2(o.ok, qPrintable(o.error + u": "_s + o.message));
        QCOMPARE(o.path, path);
        QCOMPARE(QString::fromUtf8(read(m_root, path)), QString::fromUtf8(content));
        QCOMPARE(o.line, line);
        QCOMPARE(o.kind, kind);
    }

    void newPageNeverOverwrites()
    {
        // The page appears between the plan and the write (another device's sync): left alone.
        const Plan p = planOf(u"brulion://new?title=Nowa&text=moja"_s);
        QVERIFY(p.ok());
        put(m_root, u"Nowa.md"_s, "cudza\n");
        NotebookWriter writer(*m_notebook);
        const Outcome o = execute(p, writer);
        QVERIFY(!o.ok);
        QCOMPARE(o.error, u"exists"_s);
        QCOMPARE(read(m_root, u"Nowa.md"_s), QByteArray("cudza\n"));
    }

    void headingGoneMeanwhile()
    {
        const Plan p = planOf(u"brulion://append?page=Zakupy&heading=Jutro&text=x"_s);
        QVERIFY(p.ok());
        put(m_root, u"Zakupy.md"_s, "# Zakupy\n");
        NotebookWriter writer(*m_notebook);
        const Outcome o = execute(p, writer);
        QVERIFY(!o.ok);
        QCOMPARE(o.error, u"heading_not_found"_s);
        QCOMPARE(read(m_root, u"Zakupy.md"_s), QByteArray("# Zakupy\n"));
    }

    void notUtf8()
    {
        put(m_root, u"Stara.md"_s, "- za\xbf\xf3\xb3\xe6\n"); // "zażółć" in windows-1250
        m_notebook->index();
        const Outcome o = run(u"brulion://append?page=Stara.md&text=x"_s);
        QCOMPARE(o.error, u"not_utf8"_s);
        QCOMPARE(read(m_root, u"Stara.md"_s), QByteArray("- za\xbf\xf3\xb3\xe6\n"));
    }

    void run_()
    {
        const Plan p = planOf(u"brulion://run?extension=silownia/rekord&args=%7B%22n%22%3A3%7D&page=Zakupy"_s);
        QVERIFY2(p.ok(), qPrintable(p.error));
        QCOMPARE(p.access, Access::Run);
        QCOMPARE(p.extension, u"silownia"_s);
        QCOMPARE(p.command, u"rekord"_s);
        QCOMPARE(p.args, (QJsonObject{{u"n"_s, 3}}));
        QCOMPARE(p.page, u"Zakupy.md"_s);
        // Commands run in the app (approval, questions): never headless.
        const Outcome o = run(u"brulion://run?extension=silownia&command=rekord"_s);
        QVERIFY(!o.ok);
        QCOMPARE(o.error, u"needs_app"_s);
    }

    void outcome()
    {
        const Outcome o = run(u"brulion://task?text=Zadzwoni%C4%87&remind=jutro+o+18&id=r-7"_s);
        QVERIFY(o.ok);
        const QJsonObject json = o.toJson();
        QCOMPARE(json.value(u"ok"_s).toBool(), true);
        QCOMPARE(json.value(u"action"_s).toString(), u"task"_s);
        QCOMPARE(json.value(u"path"_s).toString(), u"journals/2026-11-20.md"_s);
        QCOMPARE(json.value(u"line"_s).toInt(), 2); // 1-based for callers
        QCOMPARE(json.value(u"created"_s).toBool(), true);
        QCOMPARE(json.value(u"reminder"_s).toString(), u"2026-11-21 18:00"_s);
        QCOMPARE(json.value(u"id"_s).toString(), u"r-7"_s);
        QCOMPARE(o.callbackParams(), (QList<std::pair<QString, QString>>{
                                             {u"path"_s, u"journals/2026-11-20.md"_s}, {u"line"_s, u"2"_s}, {u"id"_s, u"r-7"_s}}));
        const Outcome bad = run(u"brulion://open?page=Brak"_s);
        QCOMPARE(bad.callbackParams().first(), (std::pair<QString, QString>{u"errorCode"_s, u"page_not_found"_s}));
        QCOMPARE(bad.toJson().value(u"ok"_s).toBool(), false);
    }

    void history()
    {
        // The replaced text is in the device's History, as for brulion-cli's edits.
        QVERIFY(run(u"brulion://append?page=Zakupy&text=mleko"_s).ok);
        QVERIFY(QDir(m_dir.filePath(u"state"_s)).exists());
    }

    void matchNotebook_data()
    {
        QTest::addColumn<QString>("wanted");
        QTest::addColumn<int>("index");
        QTest::addColumn<QString>("error");
        QTest::newRow("name") << u"praca"_s << 1 << QString();
        QTest::newRow("folder's name") << u"notatki-domowe"_s << 0 << QString();
        QTest::newRow("name without diacritics") << u"moj brulion"_s << 0 << QString();
        QTest::newRow("folder") << u"/home/ala/Praca"_s << 1 << QString();
        QTest::newRow("folder, slash at the end") << u"/home/ala/Praca/"_s << 1 << QString();
        QTest::newRow("ambiguous") << u"Wspólny"_s << -1 << u"ambiguous_notebook"_s;
        QTest::newRow("unknown") << u"Inny"_s << -1 << u"unknown_notebook"_s;
        QTest::newRow("unknown folder") << u"/tmp/x"_s << -1 << u"unknown_notebook"_s;
        QTest::newRow("empty") << QString() << -1 << u"unknown_notebook"_s;
    }

    void matchNotebook()
    {
        QFETCH(QString, wanted);
        QFETCH(int, index);
        QFETCH(QString, error);
        const QList<KnownNotebook> known{{u"/home/ala/notatki-domowe"_s, u"Mój Brulion"_s},
                                         {u"/home/ala/Praca"_s, u"Praca"_s},
                                         {u"/home/ala/a/Wspólny"_s, u"Wspólny"_s},
                                         {u"/home/ala/b/Wspólny"_s, u"Wspólny"_s}};
        const NotebookMatch m = brulion::automation::matchNotebook(wanted, known);
        QCOMPARE(m.index, index);
        QCOMPARE(m.error, error);
        if (error == u"ambiguous_notebook")
            QCOMPARE(m.names.size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestAutomationPlan)
#include "tst_automation_plan.moc"
