#include "workspaceagents.h"

#include "workspace.h"

#include "log/categories.h"
#include "markdown/encrypted.h"
#include "sync/http.h"
#include "vault/localstore.h"
#include "vault/vault.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#if QT_CONFIG(process) // (not in the browser: AI agents run in the desktop app)
#include <QProcess>
#endif
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

#ifndef BRULION_VERSION
#define BRULION_VERSION "0.0.0"
#endif

using namespace Qt::StringLiterals;

namespace brulion::shell {

/// One agent's program and the protocol over its standard input and output.
struct WorkspaceAgents::Process
{
    AgentConfig config;
    QProcess *process = nullptr;         // a program of this device, or
    acp::RemoteStream *remote = nullptr; // an agent on another computer
    acp::Client *client = nullptr;
    QString errors; // the end of what it wrote to its standard error
};

/// A command an agent runs (terminal/create): its output as it comes, its end.
struct WorkspaceAgents::Terminal
{
    QString agent;
    QString key;
    QProcess *process = nullptr;
    QByteArray output;
    qint64 limit = 0;
    bool truncated = false;
    bool ended = false;
    QJsonValue exitCode; // null: killed by a signal
    QString signal;
    QList<QJsonValue> waiting; // terminal/wait_for_exit requests to answer when it ends
};

namespace {

// (What only the desktop app uses is [[maybe_unused]] where there are no programs: the browser.)
/// The longest end of an agent's standard error kept (its last line says why it stopped).
[[maybe_unused]] constexpr qsizetype MaxErrorChars = 4000;
/// The output a terminal keeps when the agent names no limit.
[[maybe_unused]] constexpr qint64 DefaultOutputLimit = 1024 * 1024;
/// The terminals one agent may have at once.
[[maybe_unused]] constexpr int MaxTerminals = 32;
/// The key of an agent's check (no conversation's path starts with it).
const QString CheckKey = u"\x1f" "check"_s;
const QString StoreKey = u"acp/sessions"_s;

/// The last line of `errors` that says something ("" none).
[[maybe_unused]] QString lastLine(const QString &errors)
{
    const QStringList lines = errors.split(u'\n', Qt::SkipEmptyParts);
    for (qsizetype i = lines.size() - 1; i >= 0; --i) {
        const QString line = lines.at(i).simplified();
        if (!line.isEmpty())
            return line.left(300);
    }
    return {};
}

QString fileUrl(const QString &root, const QString &path)
{
    return QUrl::fromLocalFile(QDir(root).filePath(path)).toString(QUrl::FullyEncoded);
}

[[maybe_unused]] QString findProgram(const QString &program)
{
    return QStandardPaths::findExecutable(program);
}

[[maybe_unused]] bool windows()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

/// `command` split as a shell would.
QStringList splitCommand(const QString &command)
{
#if QT_CONFIG(process)
    return QProcess::splitCommand(command);
#else
    return command.split(u' ', Qt::SkipEmptyParts);
#endif
}

/// A program and its arguments as `commandLine()` resolves them on Windows.
QStringList resolved(QStringList parts, bool onWindows, const std::function<QString(const QString &)> &find)
{
    if (parts.isEmpty() || !onWindows || !find)
        return parts;
    const QString program = parts.first();
    const QString found = QFileInfo(program).isAbsolute() ? program : find(program);
    if (found.isEmpty())
        return parts;
    const QString suffix = QFileInfo(found).suffix().toLower();
    if (suffix == u"cmd" || suffix == u"bat")
        return QStringList{u"cmd.exe"_s, u"/c"_s, QDir::toNativeSeparators(found)} + parts.mid(1);
    parts[0] = found;
    return parts;
}

QVariantMap optionRow(const acp::ConfigOption &o)
{
    QVariantList choices;
    for (const acp::Choice &c : o.choices)
        choices.append(QVariantMap{{u"value"_s, c.value},
                                   {u"name"_s, c.name},
                                   {u"description"_s, c.description},
                                   {u"group"_s, c.group}});
    const QString current = o.boolean ? (o.current.toBool() ? u"true"_s : u"false"_s) : o.current.toString();
    return {{u"id"_s, o.id},
            {u"name"_s, o.name},
            {u"description"_s, o.description},
            {u"category"_s, o.category},
            {u"boolean"_s, o.boolean},
            {u"current"_s, current},
            {u"currentName"_s, o.currentName()},
            {u"choices"_s, choices}};
}

QList<acp::ConfigOption> optionsOf(const QJsonArray &known)
{
    QList<acp::ConfigOption> out;
    for (const QJsonValue &v : known) {
        if (std::optional<acp::ConfigOption> o = acp::ConfigOption::fromJson(v.toObject()))
            out << *o;
    }
    return out;
}

} // namespace

WorkspaceAgents::WorkspaceAgents(Workspace *workspace, QObject *parent)
    : QObject(parent), m_workspace(workspace), m_activityTimer(new QTimer(this))
{
    m_activityTimer->setSingleShot(true);
    m_activityTimer->setInterval(100);
    connect(m_activityTimer, &QTimer::timeout, this, [this] {
        for (const QString &path : std::exchange(m_activityPending, {}))
            Q_EMIT activityChanged(path);
    });
    // A conversation renamed: its session, and the record of it, go along (not while it answers:
    // its turn finishes under the path it started with).
    // A conversation deleted: its sessions closed, its record dropped.
    connect(m_workspace, &Workspace::pathRemoved, this, [this](const QString &path) {
        if (m_turns.contains(path))
            stop(path);
        for (Process *p : std::as_const(m_processes))
            p->client->close(path);
        store(path, std::nullopt);
    });
    connect(m_workspace, &Workspace::pathRenamed, this, [this](const QString &from, const QString &to) {
        if (m_turns.contains(from))
            return;
        for (Process *p : std::as_const(m_processes))
            p->client->rename(from, to);
        if (const std::optional<Stored> entry = stored(from)) {
            store(from, std::nullopt);
            store(to, entry);
        }
    });
}

WorkspaceAgents::~WorkspaceAgents()
{
    for (Terminal *t : std::as_const(m_terminals))
        delete t; // (their programs: below)
    m_terminals.clear();
    for (auto it = m_processes.begin(); it != m_processes.end();) {
        Process *p = it.value();
        if (!p->remote) {
            ++it;
            continue;
        }
        p->client->blockSignals(true);
        p->remote->disconnect(this);
        p->remote->stop(); // (the agent's process ends on that computer)
        delete p->remote;
        delete p->client;
        delete p;
        it = m_processes.erase(it);
    }
#if QT_CONFIG(process)
    for (Process *p : std::as_const(m_processes)) {
        p->client->blockSignals(true);
        p->process->disconnect(this);
        delete p->client;
        delete p;
    }
    // Every program still running ends now (also those asked to end a moment ago: stopProcess()).
    for (QProcess *process : findChildren<QProcess *>(Qt::FindDirectChildrenOnly)) {
        process->disconnect();
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(2000);
        }
        delete process;
    }
#endif
}

// ---- pure parts ---------------------------------------------------------------------------------------

bool WorkspaceAgents::isAgent(const QString &partner)
{
    return partner.trimmed().startsWith(Prefix);
}

bool WorkspaceAgents::available()
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_WASM) || !QT_CONFIG(process)
    return false;
#else
    return true;
#endif
}

bool WorkspaceAgents::remoteAvailable()
{
#if defined(Q_OS_WASM)
    return false;
#else
    return true;
#endif
}

QString WorkspaceAgents::deviceName()
{
    const QString host = QSysInfo::machineHostName().trimmed();
    if (!host.isEmpty() && host != u"localhost")
        return host;
    return QSysInfo::prettyProductName();
}

QString WorkspaceAgents::remoteEndText(const QString &host, acp::RemoteStream::End why, const QString &detail)
{
    using End = acp::RemoteStream::End;
    switch (why) {
    case End::AgentEnded: return detail.isEmpty() ? tr("it ended on %1").arg(host) : tr("%1 (on %2)").arg(detail, host);
    case End::Unauthorized:
        return tr("this device is no longer paired with %1 — pair it again in Settings → Extensions → AI agents")
                .arg(host);
    case End::Gone:
        return tr("%1 no longer knows this conversation's agent (it was started again) — try again").arg(host);
    case End::Unreachable:
        return tr("%1 could not be reached for a while (%2) — is it on, and is this device in its network?")
                .arg(host, detail);
    case End::Refused:
        return detail.isEmpty() ? tr("%1 did not start it").arg(host) : tr("%1 did not start it: %2").arg(host, detail);
    case End::Version:
        return tr("%1 runs another version of Brulion's agent server — update Brulion there and here").arg(host);
    }
    return detail;
}

QString WorkspaceAgents::remoteErrorText(const QString &address, const acp::RemoteError &error)
{
    using Kind = acp::RemoteError::Kind;
    switch (error.kind) {
    case Kind::None: return {};
    case Kind::Network:
        return tr("%1 could not be reached (%2): is the computer on, and is this device in its network?")
                .arg(address, error.detail);
    case Kind::Unauthorized: return tr("%1 does not know this device: pair it again with a new code.").arg(address);
    case Kind::Refused:
        if (error.code == u"bad_code")
            return tr("That is not the code: run acp pair on the computer again and type the new one.");
        if (error.code == u"no_code")
            return tr("No pairing code is valid now: run acp pair on the computer (a code is valid for 10 minutes, "
                      "once).");
        if (error.code == u"too_many_tries")
            return tr("Too many tries: wait a minute, then pair with a new code.");
        return tr("%1 refused: %2").arg(address, error.detail);
    case Kind::Server: return tr("%1 failed: %2").arg(address, error.detail);
    case Kind::Malformed:
        return tr("%1 answered in a way Brulion does not understand (%2): is it the address of brulion-cli acp serve?")
                .arg(address, error.detail);
    case Kind::Version:
        return tr("%1 runs another version of Brulion's agent server: update Brulion there and here.").arg(address);
    }
    return error.detail;
}

QList<AgentConfig> WorkspaceAgents::presets()
{
    return {{u"claude"_s, u"Claude Code"_s, u"npx -y @agentclientprotocol/claude-agent-acp"_s},
            {u"codex"_s, u"Codex"_s, u"npx -y @agentclientprotocol/codex-acp"_s},
            {u"gemini"_s, u"Gemini CLI"_s, u"npx -y @google/gemini-cli --acp"_s},
            {u"opencode"_s, u"OpenCode"_s, u"npx -y opencode-ai acp"_s}};
}

QString WorkspaceAgents::toolsProgram()
{
    const QDir dir(QCoreApplication::applicationDirPath());
    for (const QString &name : {u"brulion-cli"_s, u"brulion-cli.exe"_s}) {
        const QFileInfo cli(dir.filePath(name));
        if (cli.isFile() && cli.isExecutable())
            return QDir::cleanPath(cli.absoluteFilePath());
    }
    return QStandardPaths::findExecutable(u"brulion-cli"_s);
}

QStringList WorkspaceAgents::commandLine(const QString &command, bool onWindows,
                                         const std::function<QString(const QString &)> &find)
{
    return resolved(splitCommand(command), onWindows, find);
}

QString WorkspaceAgents::context(const QString &root, const QString &conversation, const QString &page, bool tools)
{
    QStringList lines;
    lines << u"You are answering in the AI chat of Brulion, the user's notes app. Their notebook is the folder "_s
                     + root
                     + u": Markdown notes linked with [[wiki links]], daily notes, tasks (\"- [ ]\" lines) and reminders."_s;
    if (tools)
        lines << u"The MCP server \"brulion\" has tools for this notebook (search, queries, tasks, journals, pages): "
                 u"use them to find and change notes rather than reading every file."_s;
    lines << u"This conversation is kept in the note \"%1\" and the app writes your answers into it: never change "
             u"that note yourself."_s.arg(conversation);
    if (!page.isEmpty())
        lines << u"The conversation is about the note \"%1\" (attached)."_s.arg(page);
    lines << u"Answer in Markdown, in the language of the user's message."_s;
    return lines.join(u'\n');
}

QString WorkspaceAgents::transcript(const QVariantList &history)
{
    QStringList parts;
    for (qsizetype i = 0; i + 1 < history.size(); ++i) {
        const QVariantMap m = history.at(i).toMap();
        const QString content = m.value(u"content"_s).toString().trimmed();
        if (content.isEmpty())
            continue;
        parts << (m.value(u"role"_s).toString() == u"assistant" ? u"🤖 "_s : u"🧑 "_s) + content;
    }
    if (parts.isEmpty())
        return {};
    return u"The conversation so far (🧑 the user's messages, 🤖 your answers):\n\n"_s + parts.join(u"\n\n"_s);
}

QString WorkspaceAgents::lines(const QString &text, int line, int limit)
{
    if (line <= 1 && limit <= 0)
        return text;
    qsizetype from = 0;
    for (int n = 1; n < line && from >= 0; ++n) {
        from = text.indexOf(u'\n', from);
        if (from >= 0)
            ++from;
    }
    if (from < 0)
        return {};
    if (limit <= 0)
        return text.mid(from);
    qsizetype end = from;
    for (int n = 0; n < limit && end >= 0; ++n) {
        end = text.indexOf(u'\n', end);
        if (end >= 0)
            ++end;
    }
    return end < 0 ? text.mid(from) : text.mid(from, end - from);
}

QByteArray WorkspaceAgents::keepLast(const QByteArray &output, qint64 limit, bool *cut)
{
    if (limit <= 0 || output.size() <= limit) {
        if (cut)
            *cut = false;
        return output;
    }
    qsizetype from = output.size() - limit;
    // (Not in the middle of a character: UTF-8's continuation bytes are 10xxxxxx.)
    while (from < output.size() && (static_cast<unsigned char>(output.at(from)) & 0xC0) == 0x80)
        ++from;
    if (cut)
        *cut = true;
    return output.mid(from);
}

QString WorkspaceAgents::usageText(const acp::Usage &usage)
{
    QStringList parts;
    if (usage.size > 0 && usage.used > 0) {
        const double percent = 100.0 * double(usage.used) / double(usage.size);
        parts << tr("%1 % of the context").arg(QLocale().toString(percent, 'f', percent < 10 ? 1 : 0));
    }
    if (usage.cost >= 0)
        parts << QLocale().toString(usage.cost, 'f', 2) + (usage.currency.isEmpty() ? QString() : u' ' + usage.currency);
    return parts.join(u" · "_s);
}

// ---- the agents ---------------------------------------------------------------------------------------

void WorkspaceAgents::setAgents(const QList<AgentConfig> &agents)
{
    // An agent removed or changed stops (its turns fail: it is no longer what was asked); a new
    // name, or options remembered, change nothing that runs.
    for (const QString &id : m_processes.keys()) {
        const auto same = std::ranges::find_if(agents, [&id](const AgentConfig &a) { return a.id == id; });
        if (same == agents.end() || !same->sameProgram(m_processes.value(id)->config))
            stopProcess(id);
        else
            m_processes.value(id)->config = *same;
    }
    m_agents = agents;
    Q_EMIT sessionChanged(QString());
}

const AgentConfig *WorkspaceAgents::config(const QString &id) const
{
    const auto it = std::ranges::find_if(m_agents, [&id](const AgentConfig &a) { return a.id == id; });
    return it == m_agents.end() ? nullptr : &*it;
}

QVariantList WorkspaceAgents::partners() const
{
    QVariantList out;
    for (const AgentConfig &a : m_agents) {
        if (!runs(a))
            continue;
        out.append(QVariantMap{{u"id"_s, QString(Prefix + a.id)},
                               {u"label"_s, a.name},
                               {u"extension"_s, a.isRemote() ? tr("AI agent on %1").arg(a.host) : tr("AI agent")}});
    }
    return out;
}

QString WorkspaceAgents::label(const QString &partner) const
{
    const AgentConfig *a = config(partner.trimmed().mid(Prefix.size()));
    return a ? a->name : QString();
}

WorkspaceAgents::Process *WorkspaceAgents::processFor(const AgentConfig &config)
{
    if (Process *p = m_processes.value(config.id))
        return p;
    if (!runs(config))
        return nullptr;
    auto *p = new Process;
    p->config = config;
    if (config.isRemote()) {
        p->remote = new acp::RemoteStream(transport(), QUrl(config.url), config.token, config.remote, this);
        QPointer<acp::RemoteStream> stream = p->remote;
        p->client = new acp::Client(
                [stream](const QByteArray &line) {
                    if (stream)
                        stream->write(line);
                },
                QStringLiteral(BRULION_VERSION), this);
        // (The other computer serves the agent its files and terminals: none from here.)
        p->client->setCapabilities({false, false});
        p->client->setHandler([p](const QString &, const QJsonValue &id, const QString &method, const QJsonObject &) {
            p->client->respondError(id, acp::MethodNotFound, u"Method not found: %1"_s.arg(method));
        });
    } else {
#if QT_CONFIG(process)
        p->process = new QProcess(this);
        QPointer<QProcess> process = p->process;
        p->client = new acp::Client(
                [process](const QByteArray &line) {
                    if (process && process->state() != QProcess::NotRunning)
                        process->write(line);
                },
                QStringLiteral(BRULION_VERSION), this);
        p->client->setCapabilities({true, true});
        p->client->setHandler([this, p](const QString &key, const QJsonValue &id, const QString &method,
                                        const QJsonObject &params) { handle(p, key, id, method, params); });
#endif
    }
    m_processes.insert(config.id, p);
    connectClient(p);
    if (p->remote)
        startRemote(p);
    else
        startProgram(p);
    return p;
}

void WorkspaceAgents::connectClient(Process *p)
{
    const QString id = p->config.id;
    acp::Client *client = p->client;
    const auto name = [p] { return p->config.name; };
    connect(client, &acp::Client::progress, this,
            [this](const QString &path, const QString &text, const QString &thoughts) {
                if (m_turns.contains(path))
                    Q_EMIT partial(path, text, thoughts);
            });
    connect(client, &acp::Client::status, this, [this](const QString &path, const QString &text) {
        if (m_turns.contains(path))
            Q_EMIT status(path, text);
    });
    connect(client, &acp::Client::activityChanged, this, [this](const QString &path) {
        if (m_turns.contains(path))
            activityDue(path);
    });
    connect(client, &acp::Client::sessionChanged, this, [this, id](const QString &path) {
        learn(id, path);
        Q_EMIT sessionChanged(path);
    });
    connect(client, &acp::Client::sessionStarted, this, [this, id](const QString &path, const QString &session, bool resumed) {
        // A new session saw the conversation up to its newest message (its first prompt).
        if (!resumed && m_turns.value(path) == id)
            store(path, Stored{id, session, m_seen.value(path) - 2});
    });
    connect(client, &acp::Client::opened, this, [this, id, client, name](const QString &key) {
        if (key != CheckKey || !m_checks.contains(id))
            return;
        m_checks.remove(id);
        QStringList said;
        for (const acp::ConfigOption &o : client->session(key).options) {
            if (!o.boolean && !o.currentName().isEmpty())
                said << o.name + u": "_s + o.currentName();
        }
        const QString agent = QString(client->agentName() + u' ' + client->agentVersion()).trimmed();
        QString text = tr("%1 works (%2).").arg(name(), agent.isEmpty() ? name() : agent);
        if (!said.isEmpty())
            text += u' ' + said.join(u" · "_s);
        client->forget(key);
        Q_EMIT checked(id, true, text);
    });
    connect(client, &acp::Client::permissionAsked, this,
            [this, name](const QString &path, const acp::Permission &permission) {
                Question q;
                q.text = permission.title.isEmpty() ? tr("%1 asks for your permission.").arg(name())
                                                    : tr("%1 asks: %2").arg(name(), permission.title);
                if (!permission.details.isEmpty())
                    q.text += u'\n' + permission.details;
                for (const acp::PermissionOption &o : permission.options) {
                    q.options << o.name;
                    q.ids << o.id;
                }
                if (!permission.diffs.isEmpty())
                    q.diff = permission.diffs.first();
                setQuestion(path, q);
            });
    connect(client, &acp::Client::authRequired, this,
            [this, id, p, name](const QString &path, const QList<acp::AuthMethod> &methods, const QString &message) {
                Question q;
                q.signIn = true;
                QStringList details;
                if (!message.trimmed().isEmpty())
                    details << message.trimmed();
                QStringList terminal;
                const QStringList program = splitCommand(p->config.command);
                for (const acp::AuthMethod &m : methods) {
                    if (m.terminal) {
                        terminal << (program + m.args).join(u' ');
                        continue;
                    }
                    q.options << m.name;
                    q.ids << m.id;
                    if (!m.description.isEmpty())
                        details << m.name + u": "_s + m.description;
                }
                QString why = terminal.isEmpty()
                                      ? tr("%1 asks you to sign in: sign in with its own program, then try again.")
                                                .arg(name())
                                      : tr("%1 asks you to sign in: run %2 in a terminal, then try again.")
                                                .arg(name(), terminal.join(tr(" or ")));
                if (p->config.isRemote())
                    why = terminal.isEmpty() ? tr("%1 asks you to sign in on %2: sign in with its own program there, "
                                                  "then try again.")
                                                       .arg(name(), p->config.host)
                                             : tr("%1 asks you to sign in on %2: run %3 in a terminal there (on NixOS: "
                                                  "sudo brulion-acp login …), then try again.")
                                                       .arg(name(), p->config.host, terminal.join(tr(" or ")));
                if (path == CheckKey) {
                    p->client->cancel(path);
                    m_checks.remove(id);
                    Q_EMIT checked(id, false, why);
                    return;
                }
                if (q.options.isEmpty()) {
                    // Only a sign-in in a terminal: the user does it there and tries again.
                    p->client->cancel(path);
                    finish(path);
                    Q_EMIT failed(path, why);
                    return;
                }
                q.options << tr("Cancel");
                q.ids << QString();
                q.text = tr("%1 asks you to sign in.").arg(name());
                if (!details.isEmpty())
                    q.text += u'\n' + details.join(u'\n');
                setQuestion(path, q);
            });
    connect(client, &acp::Client::finished, this, [this, id, client](const QString &path, const QString &text, const QString &) {
        if (!m_turns.contains(path))
            return;
        QString answer = text;
        if (answer.isEmpty()) {
            // A turn of tool calls only: what it did.
            QStringList done;
            if (const acp::TurnState *turn = client->lastTurn(path)) {
                for (const acp::ToolCall &t : turn->tools)
                    done << u"- "_s + t.title;
            }
            answer = done.isEmpty() ? tr("Done.") : tr("Done:") + u"\n\n"_s + done.join(u'\n');
        }
        store(path, Stored{id, client->sessionId(path), m_seen.value(path)});
        finish(path);
        Q_EMIT answered(path, answer);
    });
    connect(client, &acp::Client::failed, this, [this, id, name](const QString &path, const acp::Failure &failure) {
        if (path == CheckKey) {
            if (m_checks.remove(id))
                Q_EMIT checked(id, false, failureText(name(), failure));
            return;
        }
        if (!m_turns.contains(path))
            return;
        finish(path);
        Q_EMIT failed(path, failureText(name(), failure));
    });
}

void WorkspaceAgents::startProgram(Process *p)
{
#if !QT_CONFIG(process)
    Q_UNUSED(p)
#else
    QProcess *proc = p->process;
    const QString id = p->config.id;
    acp::Client *client = p->client;
    const QStringList parts = commandLine(p->config.command, windows(), findProgram);
    proc->setProgram(parts.value(0));
    proc->setArguments(parts.mid(1));
    proc->setWorkingDirectory(m_workspace->rootPath());
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    connect(proc, &QProcess::readyReadStandardOutput, this, [proc, client] { client->received(proc->readAllStandardOutput()); });
    connect(proc, &QProcess::readyReadStandardError, this, [p] {
        const QString text = QString::fromUtf8(p->process->readAllStandardError());
        p->errors = QString(p->errors + text).right(MaxErrorChars);
        for (const QString &line : text.split(u'\n', Qt::SkipEmptyParts))
            qCInfo(logging::lcApp, "AI agent %s: %s", qUtf8Printable(p->config.name), qUtf8Printable(line.left(500)));
    });
    const auto gone = [this, id, p](const QString &why) { ended(id, p, why); };
    // (A program that cannot start may say so within start(): heard from the event loop, once this
    // function has returned the process and its turn is known.)
    connect(proc, &QProcess::errorOccurred, this, [this, proc, gone](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        const QString why = tr("it could not be started (%1)").arg(proc->errorString());
        QTimer::singleShot(0, this, [gone, why] { gone(why); });
    });
    connect(proc, &QProcess::finished, this, [p, gone](int code, QProcess::ExitStatus exit) {
        const QString line = lastLine(p->errors);
        const QString why = exit == QProcess::CrashExit ? tr("it crashed") : tr("it ended with code %1").arg(code);
        gone(line.isEmpty() ? why : why + u": "_s + line);
    });
    qCInfo(logging::lcApp, "AI agent %s: starting %s", qUtf8Printable(p->config.name),
           qUtf8Printable(p->config.command));
    proc->start();
#endif
}

void WorkspaceAgents::startRemote(Process *p)
{
    const QString id = p->config.id;
    acp::RemoteStream *stream = p->remote;
    connect(stream, &acp::RemoteStream::received, p->client, &acp::Client::received);
    connect(stream, &acp::RemoteStream::started, this, [p](const QString &root) {
        if (!root.isEmpty())
            p->config.root = root; // (the computer's notebook folder as it says now)
    });
    connect(stream, &acp::RemoteStream::ended, this,
            [this, id, p](acp::RemoteStream::End why, const QString &detail, const QString &) {
                ended(id, p,
                      remoteEndText(p->config.host.isEmpty() ? QUrl(p->config.url).host() : p->config.host, why,
                                    detail));
            });
    qCInfo(logging::lcApp, "AI agent %s: connecting to %s (%s there)", qUtf8Printable(p->config.name),
           qUtf8Printable(p->config.url), qUtf8Printable(p->config.remote));
    stream->start();
}

void WorkspaceAgents::ended(const QString &id, Process *p, const QString &why)
{
    // The program or the stream is gone: every turn of it fails, and the next turn starts it again.
    if (m_processes.value(id) != p)
        return; // (stopped meanwhile: stopProcess())
    m_processes.remove(id);
    qCInfo(logging::lcApp, "AI agent %s ended: %s", qUtf8Printable(p->config.name), qUtf8Printable(why));
    endTerminals(id);
    p->client->closed(why);
    p->client->deleteLater();
#if QT_CONFIG(process)
    if (p->process)
        p->process->deleteLater();
#endif
    if (p->remote)
        p->remote->deleteLater();
    delete p;
}

sync::HttpTransport *WorkspaceAgents::transport()
{
    if (!m_transport) {
        auto *network = new sync::NetworkHttpTransport;
        m_ownTransport.reset(network);
        m_transport = network;
    }
    return m_transport;
}

void WorkspaceAgents::activityDue(const QString &path)
{
    // The first change at once, those after it ten times a second at most.
    if (!m_activityTimer->isActive()) {
        m_activityTimer->start();
        Q_EMIT activityChanged(path);
        return;
    }
    m_activityPending.insert(path);
}

void WorkspaceAgents::stopProcess(const QString &id)
{
    Process *p = m_processes.take(id);
    if (!p)
        return;
    endTerminals(id);
    // Its turns fail: the agent was stopped (a notebook closed says nothing: reset()).
    if (p->remote) {
        p->remote->disconnect(this);
        p->client->closed(tr("it was stopped"));
        p->client->deleteLater();
        p->remote->stop(); // (its process ends on that computer)
        p->remote->deleteLater();
        delete p;
        return;
    }
#if QT_CONFIG(process)
    QProcess *proc = p->process;
    proc->disconnect(this);
    p->client->closed(tr("it was stopped"));
    p->client->deleteLater();
    // Asked to end (its input closed, then a signal), made to after a while.
    connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
    proc->closeWriteChannel();
    proc->terminate();
    QTimer::singleShot(3000, proc, [proc] { proc->kill(); });
#endif
    delete p;
}

acp::TurnInput WorkspaceAgents::baseInput(const AgentConfig &config) const
{
    acp::TurnInput input;
    if (config.isRemote()) {
        // That computer's notebook folder; its server gives the session Brulion's tools there.
        input.cwd = config.root;
        for (auto it = config.options.constBegin(); it != config.options.constEnd(); ++it)
            input.config.insert(it.key(), it.value());
        return input;
    }
    input.cwd = QDir(m_workspace->rootPath()).absolutePath();
    const QString program = config.tools ? toolsProgram() : QString();
    if (!program.isEmpty())
        input.mcpServers << acp::McpServer{u"brulion"_s, program, {u"mcp"_s, u"--vault"_s, input.cwd}};
    for (auto it = config.options.constBegin(); it != config.options.constEnd(); ++it)
        input.config.insert(it.key(), it.value());
    return input;
}

void WorkspaceAgents::run(const QString &path, const QString &partner, const QVariantList &history, const QString &page)
{
    const QString id = partner.trimmed().mid(Prefix.size());
    const AgentConfig *agent = config(id);
    if (!agent && !available() && !remoteAvailable()) {
        Q_EMIT failed(path, tr("AI agents run only in the desktop app: choose another partner for this conversation."));
        return;
    }
    if (!agent) {
        Q_EMIT failed(path, tr("The AI agent “%1” is not set up on this device: add it in Settings → Extensions → AI "
                               "agents, or choose another partner.")
                                    .arg(id));
        return;
    }
    if (!runs(*agent)) {
        Q_EMIT failed(path,
                      agent->isRemote()
                              ? tr("An AI agent on another computer cannot be reached from here: choose another "
                                   "partner for this conversation.")
                              : tr("AI agents run only in the desktop app: on this device an agent on another computer "
                                   "can answer (Settings → Extensions → AI agents), or choose another partner."));
        return;
    }
    if (!agent->isRemote() && splitCommand(agent->command).isEmpty()) {
        Q_EMIT failed(path, tr("The AI agent “%1” has no command: set it in Settings → Extensions → AI agents.").arg(agent->name));
        return;
    }
    if (history.isEmpty() || m_turns.contains(path)) {
        Q_EMIT failed(path, tr("There is no question to answer: write one."));
        return;
    }
    // The notebook's folder as the agent sees it: this device's, or that computer's copy of it.
    const QString root = agent->isRemote() ? agent->root : m_workspace->rootPath();
    const QString conversation = path.endsWith(u".md"_s, Qt::CaseInsensitive) ? path.chopped(3) : path;
    acp::TurnInput input = baseInput(*agent);

    // The user's newest message, its pictures as pictures (a link to the file where the agent
    // takes none — or where a picture cannot be read).
    const QVariantMap newest = history.last().toMap();
    input.prompt << acp::Block::textBlock(newest.value(u"content"_s).toString());
    for (const QString &name : newest.value(u"images"_s).toStringList()) {
        const QString picture = m_workspace->resolveLink(name, path);
        if (picture.isEmpty())
            continue;
        const QString url = fileUrl(root, picture);
        if (const std::optional<ai::Image> image = m_workspace->aiPicture(name))
            input.prompt << acp::Block::image(image->data, image->mediaType, url, QFileInfo(picture).fileName());
        else
            input.prompt << acp::Block::link(url, QFileInfo(picture).fileName());
    }
    // A new session: what the agent needs to know first, the page (its text where the agent takes
    // it), the conversation so far.
    const QString pagePath = page.isEmpty() ? QString() : m_workspace->resolveLink(page, path);
    // (That computer's server gives each session Brulion's tools there, unless it was started
    // without them: its info said so at pairing.)
    const bool tools = agent->isRemote() ? agent->tools : !input.mcpServers.isEmpty();
    input.firstPrompt << acp::Block::textBlock(
            context(root, conversation, pagePath.isEmpty() ? QString() : page, tools));
    if (!pagePath.isEmpty()) {
        bool ok = false;
        const QString text = markdown::isEncryptedNotePath(pagePath) ? QString() : m_workspace->read(pagePath, &ok);
        const QString url = fileUrl(root, pagePath);
        if (ok)
            input.firstPrompt << acp::Block::resource(url, QFileInfo(pagePath).fileName(), text, u"text/markdown"_s);
        else
            input.firstPrompt << acp::Block::link(url, QFileInfo(pagePath).fileName(), u"text/markdown"_s);
    }
    if (const QString before = transcript(history); !before.isEmpty())
        input.firstPrompt << acp::Block::textBlock(before);
    input.firstPrompt << input.prompt;

    Process *p = processFor(*agent);
    if (!p) {
        Q_EMIT failed(path, tr("AI agents run only in the desktop app: choose another partner for this conversation."));
        return;
    }
    // The session goes on only if it has seen the conversation up to this message — else (another
    // partner answered meanwhile, a message written on another device) a new one gets it all.
    const int seenNow = int(history.size()) - 1;
    const std::optional<Stored> record = stored(path);
    const bool current = record && record->agent == id && record->seen == seenNow;
    if (!current)
        p->client->forget(path);
    else if (!p->client->hasSession(path))
        input.resume = record->session;
    m_turns.insert(path, id);
    m_seen.insert(path, int(history.size()) + 1);
    noteWorking();
    Q_EMIT status(path, tr("%1 is working…").arg(agent->name));
    if (!p->client->prompt(path, input)) {
        m_turns.remove(path);
        noteWorking();
        Q_EMIT failed(path, tr("There is no question to answer: write one."));
    }
}

void WorkspaceAgents::noteWorking()
{
    bool remote = false;
    for (auto it = m_turns.cbegin(); it != m_turns.cend() && !remote; ++it) {
        const AgentConfig *a = config(it.value());
        remote = a && a->isRemote();
    }
    if (remote == m_remoteWorking)
        return;
    m_remoteWorking = remote;
    Q_EMIT remoteWorkingChanged();
}

void WorkspaceAgents::stop(const QString &path)
{
    m_released.removeIf([&path](const auto &entry) { return entry.value().first == path; });
    const QString id = m_turns.take(path);
    noteWorking();
    m_activityPending.remove(path);
    clearQuestion(path);
    if (Process *p = m_processes.value(id)) {
        p->client->cancel(path);
        endTerminals(id, path); // (its commands end with it, whatever the agent does)
    }
}

bool WorkspaceAgents::running(const QString &path) const
{
    return m_turns.contains(path);
}

QString WorkspaceAgents::questionText(const QString &path) const
{
    return m_questions.value(path).text;
}

QStringList WorkspaceAgents::questionOptions(const QString &path) const
{
    return m_questions.value(path).options;
}

QVariantMap WorkspaceAgents::questionDiff(const QString &path) const
{
    const acp::Diff diff = m_questions.value(path).diff;
    if (diff.path.isEmpty())
        return {};
    return {{u"path"_s, diff.path}, {u"oldText"_s, diff.oldText}, {u"newText"_s, diff.newText}};
}

void WorkspaceAgents::answer(const QString &path, const QString &option)
{
    const auto it = m_questions.constFind(path);
    if (it == m_questions.cend())
        return;
    const Question q = *it;
    const qsizetype index = q.options.indexOf(option);
    if (index < 0)
        return;
    clearQuestion(path);
    Process *p = m_processes.value(m_turns.value(path));
    if (!p)
        return;
    if (!q.signIn) {
        p->client->answer(path, q.ids.at(index));
        return;
    }
    if (q.ids.at(index).isEmpty()) { // Cancel
        const QString name = p->config.name;
        p->client->cancel(path);
        finish(path);
        Q_EMIT failed(path, tr("Not signed in to %1.").arg(name));
        return;
    }
    Q_EMIT status(path, tr("Signing in to %1…").arg(p->config.name));
    p->client->authenticate(path, q.ids.at(index));
}

void WorkspaceAgents::setQuestion(const QString &path, const Question &question)
{
    if (!m_turns.contains(path))
        return;
    m_questions.insert(path, question);
    Q_EMIT questionChanged(path);
}

void WorkspaceAgents::clearQuestion(const QString &path)
{
    if (m_questions.remove(path))
        Q_EMIT questionChanged(path);
}

void WorkspaceAgents::finish(const QString &path)
{
    m_released.removeIf([&path](const auto &entry) { return entry.value().first == path; });
    m_turns.remove(path);
    m_seen.remove(path);
    noteWorking();
    m_activityPending.remove(path);
    clearQuestion(path);
}

QString WorkspaceAgents::failureText(const QString &name, const acp::Failure &failure) const
{
    switch (failure.kind) {
    case acp::Failure::Kind::Closed:
        return failure.detail.isEmpty() ? tr("The AI agent “%1” stopped.").arg(name)
                                        : tr("The AI agent “%1” stopped: %2.").arg(name, failure.detail);
    case acp::Failure::Kind::Version:
        return tr("The AI agent “%1” speaks version %2 of the Agent Client Protocol, Brulion version %3.")
                .arg(name, failure.detail)
                .arg(acp::ProtocolVersion);
    case acp::Failure::Kind::Error:
        return failure.detail.isEmpty() ? tr("The AI agent “%1” failed.").arg(name)
                                        : tr("The AI agent “%1” failed: %2").arg(name, failure.detail);
    case acp::Failure::Kind::Refusal:
        return tr("The AI agent “%1” refused to answer.").arg(name);
    case acp::Failure::Kind::Empty:
        return tr("The AI agent “%1” gave no answer (%2).").arg(name, failure.detail);
    case acp::Failure::Kind::Malformed:
        return tr("The AI agent “%1” answered in a way Brulion does not understand: %2").arg(name, failure.detail);
    }
    return {};
}

// ---- what a turn and a session show -------------------------------------------------------------------

QVariantList WorkspaceAgents::tools(const QString &path) const
{
    QVariantList out;
    const Process *p = m_processes.value(m_turns.value(path));
    const acp::TurnState *turn = p ? p->client->turn(path) : nullptr;
    if (!turn)
        return out;
    for (const acp::ToolCall &t : turn->tools) {
        QString output = t.output;
        const Terminal *term = m_terminals.value(t.terminal);
        if (term || m_released.contains(t.terminal)) {
            const QByteArray bytes = term ? term->output : m_released.value(t.terminal).second;
            output = QString::fromUtf8(keepLast(bytes, acp::MaxToolOutputChars)).trimmed();
            if (bytes.size() > acp::MaxToolOutputChars)
                output.prepend(u'…');
        }
        out.append(QVariantMap{{u"title"_s, t.title.isEmpty() ? t.kind : t.title},
                               {u"status"_s, t.status},
                               {u"kind"_s, t.kind},
                               {u"output"_s, output},
                               {u"diffPath"_s, t.diff ? t.diff->path : QString()},
                               {u"diffOld"_s, t.diff ? t.diff->oldText : QString()},
                               {u"diffNew"_s, t.diff ? t.diff->newText : QString()}});
    }
    return out;
}

QVariantList WorkspaceAgents::plan(const QString &path) const
{
    QVariantList out;
    const Process *p = m_processes.value(m_turns.value(path));
    const acp::TurnState *turn = p ? p->client->turn(path) : nullptr;
    if (!turn)
        return out;
    for (const acp::PlanEntry &e : turn->plan)
        out.append(QVariantMap{{u"content"_s, e.content}, {u"status"_s, e.status}, {u"priority"_s, e.priority}});
    return out;
}

QVariantList WorkspaceAgents::options(const QString &path, const QString &partner) const
{
    QVariantList out;
    const QString id = partner.trimmed().mid(Prefix.size());
    const AgentConfig *agent = config(id);
    if (!agent)
        return out;
    QList<acp::ConfigOption> options;
    const Process *p = m_processes.value(id);
    if (p && !path.isEmpty() && p->client->hasSession(path)) {
        options = p->client->session(path).options;
    } else {
        // Before a session: the options the agent's last one offered, as they will be set.
        options = optionsOf(agent->known);
        for (acp::ConfigOption &o : options) {
            if (agent->options.contains(o.id))
                o.current = agent->options.value(o.id);
        }
    }
    for (const acp::ConfigOption &o : std::as_const(options))
        out.append(optionRow(o));
    return out;
}

void WorkspaceAgents::setOption(const QString &path, const QString &partner, const QString &option, const QString &value)
{
    const QString id = partner.trimmed().mid(Prefix.size());
    const auto it = std::ranges::find_if(m_agents, [&id](const AgentConfig &a) { return a.id == id; });
    if (it == m_agents.end())
        return;
    // A bool or a choice's value, as the option is.
    bool boolean = false;
    for (const QVariant &row : options(path, partner)) {
        const QVariantMap o = row.toMap();
        if (o.value(u"id"_s).toString() == option)
            boolean = o.value(u"boolean"_s).toBool();
    }
    const QJsonValue v = boolean ? QJsonValue(value == u"true") : QJsonValue(value);
    it->options.insert(option, v);
    if (Process *p = m_processes.value(id); p && !path.isEmpty())
        p->client->setOption(path, option, v);
    Q_EMIT remembered(id, it->options, it->known);
    Q_EMIT sessionChanged(path);
}

QVariantList WorkspaceAgents::commands(const QString &path, const QString &partner) const
{
    QVariantList out;
    const QString id = partner.trimmed().mid(Prefix.size());
    const Process *p = m_processes.value(id);
    if (p && !path.isEmpty() && p->client->hasSession(path)) {
        for (const acp::Command &c : p->client->session(path).commands)
            out.append(QVariantMap{{u"name"_s, c.name}, {u"description"_s, c.description}, {u"hint"_s, c.hint}});
        return out;
    }
    for (const QJsonValue &v : m_commands.value(id))
        out.append(v.toObject().toVariantMap());
    return out;
}

QString WorkspaceAgents::usage(const QString &path) const
{
    for (const Process *p : m_processes) {
        if (!path.isEmpty() && p->client->hasSession(path))
            return usageText(p->client->session(path).usage);
    }
    return {};
}

void WorkspaceAgents::learn(const QString &agent, const QString &key)
{
    const Process *p = m_processes.value(agent);
    const auto it = std::ranges::find_if(m_agents, [&agent](const AgentConfig &a) { return a.id == agent; });
    if (!p || it == m_agents.end())
        return;
    const acp::SessionInfo info = p->client->session(key);
    QJsonArray commands;
    for (const acp::Command &c : info.commands)
        commands.append(QJsonObject{{u"name"_s, c.name}, {u"description"_s, c.description}, {u"hint"_s, c.hint}});
    if (!commands.isEmpty())
        m_commands.insert(agent, commands);
    QJsonArray known;
    for (const acp::ConfigOption &o : info.options)
        known.append(o.toJson());
    if (known.isEmpty() || known == it->known)
        return;
    it->known = known;
    Q_EMIT remembered(agent, it->options, it->known);
}

void WorkspaceAgents::check(const QString &id)
{
    const AgentConfig *agent = config(id);
    if (!agent || !runs(*agent) || (!agent->isRemote() && splitCommand(agent->command).isEmpty())) {
        Q_EMIT checked(id, false, tr("The AI agent “%1” has no command: set it in Settings → Extensions → AI agents.")
                                          .arg(agent ? agent->name : id));
        return;
    }
    if (!agent->isRemote() && m_workspace->rootPath().isEmpty()) {
        Q_EMIT checked(id, false, tr("Open a notebook first: the agent works in its folder."));
        return;
    }
    Process *p = processFor(*agent);
    if (!p) {
        Q_EMIT checked(id, false, tr("The AI agent “%1” stopped.").arg(agent->name));
        return;
    }
    m_checks.insert(id, CheckKey);
    p->client->forget(CheckKey);
    if (!p->client->open(CheckKey, baseInput(*agent))) {
        m_checks.remove(id);
        Q_EMIT checked(id, false, tr("The AI agent “%1” stopped.").arg(agent->name));
    }
}

// ---- an agent on another computer: pairing ---------------------------------------------------------------

void WorkspaceAgents::pair(const QString &address, const QString &code)
{
    const QUrl base = acp::normalizeRemoteBase(address);
    if (!base.isValid() || base.isEmpty()) {
        Q_EMIT paired(
                false,
                tr("“%1” is no address of a computer: type it as the computer shows it (https://agent.example.com).")
                        .arg(address.trimmed()),
                {});
        return;
    }
    if (acp::normalizePairingCode(code).isEmpty()) {
        Q_EMIT paired(false, tr("Type the code that acp pair shows on the computer."), {});
        return;
    }
    if (!remoteAvailable()) {
        Q_EMIT paired(false, tr("An AI agent on another computer cannot be reached from here."), {});
        return;
    }
    const QString shown = base.toString();
    QPointer<WorkspaceAgents> self(this);
    acp::pairRemote(
            transport(), base, code, deviceName(),
            [self, base, shown](const acp::RemotePairing &pairing, const acp::RemoteError &error) {
                if (!self)
                    return;
                if (error.failed()) {
                    Q_EMIT self->paired(false, remoteErrorText(shown, error), {});
                    return;
                }
                const QString token = pairing.token;
                acp::fetchRemoteInfo(
                        self->transport(), base, token,
                        [self, base, shown, token](const acp::RemoteInfo &info, const acp::RemoteError &e) {
                            if (!self)
                                return;
                            if (e.failed()) {
                                Q_EMIT self->paired(false, remoteErrorText(shown, e), {});
                                return;
                            }
                            if (info.agents.isEmpty()) {
                                Q_EMIT self->paired(
                                        false, tr("%1 offers no agent: start acp serve there with --agent.").arg(shown),
                                        {});
                                return;
                            }
                            const QString host = info.host.isEmpty() ? base.host() : info.host;
                            QList<AgentConfig> agents;
                            QStringList names;
                            for (const acp::RemoteAgent &a : info.agents) {
                                AgentConfig c;
                                c.name = a.name;
                                c.command = a.command;
                                c.tools = info.tools;
                                c.url = base.toString();
                                c.remote = a.id;
                                c.host = host;
                                c.root = info.root;
                                c.token = token;
                                agents << c;
                                names << a.name;
                            }
                            qCInfo(logging::lcApp, "AI agents: paired with %s (%s): %s", qUtf8Printable(host),
                                   qUtf8Printable(shown), qUtf8Printable(names.join(u", "_s)));
                            Q_EMIT self->paired(true, tr("Paired with %1: %2.").arg(host, names.join(u", "_s)), agents);
                        });
            });
}

// ---- what the agent asks of the app: files, terminals ----------------------------------------------------

void WorkspaceAgents::handle(Process *p, const QString &key, const QJsonValue &id, const QString &method,
                             const QJsonObject &params)
{
    if (method == u"fs/read_text_file")
        readFile(p->client, id, params);
    else if (method == u"fs/write_text_file")
        writeFile(p->client, id, params);
    else if (method.startsWith(u"terminal/"_s))
        terminalRequest(p, key, id, method, params);
    else
        p->client->respondError(id, acp::MethodNotFound, u"Method not found: %1"_s.arg(method));
}

QString WorkspaceAgents::notebookPath(const QString &file) const
{
    const QString root = QDir::cleanPath(QDir(m_workspace->rootPath()).absolutePath());
    if (root.isEmpty() || file.isEmpty())
        return {};
    const QString full = QDir::cleanPath(QDir(root).absoluteFilePath(file));
    if (!full.startsWith(root + u'/'))
        return {};
    // (A link out of the notebook is outside it too.)
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    QFileInfo info(full);
    while (!info.exists() && info.absolutePath() != info.absoluteFilePath())
        info = QFileInfo(info.absolutePath());
    const QString real = info.canonicalFilePath();
    if (!canonicalRoot.isEmpty() && !real.isEmpty() && real != canonicalRoot && !real.startsWith(canonicalRoot + u'/'))
        return {};
    return full.mid(root.size() + 1);
}

void WorkspaceAgents::readFile(acp::Client *client, const QJsonValue &id, const QJsonObject &params)
{
    const QString path = notebookPath(params.value(u"path"_s).toString());
    if (path.isEmpty()) {
        client->respondError(id, acp::InvalidParams, u"Only files of the notebook can be read: %1"_s.arg(m_workspace->rootPath()));
        return;
    }
    bool ok = false;
    const QString text = m_workspace->exists(path) || QFileInfo::exists(m_workspace->absoluteFilePath(path))
                                 ? m_workspace->read(path, &ok)
                                 : QString();
    if (!ok) {
        client->respondError(id, acp::ResourceNotFound, u"No such file: %1"_s.arg(path));
        return;
    }
    client->respond(id, QJsonObject{{u"content"_s, lines(text, params.value(u"line"_s).toInt(), params.value(u"limit"_s).toInt())}});
}

void WorkspaceAgents::writeFile(acp::Client *client, const QJsonValue &id, const QJsonObject &params)
{
    const QString path = notebookPath(params.value(u"path"_s).toString());
    if (path.isEmpty()) {
        client->respondError(id, acp::InvalidParams, u"Only files of the notebook can be written: %1"_s.arg(m_workspace->rootPath()));
        return;
    }
    if (markdown::isEncryptedNotePath(path)) {
        client->respondError(id, acp::InvalidParams, u"%1 is an encrypted page: it is changed only in Brulion"_s.arg(path));
        return;
    }
    if (!m_workspace->write(path, params.value(u"content"_s).toString())) {
        client->respondError(id, -32603, u"%1 could not be written"_s.arg(path));
        return;
    }
    client->respond(id, QJsonValue());
}

void WorkspaceAgents::terminalRequest(Process *p, const QString &key, const QJsonValue &id, const QString &method,
                                      const QJsonObject &params)
{
    acp::Client *client = p->client;
#if !QT_CONFIG(process)
    Q_UNUSED(key)
    Q_UNUSED(params)
    client->respondError(id, acp::MethodNotFound, u"Method not found: %1"_s.arg(method));
#else
    if (method == u"terminal/create") {
        const QString agent = p->config.id;
        const auto count = std::ranges::count_if(m_terminals, [&agent](const Terminal *t) { return t->agent == agent; });
        const QString command = params.value(u"command"_s).toString();
        if (command.isEmpty() || count >= MaxTerminals) {
            client->respondError(id, acp::InvalidParams, command.isEmpty() ? u"No command"_s : u"Too many terminals"_s);
            return;
        }
        QStringList args;
        for (const QJsonValue &a : params.value(u"args"_s).toArray())
            args << a.toString();
        const QStringList parts = resolved(QStringList{command} + args, windows(), findProgram);
        auto *t = new Terminal;
        t->agent = agent;
        t->key = key;
        t->limit = std::max<qint64>(0, static_cast<qint64>(params.value(u"outputByteLimit"_s).toDouble()));
        if (t->limit == 0)
            t->limit = DefaultOutputLimit;
        t->process = new QProcess(this);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const QJsonValue &v : params.value(u"env"_s).toArray())
            env.insert(v.toObject().value(u"name"_s).toString(), v.toObject().value(u"value"_s).toString());
        t->process->setProcessEnvironment(env);
        const QString cwd = params.value(u"cwd"_s).toString();
        t->process->setWorkingDirectory(cwd.isEmpty() || !QDir(cwd).exists() ? m_workspace->rootPath() : cwd);
        t->process->setProcessChannelMode(QProcess::MergedChannels);
        t->process->setProgram(parts.value(0));
        t->process->setArguments(parts.mid(1));
        const QString terminalId = u"term-%1"_s.arg(m_nextTerminal++);
        m_terminals.insert(terminalId, t);
        const auto changed = [this, t] { activityDue(t->key); };
        connect(t->process, &QProcess::readyReadStandardOutput, this, [t, changed] {
            bool cut = false;
            t->output = keepLast(t->output + t->process->readAllStandardOutput(), t->limit, &cut);
            t->truncated = t->truncated || cut;
            changed();
        });
        const auto ended = [this, t, changed](const QJsonValue &code, const QString &signal) {
            if (t->ended)
                return;
            t->ended = true;
            t->exitCode = code;
            t->signal = signal;
            Process *owner = m_processes.value(t->agent);
            for (const QJsonValue &waiter : std::exchange(t->waiting, {})) {
                if (owner)
                    owner->client->respond(waiter, QJsonObject{{u"exitCode"_s, code},
                                                               {u"signal"_s, signal.isEmpty() ? QJsonValue() : QJsonValue(signal)}});
            }
            changed();
        };
        connect(t->process, &QProcess::finished, this, [ended](int code, QProcess::ExitStatus exit) {
            if (exit == QProcess::CrashExit)
                ended(QJsonValue(), u"SIGKILL"_s);
            else
                ended(code, QString());
        });
        connect(t->process, &QProcess::errorOccurred, this, [t, ended](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
                return;
            t->output += t->process->errorString().toUtf8() + '\n';
            ended(127, QString());
        });
        qCInfo(logging::lcApp, "AI agent %s runs %s", qUtf8Printable(p->config.name), qUtf8Printable(parts.join(u' ')));
        t->process->start();
        client->respond(id, QJsonObject{{u"terminalId"_s, terminalId}});
        return;
    }
    const QString terminalId = params.value(u"terminalId"_s).toString();
    Terminal *t = m_terminals.value(terminalId);
    if (!t || t->agent != p->config.id) {
        client->respondError(id, acp::InvalidParams, u"No such terminal: %1"_s.arg(terminalId));
        return;
    }
    const QJsonValue exit = t->ended ? QJsonValue(QJsonObject{{u"exitCode"_s, t->exitCode},
                                                              {u"signal"_s, t->signal.isEmpty() ? QJsonValue() : QJsonValue(t->signal)}})
                                     : QJsonValue();
    if (method == u"terminal/output") {
        client->respond(id, QJsonObject{{u"output"_s, QString::fromUtf8(t->output)},
                                        {u"truncated"_s, t->truncated},
                                        {u"exitStatus"_s, exit}});
    } else if (method == u"terminal/wait_for_exit") {
        if (t->ended)
            client->respond(id, exit);
        else
            t->waiting << id;
    } else if (method == u"terminal/kill") {
        if (t->process->state() != QProcess::NotRunning)
            t->process->kill();
        client->respond(id, QJsonValue());
    } else if (method == u"terminal/release") {
        m_terminals.remove(terminalId);
        if (m_turns.contains(t->key))
            m_released.insert(terminalId, {t->key, t->output});
        t->process->disconnect(this);
        if (t->process->state() != QProcess::NotRunning) {
            connect(t->process, &QProcess::finished, t->process, &QObject::deleteLater);
            t->process->kill();
        } else {
            t->process->deleteLater();
        }
        for (const QJsonValue &waiter : std::as_const(t->waiting))
            client->respond(waiter, QJsonObject{{u"exitCode"_s, QJsonValue()}, {u"signal"_s, u"SIGKILL"_s}});
        delete t;
        client->respond(id, QJsonValue());
    } else {
        client->respondError(id, acp::MethodNotFound, u"Method not found: %1"_s.arg(method));
    }
#endif
}

void WorkspaceAgents::endTerminals(const QString &agent, const QString &key)
{
#if !QT_CONFIG(process)
    Q_UNUSED(agent)
    Q_UNUSED(key)
#else
    for (auto it = m_terminals.begin(); it != m_terminals.end();) {
        Terminal *t = it.value();
        if (t->agent != agent || (!key.isEmpty() && t->key != key)) {
            ++it;
            continue;
        }
        it = m_terminals.erase(it);
        t->process->disconnect(this);
        if (t->process->state() != QProcess::NotRunning) {
            connect(t->process, &QProcess::finished, t->process, &QObject::deleteLater);
            t->process->kill();
        } else {
            t->process->deleteLater();
        }
        delete t;
    }
#endif
}

// ---- the record of sessions ---------------------------------------------------------------------------

std::optional<WorkspaceAgents::Stored> WorkspaceAgents::stored(const QString &path) const
{
    LocalStore *local = m_workspace->vault() ? m_workspace->vault()->store() : nullptr;
    if (!local)
        return std::nullopt;
    const QJsonObject entry = local->get(StoreKey).toObject().value(path).toObject();
    if (entry.value(u"session"_s).toString().isEmpty())
        return std::nullopt;
    return Stored{entry.value(u"agent"_s).toString(), entry.value(u"session"_s).toString(), entry.value(u"seen"_s).toInt(-1)};
}

void WorkspaceAgents::store(const QString &path, const std::optional<Stored> &entry)
{
    LocalStore *local = m_workspace->vault() ? m_workspace->vault()->store() : nullptr;
    if (!local || path.isEmpty() || path == CheckKey)
        return;
    QJsonObject all = local->get(StoreKey).toObject();
    if (entry && !entry->session.isEmpty())
        all.insert(path, QJsonObject{{u"agent"_s, entry->agent}, {u"session"_s, entry->session}, {u"seen"_s, entry->seen}});
    else
        all.remove(path);
    // (At most the newest 500 conversations are kept: the oldest go first, by their names' dates.)
    while (all.size() > 500)
        all.remove(all.keys().first());
    local->set(StoreKey, all);
}

void WorkspaceAgents::reset()
{
    m_released.clear();
    m_turns.clear();
    noteWorking();
    m_seen.clear();
    m_activityPending.clear();
    m_checks.clear();
    const QStringList asked = m_questions.keys();
    m_questions.clear();
    for (const QString &path : asked)
        Q_EMIT questionChanged(path);
    for (const QString &id : m_processes.keys()) {
        if (Process *p = m_processes.value(id))
            p->client->blockSignals(true); // (nothing is said of its turns)
        stopProcess(id);
    }
}

} // namespace brulion::shell
