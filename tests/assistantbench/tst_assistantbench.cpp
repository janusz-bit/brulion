// The AI assistant's bench (bench.h, docs/FINETUNE.md): every task of finetune/tasks done by its own
// solution — the assistant's real script, tools and notebook, the model played by the solution —,
// so each task is one the assistant can do and its checks hold for a right answer, and its answer
// states no fact the run never showed (a grounded answer: the phone number of a page read, not
// of a line the search did not show); the checks failing a wrong one (another note changed, a
// change tried again after the user's no, a tool that does not exist, an answer untrue to what
// changed: a change claimed and none made, the run's own change said to be there already, the
// system text's example date given as a fact); text checks (folded words,
// groups, expressions by line, one that must match nowhere); a solution's step for what a result
// already shows passed; task files that are malformed refused with where and why. The model on a
// llama-server: its vocabulary read from the model's file (server.h); the training data of a run
// (training.h): one sequence while each prompt goes on from the one before, the model's own
// answers the only part learnt; a solution's step written as the model would have written it (a
// teacher's turns).

#include "bench.h"
#include "server.h"
#include "training.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::assistantbench;

namespace {

QString sourceDir()
{
    return QStringLiteral(BRULION_SOURCE_DIR);
}

QString notebooks()
{
    return sourceDir() + u"/finetune/notebooks"_s;
}

const QList<Task> &allTasks()
{
    static const QList<Task> tasks = [] {
        QString error;
        const std::optional<QList<Task>> read = readTasks(sourceDir() + u"/finetune/tasks"_s, &error);
        if (!read)
            qWarning("%s", qPrintable(error));
        return read.value_or(QList<Task>());
    }();
    return tasks;
}

Task taskNamed(const QString &id)
{
    for (const Task &t : allTasks()) {
        if (t.id == id)
            return t;
    }
    return {};
}

Step calling(const QString &name, const QVariantMap &input)
{
    Step step;
    step.calls << ai::ToolCall{QString(), name, input};
    return step;
}

Step answering(const QString &text)
{
    Step step;
    step.answer = text;
    return step;
}

/// Runs `task` with `steps` as its solution; the failures of its check.
QStringList played(Task task, const QList<Step> &steps, Outcome *outcome = nullptr)
{
    task.solution = steps;
    QTemporaryDir work;
    SolutionResponder solution;
    const Outcome o = run(task, notebooks(), work.path(), solution);
    if (outcome)
        *outcome = o;
    return check(task, o);
}

/// A task file of dom-pl with `tasks`.
QByteArray fileWith(const QJsonArray &tasks)
{
    const QJsonObject root{{u"notebook"_s, u"dom-pl"_s},
                           {u"language"_s, u"pl"_s},
                           {u"now"_s, u"2026-11-16T09:30"_s},
                           {u"tasks"_s, tasks}};
    return QJsonDocument(root).toJson();
}

/// A task "a" that is right as it is, with `changes` made to it and the keys `without` left out.
QJsonObject task(const QJsonObject &changes, const QStringList &without = {})
{
    QJsonObject t{{u"id"_s, u"a"_s},
                  {u"category"_s, u"search"_s},
                  {u"ask"_s, u"?"_s},
                  {u"solution"_s, QJsonArray{QJsonObject{{u"answer"_s, u"Tak."_s}}}}};
    for (auto it = changes.begin(); it != changes.end(); ++it)
        t.insert(it.key(), it.value());
    for (const QString &key : without)
        t.remove(key);
    return t;
}

/// Writes `json` to a file of `dir`; its path.
QString writeTasks(const QTemporaryDir &dir, const QByteArray &json)
{
    const QString file = dir.filePath(u"tasks.json"_s);
    QFile f(file);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(json);
    return file;
}

/// A GGUF file's header with these key-value pairs (no tensors): (key, GGUF type, value) — a
/// string, a number (uint32) or a list of strings or of int32.
QByteArray ggufWith(const QList<std::tuple<QByteArray, quint32, QVariant>> &pairs)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    const auto string = [&](const QByteArray &s) {
        out << quint64(s.size());
        out.writeRawData(s.constData(), int(s.size()));
    };
    out.writeRawData("GGUF", 4);
    out << quint32(3) << quint64(0) << quint64(pairs.size());
    for (const auto &[key, type, value] : pairs) {
        string(key);
        out << type;
        if (type == 8) {
            string(value.toByteArray());
        } else if (type == 4) {
            out << quint32(value.toUInt());
        } else { // 9: an array
            const QVariantList list = value.toList();
            const bool strings = list.isEmpty() || list.constFirst().typeId() == QMetaType::QByteArray;
            out << quint32(strings ? 8 : 5) << quint64(list.size());
            for (const QVariant &v : list) {
                if (strings)
                    string(v.toByteArray());
                else
                    out << qint32(v.toInt());
            }
        }
    }
    return bytes;
}

/// A prompt or an answer of pieces: (text, special, tokens).
Tokenized tokenized(const QList<std::tuple<QString, bool, QList<int>>> &pieces)
{
    Tokenized t;
    for (const auto &[text, special, tokens] : pieces) {
        t.pieces.append(ai::local::Piece{text, special, {}});
        t.tokens.append(tokens);
    }
    return t;
}

/// The training sequence `s` as "text|text…", the learnt pieces in [brackets].
QString shown(const QJsonArray &s)
{
    QStringList parts;
    for (const QJsonValue v : s) {
        const QJsonObject o = v.toObject();
        const QString text = o.value(u"text"_s).toString();
        parts << (o.value(u"train"_s).toBool() ? u'[' + text + u']' : text);
    }
    return parts.join(u'|');
}

} // namespace

class TestAssistantBench : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void tasksAreRead()
    {
        QVERIFY(allTasks().size() >= 40);
        QSet<QString> languages;
        for (const Task &t : allTasks()) {
            languages.insert(t.language);
            QVERIFY2(!t.solution.isEmpty(), qPrintable(t.id));
            QVERIFY2(t.now.isValid(), qPrintable(t.id));
        }
        QCOMPARE(languages, (QSet<QString>{u"pl"_s, u"en"_s}));
        // The file's clock is the tasks' own (wall-clock time, never through UTC).
        const Task t = taskNamed(u"dom-pl/kod-domofonu"_s);
        QCOMPARE(t.now, QDateTime(QDate(2026, 11, 16), QTime(9, 30)));
        QCOMPARE(t.notebook, u"dom-pl"_s);
        QCOMPARE(t.messages.size(), 1);
    }

    void solutionsDoTheTasks_data()
    {
        QTest::addColumn<QString>("id");
        for (const Task &t : allTasks())
            QTest::newRow(qPrintable(t.id)) << t.id;
    }

    void solutionsDoTheTasks()
    {
        QFETCH(QString, id);
        const Task task = taskNamed(id);
        QTemporaryDir work;
        SolutionResponder solution;
        const Outcome outcome = run(task, notebooks(), work.path(), solution);
        // Done, and its answer grounded: a fact the run never showed teaches a model to make one up.
        const QStringList failures = check(task, outcome) + checkGrounding(task, outcome);
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(u"\n"_s) + u"\nsaid: "_s + outcome.said
                                                + u"\nfailed calls: "_s + outcome.toolFailures.join(u" | "_s)));
        // Every step was a request (but one for what a result already showed): the solution is the
        // whole conversation, no more and no less.
        QCOMPARE(outcome.rounds.size() + solution.skipped(), task.solution.size());
        QCOMPARE(outcome.toolErrors, 0);
        QVERIFY(!outcome.lastRequest.isEmpty());
        QVERIFY(outcome.message.startsWith(outcome.said));
    }

    void aRelativeNotebooksFolder()
    {
        // The notes keep their paths in the notebook, whatever folder the notebooks are named by.
        const QString before = QDir::currentPath();
        QVERIFY(QDir::setCurrent(sourceDir()));
        const Task task = taskNamed(u"dom-pl/mleko"_s);
        QTemporaryDir work;
        SolutionResponder solution;
        const Outcome outcome = run(task, u"finetune/notebooks"_s, work.path(), solution);
        QDir::setCurrent(before);
        QVERIFY2(check(task, outcome).isEmpty(), qPrintable(check(task, outcome).join(u'\n')));
        QCOMPARE(outcome.changed(), QStringList{u"Zakupy.md"_s});
        QVERIFY(outcome.before.contains(u"Przepisy/Pierogi ruskie.md"_s));
    }

    void wrongAnswersFail()
    {
        // Another note changed than the task's, and the task's left as it was.
        const Task milk = taskNamed(u"dom-pl/mleko"_s);
        QStringList failures = played(milk, {calling(u"append_to_page"_s, {{u"name"_s, u"Dom"_s}, {u"markdown"_s, u"- mleko"_s}}),
                                             answering(u"Dopisałam."_s)});
        QVERIFY2(failures.contains(u"Dom.md: changed, and the task changes no such note"_s), qPrintable(failures.join(u'\n')));
        QVERIFY2(failures.contains(u"Zakupy.md: does not match /^- (\\[ \\] )?mleko/"_s), qPrintable(failures.join(u'\n')));
        // The whole page written over instead of added to: the tool it must not use.
        failures = played(milk, {calling(u"write_page"_s, {{u"name"_s, u"Zakupy"_s}, {u"content"_s, u"# Zakupy\n\n- mleko\n"_s}}),
                                 answering(u"Gotowe."_s)});
        QVERIFY2(failures.contains(u"tools: write_page called"_s), qPrintable(failures.join(u'\n')));
        QVERIFY2(failures.contains(u"Zakupy.md: does not match /- chleb/"_s), qPrintable(failures.join(u'\n')));

        // An answer without what was asked for, and one that never came (only tool calls).
        const Task code = taskNamed(u"dom-pl/kod-domofonu"_s);
        failures = played(code, {answering(u"Nie wiem."_s)});
        QCOMPARE(failures, QStringList{u"answer: lacks “4721”"_s});
        Outcome outcome;
        QList<Step> forever;
        for (int i = 0; i < 20; ++i)
            forever << calling(u"search_notes"_s, {{u"query"_s, u"domofon"_s}});
        failures = played(code, forever, &outcome);
        QVERIFY2(failures.contains(u"answer: none (the assistant did not finish)"_s), qPrintable(failures.join(u'\n')));
        QCOMPARE(outcome.rounds.size(), 16); // the assistant's own limit

        // A tool that does not exist: counted, the answer still checked.
        failures = played(code, {calling(u"find_code"_s, {{u"what"_s, u"domofon"_s}}), answering(u"Kod to 4721."_s)}, &outcome);
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(u'\n')));
        QCOMPARE(outcome.toolErrors, 1);
        QCOMPARE(outcome.unknownTools, 1);

        // The solution runs out: the model's error is the run's.
        failures = played(code, {calling(u"search_notes"_s, {{u"query"_s, u"kod"_s}})});
        QVERIFY2(failures.contains(u"run: the model failed: the solution has no step left"_s), qPrintable(failures.join(u'\n')));
    }

    void aStepForWhatIsShownIsPassed()
    {
        // A page read for the phone number only when no result shows it yet: after a search that
        // showed the page whole the solution answers at once, after one that did not it reads.
        const Task phone = taskNamed(u"work-en/julia-phone"_s);
        Step read = calling(u"read_page"_s, {{u"name"_s, u"Julia Martens"_s}});
        read.unlessShown = u"5557 1203"_s;
        const Step answer = answering(u"Julia Martens: +49 30 5557 1203."_s);
        Task task = phone;
        task.solution = {calling(u"read_page"_s, {{u"name"_s, u"Julia Martens"_s}}), read, answer};
        QTemporaryDir work;
        SolutionResponder solution;
        Outcome outcome = run(task, notebooks(), work.path(), solution);
        QVERIFY2(check(task, outcome).isEmpty(), qPrintable(check(task, outcome).join(u'\n')));
        QCOMPARE(outcome.toolsCalled, QStringList{u"read_page"_s});
        QCOMPARE(solution.skipped(), 1);
        task.solution = {calling(u"list_pages"_s, {}), read, answer};
        QTemporaryDir again;
        outcome = run(task, notebooks(), again.path(), solution);
        QVERIFY2(check(task, outcome).isEmpty(), qPrintable(check(task, outcome).join(u'\n')));
        QCOMPARE(outcome.toolsCalled, (QStringList{u"list_pages"_s, u"read_page"_s}));
        QCOMPARE(solution.skipped(), 0);
        QCOMPARE(checkGrounding(task, outcome), QStringList{});
    }

    void alternativesAreRead()
    {
        // A retry as right as another: named, not played.
        QJsonObject retry{{u"call"_s, u"search_notes"_s},
                          {u"input"_s, QJsonObject{{u"query"_s, u"czynsz"_s}}},
                          {u"alternatives"_s, QJsonArray{QJsonObject{{u"call"_s, u"list_pages"_s}, {u"input"_s, QJsonObject()}}}}};
        QTemporaryDir dir;
        QString error;
        const std::optional<QList<Task>> read = readTasks(
                writeTasks(dir, fileWith({task({{u"solution"_s, QJsonArray{retry, QJsonObject{{u"answer"_s, u"Tak."_s}}}}})})),
                &error);
        QVERIFY2(read, qPrintable(error));
        const Step &step = read->constFirst().solution.constFirst();
        QCOMPARE(step.calls.size(), 1);
        QCOMPARE(step.calls.constFirst().name, u"search_notes"_s);
        QCOMPARE(step.alternatives.size(), 1);
        QCOMPARE(step.alternatives.constFirst().constFirst().name, u"list_pages"_s);
    }

    void aDeclinedChangeIsNotTriedAgain()
    {
        // The first change is declined; asked again, the user's answer is yes — and that is a change
        // the task does not allow.
        const Task task = taskNamed(u"dom-pl/odmowa-chleb"_s);
        const Step add = calling(u"append_to_page"_s, {{u"name"_s, u"Zakupy"_s}, {u"markdown"_s, u"- chleb razowy"_s}});
        Outcome outcome;
        const QStringList failures = played(task, {add, add, answering(u"Dopisałam."_s)}, &outcome);
        QCOMPARE(failures, QStringList{u"Zakupy.md: changed, and the task changes no such note"_s});
        QCOMPARE(outcome.confirmations, 2);
        QCOMPARE(outcome.toolErrors, 0); // a declined change is the user's no, not a failed call
        QCOMPARE(outcome.changed(), QStringList{u"Zakupy.md"_s});
    }

    void aLineAddedTwiceFails()
    {
        // The change made again — a small model goes on adding what it added — is a failure even
        // where the note holds what the task asks for.
        const Task task = taskNamed(u"dom-pl/mleko"_s);
        QVERIFY(!task.id.isEmpty());
        const Step add = calling(u"append_to_page"_s, {{u"name"_s, u"Zakupy"_s}, {u"markdown"_s, u"- mleko"_s}});
        Outcome once;
        QVERIFY(played(task, {add, answering(u"Dopisałam mleko do [[Zakupy]]."_s)}, &once).isEmpty());
        // (The note as a second change would leave it: the assistant itself no longer makes the very
        // same call twice, but a change made again another way is the bench's to catch.)
        Outcome twice = once;
        twice.after[u"Zakupy.md"_s] += u"- mleko\n"_s;
        QCOMPARE(check(task, twice), QStringList{u"Zakupy.md: “- mleko” added twice"_s});
    }

    void theAssistantStepsIn()
    {
        // A model that only repeats the change it made: the assistant ends the question itself, and
        // its message is the answer — the attempt is no training data, nor one whose answer it asked
        // for again (a change said to be made, none made).
        const Task task = taskNamed(u"dom-pl/mleko"_s);
        const Step add = calling(u"append_to_page"_s, {{u"name"_s, u"Zakupy"_s}, {u"markdown"_s, u"- mleko"_s}});
        Outcome outcome;
        QCOMPARE(played(task, {add, add, add}, &outcome), QStringList{});
        QCOMPARE(outcome.said, u"Gotowe."_s);
        QCOMPARE(outcome.message, u"Gotowe.\n\n✏️ Zmienione: [[Zakupy]]"_s);
        QCOMPARE(outcome.rounds.size(), 3);
        QVERIFY(outcome.assisted);
        QCOMPARE(played(task, {answering(u"Dopisałam mleko do [[Zakupy]]."_s), add, answering(u"Dopisałam mleko."_s)}, &outcome),
                 QStringList{});
        QCOMPARE(outcome.said, u"Dopisałam mleko."_s);
        QVERIFY(outcome.assisted);
        // The model's own answer, after a repeat too: as ever.
        QCOMPARE(played(task, {add, add, answering(u"Dopisałam mleko."_s)}, &outcome), QStringList{});
        QCOMPARE(outcome.said, u"Dopisałam mleko."_s);
        QVERIFY(!outcome.assisted);
        // The steps run out: no answer.
        QList<Step> forever;
        for (int i = 0; i < 20; ++i)
            forever << calling(u"read_page"_s, {{u"name"_s, u"Zakupy"_s}});
        played(task, forever, &outcome);
        QCOMPARE(outcome.said, QString());
        QVERIFY(!outcome.assisted);
    }

    void theAnswerIsTrueToWhatChanged()
    {
        // A change claimed, no note changed: a draft "saved" to the journal (the real answer of a
        // model to "Przygotuj krótką informację…"), in either language; said not to be made, it is
        // no claim. (The answer as the run would end with it: the assistant may ask the model again.)
        const Task percent = taskNamed(u"dom-pl/procent"_s);
        Outcome draft;
        QVERIFY(played(percent, percent.solution, &draft).isEmpty());
        const auto saying = [](Outcome outcome, const QString &said) {
            outcome.said = said;
            return outcome;
        };
        QCOMPARE(check(percent, saying(draft, u"Zapisałem w dzienniku: 15% z 240 zł to 36 zł."_s)),
                 QStringList{u"answer: claims a change (“Zapisałem”), and no note changed"_s});
        QCOMPARE(check(percent, saying(draft, u"15% z 240 zł to 36 zł. Nie zapisałam tego w dzienniku."_s)), QStringList{});
        const Task email = taskNamed(u"work-en/decline-email"_s);
        QVERIFY(played(email, email.solution, &draft).isEmpty());
        QCOMPARE(check(email, saying(draft, u"I've added a draft to today's journal: unfortunately I can't make it on Friday."_s)),
                 QStringList{u"answer: claims a change (“I've added”), and no note changed"_s});
        QCOMPARE(check(email, saying(draft, u"Hi, I'm afraid I won't be able to make it this time. Best regards"_s)),
                 QStringList{});

        // The run's own change said to have been there already (the model's second call refused as
        // "so already"): the user is told nothing was done. Said to be made, or to be so now, it passes.
        const Task tyres = taskNamed(u"dom-pl/opony-zrobione"_s);
        Outcome ticked;
        QVERIFY(played(tyres, tyres.solution, &ticked).isEmpty());
        QCOMPARE(ticked.changed(), QStringList{u"Samochód.md"_s});
        QCOMPARE(check(tyres, saying(ticked, u"Zadanie „Wymienić opony na zimowe” było już odhaczone. Nie trzeba nic zmieniać."_s)),
                 QStringList{u"answer: says it was so already (“było już”), and the run changed Samochód.md"_s});
        QCOMPARE(check(tyres, saying(ticked, u"The task was already marked done, so there's nothing left to check off."_s)),
                 QStringList{u"answer: says it was so already (“was already”), and the run changed Samochód.md"_s});
        QCOMPARE(check(tyres, saying(ticked, u"Opony są już wymienione: odhaczyłam to zadanie w [[Samochód]]."_s)), QStringList{});
        QCOMPARE(check(tyres, saying(ticked, u"Zadanie „Wymienić opony na zimowe” jest teraz odhaczone ✅."_s)), QStringList{});

        // A date only the system text names (its example of a journal's name) given as the notebook's;
        // one a tool showed is the notebook's.
        const Task code = taskNamed(u"dom-pl/kod-domofonu"_s);
        Outcome found;
        QVERIFY(played(code, code.solution, &found).isEmpty());
        QCOMPARE(check(code, saying(found, u"Kod do domofonu to 4721 (wpis z 2026-11-12)."_s)),
                 QStringList{u"answer: “2026-11-12” is only the system text's example, no fact of the notebook"_s});
        QVERIFY(played(code, {calling(u"read_page"_s, {{u"name"_s, u"2026-11-12"_s}}), answering(u"Kod to 4721."_s)}, &found).isEmpty());
        QCOMPARE(check(code, saying(found, u"Kod do domofonu to 4721 (wpis z 2026-11-12)."_s)), QStringList{});
    }

    void aReminderMayGoToEitherDay()
    {
        const Task task = taskNamed(u"dom-pl/przypomnij-mama"_s);
        const QString reminder = u"- [ ] Zadzwonić do mamy ⏰ 2026-11-17 18:00"_s;
        QStringList failures = played(task, {calling(u"append_to_journal"_s, {{u"markdown"_s, reminder}, {u"date"_s, u"2026-11-17"_s}}),
                                             answering(u"Dodałam."_s)});
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(u'\n')));
        // A wrong hour fails in the note it went to.
        failures = played(task, {calling(u"append_to_journal"_s, {{u"markdown"_s, u"- Zadzwonić do mamy ⏰ 2026-11-17 08:00"_s}}),
                                 answering(u"Dodałam."_s)});
        const QString missed = u"journals/2026-11-16.md: does not match /^(?=.*mam).*⏰ 2026-11-17 18:00/"_s;
        QCOMPARE(failures, QStringList{missed});
        // The reminder on a line of its own, the task without it: the alarm would ring with no text.
        failures = played(task, {calling(u"append_to_journal"_s, {{u"markdown"_s, u"- [ ] Zadzwonić do mamy\n⏰ 2026-11-17 18:00"_s}}),
                                 answering(u"Dodałam."_s)});
        QCOMPARE(failures, QStringList{missed});
        // Nowhere.
        failures = played(task, {answering(u"Dobrze."_s)});
        QCOMPARE(failures, QStringList{u"journals/2026-11-16.md | journals/2026-11-17.md: no such note"_s});
    }

    void textChecks_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QStringList>("contains");
        QTest::addColumn<QStringList>("anyOf");
        QTest::addColumn<QStringList>("notContains");
        QTest::addColumn<QStringList>("matches");
        QTest::addColumn<QStringList>("failures");
        QTest::newRow("folded") << u"ŻÓŁW je sałatę"_s << QStringList{u"zolw"_s, u"Sałatę"_s} << QStringList{}
                                << QStringList{} << QStringList{} << QStringList{};
        QTest::newRow("missing") << u"Żółw"_s << QStringList{u"kot"_s} << QStringList{} << QStringList{} << QStringList{}
                                 << QStringList{u"t: lacks “kot”"_s};
        QTest::newRow("any of a group") << u"do 31 grudnia"_s << QStringList{} << QStringList{u"2026-12-31"_s, u"31 GRUDNIA"_s}
                                        << QStringList{} << QStringList{} << QStringList{};
        QTest::newRow("none of a group") << u"jutro"_s << QStringList{} << QStringList{u"dziś"_s, u"teraz"_s} << QStringList{}
                                         << QStringList{} << QStringList{u"t: lacks any of “dziś” | “teraz”"_s};
        QTest::newRow("must not hold") << u"Filtr w okapie"_s << QStringList{} << QStringList{} << QStringList{u"filtr"_s}
                                       << QStringList{} << QStringList{u"t: holds “filtr”"_s};
        QTest::newRow("by line") << u"# Zakupy\n\n- chleb\n- mleko\n"_s << QStringList{} << QStringList{} << QStringList{}
                                 << QStringList{u"^- mleko$"_s, u"^# zakupy"_s} << QStringList{};
        QTest::newRow("no match") << u"- [ ] mleko"_s << QStringList{} << QStringList{} << QStringList{}
                                  << QStringList{u"^- \\[x\\]"_s} << QStringList{u"t: does not match /^- \\[x\\]/"_s};
        QTest::newRow("narrow spaces, hyphens") << u"Tel. 601\u202F234\u00A0567, do 2026\u201112\u201131"_s
                                                << QStringList{u"601 234 567"_s} << QStringList{u"2026-12-31"_s}
                                                << QStringList{} << QStringList{u"do \\d{4}-\\d\\d-31"_s}
                                                << QStringList{};
        QTest::newRow("typographic apostrophes") << u"I didn\u2019t find it"_s << QStringList{u"didn't find"_s}
                                                 << QStringList{} << QStringList{} << QStringList{u"didn't"_s}
                                                 << QStringList{};
        QTest::newRow("a number alone") << u"50 sztuk"_s << QStringList{} << QStringList{} << QStringList{}
                                        << QStringList{u"(^|[^0-9])5([^0-9]|$)"_s}
                                        << QStringList{u"t: does not match /(^|[^0-9])5([^0-9]|$)/"_s};
    }

    void textChecks()
    {
        QFETCH(QString, text);
        QFETCH(QStringList, contains);
        QFETCH(QStringList, anyOf);
        QFETCH(QStringList, notContains);
        QFETCH(QStringList, matches);
        QFETCH(QStringList, failures);
        TextCheck c;
        c.contains = contains;
        if (!anyOf.isEmpty())
            c.anyOf << anyOf;
        c.notContains = notContains;
        c.matches = matches;
        QCOMPARE(checkText(c, text, u"t"_s), failures);
    }

    void aWordInOneSenseOnly_data()
    {
        // notMatches: an expression that must match nowhere. The unpaid invoices may name the paid
        // one as paid — what every model measured did (its real answers first) — but never as one to
        // be paid.
        QTest::addColumn<QString>("answer");
        QTest::addColumn<QStringList>("failures");
        QTest::newRow("best, thinking: a table, the paid one said") << u"From the **Invoices** page:\n"
                   "\n"
                   "| Number | Client | Amount | Paid |\n"
                   "|---|---|---|---|\n"
                   "| 2026-031 | Northwind Bakery | 3 000 EUR | yes |\n"
                   "| 2026-032 | Harbor Fitness | 4 500 EUR | no |\n"
                   "| 2026-033 | Northwind Bakery | 2 250 EUR | no |\n"
                   "\n"
                   "**Not paid yet:**\n"
                   "- **2026-032** – Harbor Fitness — 4,500 EUR\n"
                   "- **2026-033** – Northwind Bakery — 2,250 EUR\n"
                   "\n"
                   "(2026-031 is marked as paid.)"_s
                << QStringList{};
        QTest::newRow("GRPO 1: a table, the paid one said") << u"From the **Invoices** page:\n"
                   "\n"
                   "| Number | Client | Amount | Paid |\n"
                   "|---|---|---|---|\n"
                   "| 2026-031 | Northwind Bakery | 3 000 EUR | yes |\n"
                   "| 2026-032 | Harbor Fitness | 4 500 EUR | no |\n"
                   "| 2026-033 | Northwind Bakery | 2 250 EUR | no |\n"
                   "\n"
                   "The invoices **not yet paid** are:\n"
                   "- **2026-032** – Harbor Fitness, 4 500 EUR\n"
                   "- **2026-033** – Northwind Bakery, 2 250 EUR\n"
                   "\n"
                   "(2026-031 for Northwind Bakery is marked as paid.)"_s
                << QStringList{};
        QTest::newRow("SFT 1, epoch 1: a note at the end") << u"The unpaid invoices are:\n"
                   "\n"
                   "- **2026-032** – Harbor Fitness – 4,500 EUR (not paid)\n"
                   "- **2026-033** – Northwind Bakery – 2,250 EUR (not paid)\n"
                   "\n"
                   "(Note: invoice 2026-031 for Northwind Bakery is marked as paid.)"_s
                << QStringList{};
        QTest::newRow("SFT 2: a note at the end") << u"The unpaid invoices are:\n"
                   "- 2026-032 (Harbor Fitness) — 4500 EUR, not paid\n"
                   "- 2026-033 (Northwind Bakery) — 2250 EUR, not paid\n"
                   "\n"
                   "(Note: invoice 2026-031 for Northwind Bakery is marked as paid.)"_s
                << QStringList{};
        QTest::newRow("only the unpaid") << u"2026-032 (Harbor Fitness) and 2026-033 (Northwind Bakery)."_s << QStringList{};
        QTest::newRow("on one line") << u"Not paid: 2026-032 and 2026-033; 2026-031 has been paid."_s << QStringList{};
        QTest::newRow("paid, in brackets") << u"2026-032 and 2026-033 are unpaid (2026-031 was paid on 2026-11-05)."_s
                                           << QStringList{};
        // An invoice named by its amount: GRPO 2's real answer, the paid one said by a quoted "yes".
        QTest::newRow("GRPO 2: the amounts") << u"The unpaid invoices are:\n"
                   "\n"
                   "- **Harbor Fitness** — 4 500 EUR (not paid)\n"
                   "- **Northwind Bakery** — 2 250 EUR (not paid)\n"
                   "\n"
                   "(Note: the table also lists a third row for Northwind Bakery at 3 000 EUR marked as \"yes\" paid, so only "
                   "the two rows above are unpaid.)"_s
                << QStringList{};
        QTest::newRow("a number and an amount") << u"Unpaid: 2026-032 and the 2250 EUR one."_s << QStringList{};
        QTest::newRow("the clients only") << u"Harbor Fitness and Northwind Bakery haven't paid."_s
                                          << QStringList{u"answer: does not match"_s, u"answer: does not match"_s};
        const QStringList named{u"answer: matches"_s};
        QTest::newRow("the paid one by its amount") << u"Unpaid: Harbor Fitness 4 500 EUR, Northwind Bakery 3 000 EUR and 2 250 EUR."_s
                                                    << named;
        QTest::newRow("all three") << u"Unpaid: 2026-031, 2026-032 and 2026-033."_s << named;
        QTest::newRow("in the list") << u"The invoices not yet paid:\n- 2026-031 — 3 000 EUR\n- 2026-032 — 4 500 EUR\n"
                                        "- 2026-033 — 2 250 EUR"_s
                                     << named;
        QTest::newRow("said unpaid") << u"2026-031 hasn't been paid, nor has 2026-032 or 2026-033."_s << named;
        QTest::newRow("not yet paid") << u"2026-031 (Northwind Bakery) is not yet paid; 2026-032 and 2026-033 neither."_s
                                      << named;
        QTest::newRow("still to be paid") << u"Still to be paid: 2026-031, 2026-032 and 2026-033."_s << named;
        QTest::newRow("none of them paid") << u"Unpaid: 2026-031, 2026-032 and 2026-033 (none of them paid yet)."_s << named;
        QTest::newRow("its row says no") << u"| 2026-031 | Northwind Bakery | 3 000 EUR | no |\n2026-032, 2026-033"_s << named;
    }

    void aWordInOneSenseOnly()
    {
        QFETCH(QString, answer);
        QFETCH(QStringList, failures);
        const Task task = taskNamed(u"work-en/unpaid-invoices"_s);
        QVERIFY(!task.answer.notMatches.isEmpty());
        QStringList seen = checkText(task.answer, answer, u"answer"_s);
        for (QString &f : seen)
            f = f.section(u" /"_s, 0, 0); // (the expression itself left out)
        QCOMPARE(seen, failures);

        // The kind alone: a text that matches fails with what matched.
        TextCheck c;
        c.notMatches << u"^- \\[ \\] mleko"_s;
        QCOMPARE(checkText(c, u"# Zakupy\n- [x] mleko\n"_s, u"t"_s), QStringList{});
        QCOMPARE(checkText(c, u"# Zakupy\n- [ ] mleko\n"_s, u"t"_s), QStringList{u"t: matches /^- \\[ \\] mleko/ (“- [ ] mleko”)"_s});
    }

    void ungroundedFacts_data()
    {
        QTest::addColumn<QString>("answer");
        QTest::addColumn<QString>("seen");
        QTest::addColumn<QStringList>("workedOut"); // an anyOf group of the values it works out
        QTest::addColumn<QStringList>("unread");    // the facts it states without having read them
        QTest::newRow("read") << u"Hydraulik: pan Marek Wiśniewski, tel. 601 234 567 ([[Kontakty]])."_s
                              << u"- [[Kontakty]], line 3: Hydraulik: pan Marek Wiśniewski, tel. 601 234 567"_s
                              << QStringList{} << QStringList{};
        QTest::newRow("made up") << u"Your vet is Kate Edwards, phone 07700 900666 ([[Kate Edwards]])."_s
                                 << u"- [[Kate Edwards]], line 3: Vet (ref People/Kate Edwards.md#L3@1a2b3c4d)"_s
                                 << QStringList{} << QStringList{u"07700 900666"_s};
        QTest::newRow("grouped otherwise") << u"4500 EUR and 2,250 EUR; 07700985955"_s
                                           << u"| 4 500 EUR | no |\n| 2 250 EUR | no |\nPhone: 07700 985955"_s
                                           << QStringList{} << QStringList{};
        QTest::newRow("a part of a number") << u"5557 1203, not 557 120"_s << u"- Phone: +49 30 5557 1203"_s
                                            << QStringList{} << QStringList{u"557 120"_s};
        QTest::newRow("spaces and hyphens of every kind")
                << u"Tel. 601 234 567, do 2026‑12‑31"_s << u"tel. 601 234 567, ważne do 2026-12-31"_s
                << QStringList{} << QStringList{};
        QTest::newRow("another day") << u"Ważne do 2026-12-30."_s << u"ważne do 2026-12-31"_s << QStringList{}
                                     << QStringList{u"2026-12-30"_s};
        QTest::newRow("times on either clock") << u"Przed 9:00, after 4 pm, at 16:30 and 7:15 am"_s
                                               << u"przed 09:00; call after 16:00; 4:30 pm; 07.15"_s << QStringList{}
                                               << QStringList{};
        QTest::newRow("another time") << u"Call after 5 pm, or at 10:00."_s << u"Call after 4 pm, 10.00 at the latest"_s
                                      << QStringList{} << QStringList{u"5 pm"_s};
        QTest::newRow("a year") << u"Skoda Fabia z 2018 roku, 84 200 km"_s << u"- Skoda Fabia, rok 2017, przy 84 200 km"_s
                                << QStringList{} << QStringList{u"2018"_s};
        QTest::newRow("small numbers are no facts") << u"6 h, 12 km, release 1.5, 15%"_s << QString() << QStringList{}
                                                    << QStringList{};
        QTest::newRow("a sum") << u"Razem 338,55 zł (86,40 zł + 42,15 zł + 210 zł)."_s
                               << u"Wydatki: zakupy w Biedronce 86,40 zł\nWydatki: apteka 42,15 zł, paliwo 210 zł"_s
                               << QStringList{u"338,55"_s, u"338.55"_s} << QStringList{};
        QTest::newRow("a sum, not asked for") << u"Razem 338,55 zł (86,40 zł + 42,15 zł + 210 zł)."_s
                                              << u"Wydatki: zakupy w Biedronce 86,40 zł\nWydatki: apteka 42,15 zł, paliwo 210 zł"_s
                                              << QStringList{} << QStringList{u"338,55"_s};
        QTest::newRow("a sum of a wrong part") << u"Razem 338,65 zł (86,50 zł + 42,15 zł + 210 zł)."_s
                                               << u"zakupy 86,40 zł; apteka 42,15 zł, paliwo 210 zł"_s
                                               << QStringList{u"338,55"_s} << QStringList{u"338,65"_s, u"86,50"_s};
        // A bare hour of the notes, said as a time: the full hour, morning or afternoon.
        QTest::newRow("bare hours as times") << u"Dzwoń między 12:00 a 14:00."_s << u"- Dzwonić między 12 a 14"_s
                                             << QStringList{} << QStringList{};
        QTest::newRow("bare hours, the other clock") << u"Between 12 pm and 2 pm, or from 09:00."_s
                                                     << u"call between 12 and 2 pm; open from 9 to 17"_s << QStringList{}
                                                     << QStringList{};
        QTest::newRow("a bare hour has no minutes") << u"O 12:30."_s << u"dzwonić między 12 a 14"_s << QStringList{}
                                                    << QStringList{u"12:30"_s};
        QTest::newRow("a day of the month is no hour") << u"Do 15:00."_s << u"Czynsz płatny do 15. dnia miesiąca, 15 zł"_s
                                                       << QStringList{} << QStringList{u"15:00"_s};
        // Its own arithmetic, shown: the result is the answer's work, its parts must be read (or small).
        QTest::newRow("worked out, shown") << u"8 × 60 = 480 minut, plus 30 = 510 minut."_s << u"Ile minut ma 8,5 godziny?"_s
                                           << QStringList{} << QStringList{};
        QTest::newRow("a sum, shown") << u"£245.67 + £207.46 = £453.13, and 2 250 + 4 500 = 6 750 EUR"_s
                                      << u"Shopping at Tesco: £245.67\nShopping at Aldi: £207.46\n| 2 250 EUR |\n| 4 500 EUR |"_s
                                      << QStringList{} << QStringList{};
        QTest::newRow("worked out wrong") << u"8 × 60 = 490 minut."_s << QString() << QStringList{} << QStringList{u"490"_s};
        QTest::newRow("no knowledge for a part") << u"2 kg to 2 × 1000 = 2000 g."_s << u"2 kg mąki"_s << QStringList{}
                                                 << QStringList{u"1000"_s};
        QTest::newRow("no work, no result") << u"2 kg to 2000 g."_s << u"2 kg mąki"_s << QStringList{}
                                            << QStringList{u"2000"_s};
    }

    void ungroundedFacts()
    {
        QFETCH(QString, answer);
        QFETCH(QString, seen);
        QFETCH(QStringList, workedOut);
        QFETCH(QStringList, unread);
        TextCheck worked;
        if (!workedOut.isEmpty())
            worked.anyOf << workedOut;
        QStringList expected;
        for (const QString &fact : std::as_const(unread))
            expected << u"answer: “%1” is in nothing the model read — no tool result, message or system text of the run holds it"_s.arg(fact);
        QCOMPARE(assistantbench::ungroundedFacts(answer, seen, worked), expected);
    }

    void aSolutionStatesOnlyWhatItRead()
    {
        // A number the run never showed: done by the check (the number is right), but not grounded —
        // the model would learn to give a number it never saw.
        const Task phone = taskNamed(u"work-en/julia-phone"_s);
        const Step answer = answering(u"Julia Martens: +49 30 5557 1203."_s);
        Outcome outcome;
        QVERIFY(played(phone, {answer}, &outcome).isEmpty());
        QString seen = seenBefore(outcome);
        QVERIFY2(seen.contains(u"What's Julia's phone number?"_s) && seen.contains(u"Brulion"_s), qPrintable(seen));
        // (Of the system text its words: its date is the journal's example, no fact.)
        QVERIFY2(!seen.contains(u"2026-11-12"_s), qPrintable(seen));
        QCOMPARE(checkGrounding(phone, outcome).size(), 1);
        // Read: grounded.
        QVERIFY(played(phone, {calling(u"read_page"_s, {{u"name"_s, u"Julia Martens"_s}}), answer}, &outcome).isEmpty());
        QCOMPARE(checkGrounding(phone, outcome), QStringList{});

        // What the model wrote in its own call counts: a reminder's day and hour, worked out from "jutro".
        const Task reminder = taskNamed(u"dom-pl/przypomnij-mama"_s);
        QVERIFY(played(reminder, {calling(u"append_to_journal"_s, {{u"markdown"_s, u"- [ ] Zadzwonić do mamy ⏰ 2026-11-17 18:00"_s}}),
                                  answering(u"Przypomnę 2026-11-17 o 18:00."_s)},
                       &outcome)
                        .isEmpty());
        seen = seenBefore(outcome);
        QVERIFY(seen.contains(u"⏰ 2026-11-17 18:00"_s));
        QVERIFY(seen.contains(u"2026-11-16"_s)); // (the day goes along)
        QCOMPARE(checkGrounding(reminder, outcome), QStringList{});
        // The app's calendar of the days to come is no ground: a day the answer gets wrong
        // ("pojutrze" for the call's "jutro") is the answer's own, whatever the calendar lists.
        played(reminder, {calling(u"append_to_journal"_s, {{u"markdown"_s, u"- [ ] Zadzwonić do mamy ⏰ 2026-11-17 18:00"_s}}),
                          answering(u"Przypomnę 2026-11-18 o 18:00 (dziś jest 2026-11-16)."_s)},
               &outcome);
        QCOMPARE(checkGrounding(reminder, outcome).size(), 1);
        QVERIFY2(checkGrounding(reminder, outcome).constFirst().contains(u"2026-11-18"_s),
                 qPrintable(checkGrounding(reminder, outcome).join(u'\n')));
        Outcome calendar;
        calendar.lastRequest = QVariantMap{
                {u"system"_s, u"Jesteś asystentem."_s},
                {u"messages"_s, QVariantList{QVariantMap{{u"role"_s, u"user"_s},
                                                         {u"content"_s, u"Dziś jest środa, 2027-04-19. Jutro: czwartek 2027-04-20; "
                                                                        "pojutrze: piątek 2027-04-21.\n\nPrzypomnij mi o 2027-04-30."_s}}}}};
        seen = seenBefore(calendar);
        QVERIFY2(seen.contains(u"2027-04-19"_s) && seen.contains(u"2027-04-30"_s) && !seen.contains(u"2027-04-20"_s)
                         && !seen.contains(u"2027-04-21"_s),
                 qPrintable(seen));
        QCOMPARE(checkGrounding(reminder, Outcome{}), QStringList{}); // (no answer: nothing to ground)

        // A sum the task works out: what its checks ask for is the answer's own; its parts are read.
        const Task spent = taskNamed(u"dom-pl/wydatki-tydzien"_s);
        QVERIFY(played(spent, {calling(u"search_notes"_s, {{u"query"_s, u"Wydatki"_s}}),
                               answering(u"338,55 zł: 86,40 zł + 42,15 zł + 210 zł."_s)},
                       &outcome)
                        .isEmpty());
        QCOMPARE(checkGrounding(spent, outcome), QStringList{});
        played(spent, {calling(u"search_notes"_s, {{u"query"_s, u"Wydatki"_s}}), answering(u"338,55 zł: 86,40 zł + 42,15 zł + 210 zł, a w sumie z czynszem 1 690 zł."_s)},
               &outcome);
        QCOMPARE(checkGrounding(spent, outcome).size(), 1);
    }

    void malformedTasksAreRefused_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("error");
        QTest::newRow("not JSON") << QByteArray("{") << u"no JSON object"_s;
        QTest::newRow("no tasks") << fileWith({}) << u"tasks.json: no tasks"_s;
        QTest::newRow("unknown key") << fileWith({task({{u"expct"_s, QJsonObject()}})})
                                     << u"tasks.json: a: unknown key \"expct\""_s;
        QTest::newRow("unknown category") << fileWith({task({{u"category"_s, u"chat"_s}})})
                                          << u"tasks.json: a: unknown category \"chat\""_s;
        QTest::newRow("no solution") << fileWith({task({}, {u"solution"_s})}) << u"tasks.json: a: no solution"_s;
        QTest::newRow("ends with a call")
                << fileWith({task({{u"solution"_s, QJsonArray{QJsonObject{{u"call"_s, u"list_pages"_s}}}}})})
                << u"tasks.json: a: the solution must end with an answer"_s;
        QTest::newRow("a step does both")
                << fileWith({task({{u"solution"_s, QJsonArray{QJsonObject{{u"call"_s, u"list_pages"_s}, {u"answer"_s, u"x"_s}}}}})})
                << u"tasks.json: a: solution step 1: a step either calls tools or answers"_s;
        QTest::newRow("ask and messages") << fileWith({task({{u"messages"_s, QJsonArray()}})})
                                          << u"tasks.json: a: either \"ask\""_s;
        QTest::newRow("assistant last")
                << fileWith({task({{u"messages"_s, QJsonArray{QJsonObject{{u"role"_s, u"assistant"_s}, {u"content"_s, u"Hej"_s}}}}},
                                  {u"ask"_s})})
                << u"tasks.json: a: the conversation must end with the user's message"_s;
        QTest::newRow("a bad clock") << fileWith({task({{u"now"_s, u"jutro"_s}})})
                                     << u"tasks.json: a: \"jutro\" is no date and time"_s;
        QTest::newRow("a bad expression")
                << fileWith({task({{u"expect"_s, QJsonObject{{u"answer"_s, QJsonObject{{u"matches"_s, QJsonArray{u"(x"_s}}}}}}})})
                << u"is malformed"_s;
        QTest::newRow("unlessShown on an answer")
                << fileWith({task({{u"solution"_s, QJsonArray{QJsonObject{{u"answer"_s, u"x"_s}, {u"unlessShown"_s, u"x"_s}}}}})})
                << u"tasks.json: a: solution step 1: \"unlessShown\" and \"alternatives\" belong to a step that calls tools"_s;
        QTest::newRow("an empty alternative")
                << fileWith({task({{u"solution"_s,
                                    QJsonArray{QJsonObject{{u"call"_s, u"list_pages"_s}, {u"alternatives"_s, QJsonArray{QJsonObject()}}},
                                               QJsonObject{{u"answer"_s, u"x"_s}}}}})})
                << u"tasks.json: a: solution step 1: an alternative is a call"_s;
        QTest::newRow("a bad answer") << fileWith({task({{u"answers"_s, QJsonArray{u"tak"_s}}})})
                                      << u"tasks.json: a: an answer is \"yes\", \"all\" or \"no\""_s;
        QTest::newRow("an id twice") << fileWith({task({}), task({})})
                                     << u"tasks.json: the id \"a\" is taken by another task"_s;
    }

    void malformedTasksAreRefused()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, error);
        QTemporaryDir dir;
        QString message;
        QVERIFY(!readTasks(writeTasks(dir, json), &message));
        QVERIFY2(message.contains(error), qPrintable(message));
    }

    void aModelsVocabularyIsRead()
    {
        // GPT-2's letters for bytes: "Ġ" a space, "Ċ" a line break, "Ã³" the two bytes of "ó".
        const QVariantList tokens{QByteArray("a"), QByteArray("\xc4\xa0" "b"), QByteArray("<|im_end|>"), QByteArray("<tool_call>"),
                                  QByteArray("\xc3\x83\xc2\xb3"), QByteArray("<unused0>"), QByteArray("\xc4\x8a"), QByteArray("<")};
        const QVariantList types{1, 1, 3, 4, 1, 5, 1, 4};
        QTemporaryDir dir;
        const auto file = [&](const QString &name, const QByteArray &bytes) {
            QFile f(dir.filePath(name));
            if (f.open(QIODevice::WriteOnly))
                f.write(bytes);
            return f.fileName();
        };
        QString error;
        const std::optional<Vocabulary> v = readVocabulary(
                file(u"model.gguf"_s, ggufWith({{"general.architecture", 8, QByteArray("lfm3")},
                                                {"general.alignment", 4, 32},
                                                {"tokenizer.ggml.model", 8, QByteArray("gpt2")},
                                                {"tokenizer.ggml.merges", 9, QVariantList{QByteArray("a b")}},
                                                {"tokenizer.ggml.tokens", 9, tokens},
                                                {"tokenizer.ggml.token_type", 9, types}})),
                &error);
        QVERIFY2(v, qPrintable(error));
        // (A control token of one letter is no place to cut a text at.)
        QCOMPARE(v->controls, (QHash<QString, int>{{u"<|im_end|>"_s, 2}, {u"<tool_call>"_s, 3}}));
        QCOMPARE(v->bytes, (QList<QByteArray>{"a", " b", "<|im_end|>", "<tool_call>", "\xc3\xb3", "", "\n", "<"}));

        QVERIFY(!readVocabulary(file(u"notes.md"_s, "# Not a model\n"), &error));
        QVERIFY2(error.contains(u"not a GGUF file"_s), qPrintable(error));
        QVERIFY(!readVocabulary(file(u"spm.gguf"_s, ggufWith({{"tokenizer.ggml.model", 8, QByteArray("llama")},
                                                                {"tokenizer.ggml.tokens", 9, tokens},
                                                                {"tokenizer.ggml.token_type", 9, types}})),
                                &error));
        QVERIFY2(error.contains(u"only byte-level BPE"_s), qPrintable(error));
        QVERIFY(!readVocabulary(file(u"short.gguf"_s, ggufWith({{"tokenizer.ggml.model", 8, QByteArray("gpt2")},
                                                                  {"tokenizer.ggml.tokens", 9, tokens}})
                                                               .chopped(5)),
                                &error));
        QVERIFY2(error.contains(u"ends too early"_s), qPrintable(error));
    }

    void trainingSequences_data()
    {
        QTest::addColumn<QList<Turn>>("turns");
        QTest::addColumn<QStringList>("sequences");
        // The first request and the model's answer; the next request holds both, token for token.
        const Tokenized ask = tokenized({{u"<|im_start|>"_s, true, {1}},
                                         {u"user\nCo mam kupić?"_s, false, {10, 11, 12}},
                                         {u"<|im_end|>"_s, true, {2}},
                                         {u"\n"_s, false, {13}},
                                         {u"<|im_start|>"_s, true, {1}},
                                         {u"assistant\n"_s, false, {14}}});
        const Tokenized call = tokenized({{u"<tool_call>"_s, true, {5}}, {u"read_page Zakupy"_s, false, {30, 31}}});
        Tokenized again = ask;
        for (const Tokenized &more : {call, tokenized({{u"<|im_end|>"_s, true, {2}},
                                                        {u"\n"_s, false, {13}},
                                                        {u"<|im_start|>"_s, true, {1}},
                                                        {u"tool\n- [ ] masło"_s, false, {40, 41}},
                                                        {u"<|im_end|>"_s, true, {2}},
                                                        {u"<|im_start|>"_s, true, {1}},
                                                        {u"assistant\n"_s, false, {14}}})}) {
            again.pieces += more.pieces;
            again.tokens += more.tokens;
        }
        const Tokenized answer = tokenized({{u"Masło."_s, false, {50, 51}}});
        const QString first = u"<|im_start|>|user\nCo mam kupić?|<|im_end|>|\n|<|im_start|>|assistant\n"_s;
        const QString rest = u"\n|<|im_start|>|tool\n- [ ] masło|<|im_end|>|<|im_start|>|assistant\n"_s;

        QTest::newRow("one turn") << QList<Turn>{{ask, answer, true}} << QStringList{first + u"|[Masło.]|[<|im_end|>]"_s};
        QTest::newRow("it goes on") << QList<Turn>{{ask, call, true}, {again, answer, true}}
                                    << QStringList{first + u"|[<tool_call>]|[read_page Zakupy]|[<|im_end|>]|"_s + rest
                                                   + u"|[Masło.]|[<|im_end|>]"_s};
        // The app's prompt writes the answer in other tokens than the model did: the model on the
        // device reads it anew, so does the training.
        Tokenized retold = again;
        retold.tokens[7] = {30, 32, 33};
        QTest::newRow("written otherwise") << QList<Turn>{{ask, call, true}, {retold, answer, true}}
                                           << QStringList{first + u"|[<tool_call>]|[read_page Zakupy]|[<|im_end|>]"_s,
                                                          first + u"|<tool_call>|read_page Zakupy|<|im_end|>|"_s + rest
                                                                  + u"|[Masło.]|[<|im_end|>]"_s};
        // The same tokens, but the end of the turn and what follows it one piece of the next
        // prompt: no place to go on from.
        Tokenized joined = again;
        joined.pieces[8].text += joined.pieces[9].text;
        joined.tokens[8] += joined.tokens[9];
        joined.pieces.removeAt(9);
        joined.tokens.removeAt(9);
        QTest::newRow("no boundary") << QList<Turn>{{ask, call, true}, {joined, answer, true}}
                                     << QStringList{first + u"|[<tool_call>]|[read_page Zakupy]|[<|im_end|>]"_s,
                                                    first + u"|<tool_call>|read_page Zakupy|<|im_end|>\n|"_s + rest.mid(2)
                                                            + u"|[Masło.]|[<|im_end|>]"_s};
        QTest::newRow("cut short") << QList<Turn>{{ask, answer, false}} << QStringList{first + u"|Masło."_s};
        // The model's own step before a teacher's correction: read, its end of turn too; the
        // teacher's answer after it learnt.
        QTest::newRow("only read") << QList<Turn>{{ask, call, true, false}, {again, answer, true}}
                                   << QStringList{first + u"|<tool_call>|read_page Zakupy|<|im_end|>|"_s + rest
                                                  + u"|[Masło.]|[<|im_end|>]"_s};
        QTest::newRow("nothing") << QList<Turn>{} << QStringList{};
    }

    // A solution's step as the model would have written it: the turn prompt() writes back into
    // the conversation, which reply() reads as the same answer.
    void aSolutionIsWrittenAsTheModelWrites_data()
    {
        QTest::addColumn<Step>("step");
        QTest::addColumn<QString>("effort");
        QTest::addColumn<QStringList>("written");

        Step search = calling(u"search_notes"_s, {{u"query"_s, u"kod do furtki"_s}});
        search.thoughts = u"Użytkownik pyta o kod: szukam w notatkach."_s;
        QTest::newRow("a call") << search << QString()
                                << QStringList{u"Użytkownik pyta o kod: szukam w notatkach."_s, u"[</think>]"_s,
                                               u"[<tool_call>]"_s, u"\n"_s, u"[<function=]"_s,
                                               u"search_notes>\n"_s, u"[<parameter=]"_s,
                                               u"query>\nkod do furtki\n"_s, u"[</parameter>]"_s, u"\n"_s,
                                               u"[</function>]"_s, u"\n"_s, u"[</tool_call>]"_s};
        Step answer = answering(u"Kod do furtki to 4721 ([[Dom]])."_s);
        answer.thoughts = u"Mam kod."_s;
        QTest::newRow("an answer") << answer << QString()
                                   << QStringList{u"Mam kod."_s, u"[</think>]"_s, u"Kod do furtki to 4721 ([[Dom]])."_s};
        QTest::newRow("no thoughts") << answering(u"Żółw ma 4 nogi."_s) << QString()
                                     << QStringList{u"[</think>]"_s, u"Żółw ma 4 nogi."_s};
        QTest::newRow("no thinking") << answering(u"Żółw ma 4 nogi."_s) << u"low"_s
                                     << QStringList{u"Żółw ma 4 nogi."_s};
    }

    void aSolutionIsWrittenAsTheModelWrites()
    {
        QFETCH(Step, step);
        QFETCH(QString, effort);
        QFETCH(QStringList, written);
        ai::Request request;
        request.system = u"Jesteś asystentem w aplikacji do notatek."_s;
        request.effort = effort;
        request.tools << ai::Tool{u"search_notes"_s, u"Szuka w notatkach."_s,
                                  QJsonObject{{u"type"_s, u"object"_s},
                                              {u"properties"_s,
                                               QJsonObject{{u"query"_s, QJsonObject{{u"type"_s, u"string"_s}}}}}}};
        request.messages << ai::textMessage(u"user"_s, u"Jaki jest kod do furtki?"_s);
        QString error;
        const std::optional<QList<ai::local::Piece>> pieces =
                writtenAnswer(u"lfm3"_s, request, replyOf(step, request), u"<|im_end|>"_s, &error);
        QVERIFY2(pieces, qPrintable(error));
        QStringList seen;
        for (const ai::local::Piece &p : *pieces)
            seen << (p.special ? u"[%1]"_s.arg(p.text) : p.text);
        QCOMPARE(seen, written);
    }

    void trainingSequences()
    {
        QFETCH(QList<Turn>, turns);
        QFETCH(QStringList, sequences);
        const QList<QJsonArray> made = assistantbench::trainingSequences(turns, u"<|im_end|>"_s, 2);
        QStringList seen;
        for (const QJsonArray &s : made) {
            seen << shown(s);
            // Every segment's tokens, in order: the sequence as the model reads it.
            for (const QJsonValue v : s)
                QVERIFY(!v.toObject().value(u"tokens"_s).toArray().isEmpty());
        }
        QCOMPARE(seen, sequences);
    }
};

QTEST_GUILESS_MAIN(TestAssistantBench)
#include "tst_assistantbench.moc"
