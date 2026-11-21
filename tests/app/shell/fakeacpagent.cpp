// An AI agent that speaks the Agent Client Protocol on its standard input and output, played for
// tst_shell_agents: what it answers depends on the last paragraph of the user's newest message.
//
//   "… pozwolenie …"  asks whether it may write Zgoda.md (session/request_permission); allowed: it
//                     writes the file and says "Zapisano.", else "Nie zapisano."
//   "… czekaj …"      answers nothing until the turn is cancelled (session/cancel)
//   "… powoli …"      a tool call ("Read Dom.md"), "Powoli: ", 1.5 s later "gotowe."
//   "… koniec …"      writes "fake agent: koniec" to its standard error and exits with code 3
//   "… czytaj …"      reads the first line of Dom.md through the client (fs/read_text_file):
//                     "Przeczytano: <it>"
//   "… zapisz …"      writes Od agenta.md through the client (fs/write_text_file) and tries a file
//                     outside the notebook: "Zapisano przez klienta; poza notatnikiem: <the error>"
//   "… terminal …"    runs `sh -c 'printf "ąę\ndrugi\n"; sleep 0.4; exit 4'` in a terminal of the client, as a
//                     tool call showing it: "Wyjście (4): <its output>"
//   "… pokaż …"       a plan, a call done, a command that runs until the turn is cancelled
//   "… plan …"        a plan of two steps, then "Plan gotowy."
//   "… narzędzia …"   a tool call and no words
//   "… model …"       "Model: <the model option now>"
//   "… sesja …"       "Sesja: <the session's id>"
//   anything else     a thought, then "Odpowiedź na: <the message>" in two chunks
//
// After each session/new it offers slash commands and says how much of its context is used.
// `--auth`: a new session needs a sign-in first (auth_required, the method "login"). `--config`:
// its sessions take options (configOptions: a model, a bool; the older modes). `--resume`: it
// resumes sessions (session/resume, any id but "gone"). Every line it reads is appended to the
// file $FAKE_ACP_LOG (when set), for the test to look at.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <cstdio>
#include <functional>
#include <iostream>
#include <string>

using namespace Qt::StringLiterals;

namespace {

void write(const QJsonObject &message)
{
    QJsonObject full = message;
    full.insert(u"jsonrpc"_s, u"2.0"_s);
    const QByteArray line = QJsonDocument(full).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fflush(stdout);
}

void reply(const QJsonValue &id, const QJsonValue &result)
{
    write({{u"id"_s, id}, {u"result"_s, result}});
}

void update(const QString &session, const QJsonObject &u)
{
    write({{u"method"_s, u"session/update"_s}, {u"params"_s, QJsonObject{{u"sessionId"_s, session}, {u"update"_s, u}}}});
}

void say(const QString &session, const QString &text, const QString &kind = u"agent_message_chunk"_s)
{
    update(session, {{u"sessionUpdate"_s, kind}, {u"content"_s, QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, text}}}});
}

using Then = std::function<void(const QJsonObject &answer)>;
QHash<QString, Then> waitingFor; // our requests' ids → what follows their answer
int nextRequest = 1;

void ask(const QString &method, const QJsonObject &params, Then then)
{
    const QString id = u"req-%1"_s.arg(nextRequest++);
    waitingFor.insert(id, std::move(then));
    write({{u"id"_s, id}, {u"method"_s, method}, {u"params"_s, params}});
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const bool needsAuth = args.contains(u"--auth"_s);
    const bool config = args.contains(u"--config"_s);
    const bool resumes = args.contains(u"--resume"_s);
    bool signedIn = false;
    int sessions = 0;
    QFile log(qEnvironmentVariable("FAKE_ACP_LOG"));
    if (!log.fileName().isEmpty())
        (void)log.open(QIODevice::Append);

    QString model = u"sonnet"_s;
    bool fast = true;
    QString mode = u"ask"_s;
    const auto options = [&] {
        return QJsonArray{
                QJsonObject{{u"id"_s, u"model"_s}, {u"name"_s, u"Model"_s}, {u"category"_s, u"model"_s}, {u"type"_s, u"select"_s},
                            {u"currentValue"_s, model},
                            {u"options"_s, QJsonArray{QJsonObject{{u"value"_s, u"sonnet"_s}, {u"name"_s, u"Sonnet"_s}},
                                                      QJsonObject{{u"value"_s, u"opus"_s}, {u"name"_s, u"Opus"_s},
                                                                  {u"description"_s, u"Największy"_s}}}}},
                QJsonObject{{u"id"_s, u"fast"_s}, {u"name"_s, u"Fast"_s}, {u"type"_s, u"boolean"_s}, {u"currentValue"_s, fast}}};
    };
    const auto sessionResult = [&](const QString &id) {
        QJsonObject r;
        if (!id.isEmpty())
            r.insert(u"sessionId"_s, id);
        if (config) {
            r.insert(u"configOptions"_s, options());
            r.insert(u"modes"_s, QJsonObject{{u"currentModeId"_s, mode},
                                             {u"availableModes"_s, QJsonArray{QJsonObject{{u"id"_s, u"ask"_s}, {u"name"_s, u"Ask"_s}},
                                                                              QJsonObject{{u"id"_s, u"code"_s}, {u"name"_s, u"Code"_s}}}}});
        }
        return r;
    };
    const auto afterSession = [&](const QString &session) {
        update(session, {{u"sessionUpdate"_s, u"available_commands_update"_s},
                         {u"availableCommands"_s,
                          QJsonArray{QJsonObject{{u"name"_s, u"review"_s}, {u"description"_s, u"Przejrzyj zmiany"_s}},
                                     QJsonObject{{u"name"_s, u"plan"_s}, {u"description"_s, u"Zaplanuj"_s},
                                                 {u"input"_s, QJsonObject{{u"hint"_s, u"czego dotyczy"_s}}}}}}});
    };

    QJsonValue waiting;      // the prompt that waits: for a cancel, or for the permission's answer
    QString waitingSession;
    QString cwd;

    std::string raw;
    while (std::getline(std::cin, raw)) {
        const QByteArray line = QByteArray::fromStdString(raw);
        if (log.isOpen()) {
            log.write(line + '\n');
            log.flush();
        }
        const QJsonObject m = QJsonDocument::fromJson(line).object();
        const QString method = m.value(u"method"_s).toString();
        const QJsonValue id = m.value(u"id"_s);
        const QJsonObject params = m.value(u"params"_s).toObject();

        if (method.isEmpty()) {
            // An answer to one of our requests.
            if (const auto then = waitingFor.take(id.toString())) {
                then(m);
                continue;
            }
            if (id.toString() != u"perm-1" || waiting.isUndefined())
                continue;
            const QJsonObject outcome = m.value(u"result"_s).toObject().value(u"outcome"_s).toObject();
            if (outcome.value(u"outcome"_s).toString() == u"cancelled") {
                reply(waiting, QJsonObject{{u"stopReason"_s, u"cancelled"_s}});
            } else if (outcome.value(u"optionId"_s).toString() == u"allow") {
                QFile f(QDir(cwd).filePath(u"Zgoda.md"_s));
                if (f.open(QIODevice::WriteOnly))
                    f.write("- zgoda\n");
                update(waitingSession, {{u"sessionUpdate"_s, u"tool_call_update"_s}, {u"toolCallId"_s, u"w1"_s},
                                        {u"status"_s, u"completed"_s}});
                say(waitingSession, u"Zapisano."_s);
                reply(waiting, QJsonObject{{u"stopReason"_s, u"end_turn"_s}});
            } else {
                say(waitingSession, u"Nie zapisano."_s);
                reply(waiting, QJsonObject{{u"stopReason"_s, u"end_turn"_s}});
            }
            waiting = QJsonValue::Undefined;
            continue;
        }
        if (method == u"initialize") {
            QJsonObject caps{{u"loadSession"_s, resumes}};
            if (resumes)
                caps.insert(u"sessionCapabilities"_s, QJsonObject{{u"resume"_s, QJsonObject()}});
            reply(id, QJsonObject{
                              {u"protocolVersion"_s, 1},
                              {u"agentInfo"_s, QJsonObject{{u"name"_s, u"fake"_s}, {u"title"_s, u"Fake Agent"_s}, {u"version"_s, u"1.2"_s}}},
                              {u"agentCapabilities"_s, caps},
                              {u"authMethods"_s, QJsonArray{QJsonObject{{u"id"_s, u"login"_s}, {u"name"_s, u"Log in"_s},
                                                                        {u"description"_s, u"Opens the browser"_s}}}}});
        } else if (method == u"authenticate") {
            signedIn = true;
            reply(id, QJsonObject());
        } else if (method == u"session/new") {
            if (needsAuth && !signedIn) {
                write({{u"id"_s, id},
                       {u"error"_s, QJsonObject{{u"code"_s, -32000}, {u"message"_s, u"Authentication required"_s}}}});
                continue;
            }
            cwd = params.value(u"cwd"_s).toString();
            const QString session = u"s%1"_s.arg(++sessions);
            reply(id, sessionResult(session));
            afterSession(session);
        } else if (method == u"session/resume") {
            const QString session = params.value(u"sessionId"_s).toString();
            if (session == u"gone") {
                write({{u"id"_s, id}, {u"error"_s, QJsonObject{{u"code"_s, -32603}, {u"message"_s, u"No such session"_s}}}});
                continue;
            }
            cwd = params.value(u"cwd"_s).toString();
            reply(id, sessionResult(QString()));
            afterSession(session);
        } else if (method == u"session/set_config_option") {
            const QString option = params.value(u"configId"_s).toString();
            if (option == u"model")
                model = params.value(u"value"_s).toString();
            else if (option == u"fast")
                fast = params.value(u"value"_s).toBool();
            reply(id, QJsonObject{{u"configOptions"_s, options()}});
        } else if (method == u"session/set_mode") {
            mode = params.value(u"modeId"_s).toString();
            reply(id, QJsonObject());
        } else if (method == u"session/cancel") {
            if (!waiting.isUndefined()) {
                reply(waiting, QJsonObject{{u"stopReason"_s, u"cancelled"_s}});
                waiting = QJsonValue::Undefined;
            }
        } else if (method == u"session/prompt") {
            const QString session = params.value(u"sessionId"_s).toString();
            const QJsonArray prompt = params.value(u"prompt"_s).toArray();
            QString text;
            for (const QJsonValue &b : prompt) {
                if (b.toObject().value(u"type"_s).toString() == u"text")
                    text = b.toObject().value(u"text"_s).toString(); // (the last: the newest message)
            }
            // (Messages the agent did not answer come as one: its last paragraph is the newest.)
            text = text.section(u"\n\n"_s, -1).trimmed();
            const auto end = [id, session](const QString &words) {
                if (!words.isEmpty())
                    say(session, words);
                update(session, {{u"sessionUpdate"_s, u"usage_update"_s}, {u"used"_s, 40000}, {u"size"_s, 200000},
                                 {u"cost"_s, QJsonObject{{u"amount"_s, 0.25}, {u"currency"_s, u"USD"_s}}}});
                reply(id, QJsonObject{{u"stopReason"_s, u"end_turn"_s}});
            };
            if (text.contains(u"koniec"_s)) {
                std::fputs("fake agent: koniec\n", stderr);
                std::fflush(stderr);
                return 3;
            }
            if (text.contains(u"powoli"_s)) {
                // A long turn (the device may be gone meanwhile): a tool call, then the answer.
                update(session, {{u"sessionUpdate"_s, u"tool_call"_s}, {u"toolCallId"_s, u"p1"_s},
                                 {u"title"_s, u"Read Dom.md"_s}, {u"kind"_s, u"read"_s}, {u"status"_s, u"completed"_s}});
                say(session, u"Powoli: "_s);
                QThread::msleep(1500);
                end(u"gotowe."_s);
                continue;
            }
            if (text.contains(u"czekaj"_s)) {
                waiting = id;
                waitingSession = session;
                say(session, u"Czekam…"_s, u"agent_thought_chunk"_s);
                continue;
            }
            if (text.contains(u"pozwolenie"_s)) {
                waiting = id;
                waitingSession = session;
                update(session, {{u"sessionUpdate"_s, u"tool_call"_s}, {u"toolCallId"_s, u"w1"_s},
                                 {u"title"_s, u"Write Zgoda.md"_s}, {u"kind"_s, u"edit"_s}, {u"status"_s, u"pending"_s}});
                write({{u"id"_s, u"perm-1"_s},
                       {u"method"_s, u"session/request_permission"_s},
                       {u"params"_s,
                        QJsonObject{{u"sessionId"_s, session},
                                    {u"toolCall"_s,
                                     QJsonObject{{u"toolCallId"_s, u"w1"_s},
                                                 {u"content"_s, QJsonArray{QJsonObject{{u"type"_s, u"diff"_s},
                                                                                       {u"path"_s, u"Zgoda.md"_s},
                                                                                       {u"oldText"_s, u"- nic\n"_s},
                                                                                       {u"newText"_s, u"- zgoda"_s}}}}}},
                                    {u"options"_s,
                                     QJsonArray{QJsonObject{{u"optionId"_s, u"allow"_s}, {u"name"_s, u"Allow"_s},
                                                            {u"kind"_s, u"allow_once"_s}},
                                                QJsonObject{{u"optionId"_s, u"reject"_s}, {u"name"_s, u"Reject"_s},
                                                            {u"kind"_s, u"reject_once"_s}}}}}}});
                continue;
            }
            if (text.contains(u"czytaj"_s)) {
                ask(u"fs/read_text_file"_s,
                    {{u"sessionId"_s, session}, {u"path"_s, QDir(cwd).filePath(u"Dom.md"_s)}, {u"line"_s, 1}, {u"limit"_s, 1}},
                    [end](const QJsonObject &a) {
                        end(u"Przeczytano: "_s + a.value(u"result"_s).toObject().value(u"content"_s).toString().trimmed());
                    });
                continue;
            }
            if (text.contains(u"zapisz"_s)) {
                ask(u"fs/write_text_file"_s,
                    {{u"sessionId"_s, session}, {u"path"_s, QDir(cwd).filePath(u"Od agenta.md"_s)}, {u"content"_s, u"- od agenta\n"_s}},
                    [end, session](const QJsonObject &a) {
                        const bool ok = !a.contains(u"error"_s);
                        ask(u"fs/write_text_file"_s,
                            {{u"sessionId"_s, session}, {u"path"_s, u"/etc/brulion-test-outside"_s}, {u"content"_s, u"x"_s}},
                            [end, ok](const QJsonObject &outside) {
                                end((ok ? u"Zapisano przez klienta; poza notatnikiem: "_s : u"Błąd; poza: "_s)
                                    + QString::number(outside.value(u"error"_s).toObject().value(u"code"_s).toInt()));
                            });
                    });
                continue;
            }
            if (text.contains(u"terminal"_s)) {
                ask(u"terminal/create"_s,
                    {{u"sessionId"_s, session}, {u"command"_s, u"sh"_s},
                     {u"args"_s, QJsonArray{u"-c"_s, u"printf 'ąę\\ndrugi\\n'; sleep 0.4; exit 4"_s}}},
                    [end, session](const QJsonObject &created) {
                        const QString terminal = created.value(u"result"_s).toObject().value(u"terminalId"_s).toString();
                        update(session, {{u"sessionUpdate"_s, u"tool_call"_s}, {u"toolCallId"_s, u"t1"_s}, {u"title"_s, u"Run sh"_s},
                                         {u"kind"_s, u"execute"_s}, {u"status"_s, u"in_progress"_s},
                                         {u"content"_s, QJsonArray{QJsonObject{{u"type"_s, u"terminal"_s}, {u"terminalId"_s, terminal}}}}});
                        ask(u"terminal/wait_for_exit"_s, {{u"sessionId"_s, session}, {u"terminalId"_s, terminal}},
                            [end, session, terminal](const QJsonObject &exited) {
                                const int code = exited.value(u"result"_s).toObject().value(u"exitCode"_s).toInt(-1);
                                ask(u"terminal/output"_s, {{u"sessionId"_s, session}, {u"terminalId"_s, terminal}},
                                    [end, session, terminal, code](const QJsonObject &output) {
                                        const QString out = output.value(u"result"_s).toObject().value(u"output"_s).toString();
                                        update(session, {{u"sessionUpdate"_s, u"tool_call_update"_s}, {u"toolCallId"_s, u"t1"_s},
                                                         {u"status"_s, u"completed"_s}});
                                        ask(u"terminal/release"_s, {{u"sessionId"_s, session}, {u"terminalId"_s, terminal}},
                                            [end, code, out](const QJsonObject &) {
                                                end(u"Wyjście (%1): %2"_s.arg(code).arg(out.trimmed().replace(u'\n', u'|')));
                                            });
                                    });
                            });
                    });
                continue;
            }
            if (text.contains(u"pokaż"_s)) {
                // (For screenshots: a plan, a call done, a command that runs until Stop.)
                waiting = id;
                waitingSession = session;
                update(session, {{u"sessionUpdate"_s, u"plan"_s},
                                 {u"entries"_s, QJsonArray{QJsonObject{{u"content"_s, u"Przeczytać notatkę Dom"_s}, {u"status"_s, u"completed"_s}},
                                                           QJsonObject{{u"content"_s, u"Policzyć zadania na ten tydzień"_s}, {u"status"_s, u"in_progress"_s}},
                                                           QJsonObject{{u"content"_s, u"Napisać podsumowanie"_s}, {u"status"_s, u"pending"_s}}}}});
                update(session, {{u"sessionUpdate"_s, u"tool_call"_s}, {u"toolCallId"_s, u"r1"_s}, {u"title"_s, u"Read Dom.md"_s},
                                 {u"kind"_s, u"read"_s}, {u"status"_s, u"completed"_s}});
                update(session, {{u"sessionUpdate"_s, u"tool_call"_s}, {u"toolCallId"_s, u"e2"_s}, {u"title"_s, u"Edit Dom.md"_s},
                                 {u"kind"_s, u"edit"_s}, {u"status"_s, u"completed"_s},
                                 {u"content"_s, QJsonArray{QJsonObject{{u"type"_s, u"diff"_s}, {u"path"_s, QDir(cwd).filePath(u"Dom.md"_s)},
                                                                       {u"oldText"_s, u"- [ ] Zapłacić czynsz\n"_s},
                                                                       {u"newText"_s, u"- [x] Zapłacić czynsz\n- [ ] Kupić mleko\n"_s}}}}});
                say(session, u"Sprawdzam notatki i liczę zadania…"_s);
                ask(u"terminal/create"_s,
                    {{u"sessionId"_s, session}, {u"command"_s, u"sh"_s},
                     {u"args"_s, QJsonArray{u"-c"_s, u"grep -rn -- '- \\[ \\]' . | head -5; sleep 60"_s}}},
                    [session](const QJsonObject &created) {
                        const QString terminal = created.value(u"result"_s).toObject().value(u"terminalId"_s).toString();
                        update(session, {{u"sessionUpdate"_s, u"tool_call"_s}, {u"toolCallId"_s, u"t2"_s},
                                         {u"title"_s, u"grep -rn '- [ ]' ."_s}, {u"kind"_s, u"execute"_s}, {u"status"_s, u"in_progress"_s},
                                         {u"content"_s, QJsonArray{QJsonObject{{u"type"_s, u"terminal"_s}, {u"terminalId"_s, terminal}}}}});
                    });
                continue;
            }
            if (text.contains(u"plan"_s)) {
                update(session, {{u"sessionUpdate"_s, u"plan"_s},
                                 {u"entries"_s, QJsonArray{QJsonObject{{u"content"_s, u"Przeczytać notatki"_s}, {u"status"_s, u"completed"_s}},
                                                           QJsonObject{{u"content"_s, u"Napisać plan"_s}, {u"status"_s, u"in_progress"_s}}}}});
                QThread::msleep(300); // (a plan is shown while the turn goes on)
                end(u"Plan gotowy."_s);
                continue;
            }
            if (text.contains(u"narzędzia"_s)) {
                update(session, {{u"sessionUpdate"_s, u"tool_call"_s}, {u"toolCallId"_s, u"e1"_s}, {u"title"_s, u"Edit Dom.md"_s},
                                 {u"kind"_s, u"edit"_s}, {u"status"_s, u"completed"_s}});
                end(QString());
                continue;
            }
            if (text.contains(u"model"_s)) {
                end(u"Model: "_s + model + (fast ? u" (fast)"_s : QString()) + u", tryb: "_s + mode);
                continue;
            }
            if (text.contains(u"sesja"_s)) {
                end(u"Sesja: "_s + session);
                continue;
            }
            say(session, u"Myślę."_s, u"agent_thought_chunk"_s);
            say(session, u"Odpowiedź "_s);
            end(u"na: "_s + text);
        } else if (!id.isUndefined()) {
            write({{u"id"_s, id}, {u"error"_s, QJsonObject{{u"code"_s, -32601}, {u"message"_s, u"Method not found"_s}}}});
        }
    }
    return 0;
}
