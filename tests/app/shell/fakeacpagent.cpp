// An AI agent that speaks the Agent Client Protocol on its standard input and output, played for
// tst_shell_agents: what it answers depends on the last paragraph of the user's newest message.
//
//   "… pozwolenie …"  asks whether it may write Zgoda.md (session/request_permission); allowed: it
//                     writes the file and says "Zapisano.", else "Nie zapisano."; answered
//                     "cancelled": ends the turn with that stop reason. "… później …" too: it asks
//                     1.5 s later (the device may be gone by then)
//   "… czekaj …"      answers nothing until the turn is cancelled (session/cancel)
//   "… długo …"       "Długo: " and 70 000 × "ż" as the answer being written, then nothing until the
//                     turn is cancelled (an answer longer than a device is given at once)
//   "… powoli …"      a tool call ("Read Dom.md"), "Powoli: ", 1.5 s later "gotowe."
//   "… koniec …"      writes "fake agent: koniec" to its standard error and exits with code 3
//   "… czytaj …"      reads the first line of Dom.md through the client (fs/read_text_file):
//                     "Przeczytano: <it>"
//   "… zapisz …"      writes Od agenta.md through the client (fs/write_text_file) and tries a file
//                     outside the notebook — <cwd>-outside.txt, beside the folder: a test's own,
//                     writable everywhere (a Code mode thread may write it; /etc/… would be C:\etc
//                     on Windows): "Zapisano przez klienta; poza notatnikiem: <the error>"
//   "… terminal …"    runs `sh -c 'printf "ąę\ndrugi\n"; sleep 0.4; exit 4'` in a terminal of the client, as a
//                     tool call showing it: "Wyjście (4): <its output>"
//   "… pokaż …"       a plan, a call done, a command that runs until the turn is cancelled
//   "… plan …"        a plan of two steps, then "Plan gotowy."
//   "… zatwierdź …"   asks to approve a plan (Claude Code leaving its plan mode: a switch_mode call,
//                     the plan in its input): "Plan przyjęty." / "Planuję dalej."
//   "… edytuj …"      changes Dom.md by itself, as Claude Code's own Edit does: a pending edit call
//                     naming the file, then the file changed, then the call completed with its diff:
//                     "Zmieniono Dom.md."
//   "… narzędzia …"   a tool call and no words
//   "… model …"       "Model: <the model option now>"
//   "… obrazy …"      "Obrazy: <the prompt's pictures: the file names of its image and resource_link blocks>"
//   "… sesja …"       "Sesja: <the session's id>"
//   "… katalog …"     "Katalog: <its cwd>"
//   "… pwd …"         runs `pwd` in a terminal of the client without a folder, then with the folder
//                     "/": "Pwd: <its output> | /: <the second's output, or its error's code>"
//   "… do gita …"     writes .git/brulion-test in its cwd through the client: "Do gita: <the error's
//                     code, or 0>"
//   "aplikacja <tool> <JSON arguments> [&& <tool> <JSON> …]"
//                     starts the session's MCP server (its command, arguments and environment from
//                     session/new — Brulion's, given by the app or by acp serve) and calls each tool
//                     there: "Aplikacja: <the result's envelope> | …" (" [obraz]" after one with a
//                     picture); the tool "lista" lists its brulion_app_* tools; no server:
//                     "Aplikacja: brak serwera"
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
#include <QLockFile>
#include <QProcess>
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

/// One line of the MCP server's answers (an object), waiting for it.
QJsonObject readAnswer(QProcess &server, int id)
{
    QByteArray pending;
    for (int i = 0; i < 600; ++i) {
        while (server.canReadLine()) {
            const QJsonObject o = QJsonDocument::fromJson(server.readLine()).object();
            if (o.value(u"id"_s).toInt(-1) == id)
                return o;
        }
        if (server.state() == QProcess::NotRunning && !server.canReadLine())
            return {};
        server.waitForReadyRead(200);
    }
    return {};
}

/// The calls "<tool> <JSON> && <tool> <JSON> …" of a session's MCP server `config` ({command, args,
/// env: [{name, value}]}): what each one answered.
QString callTools(const QJsonObject &config, const QString &calls)
{
    if (config.isEmpty())
        return u"Aplikacja: brak serwera"_s;
    QProcess server;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QJsonValue &v : config.value(u"env"_s).toArray())
        env.insert(v.toObject().value(u"name"_s).toString(), v.toObject().value(u"value"_s).toString());
    server.setProcessEnvironment(env);
    QStringList args;
    for (const QJsonValue &a : config.value(u"args"_s).toArray())
        args << a.toString();
    server.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    server.start(config.value(u"command"_s).toString(), args);
    if (!server.waitForStarted(30000))
        return u"Aplikacja: serwer nie wystartował"_s;
    const auto send = [&server](const QJsonObject &m) {
        server.write(QJsonDocument(m).toJson(QJsonDocument::Compact) + '\n');
        server.waitForBytesWritten(5000);
    };
    send({{u"jsonrpc"_s, u"2.0"_s},
          {u"id"_s, 1},
          {u"method"_s, u"initialize"_s},
          {u"params"_s, QJsonObject{{u"protocolVersion"_s, u"2025-06-18"_s},
                                    {u"capabilities"_s, QJsonObject()},
                                    {u"clientInfo"_s, QJsonObject{{u"name"_s, u"fake"_s}, {u"version"_s, u"1"_s}}}}}});
    if (readAnswer(server, 1).isEmpty())
        return u"Aplikacja: serwer nie odpowiada"_s;
    send({{u"jsonrpc"_s, u"2.0"_s}, {u"method"_s, u"notifications/initialized"_s}});
    QStringList said;
    int id = 2;
    for (const QString &part : calls.split(u"&&"_s, Qt::SkipEmptyParts)) {
        const QString call = part.trimmed();
        const QString tool = call.section(u' ', 0, 0);
        if (tool == u"lista") {
            send({{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, id}, {u"method"_s, u"tools/list"_s}});
            QStringList names;
            for (const QJsonValue &t :
                 readAnswer(server, id++).value(u"result"_s).toObject().value(u"tools"_s).toArray()) {
                if (const QString name = t.toObject().value(u"name"_s).toString(); name.startsWith(u"brulion_app_"_s))
                    names << name;
            }
            said << names.join(u',');
            continue;
        }
        const QJsonObject arguments = QJsonDocument::fromJson(call.section(u' ', 1).trimmed().toUtf8()).object();
        send({{u"jsonrpc"_s, u"2.0"_s},
              {u"id"_s, id},
              {u"method"_s, u"tools/call"_s},
              {u"params"_s, QJsonObject{{u"name"_s, tool}, {u"arguments"_s, arguments}}}});
        const QJsonObject answer = readAnswer(server, id++);
        const QJsonObject result = answer.value(u"result"_s).toObject();
        if (result.isEmpty()) {
            said << QString::fromUtf8(
                    QJsonDocument(answer.value(u"error"_s).toObject()).toJson(QJsonDocument::Compact));
            continue;
        }
        QString text = QString::fromUtf8(
                QJsonDocument(result.value(u"structuredContent"_s).toObject()).toJson(QJsonDocument::Compact));
        for (const QJsonValue &c : result.value(u"content"_s).toArray()) {
            if (c.toObject().value(u"type"_s).toString() == u"image")
                text += u" [obraz]"_s;
        }
        said << text;
    }
    server.closeWriteChannel();
    if (!server.waitForFinished(10000))
        server.kill();
    return u"Aplikacja: "_s + said.join(u" | "_s);
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
    QHash<QString, QJsonObject> servers; // session → its first MCP server (session/new, /resume)

    std::string raw;
    while (std::getline(std::cin, raw)) {
        const QByteArray line = QByteArray::fromStdString(raw);
        if (log.isOpen()) {
            // Several agents write to one log: each line at the end as it is now, one writer at a
            // time (Windows' append mode only starts at the end, so two agents overwrote each other).
            QLockFile lock(log.fileName() + u".lock"_s);
            (void)lock.lock();
            (void)log.seek(log.size());
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
            servers.insert(session, params.value(u"mcpServers"_s).toArray().at(0).toObject());
            reply(id, sessionResult(session));
            afterSession(session);
        } else if (method == u"session/resume") {
            const QString session = params.value(u"sessionId"_s).toString();
            if (session == u"gone") {
                write({{u"id"_s, id}, {u"error"_s, QJsonObject{{u"code"_s, -32603}, {u"message"_s, u"No such session"_s}}}});
                continue;
            }
            cwd = params.value(u"cwd"_s).toString();
            servers.insert(session, params.value(u"mcpServers"_s).toArray().at(0).toObject());
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
            if (text.startsWith(u"aplikacja "_s)) {
                update(session, {{u"sessionUpdate"_s, u"tool_call"_s},
                                 {u"toolCallId"_s, u"a1"_s},
                                 {u"title"_s, u"brulion (MCP)"_s},
                                 {u"kind"_s, u"other"_s},
                                 {u"status"_s, u"in_progress"_s}});
                end(callTools(servers.value(session), text.mid(10)));
                continue;
            }
            if (text.contains(u"obrazy"_s)) {
                QStringList names;
                for (const QJsonValue &b : prompt) {
                    const QJsonObject block = b.toObject();
                    // (It takes no images: a picture comes as a link to its file.)
                    const QString type = block.value(u"type"_s).toString();
                    if (type == u"image" || type == u"resource_link")
                        names << (block.value(u"uri"_s).toString().isEmpty()
                                          ? u"?"_s
                                          : block.value(u"uri"_s).toString().section(u'/', -1));
                }
                end(u"Obrazy: "_s + (names.isEmpty() ? u"brak"_s : names.join(u", "_s)));
                continue;
            }
            if (text.contains(u"koniec"_s)) {
                std::fputs("fake agent: koniec\n", stderr);
                std::fflush(stderr);
                return 3;
            }
            if (text.contains(u"powoli"_s)) {
                // A long turn (the device may be gone meanwhile): a tool call, then the answer.
                update(session, {{u"sessionUpdate"_s, u"tool_call"_s},
                                 {u"toolCallId"_s, u"p1"_s},
                                 {u"title"_s, u"Read Dom.md"_s},
                                 {u"kind"_s, u"read"_s},
                                 {u"status"_s, u"completed"_s}});
                say(session, u"Powoli: "_s);
                QThread::msleep(1500);
                end(u"gotowe."_s);
                continue;
            }
            if (text.contains(u"długo"_s)) {
                waiting = id;
                waitingSession = session;
                say(session, u"Długo: "_s + QString(70000, u'ż'));
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
                if (text.contains(u"później"_s))
                    QThread::msleep(1500);
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
                    [end, session, outside = QString(QDir::cleanPath(cwd) + u"-outside.txt"_s)](const QJsonObject &a) {
                        const bool ok = !a.contains(u"error"_s);
                        ask(u"fs/write_text_file"_s,
                            {{u"sessionId"_s, session}, {u"path"_s, outside}, {u"content"_s, u"x"_s}},
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
            if (text.contains(u"zatwierdź"_s)) {
                update(session, {{u"sessionUpdate"_s, u"tool_call"_s}, {u"toolCallId"_s, u"x1"_s},
                                 {u"title"_s, u"Approve Plan"_s}, {u"kind"_s, u"switch_mode"_s}, {u"status"_s, u"pending"_s}});
                ask(u"session/request_permission"_s,
                    {{u"sessionId"_s, session},
                     {u"toolCall"_s, QJsonObject{{u"toolCallId"_s, u"x1"_s}, {u"title"_s, u"Ready to code?"_s},
                                                 {u"rawInput"_s, QJsonObject{{u"plan"_s, u"# Porządki\n\n1. Połączyć notatki\n2. Usunąć duplikaty"_s}}}}},
                     {u"options"_s, QJsonArray{QJsonObject{{u"optionId"_s, u"yes"_s}, {u"name"_s, u"Yes"_s}, {u"kind"_s, u"allow_always"_s}},
                                               QJsonObject{{u"optionId"_s, u"no"_s}, {u"name"_s, u"No, keep planning"_s},
                                                           {u"kind"_s, u"reject_once"_s}}}}},
                    [end](const QJsonObject &a) {
                        const QJsonObject outcome = a.value(u"result"_s).toObject().value(u"outcome"_s).toObject();
                        end(outcome.value(u"optionId"_s).toString() == u"yes" ? u"Plan przyjęty."_s : u"Planuję dalej."_s);
                    });
                continue;
            }
            if (text.contains(u"edytuj"_s)) {
                const QString file = QDir(cwd).filePath(u"Dom.md"_s);
                update(session, {{u"sessionUpdate"_s, u"tool_call"_s}, {u"toolCallId"_s, u"n1"_s}, {u"title"_s, u"Edit Dom.md"_s},
                                 {u"kind"_s, u"edit"_s}, {u"status"_s, u"pending"_s},
                                 {u"locations"_s, QJsonArray{QJsonObject{{u"path"_s, file}}}}});
                QThread::msleep(300); // (the client sees the call before the file changes)
                QFile f(file);
                QByteArray before;
                if (f.open(QIODevice::ReadOnly))
                    before = f.readAll();
                f.close();
                const QByteArray after = QByteArray(before).replace("Zapłacić czynsz", "Zapłacić czynsz i prąd");
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    f.write(after);
                f.close();
                update(session, {{u"sessionUpdate"_s, u"tool_call_update"_s}, {u"toolCallId"_s, u"n1"_s}, {u"status"_s, u"completed"_s},
                                 {u"content"_s, QJsonArray{QJsonObject{{u"type"_s, u"diff"_s}, {u"path"_s, file},
                                                                       {u"oldText"_s, u"Zapłacić czynsz"_s},
                                                                       {u"newText"_s, u"Zapłacić czynsz i prąd"_s}}}}});
                end(u"Zmieniono Dom.md."_s);
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
            if (text.contains(u"katalog"_s)) {
                end(u"Katalog: "_s + cwd);
                continue;
            }
            if (text.contains(u"do gita"_s)) {
                ask(u"fs/write_text_file"_s,
                    {{u"sessionId"_s, session}, {u"path"_s, QDir(cwd).filePath(u".git/brulion-test"_s)}, {u"content"_s, u"x"_s}},
                    [end](const QJsonObject &a) {
                        end(u"Do gita: "_s + QString::number(a.value(u"error"_s).toObject().value(u"code"_s).toInt()));
                    });
                continue;
            }
            if (text.contains(u"pwd"_s)) {
                // (A command's output once it ended: created, waited for, read, released.)
                const auto run = [session](const QJsonObject &create, std::function<void(const QString &)> then) {
                    ask(u"terminal/create"_s, create, [session, then](const QJsonObject &created) {
                        if (created.contains(u"error"_s)) {
                            then(QString::number(created.value(u"error"_s).toObject().value(u"code"_s).toInt()));
                            return;
                        }
                        const QString terminal = created.value(u"result"_s).toObject().value(u"terminalId"_s).toString();
                        ask(u"terminal/wait_for_exit"_s, {{u"sessionId"_s, session}, {u"terminalId"_s, terminal}},
                            [session, terminal, then](const QJsonObject &) {
                                ask(u"terminal/output"_s, {{u"sessionId"_s, session}, {u"terminalId"_s, terminal}},
                                    [session, terminal, then](const QJsonObject &output) {
                                        const QString out =
                                                output.value(u"result"_s).toObject().value(u"output"_s).toString().trimmed();
                                        ask(u"terminal/release"_s, {{u"sessionId"_s, session}, {u"terminalId"_s, terminal}},
                                            [then, out](const QJsonObject &) { then(out); });
                                    });
                            });
                    });
                };
                run({{u"sessionId"_s, session}, {u"command"_s, u"pwd"_s}}, [run, session, end](const QString &here) {
                    run({{u"sessionId"_s, session}, {u"command"_s, u"pwd"_s}, {u"cwd"_s, u"/"_s}},
                        [end, here](const QString &root) { end(u"Pwd: "_s + here + u" | /: "_s + root); });
                });
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
