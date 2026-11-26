// `brulion-cli acp serve` and its companions `acp pair`, `acp clients`, `acp revoke` (acphost.h,
// docs/CLI.md "AI agents for other devices").
#include "acphost.h"

#include "acptrust.h"
#include "asynccontrolclient.h"
#include "controlclient.h"
#include "controlprotocol.h"
#include "controlserver.h"

#include "acp/acp.h"
#include "acp/appchats.h"
#include "acp/apptunnel.h"
#include "acp/remote.h"
#include "automation/uri.h"
#include "commands_p.h"
#include "gitcli/client.h"
#include "gitcli/gitcli.h"
#include "hostsync.h"
#include "markdown/encrypted.h"
#include "notebook/session.h"
#include "qr/qrcode.h"
#include "tailscale/localapi.h"
#include "tailscale/tailscale.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QLockFile>
#include <QProcess>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#if QT_CONFIG(ssl)
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#endif

#include <algorithm>
#include <cstdio>
#include <thread>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

constexpr auto kStateFileName = "acp-remote.json";
constexpr int kLockTimeoutMs = 5000;
constexpr int kMaxDevices = 50;
/// Pairing requests in a minute, from anyone (a limit on guessing codes).
constexpr int kMaxPairsPerMinute = 20;
/// The terminals one stream may have at once, and the output one keeps when the agent names no limit.
constexpr int kMaxTerminals = 32;
constexpr qint64 kDefaultOutputLimit = 1024 * 1024;
/// The most of the agent's messages one receive answers with (the rest: the next one).
constexpr qint64 kMaxAnswerBytes = 4 * 1024 * 1024;
/// The end of an agent's standard error kept (its last line says why it stopped).
constexpr qsizetype kMaxErrorChars = 4000;
/// Letters and digits of pairing codes and ids: none to mistake for another (no 0/O, 1/I/L).
constexpr QLatin1StringView kCodeAlphabet{"ABCDEFGHJKMNPQRSTUVWXYZ23456789"};
/// The tunnel's grants one stream keeps (a session's each; the oldest end first).
constexpr int kMaxAppGrants = 64;
/// The biggest file of a project a thread's agent reads through this server.
constexpr qint64 kMaxCodeFileBytes = 16LL * 1024 * 1024;
/// How long a request of the app's conversations waits for the app beyond its own wait.
constexpr int kAppCallMarginMs = 15000;
/// The bodies of chats/send and chats/start (a message), and of chats/stop and chats/answer.
constexpr qint64 kMaxChatMessageBody = 256 * 1024;
constexpr qint64 kMaxChatSmallBody = 16 * 1024;

void logLine(const QString &line)
{
    const QByteArray bytes =
            (QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH:mm:ss"_s) + u' ' + line + u'\n').toUtf8();
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stderr);
    std::fflush(stderr);
}

QString randomString(int length)
{
    QString out;
    out.reserve(length);
    for (int i = 0; i < length; ++i)
        out += kCodeAlphabet.at(qsizetype(QRandomGenerator::system()->bounded(int(kCodeAlphabet.size()))));
    return out;
}

QString timeText(const QDateTime &t)
{
    return t.isValid() ? t.toUTC().toString(Qt::ISODate) : QString();
}

QDateTime timeOf(const QJsonValue &v)
{
    return QDateTime::fromString(v.toString(), Qt::ISODate);
}

/// Equal without telling how much of them is (a token's hash against the ones kept).
} // namespace

QJsonObject HostTurn::toJson() const
{
    const QString name = state == State::Running ? u"running"_s : state == State::Done ? u"done"_s : u"failed"_s;
    QJsonObject o{{u"session"_s, session},
                  {u"turn"_s, id},
                  {u"state"_s, name},
                  {u"text"_s, text},
                  {u"tools"_s, QJsonArray::fromStringList(tools)},
                  {u"asking"_s, asking},
                  {u"started"_s, timeText(started)}};
    if (!question.isEmpty())
        o.insert(u"question"_s, question);
    if (!checkpoint.isEmpty())
        o.insert(u"checkpoint"_s, QJsonObject{{u"id"_s, checkpoint}, {u"at"_s, checkpointAt}});
    if (state == State::Done)
        o.insert(u"stopReason"_s, stopReason);
    if (state == State::Failed)
        o.insert(u"error"_s, error);
    if (ended.isValid())
        o.insert(u"ended"_s, timeText(ended));
    return o;
}

namespace {

bool sameSecret(const QString &a, const QString &b)
{
    if (a.size() != b.size())
        return false;
    ushort diff = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        diff |= a.at(i).unicode() ^ b.at(i).unicode();
    return diff == 0;
}

/// A name a device gave itself, as it may be shown: one line, no control characters, 80 at most.
QString deviceName(const QString &given)
{
    QString out;
    for (const QChar c : given) {
        if (c.isPrint() || c.isSpace())
            out += c;
    }
    return out.simplified().left(80);
}

http::Response apiError(int status, const QString &code, const QString &message)
{
    return http::Response::json(status,
                                QJsonObject{{u"error"_s, QJsonObject{{u"code"_s, code}, {u"message"_s, message}}}});
}

QString normalizedHost(const QString &host, bool https)
{
    QString h = host.trimmed().toLower();
    const QString defaultPort = https ? u":443"_s : u":80"_s;
    if (h.endsWith(defaultPort))
        h.chop(defaultPort.size());
    return h;
}

/// The key of a JSON-RPC id (a number or a string).
QString idKey(const QJsonValue &id)
{
    return id.isString() ? u"s:"_s + id.toString() : u"n:"_s + QString::number(id.toDouble(), 'g', 17);
}

QByteArray compact(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString turnKey(const QString &device, const QString &session)
{
    return device + u'\n' + session;
}

/// The last line of `errors` that says something ("" none).
QString lastLine(const QString &errors)
{
    const QStringList lines = errors.split(u'\n', Qt::SkipEmptyParts);
    for (qsizetype i = lines.size() - 1; i >= 0; --i) {
        if (const QString line = lines.at(i).simplified(); !line.isEmpty())
            return line.left(300);
    }
    return {};
}

/// The agents the app offers too (WorkspaceAgents::presets()), for `--agent claude`.
std::optional<HostAgent> preset(const QString &id)
{
    static const QList<HostAgent> presets{
            {u"claude"_s, u"Claude Code"_s, u"npx -y @agentclientprotocol/claude-agent-acp"_s},
            {u"codex"_s, u"Codex"_s, u"npx -y @agentclientprotocol/codex-acp"_s},
            {u"gemini"_s, u"Gemini CLI"_s, u"npx -y @google/gemini-cli --acp"_s},
            {u"opencode"_s, u"OpenCode"_s, u"npx -y opencode-ai acp"_s}};
    for (const HostAgent &a : presets) {
        if (a.id == id)
            return a;
    }
    return std::nullopt;
}

/// The program that serves Brulion's tools: brulion-cli itself, else one next to this program (the
/// app's binary runs `brulion cli …`), else on PATH ("" none).
QString toolsProgram()
{
    const QFileInfo self(QCoreApplication::applicationFilePath());
    if (self.fileName().startsWith(u"brulion-cli"_s))
        return self.absoluteFilePath();
    for (const QString &name : {u"brulion-cli"_s, u"brulion-cli.exe"_s}) {
        const QFileInfo cli(self.absoluteDir().filePath(name));
        if (cli.isFile() && cli.isExecutable())
            return cli.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(u"brulion-cli"_s);
}

bool validAgentId(const QString &id)
{
    if (id.isEmpty() || id.size() > 40)
        return false;
    return std::ranges::all_of(id, [](QChar c) {
        return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'-'
               || c == u'_';
    });
}

} // namespace

std::optional<HostAgent> parseHostAgent(const QString &spec, QString *error)
{
    const QString s = spec.trimmed();
    const qsizetype eq = s.indexOf(u'=');
    QString head = (eq < 0 ? s : s.left(eq)).trimmed();
    const QString command = eq < 0 ? QString() : s.mid(eq + 1).trimmed();
    QString name;
    if (const qsizetype colon = head.indexOf(u':'); colon >= 0) {
        name = head.mid(colon + 1).trimmed();
        head = head.left(colon).trimmed();
    }
    if (!validAgentId(head)) {
        *error = u"an agent's id is letters, digits, \"-\" and \"_\": %1"_s.arg(spec);
        return std::nullopt;
    }
    if (command.isEmpty()) {
        std::optional<HostAgent> known = preset(head);
        if (!known) {
            *error = u"no command for the agent %1 (id=command, or one of claude, codex, gemini, opencode)"_s.arg(head);
            return std::nullopt;
        }
        if (!name.isEmpty())
            known->name = name;
        return known;
    }
    if (QProcess::splitCommand(command).isEmpty()) {
        *error = u"the agent %1 has no command"_s.arg(head);
        return std::nullopt;
    }
    const std::optional<HostAgent> known = preset(head);
    return HostAgent{head, !name.isEmpty() ? name : known ? known->name : head, command};
}

// ---- The state file -----------------------------------------------------------------------------------

QJsonObject HostState::toJson() const
{
    QJsonArray list;
    for (const PairedDevice &d : devices)
        list.append(QJsonObject{{u"id"_s, d.id},
                                {u"name"_s, d.name},
                                {u"tokenHash"_s, d.tokenHash},
                                {u"created"_s, timeText(d.created)},
                                {u"lastUsed"_s, timeText(d.lastUsed)}});
    QJsonObject o{{u"format"_s, u"brulion-acp-host"_s}, {u"version"_s, 1}, {u"devices"_s, list}};
    if (!pairingHash.isEmpty())
        o.insert(u"pairing"_s, QJsonObject{{u"hash"_s, pairingHash},
                                           {u"expires"_s, timeText(pairingExpires)},
                                           {u"attempts"_s, pairingAttempts}});
    if (!publicUrl.isEmpty())
        o.insert(u"publicUrl"_s, publicUrl);
    return o;
}

HostState HostState::fromJson(const QJsonObject &o)
{
    HostState s;
    for (const QJsonValue &v : o.value(u"devices"_s).toArray()) {
        const QJsonObject d = v.toObject();
        PairedDevice device{d.value(u"id"_s).toString(), d.value(u"name"_s).toString(),
                            d.value(u"tokenHash"_s).toString(), timeOf(d.value(u"created"_s)),
                            timeOf(d.value(u"lastUsed"_s))};
        if (!device.id.isEmpty() && device.tokenHash.size() == 64)
            s.devices.append(device);
    }
    const QJsonObject pairing = o.value(u"pairing"_s).toObject();
    s.pairingHash = pairing.value(u"hash"_s).toString();
    s.pairingExpires = timeOf(pairing.value(u"expires"_s));
    s.pairingAttempts = pairing.value(u"attempts"_s).toInt();
    s.publicUrl = o.value(u"publicUrl"_s).toString();
    return s;
}

HostStateStore::HostStateStore(QString path) : m_path(std::move(path)) { }

bool HostStateStore::load(QString *error)
{
    const QFileInfo info(m_path);
    if (!info.exists()) {
        m_state = {};
        m_modified = {};
        m_size = -1;
        return true;
    }
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(file.read(4 * 1024 * 1024), &parse);
    if (!doc.isObject() || doc.object().value(u"format"_s).toString() != u"brulion-acp-host") {
        if (error)
            *error = u"not a state file of brulion-cli acp serve"_s;
        return false;
    }
    m_state = HostState::fromJson(doc.object());
    m_modified = info.lastModified(QTimeZone::UTC);
    m_size = info.size();
    return true;
}

bool HostStateStore::refresh(QString *error)
{
    const QFileInfo info(m_path);
    if (info.exists() && info.lastModified(QTimeZone::UTC) == m_modified && info.size() == m_size)
        return true;
    if (!info.exists() && m_size < 0)
        return true;
    return load(error);
}

bool HostStateStore::update(const std::function<bool(HostState &)> &change, QString *error)
{
    const QFileInfo info(m_path);
    const QDir dir = info.absoluteDir();
    if (!dir.exists()) {
        if (!QDir().mkpath(dir.absolutePath())) {
            if (error)
                *error = u"cannot create %1"_s.arg(dir.absolutePath());
            return false;
        }
        QFile::setPermissions(dir.absolutePath(),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    QLockFile lock(m_path + u".lock"_s);
    if (!lock.tryLock(kLockTimeoutMs)) {
        if (error)
            *error = u"the state file is locked by another process"_s;
        return false;
    }
    if (!load(error))
        return false;
    HostState next = m_state;
    if (!change(next))
        return true;
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(next.toJson()).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return load(error);
}

QString newPairingCode()
{
    return randomString(kPairingCodeLength);
}

QString pairingLink(const QString &url, const QString &code)
{
    if (url.isEmpty())
        return {};
    automation::Request r;
    r.action = automation::Action::Pair;
    r.params = {{u"url"_s, url}, {u"code"_s, code}};
    return automation::buildUri(r);
}

QString terminalQrCode(const QString &text)
{
    const qr::Code code = qr::encode(text);
    if (code.isNull())
        return {};
    // Two modules a character (upper and lower half), the light ones drawn: right on a terminal's
    // dark background — phone cameras read a code drawn the other way round too.
    const int q = qr::QuietZone;
    const int size = code.size + 2 * q;
    const auto light = [&](int x, int y) { return y >= size || !code.dark(x - q, y - q); };
    QString out;
    for (int y = 0; y < size; y += 2) {
        for (int x = 0; x < size; ++x) {
            const bool top = light(x, y);
            const bool bottom = light(x, y + 1);
            out += top && bottom ? u'█' : top ? u'▀' : bottom ? u'▄' : u' ';
        }
        out += u'\n';
    }
    return out;
}

QString formatPairingCode(const QString &code)
{
    return code.size() == kPairingCodeLength ? code.left(4) + u'-' + code.mid(4) : code;
}

QString newDeviceToken()
{
    QByteArray bytes(32, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(bytes.data()), bytes.size() / 4);
    return u"bat_"_s
           + QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString secretHash(const QString &secret)
{
    return QString::fromLatin1(QCryptographicHash::hash(secret.toUtf8(), QCryptographicHash::Sha256).toHex());
}

// ---- What goes to the agent ---------------------------------------------------------------------------

QJsonObject toolsServer(const QString &program, const QString &root)
{
    // (A thread's: the app's tools alone, none of the notebook's.)
    return QJsonObject{{u"name"_s, u"brulion"_s},
                       {u"command"_s, program},
                       {u"args"_s, root.isEmpty() ? QJsonArray{u"mcp"_s, u"--no-notes"_s}
                                                  : QJsonArray{u"mcp"_s, u"--vault"_s, root}},
                       {u"env"_s, QJsonArray()}};
}

std::optional<QJsonObject> rewriteForAgent(const QJsonObject &message, const QString &root,
                                           const QJsonArray &mcpServers)
{
    if (message.value(u"jsonrpc"_s).toString() != u"2.0")
        return std::nullopt;
    const QString method = message.value(u"method"_s).toString();
    if (method.isEmpty() && !message.contains(u"id"_s))
        return std::nullopt;
    QJsonObject out = message;
    if (method == u"initialize") {
        QJsonObject params = out.value(u"params"_s).toObject();
        QJsonObject capabilities = params.value(u"clientCapabilities"_s).toObject();
        capabilities.insert(u"fs"_s, QJsonObject{{u"readTextFile"_s, true}, {u"writeTextFile"_s, true}});
        capabilities.insert(u"terminal"_s, true);
        params.insert(u"clientCapabilities"_s, capabilities);
        out.insert(u"params"_s, params);
    } else if (method == u"session/new" || method == u"session/load" || method == u"session/resume") {
        QJsonObject params = out.value(u"params"_s).toObject();
        params.insert(u"cwd"_s, root);
        params.insert(u"mcpServers"_s, mcpServers);
        out.insert(u"params"_s, params);
    }
    return out;
}

bool servedHere(const QJsonObject &message)
{
    if (!message.contains(u"id"_s))
        return false;
    const QString method = message.value(u"method"_s).toString();
    return method.startsWith(u"fs/"_s) || method.startsWith(u"terminal/"_s);
}

QString codeFilePath(const QString &folder, const QString &file, bool write)
{
    if (folder.isEmpty() || file.isEmpty() || file.contains(QChar(u'\0')) || file.size() > acp::MaxRemotePathChars)
        return {};
    const QString full = QDir::cleanPath(QDir::isAbsolutePath(file) ? file : QDir(folder).absoluteFilePath(file));
    if (full != folder && !full.startsWith(folder + u'/'))
        return {};
    // (A link that leads out of the folder is outside it too: the nearest part that is there, as
    // it really is.)
    QFileInfo info(full);
    while (!info.exists() && info.absolutePath() != info.absoluteFilePath())
        info = QFileInfo(info.absolutePath());
    const QString real = info.canonicalFilePath();
    if (real.isEmpty() || (real != folder && !real.startsWith(folder + u'/')))
        return {};
    if (write) {
        const QString first = full.mid(folder.size() + 1).section(u'/', 0, 0);
        if (full == folder || first.compare(u".git"_s, Qt::CaseInsensitive) == 0)
            return {};
    }
    return full;
}

// ---- Code mode's projects ----------------------------------------------------------------------------

HostProjectsStore::HostProjectsStore(QString path) : m_path(std::move(path)) { }

void HostProjectsStore::refresh()
{
    const QFileInfo info(m_path);
    const QDateTime modified = info.exists() ? info.lastModified(QTimeZone::UTC) : QDateTime();
    const qint64 size = info.exists() ? info.size() : -1;
    if (modified == m_modified && size == m_size)
        return;
    m_modified = modified;
    m_size = size;
    m_projects.clear();
    if (size < 0) {
        if (log)
            log(u"no projects: %1 is not there"_s.arg(m_path));
        return;
    }
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (log)
            log(u"no projects: %1 not read (%2)"_s.arg(m_path, f.errorString()));
        return;
    }
    QString error;
    const std::optional<QList<acp::HostProject>> read = acp::parseHostProjects(f.read(4 * 1024 * 1024), &error);
    if (!read) {
        if (log)
            log(u"no projects: %1 is %2"_s.arg(m_path, error));
        return;
    }
    m_projects = *read;
    if (log)
        log(u"projects: %1"_s.arg(m_projects.size()));
}

const QList<acp::HostProject> &HostProjectsStore::projects()
{
    refresh();
    return m_projects;
}

std::optional<acp::HostProject> HostProjectsStore::find(const QString &id)
{
    for (const acp::HostProject &p : projects()) {
        if (p.id == id)
            return p;
    }
    return std::nullopt;
}

// ---- The server -----------------------------------------------------------------------------------------

/// A command an agent runs (terminal/create).
struct AcpHost::Terminal {
    QProcess *process = nullptr;
    QByteArray output;
    qint64 limit = 0;
    bool truncated = false;
    bool ended = false;
    QJsonValue exitCode; // null: killed by a signal
    QString signal;
    QList<QJsonValue> waiting; // terminal/wait_for_exit requests to answer when it ends
};

/// One agent's process for one device.
struct AcpHost::Stream {
    QString id;
    QString device;
    HostAgent agent;
    QProcess *process = nullptr;
    QByteArray partial;    // the agent's output after its last line end
    bool skipping = false; // a line longer than acp::MaxLineBytes: dropped up to its end
    QString errors;
    // To the device: the agent's messages not fetched yet.
    QList<std::pair<qint64, QJsonObject>> out;
    QList<qint64> outSizes;
    qint64 nextSeq = 1;
    qint64 outBytes = 0;
    // From the device.
    qint64 received = 0;
    bool closed = false;
    QString why;
    http::Reply waiter;
    QTimer *waitTimer = nullptr;
    QDateTime lastSeen;
    QSet<QString> prompts;            // the ids of the device's session/prompt requests not answered yet
    // The agent's session/request_permission not answered yet, in the order asked.
    struct Asked {
        QString key; // idKey() of its id: the device answers it by this (HostTurn::question)
        QJsonValue id;
        QString session;
        QJsonObject params;
        QDateTime heard; // when it was asked, or the device last fetched its stream or turn since
    };
    QList<Asked> asked;
    bool retired = false; // nobody fetches from it: it ends after its turns
    QString retiredWhy;
    QDateTime retiredAt;
    bool holding = false; // messages wait for the sync before a turn
    // Messages to the agent that wait: for the sync, or for a check or a checkpoint of their own
    // (`ticket`, 0 when ready) — delivered in order.
    struct Held {
        QJsonObject message;
        int ticket = 0;
    };
    QList<Held> held;
    QTimer *holdTimer = nullptr;
    // Code mode's threads: where each of their sessions works (by the agent's session id), the
    // session/new requests whose answer names it (by idKey()), the order they came in (bounded),
    // each session's checkpoints taken here (oldest first).
    QHash<QString, CodePlace> codeSessions;
    QHash<QString, CodePlace> pendingCode;
    QStringList codeOrder;
    QHash<QString, QStringList> checkpoints;
    QHash<QString, Terminal *> terminals;
    // The app's calls of the agent's tools (acp/apptunnel.h).
    bool app = false; // its device answers them (POST streams {features: [app]})
    struct AppPending {
        ipc::ControlReply reply;
        QTimer *timer = nullptr;
        QString method;
        QString path; // a screenshot's file here ("" the runtime folder)
    };
    QHash<QString, AppPending> appCalls; // by idKey() of the call's id
    QStringList appGrants;               // the tunnel's tokens of its sessions, oldest first
};

AcpHost::AcpHost(Context &context, notebook::Notebook *notebook, AcpHostOptions options, QObject *parent)
    : QObject(parent), m_ctx(context), m_notebook(notebook), m_options(std::move(options))
{ }

AcpHost::~AcpHost()
{
    // (The app's answers still on their way are dropped with the link.)
    m_app.reset();
    for (Stream *s : std::as_const(m_streams)) {
        failAppCalls(s, QString::fromLatin1(ipc::ControlError::NotReady), u"acp serve stopped"_s);
        endTerminals(s);
        if (s->process) {
            s->process->disconnect(this);
            s->process->kill();
            s->process->waitForFinished(2000);
            delete s->process;
        }
        delete s;
    }
    m_streams.clear();
}

void AcpHost::say(const QString &line) const
{
    if (log)
        log(line);
}

QDateTime AcpHost::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTimeUtc();
}

quint16 AcpHost::port() const
{
    return m_http ? m_http->port() : 0;
}

QString AcpHost::endpoint() const
{
    return m_publicUrl + u'/' + acp::RemoteApiPrefix;
}

QString AcpHost::route(const QByteArray &path) const
{
    const QString p = QString::fromUtf8(QByteArray::fromPercentEncoding(path));
    if (m_basePath.isEmpty())
        return p;
    if (p == m_basePath)
        return u"/"_s;
    if (p.startsWith(m_basePath + u'/'))
        return p.mid(m_basePath.size());
    return p; // (a proxy that strips the prefix)
}

bool AcpHost::loadTls(QString *error)
{
#if QT_CONFIG(ssl)
    QFile certFile(m_options.tlsCertificate);
    QFile keyFile(m_options.tlsKey);
    if (!certFile.open(QIODevice::ReadOnly) || !keyFile.open(QIODevice::ReadOnly)) {
        *error = tr("Cannot read the certificate or its key");
        return false;
    }
    const QList<QSslCertificate> chain = QSslCertificate::fromData(certFile.readAll(), QSsl::Pem);
    const QByteArray keyData = keyFile.readAll();
    QSslKey key(keyData, QSsl::Rsa, QSsl::Pem);
    if (key.isNull())
        key = QSslKey(keyData, QSsl::Ec, QSsl::Pem);
    if (chain.isEmpty() || key.isNull()) {
        *error = tr("The certificate or its key is not PEM");
        return false;
    }
    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
    config.setLocalCertificateChain(chain);
    config.setPrivateKey(key);
    config.setPeerVerifyMode(QSslSocket::VerifyNone);
    m_http->setTlsConfiguration(config);
    m_certModified = QFileInfo(m_options.tlsCertificate).lastModified(QTimeZone::UTC);
    m_keyModified = QFileInfo(m_options.tlsKey).lastModified(QTimeZone::UTC);
    return true;
#else
    *error = tr("This build has no TLS");
    return false;
#endif
}

bool AcpHost::start(QString *error)
{
    m_root = QDir::cleanPath(QDir(m_ctx.vaultRoot()).absolutePath());
    if (m_options.agents.isEmpty()) {
        *error = tr("No agent: give one with --agent (or --agents)");
        return false;
    }
    const bool tls = !m_options.tlsCertificate.isEmpty() || !m_options.tlsKey.isEmpty();
    if (tls && (m_options.tlsCertificate.isEmpty() || m_options.tlsKey.isEmpty())) {
        *error = tr("--tls-cert and --tls-key go together");
        return false;
    }
    const bool anyAddress = m_options.address == QHostAddress::Any || m_options.address == QHostAddress::AnyIPv4
                            || m_options.address == QHostAddress::AnyIPv6;
    QString publicUrl = m_options.publicUrl.trimmed();
    if (publicUrl.isEmpty() && anyAddress) {
        *error = tr("--public-url is needed when the server listens on all addresses");
        return false;
    }
    m_http = std::make_unique<http::Server>(m_options.limits);
    m_http->onLog = [this](const QString &line) { say(line); };
    m_http->onHeaders = [this](const http::Request &head, qint64 *maxBody) {
        std::optional<http::Response> refusal = onHeaders(head, maxBody);
        if (refusal && head.method != "OPTIONS") {
            // (The app in a browser reads why: a refusal carries the CORS header fields too.)
            const http::Headers cors = corsHeaders(head.headers.value("Origin"));
            for (const auto &f : cors.fields())
                refusal->headers.add(f.name, f.value);
        }
        return refusal;
    };
    m_http->onRequest = [this](const http::Request &request, const http::Reply &reply) { onRequest(request, reply); };
    if (tls && !loadTls(error))
        return false;
    if (!m_http->listen(m_options.address, m_options.port, error)) {
        *error = tr("Cannot listen on %1 port %2: %3")
                         .arg(m_options.address.toString(), QString::number(m_options.port), *error);
        return false;
    }
    const quint16 port = m_http->port();
    if (publicUrl.isEmpty()) {
        const QString host = !m_options.publicHost.isEmpty() ? m_options.publicHost
                             : m_options.address.protocol() == QAbstractSocket::IPv6Protocol
                                     ? u'[' + m_options.address.toString() + u']'
                                     : m_options.address.toString();
        publicUrl = (tls ? u"https://"_s : u"http://"_s) + host + u':' + QString::number(port);
    }
    const QUrl url(publicUrl, QUrl::StrictMode);
    const bool loopbackHost = url.host() == u"localhost" || QHostAddress(url.host()).isLoopback();
    // (Plain http to a tailnet's name or address goes through its WireGuard tunnel, encrypted.)
    const bool plainAllowed = loopbackHost || tailscale::isTailnetHost(url.host()) || m_options.allowHttp;
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()
        || (url.scheme() != u"https" && !(url.scheme() == u"http" && plainAllowed))) {
        *error = tr("--public-url must be an https:// address (http:// only on this computer, in a tailnet or with "
                    "--allow-http): %1")
                         .arg(publicUrl);
        return false;
    }
    QString path = url.path(QUrl::FullyEncoded);
    while (path.endsWith(u'/'))
        path.chop(1);
    m_basePath = path;
    QUrl base = url;
    base.setPath(path);
    m_publicUrl = base.toString(QUrl::FullyEncoded);
    while (m_publicUrl.endsWith(u'/'))
        m_publicUrl.chop(1);

    const bool https = url.scheme() == u"https";
    m_hosts = {normalizedHost(url.authority(QUrl::FullyEncoded), https)};
    for (const QString &loopback : {u"localhost"_s, u"127.0.0.1"_s, u"[::1]"_s})
        m_hosts.append(loopback + u':' + QString::number(port));
    if (!anyAddress) {
        const QString a = m_options.address.protocol() == QAbstractSocket::IPv6Protocol
                                  ? u'[' + m_options.address.toString() + u']'
                                  : m_options.address.toString();
        m_hosts.append(a.toLower() + u':' + QString::number(port));
    }
    for (const QString &h : std::as_const(m_options.allowedHosts))
        m_hosts.append(normalizedHost(h, https));
    m_origins.clear();
    for (const QString &o : std::as_const(m_options.allowedOrigins)) {
        const QUrl origin(o.trimmed(), QUrl::StrictMode);
        if (!origin.isValid() || origin.host().isEmpty()
            || (origin.scheme() != u"https" && origin.scheme() != u"http")) {
            *error = tr("--allowed-origin takes the address of a site (https://notes.example.com): %1").arg(o);
            return false;
        }
        m_origins.append(http::originOf(origin));
    }

    if (m_options.tools && !m_options.toolsProgram.isEmpty())
        m_mcpServers = QJsonArray{toolsServer(m_options.toolsProgram, m_root)};
    // The tunnel for the app's calls of the agents' tools: a control socket of its own in the user's
    // runtime folder, only for the grants it makes (acp/apptunnel.h). Without one, the devices'
    // apps are not reached (said in the log and in info).
    if (!m_mcpServers.isEmpty() && m_options.appTools) {
        m_tunnelPrefix = randomString(8);
        QString key = m_options.tunnelKey;
        if (key.isEmpty()) {
#ifdef Q_OS_WIN
            const QString runtime = QDir::tempPath();
#else
            const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
#endif
            const QString name = u"brulion-acp-"_s + randomString(12).toLower();
            key = runtime.isEmpty() ? name : QDir(runtime).absoluteFilePath(name);
        }
        m_tunnel = std::make_unique<ipc::ControlServer>();
        m_tunnel->setOpen(false);
        m_tunnel->setHandler(
                [this](const ipc::ControlRequest &request, ipc::ControlReply reply) { tunnelRequest(request, reply); });
        if (m_tunnel->listen(key)) {
            m_tunnelKey = key;
        } else {
            say(u"the agents' tools do not reach the devices' apps: %1"_s.arg(m_tunnel->errorString()));
            m_tunnel.reset();
        }
    }

    // The app that started this server shares its own conversations with the devices: a lasting
    // link to its control socket with the grant it gave (acp/appchats.h).
    if (!m_options.appSocket.isEmpty() && !m_options.appToken.isEmpty()) {
        m_app = std::make_unique<ipc::AsyncControlClient>(m_options.appSocket, m_options.appToken, u"agent sharing"_s);
        connect(m_app.get(), &ipc::AsyncControlClient::readyChanged, this, [this](bool ready) {
            if (ready)
                say(u"the app's conversations are shared with the devices (its control socket answers)"_s);
            else
                say(u"the app does not answer (%1): its conversations are not shared until it does"_s.arg(
                        m_app->lastError()));
        });
        m_app->start();
        m_pollCheck = new QTimer(this);
        m_pollCheck->setInterval(1000);
        connect(m_pollCheck, &QTimer::timeout, this, &AcpHost::dropLeftPolls);
    }
    if (!m_options.projectsFile.isEmpty()) {
        m_projects = std::make_unique<HostProjectsStore>(m_options.projectsFile);
        m_projects->log = [this](const QString &line) { say(line); };
        (void)m_projects->projects(); // (said in the log at once)
    }
    m_store = std::make_unique<HostStateStore>(m_options.stateFile);
    QString stateError;
    if (!m_store->update(
                [this](HostState &s) {
                    if (s.publicUrl == m_publicUrl)
                        return false;
                    s.publicUrl = m_publicUrl;
                    return true;
                },
                &stateError)) {
        *error = tr("Cannot write %1: %2").arg(m_options.stateFile, stateError);
        return false;
    }
    m_maintenance = new QTimer(this);
    connect(m_maintenance, &QTimer::timeout, this, &AcpHost::maintain);
    m_maintenance->start(m_options.maintenanceIntervalMs);
    return true;
}

bool AcpHost::hostAllowed(const QByteArray &host) const
{
    const QString h = QString::fromLatin1(host).trimmed().toLower();
    return std::ranges::any_of(m_hosts, [&h](const QString &allowed) { return allowed == h; })
           || std::ranges::any_of(m_hosts, [&h](const QString &allowed) {
                  // A Host without the port of the scheme's default (normalizedHost dropped it).
                  return normalizedHost(h, true) == allowed || normalizedHost(h, false) == allowed;
              });
}

const PairedDevice *AcpHost::authenticate(const http::Request &request, http::Response *refusal)
{
    const QByteArray header = request.headers.value("Authorization");
    const auto refuse = [refusal](const QString &message) {
        *refusal = apiError(401, u"unauthorized"_s, message);
        refusal->headers.set("WWW-Authenticate"_ba, "Bearer realm=\"brulion\""_ba);
        return nullptr;
    };
    if (!header.startsWith("Bearer "))
        return refuse(u"A token is needed: pair this device first"_s);
    const QString hash = secretHash(QString::fromLatin1(header.mid(7).trimmed()));
    QString error;
    if (!m_store->refresh(&error))
        say(u"state file not read: %1"_s.arg(error));
    for (const PairedDevice &d : m_store->state().devices) {
        if (sameSecret(d.tokenHash, hash))
            return &d;
    }
    return refuse(u"This device is not paired (any more)"_s);
}

std::optional<http::Response> AcpHost::onHeaders(const http::Request &head, qint64 *maxBody)
{
    *maxBody = 0;
    if (!hostAllowed(head.headers.value("Host"))) {
        say(u"%1 refused: Host %2"_s.arg(head.peer.toString(),
                                         QString::fromLatin1(head.headers.value("Host").left(100))));
        return http::Response::text(421, "This server does not answer for that host name"_ba);
    }
    const QByteArray origin = head.headers.value("Origin");
    if (head.headers.has("Origin") && !originAllowed(origin)) {
        // A browser's page (another site, DNS rebinding): the app sends no Origin, the app in a
        // browser the origin it was allowed (--allowed-origin).
        say(u"%1 refused: Origin %2"_s.arg(head.peer.toString(), QString::fromLatin1(origin.left(100))));
        return apiError(403, u"origin"_s, u"Requests from web pages are not allowed"_s);
    }
    if (head.method == "OPTIONS") {
        // The CORS preflight of the app in a browser (an allowed origin: above).
        if (!head.headers.has("Origin"))
            return apiError(405, u"method"_s, u"Method not allowed"_s);
        http::Response r204 = http::Response::empty(204);
        r204.headers.set("Access-Control-Allow-Origin"_ba, origin);
        r204.headers.set("Access-Control-Allow-Methods"_ba, "GET, POST, DELETE, OPTIONS"_ba);
        r204.headers.set("Access-Control-Allow-Headers"_ba, "Authorization, Content-Type, Accept"_ba);
        r204.headers.set("Access-Control-Max-Age"_ba, "600"_ba);
        r204.headers.set("Vary"_ba, "Origin"_ba);
        // (Chrome's Private Network Access: a public page asks before it calls an address of the
        // user's own network — a tailnet's is one.)
        if (head.headers.value("Access-Control-Request-Private-Network") == "true")
            r204.headers.set("Access-Control-Allow-Private-Network"_ba, "true"_ba);
        return r204;
    }
    const QString r = route(head.path);
    if (r == u"/" || r.isEmpty()) {
        if (head.method != "GET" && head.method != "HEAD")
            return apiError(405, u"method"_s, u"Method not allowed"_s);
        return std::nullopt;
    }
    const QString api = u'/' + acp::RemoteApiPrefix;
    if (!r.startsWith(api))
        return apiError(404, u"not_found"_s, u"Not found"_s);
    const QString endpoint = r.mid(api.size());
    if (endpoint == u"pair") {
        if (head.method != "POST")
            return apiError(405, u"method"_s, u"Method not allowed"_s);
        *maxBody = 4096;
        return std::nullopt;
    }
    http::Response refusal;
    if (!authenticate(head, &refusal))
        return refusal;
    if (endpoint == u"info") {
        if (head.method != "GET")
            return apiError(405, u"method"_s, u"Method not allowed"_s);
        return std::nullopt;
    }
    if (endpoint == u"streams") {
        if (head.method != "POST")
            return apiError(405, u"method"_s, u"Method not allowed"_s);
        *maxBody = 4096;
        return std::nullopt;
    }
    const QStringList parts = endpoint.split(u'/');
    if (parts.size() == 3 && parts.at(0) == u"streams" && parts.at(2) == u"send") {
        if (head.method != "POST")
            return apiError(405, u"method"_s, u"Method not allowed"_s);
        *maxBody = m_options.maxBodyBytes;
        return std::nullopt;
    }
    if (parts.size() == 3 && parts.at(0) == u"streams" && parts.at(2) == u"receive") {
        if (head.method != "GET")
            return apiError(405, u"method"_s, u"Method not allowed"_s);
        return std::nullopt;
    }
    if (parts.size() == 2 && parts.at(0) == u"streams") {
        if (head.method != "DELETE")
            return apiError(405, u"method"_s, u"Method not allowed"_s);
        return std::nullopt;
    }
    if (parts.size() == 3 && parts.at(0) == u"sessions" && parts.at(2) == u"turn") {
        if (head.method != "GET" && head.method != "DELETE" && head.method != "POST")
            return apiError(405, u"method"_s, u"Method not allowed"_s);
        *maxBody = head.method == "POST" ? 4096 : 0;
        return std::nullopt;
    }
    // The app's own conversations (acp/appchats.h).
    if (parts.at(0) == u"chats") {
        const bool list = parts.size() == 1;
        const bool page = parts.size() == 2 && parts.at(1) == u"page";
        const bool action = parts.size() == 2
                            && (parts.at(1) == u"send" || parts.at(1) == u"start" || parts.at(1) == u"stop"
                                || parts.at(1) == u"answer");
        if (!list && !page && !action)
            return apiError(404, u"not_found"_s, u"Not found"_s);
        if (head.method != (action ? "POST"_ba : "GET"_ba))
            return apiError(405, u"method"_s, u"Method not allowed"_s);
        if (action)
            *maxBody = parts.at(1) == u"send" || parts.at(1) == u"start" ? kMaxChatMessageBody : kMaxChatSmallBody;
        return std::nullopt;
    }
    // Code mode's projects (acp/codehost.h).
    if (parts.at(0) == u"projects") {
        const bool list = parts.size() == 1;
        const bool action = parts.size() == 3
                            && (parts.at(2) == u"worktrees" || parts.at(2) == u"changes" || parts.at(2) == u"diff");
        if (!list && !action)
            return apiError(404, u"not_found"_s, u"Not found"_s);
        const QByteArray method = action && parts.at(2) == u"worktrees" ? "POST"_ba : "GET"_ba;
        if (head.method != method)
            return apiError(405, u"method"_s, u"Method not allowed"_s);
        *maxBody = method == "POST" ? 4096 : 0;
        return std::nullopt;
    }
    return apiError(404, u"not_found"_s, u"Not found"_s);
}

bool AcpHost::originAllowed(const QByteArray &origin) const
{
    if (origin.isEmpty())
        return true;
    if (origin == "null")
        return false;
    return m_origins.contains(http::originOf(QUrl(QString::fromLatin1(origin))));
}

http::Headers AcpHost::corsHeaders(const QByteArray &origin) const
{
    http::Headers h;
    if (origin.isEmpty() || !originAllowed(origin))
        return h;
    h.add("Access-Control-Allow-Origin"_ba, origin);
    h.add("Access-Control-Expose-Headers"_ba, "WWW-Authenticate"_ba);
    h.add("Vary"_ba, "Origin"_ba);
    return h;
}

void AcpHost::onRequest(const http::Request &request, const http::Reply &rawReply)
{
    const http::Reply reply = rawReply.with(corsHeaders(request.headers.value("Origin")));
    const QString r = route(request.path);
    if (r == u"/" || r.isEmpty()) {
        reply.send(http::Response::text(200, "Brulion: AI agents of this computer (brulion-cli acp serve)\n"_ba));
        return;
    }
    const QString endpoint = r.mid(1 + acp::RemoteApiPrefix.size());
    if (endpoint == u"pair") {
        pair(request, reply);
        return;
    }
    http::Response refusal;
    const PairedDevice *device = authenticate(request, &refusal);
    if (!device) {
        reply.send(refusal);
        return;
    }
    const PairedDevice who = *device;
    if (endpoint == u"info") {
        reply.send(info());
        return;
    }
    if (endpoint == u"streams") {
        reply.send(openStream(who, request));
        return;
    }
    const QStringList parts = endpoint.split(u'/');
    if (parts.size() == 3 && parts.at(0) == u"sessions") {
        sessionTurn(who, parts.at(1), request, reply); // (route() decoded it)
        return;
    }
    if (parts.at(0) == u"projects") {
        projectsRequest(parts, request, reply);
        return;
    }
    if (parts.at(0) == u"chats") {
        chatsRequest(who, parts, request, reply);
        return;
    }
    Stream *s = parts.size() >= 2 ? m_streams.value(parts.at(1)) : nullptr;
    if (!s || s->device != who.id) {
        reply.send(apiError(404, u"no_stream"_s, u"No such stream: the agent's process ended here"_s));
        return;
    }
    s->lastSeen = now();
    if (parts.size() == 2) { // DELETE
        // (A turn it runs is finished first and kept: the app was closed while the agent worked,
        // and asks for the answer when it starts again.)
        retire(s, u"stopped by the device"_s);
        reply.send(http::Response::empty(204));
    } else if (parts.at(2) == u"send") {
        reply.send(send(s, request));
    } else {
        receive(s, request, reply);
    }
}

void AcpHost::pair(const http::Request &request, const http::Reply &reply)
{
    const QDateTime t = now();
    m_pairTries.removeIf([&t](const QDateTime &when) { return when.secsTo(t) >= 60; });
    if (m_pairTries.size() >= kMaxPairsPerMinute) {
        reply.send(apiError(429, u"too_many_tries"_s, u"Too many pairing tries: wait a minute"_s));
        return;
    }
    m_pairTries.append(t);
    const QJsonDocument doc = QJsonDocument::fromJson(request.body);
    const QJsonObject body = doc.object();
    const QString code = acp::normalizePairingCode(body.value(u"code"_s).toString());
    const QString name = deviceName(body.value(u"device"_s).toString());
    if (!doc.isObject()) {
        reply.send(apiError(400, u"bad_request"_s, u"A pairing code is needed"_s));
        return;
    }
    if (code.isEmpty()) {
        // Without a code: the device's identity decides (in a tailnet, acptrust.h), else the code
        // is needed.
        if (!m_pairTrust) {
            reply.send(apiError(403, u"code_needed"_s, u"A pairing code is needed: run acp pair on the computer"_s));
            return;
        }
        const QString peer = request.peer.toString();
        QPointer<AcpHost> self(this);
        m_pairTrust(request, [self, reply, name, peer](const PairTrust &verdict) {
            if (!self)
                return;
            if (!verdict.trusted) {
                self->say(u"%1 pairing without a code refused: %2"_s.arg(peer, verdict.why));
                reply.send(apiError(403, u"code_needed"_s, u"A pairing code is needed: %1"_s.arg(verdict.why)));
                return;
            }
            reply.send(self->addTrustedDevice(name.isEmpty() ? verdict.node : name,
                                              u"%1 without a code: %2"_s.arg(peer, verdict.why)));
        });
        return;
    }
    reply.send(pairWithCode(code, name, request.peer.toString(), t));
}

http::Response AcpHost::addTrustedDevice(const QString &name, const QString &how)
{
    const QString token = newDeviceToken();
    const QDateTime t = now();
    PairedDevice paired;
    bool tooMany = false;
    QString error;
    const bool written = m_store->update(
            [&](HostState &s) {
                if (s.devices.size() >= kMaxDevices) {
                    tooMany = true;
                    return false;
                }
                paired = PairedDevice{u"dev_"_s + randomString(10), name.isEmpty() ? u"device"_s : deviceName(name),
                                      secretHash(token), t, t};
                s.devices.append(paired);
                return true;
            },
            &error);
    if (!written) {
        say(u"pairing not saved: %1"_s.arg(error));
        return apiError(500, u"state"_s, u"The server cannot save the pairing"_s);
    }
    if (tooMany)
        return apiError(403, u"too_many_devices"_s, u"Too many devices are paired: end some with acp revoke"_s);
    say(u"paired %1 (%2): %3"_s.arg(paired.name, paired.id, how));
    return http::Response::json(
            200, QJsonObject{{u"token"_s, token},
                             {u"device"_s, QJsonObject{{u"id"_s, paired.id}, {u"name"_s, paired.name}}}});
}

http::Response AcpHost::pairWithCode(const QString &code, const QString &name, const QString &peer,
                                     const QDateTime &t)
{
    const QString token = newDeviceToken();
    PairedDevice paired;
    QString outcome;
    QString error;
    const bool written = m_store->update(
            [&](HostState &s) {
                if (s.pairingHash.isEmpty() || !s.pairingExpires.isValid() || s.pairingExpires <= t) {
                    outcome = u"no_code"_s;
                    const bool stale = !s.pairingHash.isEmpty();
                    s.pairingHash.clear();
                    return stale;
                }
                if (!sameSecret(s.pairingHash, secretHash(code))) {
                    outcome = u"bad_code"_s;
                    if (++s.pairingAttempts >= kPairingAttempts) {
                        s.pairingHash.clear();
                        s.pairingAttempts = 0;
                    }
                    return true;
                }
                if (s.devices.size() >= kMaxDevices) {
                    outcome = u"too_many_devices"_s;
                    return false;
                }
                paired = PairedDevice{u"dev_"_s + randomString(10), name.isEmpty() ? u"device"_s : name,
                                      secretHash(token), t, t};
                s.devices.append(paired);
                s.pairingHash.clear();
                s.pairingAttempts = 0;
                outcome = u"paired"_s;
                return true;
            },
            &error);
    if (!written) {
        say(u"pairing not saved: %1"_s.arg(error));
        return apiError(500, u"state"_s, u"The server cannot save the pairing"_s);
    }
    if (outcome == u"no_code")
        return apiError(403, u"no_code"_s, u"No pairing code is valid now: run acp pair on the computer"_s);
    if (outcome == u"bad_code") {
        say(u"%1 pairing refused: a wrong code"_s.arg(peer));
        return apiError(403, u"bad_code"_s, u"That is not the pairing code"_s);
    }
    if (outcome == u"too_many_devices")
        return apiError(403, u"too_many_devices"_s, u"Too many devices are paired: end some with acp revoke"_s);
    say(u"paired %1 (%2)"_s.arg(paired.name, paired.id));
    return http::Response::json(
            200, QJsonObject{{u"token"_s, token},
                             {u"device"_s, QJsonObject{{u"id"_s, paired.id}, {u"name"_s, paired.name}}}});
}

http::Response AcpHost::info() const
{
    QJsonArray features;
    if (m_tunnel)
        features.append(QString(acp::AppFeature));
    if (m_projects)
        features.append(QString(acp::CodeFeature));
    if (appLinked())
        features.append(QString(acp::ChatsFeature));
    QJsonArray agents;
    for (const HostAgent &a : m_options.agents)
        agents.append(QJsonObject{{u"id"_s, a.id}, {u"name"_s, a.name}, {u"command"_s, a.command}});
    return http::Response::json(
            200, QJsonObject{{u"protocol"_s, acp::RemoteProtocol},
                             {u"version"_s, QCoreApplication::applicationVersion()},
                             {u"host"_s, QSysInfo::machineHostName()},
                             {u"root"_s, m_root},
                             {u"tools"_s, !m_mcpServers.isEmpty()},
                             {u"features"_s, features},
                             {u"agents"_s, agents},
                             {u"sync"_s, m_sync ? m_sync->status() : QJsonObject{{u"enabled"_s, false}}}});
}

http::Response AcpHost::openStream(const PairedDevice &device, const http::Request &request)
{
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    const QString id = body.value(u"agent"_s).toString();
    const auto agent = std::ranges::find_if(m_options.agents, [&id](const HostAgent &a) { return a.id == id; });
    if (agent == m_options.agents.end())
        return apiError(404, u"unknown_agent"_s, u"This computer has no agent %1"_s.arg(id));
    // The app keeps one process per agent: a new stream of the device for the same agent means
    // that it started again (or gave the old one up), and the old one's agent would only take
    // memory here until it idled out.
    // (One that runs a turn finishes it first: its device hears how it ended through the turn kept.)
    for (Stream *old : m_streams.values()) {
        if (old->device == device.id && old->agent.id == agent->id && !old->retired)
            retire(old, u"replaced by a new stream of the device"_s);
    }
    int running = 0;
    int mine = 0;
    for (const Stream *s : std::as_const(m_streams)) {
        running += s->closed ? 0 : 1;
        mine += s->device == device.id && !s->closed ? 1 : 0;
    }
    if (running >= m_options.maxStreams || mine >= m_options.maxStreamsPerDevice)
        return apiError(429, u"too_many_streams"_s, u"Too many agents run for devices here"_s);
    auto *s = new Stream;
    s->id = u"s_"_s + randomString(16);
    s->device = device.id;
    s->agent = *agent;
    s->lastSeen = now();
    // The device answers the app's calls of the agent's tools, and they are tunnelled here.
    const QJsonArray asked = body.value(u"features"_s).toArray();
    s->app = m_tunnel && asked.contains(QString(acp::AppFeature));
    // (Its threads work in this computer's projects: said to a device that asks.)
    QJsonArray features = s->app ? QJsonArray{QString(acp::AppFeature)} : QJsonArray();
    if (m_projects && asked.contains(QString(acp::CodeFeature)))
        features.append(QString(acp::CodeFeature));
    m_streams.insert(s->id, s);
    say(u"%1 starts %2 for %3 (%4)%5"_s.arg(s->id, agent->name, device.name, device.id,
                                            s->app ? u", its app reached by the agent's tools"_s : QString()));
    startAgent(s);
    return http::Response::json(201, QJsonObject{{u"stream"_s, s->id}, {u"root"_s, m_root}, {u"features"_s, features}});
}

http::Response AcpHost::send(Stream *s, const http::Request &request)
{
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(request.body, &parse);
    const QJsonObject body = doc.object();
    const QJsonValue fromValue = body.value(u"from"_s);
    if (!doc.isObject() || !fromValue.isDouble() || !body.value(u"messages"_s).isArray())
        return apiError(400, u"bad_request"_s, u"{from, messages} is needed"_s);
    const qint64 from = fromValue.toInteger(-1);
    if (from < 0 || from > s->received)
        return apiError(409, u"gap"_s, u"Messages before these are missing (%1 received)"_s.arg(s->received));
    const QJsonArray messages = body.value(u"messages"_s).toArray();
    for (qsizetype i = 0; i < messages.size(); ++i) {
        if (from + i < s->received)
            continue; // (a batch sent again: its answer was lost)
        s->received = from + i + 1;
        const QJsonValue v = messages.at(i);
        if (s->closed)
            continue;
        if (!v.isObject()) {
            say(u"%1: a message of the device dropped (no JSON object)"_s.arg(s->id));
            continue;
        }
        toAgent(s, v.toObject());
    }
    return http::Response::json(200, QJsonObject{{u"received"_s, s->received}});
}

void AcpHost::receive(Stream *s, const http::Request &request, const http::Reply &reply)
{
    const QUrlQuery query(QString::fromLatin1(request.query));
    bool ok = false;
    const qint64 after = query.queryItemValue(u"after"_s).toLongLong(&ok);
    const int wait = std::clamp(query.queryItemValue(u"wait"_s).toInt(), 0, m_options.maxWaitSeconds);
    if (ok) {
        while (!s->out.isEmpty() && s->out.first().first <= after) {
            s->out.removeFirst();
            s->outBytes -= s->outSizes.takeFirst();
        }
    }
    if (s->waiter.alive())
        answerWaiter(s); // (one receive at a time: an older one is answered at once)
    s->waiter = reply;
    if (!s->out.isEmpty() || s->closed || s->retired || wait == 0) {
        answerWaiter(s);
        return;
    }
    if (!s->waitTimer) {
        s->waitTimer = new QTimer(this);
        s->waitTimer->setSingleShot(true);
        const QString id = s->id;
        connect(s->waitTimer, &QTimer::timeout, this, [this, id] {
            if (Stream *found = m_streams.value(id))
                answerWaiter(found);
        });
    }
    s->waitTimer->start(wait * 1000);
}

void AcpHost::answerWaiter(Stream *s)
{
    if (s->waitTimer)
        s->waitTimer->stop();
    const http::Reply reply = std::exchange(s->waiter, http::Reply());
    if (!reply.alive())
        return;
    QJsonArray messages;
    qint64 bytes = 0;
    for (qsizetype i = 0; i < s->out.size(); ++i) {
        if (i > 0 && bytes + s->outSizes.at(i) > kMaxAnswerBytes)
            break; // (the rest: the next receive)
        bytes += s->outSizes.at(i);
        messages.append(QJsonObject{{u"seq"_s, s->out.at(i).first}, {u"message"_s, s->out.at(i).second}});
    }
    const bool all = messages.size() == s->out.size();
    const bool over = (s->closed || s->retired) && all;
    QJsonObject o{{u"messages"_s, messages}, {u"closed"_s, over}};
    if (over)
        o.insert(u"why"_s, s->closed ? s->why : s->retiredWhy);
    reply.send(http::Response::json(200, o));
}

void AcpHost::closeStream(Stream *s, const QString &why)
{
    if (s->closed)
        return;
    s->closed = true;
    s->why = why;
    failAppCalls(s, QString::fromLatin1(ipc::ControlError::NotReady),
                 u"The agent's connection to the app on the device ended (%1)"_s.arg(why));
    revokeAppGrants(s);
    turnsFailed(s, why);
    s->prompts.clear();
    s->asked.clear();
    s->held.clear();
    s->holding = false;
    s->pendingCode.clear();
    if (s->holdTimer)
        s->holdTimer->stop();
    endTerminals(s);
    if (QProcess *proc = std::exchange(s->process, nullptr)) {
        proc->disconnect(this);
        if (proc->state() != QProcess::NotRunning) {
            connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
            proc->closeWriteChannel();
            proc->terminate();
            QTimer::singleShot(3000, proc, [proc] { proc->kill(); });
        } else {
            proc->deleteLater();
        }
    }
    say(u"%1 ended: %2"_s.arg(s->id, why));
    answerWaiter(s);
}

void AcpHost::removeStream(Stream *s)
{
    failAppCalls(s, QString::fromLatin1(ipc::ControlError::NotReady), u"The agent's connection to the app ended"_s);
    revokeAppGrants(s);
    answerWaiter(s);
    m_streams.remove(s->id);
    delete s->waitTimer;
    delete s->holdTimer;
    delete s;
}

void AcpHost::removeLater(Stream *s)
{
    const QString id = s->id;
    QTimer::singleShot(0, this, [this, id] {
        if (Stream *found = m_streams.value(id))
            removeStream(found);
    });
}

void AcpHost::retire(Stream *s, const QString &why)
{
    if (s->closed || s->retired)
        return;
    if (s->prompts.isEmpty()) {
        closeStream(s, why);
        removeStream(s);
        return;
    }
    s->retired = true;
    s->retiredWhy = why;
    s->retiredAt = now();
    say(u"%1: %2; it ends after its turn"_s.arg(s->id, why));
    // Nobody fetches the stream: the app's calls of its tools are answered by nobody now.
    failAppCalls(s, QString::fromLatin1(ipc::ControlError::NotReady),
                 u"The app on %1 is not connected now (it was closed, or it lost the network): its tools reach it "
                 u"again in the next turn the app sends"_s.arg(pairedName(s->device)));
    // The questions its agent waits on wait for the device with their turns (or, without a wait,
    // nobody is there to answer them); what nobody will fetch goes.
    if (m_options.questionWaitMs <= 0) {
        QSet<QString> sessions;
        while (!s->asked.isEmpty()) {
            sessions.insert(s->asked.constFirst().session);
            answerQuestion(s, 0, QJsonObject{{u"outcome"_s, u"cancelled"_s}});
        }
        for (const QString &session : std::as_const(sessions))
            questionsChanged(s, session);
    } else {
        for (Stream::Asked &q : s->asked)
            q.heard = now(); // (its device was there just now)
    }
    s->out.clear();
    s->outSizes.clear();
    s->outBytes = 0;
    answerWaiter(s);
}

// ---- The turns kept ------------------------------------------------------------------------------------

const HostTurn *AcpHost::turn(const QString &device, const QString &session) const
{
    const auto it = m_turns.constFind(turnKey(device, session));
    return it == m_turns.cend() ? nullptr : &*it;
}

void AcpHost::turnStarted(Stream *s, const QJsonObject &params, const QString &prompt)
{
    const QString session = params.value(u"sessionId"_s).toString();
    if (session.isEmpty())
        return;
    HostTurn t;
    t.device = s->device;
    t.session = session;
    t.stream = s->id;
    t.prompt = prompt;
    t.id = params.value(u"_meta"_s).toObject().value(acp::TurnMetaKey).toString().left(200);
    t.started = now();
    const QString key = turnKey(s->device, session);
    m_turns.insert(key, t);
    // At most kMaxTurnsPerDevice of a device: the ones that ended longest ago go.
    QList<std::pair<QDateTime, QString>> ended;
    for (auto it = m_turns.cbegin(); it != m_turns.cend(); ++it) {
        if (it->device == s->device && it->state != HostTurn::State::Running)
            ended.append({it->ended, it.key()});
    }
    const qsizetype mine = std::ranges::count_if(m_turns, [s](const HostTurn &x) { return x.device == s->device; });
    std::ranges::sort(ended);
    for (qsizetype i = 0; i < ended.size() && mine - i > kMaxTurnsPerDevice; ++i)
        m_turns.remove(ended.at(i).second);
    turnChanged(key);
}

void AcpHost::turnUpdate(Stream *s, const QJsonObject &params)
{
    const auto it = m_turns.find(turnKey(s->device, params.value(u"sessionId"_s).toString()));
    if (it == m_turns.end() || it->stream != s->id || it->state != HostTurn::State::Running)
        return;
    const QJsonObject update = params.value(u"update"_s).toObject();
    const QString kind = update.value(u"sessionUpdate"_s).toString();
    if (kind == u"agent_message_chunk") {
        const QJsonObject content = update.value(u"content"_s).toObject();
        if (content.value(u"type"_s).toString() == u"text" && it->text.size() < kMaxTurnTextChars)
            it->text += content.value(u"text"_s).toString().left(kMaxTurnTextChars - it->text.size());
    } else if (kind == u"tool_call" || kind == u"tool_call_update") {
        const QString title = update.value(u"title"_s).toString().trimmed().left(500);
        if (kind == u"tool_call" && !title.isEmpty() && it->tools.size() < kMaxTurnTools)
            it->tools.append(title);
        const QString call = update.value(u"toolCallId"_s).toString().left(200);
        if (!title.isEmpty() && !call.isEmpty()
            && (it->toolTitles.size() < 4 * kMaxTurnTools || it->toolTitles.contains(call)))
            it->toolTitles.insert(call, title);
    }
}

void AcpHost::turnEnded(Stream *s, const QString &prompt, const QJsonObject &response)
{
    for (auto it = m_turns.begin(); it != m_turns.end(); ++it) {
        if (it->stream != s->id || it->prompt != prompt || it->state != HostTurn::State::Running)
            continue;
        it->ended = now();
        it->asking = false;
        it->question = {};
        // (Its questions went with it: the agent waits on none of them any more.)
        s->asked.removeIf([&it](const Stream::Asked &q) { return q.session == it->session; });
        if (response.contains(u"error"_s)) {
            const QJsonObject e = response.value(u"error"_s).toObject();
            it->state = HostTurn::State::Failed;
            it->error = e.value(u"message"_s).toString();
            if (it->error.isEmpty())
                it->error = u"error %1"_s.arg(e.value(u"code"_s).toInt());
        } else {
            it->state = HostTurn::State::Done;
            it->stopReason = response.value(u"result"_s).toObject().value(u"stopReason"_s).toString();
        }
        turnChanged(it.key());
        return;
    }
}

void AcpHost::turnsFailed(Stream *s, const QString &why)
{
    for (auto it = m_turns.begin(); it != m_turns.end(); ++it) {
        if (it->stream != s->id || it->state != HostTurn::State::Running)
            continue;
        it->state = HostTurn::State::Failed;
        it->error = why;
        it->ended = now();
        it->asking = false;
        it->question = {};
        turnChanged(it.key());
    }
}

void AcpHost::turnChanged(const QString &key)
{
    const auto it = m_turns.constFind(key);
    for (qsizetype i = m_turnWaiters.size() - 1; i >= 0; --i) {
        if (m_turnWaiters.at(i).key != key)
            continue;
        const TurnWaiter w = m_turnWaiters.takeAt(i);
        delete w.timer;
        if (!w.reply.alive())
            continue;
        if (it == m_turns.cend())
            w.reply.send(apiError(404, u"no_turn"_s, u"This computer knows no turn of that session"_s));
        else
            w.reply.send(http::Response::json(200, it->toJson()));
    }
}

void AcpHost::answerTurnWaiter(QTimer *timer)
{
    for (qsizetype i = 0; i < m_turnWaiters.size(); ++i) {
        if (m_turnWaiters.at(i).timer != timer)
            continue;
        const TurnWaiter w = m_turnWaiters.takeAt(i);
        timer->deleteLater();
        const auto it = m_turns.constFind(w.key);
        if (!w.reply.alive())
            return;
        if (it == m_turns.cend())
            w.reply.send(apiError(404, u"no_turn"_s, u"This computer knows no turn of that session"_s));
        else
            w.reply.send(http::Response::json(200, it->toJson()));
        return;
    }
}

void AcpHost::questionsChanged(Stream *s, const QString &session)
{
    const auto it = m_turns.find(turnKey(s->device, session));
    if (it == m_turns.end() || it->stream != s->id || it->state != HostTurn::State::Running)
        return;
    const auto first = std::ranges::find(s->asked, session, &Stream::Asked::session);
    const bool asking = first != s->asked.cend();
    QJsonObject question;
    if (asking) {
        // (Its call's title as the turn knows it, where the question names none: "Write Dom.md".)
        QJsonObject call = first->params.value(u"toolCall"_s).toObject();
        if (call.value(u"title"_s).toString().trimmed().isEmpty()) {
            if (const QString title = it->toolTitles.value(call.value(u"toolCallId"_s).toString()); !title.isEmpty())
                call.insert(u"title"_s, title);
        }
        question = QJsonObject{
                {u"id"_s, first->key}, {u"toolCall"_s, call}, {u"options"_s, first->params.value(u"options"_s)}};
    }
    if (it->asking == asking && it->question == question)
        return;
    it->asking = asking;
    it->question = question;
    turnChanged(it.key());
}

void AcpHost::answerQuestion(Stream *s, qsizetype index, const QJsonObject &outcome)
{
    const Stream::Asked q = s->asked.takeAt(index);
    respond(s, q.id, QJsonObject{{u"outcome"_s, outcome}});
    // (A turn nobody fetches from gets its time again: it did not work while it waited.)
    if (s->retired)
        s->retiredAt = now();
}

http::Response AcpHost::answerTurnQuestion(const QString &key, const http::Request &request)
{
    const auto it = m_turns.constFind(key);
    Stream *s = it == m_turns.cend() ? nullptr : m_streams.value(it->stream);
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    const QString question = body.value(u"question"_s).toString();
    qsizetype index = -1;
    if (s && !s->closed && it->state == HostTurn::State::Running) {
        for (qsizetype i = 0; i < s->asked.size() && index < 0; ++i) {
            if (s->asked.at(i).key == question && s->asked.at(i).session == it->session)
                index = i;
        }
    }
    if (index < 0)
        return apiError(404, u"no_question"_s,
                        u"That question does not wait any more: it was answered, or it waited too long"_s);
    // One of the options the agent offered, else declined ("cancelled"), as the app's client answers.
    const QString option = body.value(u"optionId"_s).toString();
    const QJsonArray offered = s->asked.at(index).params.value(u"options"_s).toArray();
    const bool known = !option.isEmpty() && std::ranges::any_of(offered, [&option](const QJsonValue &o) {
        return o.toObject().value(u"optionId"_s).toString() == option;
    });
    const QString session = it->session;
    answerQuestion(s, index,
                   known ? QJsonObject{{u"outcome"_s, u"selected"_s}, {u"optionId"_s, option}}
                         : QJsonObject{{u"outcome"_s, u"cancelled"_s}});
    say(u"%1: the device answered a question of session %2 (%3)"_s.arg(s->id, session.left(100),
                                                                       known ? option.left(100) : u"cancelled"_s));
    questionsChanged(s, session);
    const auto current = m_turns.constFind(key);
    return http::Response::json(200, current == m_turns.cend() ? QJsonObject() : current->toJson());
}

void AcpHost::expireQuestions(Stream *s)
{
    if (s->closed || s->asked.isEmpty())
        return;
    const QDateTime t = now();
    // The device is there while it fetches the stream (not one it lost) or waits for the turn.
    const bool fetching = !s->retired && s->waiter.alive();
    QSet<QString> changed;
    for (qsizetype i = s->asked.size() - 1; i >= 0; --i) {
        Stream::Asked &q = s->asked[i];
        const QString key = turnKey(s->device, q.session);
        if (fetching || std::ranges::any_of(m_turnWaiters, [&key](const TurnWaiter &w) {
                return w.key == key && w.reply.alive();
            }))
            q.heard = t;
        const QDateTime heard = s->retired ? q.heard : std::max(q.heard, s->lastSeen);
        if (heard.msecsTo(t) <= m_options.questionWaitMs)
            continue;
        say(u"%1: a question of the agent waited %2 minutes for the device: answered \"cancelled\""_s.arg(s->id).arg(
                heard.msecsTo(t) / 60000));
        changed.insert(q.session);
        answerQuestion(s, i, QJsonObject{{u"outcome"_s, u"cancelled"_s}});
    }
    for (const QString &session : std::as_const(changed))
        questionsChanged(s, session);
}

void AcpHost::forgetTurns()
{
    const QDateTime t = now();
    QStringList gone;
    for (auto it = m_turns.cbegin(); it != m_turns.cend(); ++it) {
        if (it->state != HostTurn::State::Running && it->ended.msecsTo(t) > kTurnKeptMs)
            gone << it.key();
    }
    for (const QString &key : std::as_const(gone)) {
        m_turns.remove(key);
        turnChanged(key);
    }
    // (Waiters whose connection went: dropped.)
    for (qsizetype i = m_turnWaiters.size() - 1; i >= 0; --i) {
        if (!m_turnWaiters.at(i).reply.alive())
            delete m_turnWaiters.takeAt(i).timer;
    }
}

void AcpHost::sessionTurn(const PairedDevice &device, const QString &session, const http::Request &request,
                          const http::Reply &reply)
{
    const QString key = turnKey(device.id, session);
    const auto it = m_turns.find(key);
    if (it == m_turns.end()) {
        reply.send(apiError(404, u"no_turn"_s,
                            u"This computer knows no turn of that session (it was started again, or the turn "
                            "ended long ago)"_s));
        return;
    }
    Stream *s = m_streams.value(it->stream);
    if (request.method == "DELETE") {
        // The device stops the turn (its questions too, as a client's cancel answers them).
        if (it->state == HostTurn::State::Running && s && !s->closed) {
            deliver(s, QJsonObject{{u"jsonrpc"_s, u"2.0"_s},
                                   {u"method"_s, u"session/cancel"_s},
                                   {u"params"_s, QJsonObject{{u"sessionId"_s, session}}}});
            for (qsizetype i = s->asked.size() - 1; i >= 0; --i) {
                if (s->asked.at(i).session == session)
                    answerQuestion(s, i, QJsonObject{{u"outcome"_s, u"cancelled"_s}});
            }
            questionsChanged(s, session);
            say(u"%1: the turn of session %2 stopped by the device"_s.arg(s->id, session.left(100)));
        }
        reply.send(http::Response::empty(204));
        return;
    }
    if (request.method == "POST") {
        reply.send(answerTurnQuestion(key, request));
        return;
    }
    if (s) {
        for (Stream::Asked &q : s->asked) {
            if (q.session == session)
                q.heard = now(); // (the device is there: its question waits for it)
        }
    }
    // The device asks because it lost the stream that runs the turn: nobody fetches from it now.
    if (s && it->state == HostTurn::State::Running)
        retire(s, u"its device lost it"_s);
    const QUrlQuery query(QString::fromLatin1(request.query));
    const int wait = std::clamp(query.queryItemValue(u"wait"_s).toInt(), 0, m_options.maxWaitSeconds);
    const auto found = m_turns.constFind(key); // (retire() may have changed it)
    if (found == m_turns.cend() || found->state != HostTurn::State::Running || wait == 0) {
        if (found == m_turns.cend())
            reply.send(apiError(404, u"no_turn"_s, u"This computer knows no turn of that session"_s));
        else
            reply.send(http::Response::json(200, found->toJson()));
        return;
    }
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, timer] { answerTurnWaiter(timer); });
    m_turnWaiters.append({key, reply, timer});
    timer->start(wait * 1000);
}

void AcpHost::maintain()
{
    QString error;
    if (!m_store->refresh(&error))
        say(u"state file not read: %1"_s.arg(error));
    const HostState &state = m_store->state();
    const QDateTime t = now();
    // The devices' last use (once a while, not on every request).
    QHash<QString, QDateTime> used;
    for (const Stream *s : std::as_const(m_streams))
        used[s->device] = std::max(used.value(s->device), s->lastSeen);
    const bool stale = std::ranges::any_of(state.devices, [&used](const PairedDevice &d) {
        return used.contains(d.id) && d.lastUsed.secsTo(used.value(d.id)) > 60;
    });
    if (stale) {
        m_store->update(
                [&used](HostState &s) {
                    for (PairedDevice &d : s.devices) {
                        if (used.contains(d.id))
                            d.lastUsed = std::max(d.lastUsed, used.value(d.id));
                    }
                    return true;
                },
                &error);
    }
    for (Stream *s : m_streams.values()) {
        const bool paired =
                std::ranges::any_of(m_store->state().devices, [s](const PairedDevice &d) { return d.id == s->device; });
        if (!paired) {
            // Its receive waiting hears that the device is not paired (any more), not of an agent's end.
            const http::Reply waiting = std::exchange(s->waiter, http::Reply());
            closeStream(s, u"the device was unpaired"_s);
            removeStream(s);
            if (waiting.alive())
                waiting.send(apiError(401, u"unauthorized"_s, u"This device is not paired (any more)"_s));
            continue;
        }
        expireQuestions(s);
        // (A stream that ended is kept a while for its device to hear why, then given up.)
        const qint64 quiet = s->lastSeen.msecsTo(t);
        const bool idle = s->closed ? quiet > std::min<qint64>(m_options.streamIdleMs, 5 * 60 * 1000)
                                    : quiet > m_options.streamIdleMs && s->prompts.isEmpty();
        if (s->retired && !s->closed
            && (s->prompts.isEmpty() || (s->asked.isEmpty() && s->retiredAt.msecsTo(t) > m_options.retiredTurnMs))) {
            // (A turn nobody waits for gets a while to end, not forever.)
            closeStream(s, s->prompts.isEmpty() ? s->retiredWhy : u"its turn did not end in time"_s);
            removeStream(s);
        } else if (idle) {
            closeStream(s, u"no device fetched from it for %1 minutes"_s.arg(m_options.streamIdleMs / 60000));
            removeStream(s);
        }
    }
    forgetTurns();
    if (!m_http || !m_http->tls())
        return;
    const QDateTime cert = QFileInfo(m_options.tlsCertificate).lastModified(QTimeZone::UTC);
    const QDateTime key = QFileInfo(m_options.tlsKey).lastModified(QTimeZone::UTC);
    if (cert == m_certModified && key == m_keyModified)
        return;
    if (loadTls(&error))
        say(u"TLS certificate reloaded"_s);
    else
        say(u"TLS certificate not reloaded: %1"_s.arg(error));
}

// ---- The app's calls of the agents' tools (acp/apptunnel.h) -------------------------------------------

// ---- The app's own conversations (acp/appchats.h) -------------------------------------------------

namespace {

/// The device's answer to an error of the app (its code kept, its message passed on).
http::Response appError(const ipc::ControlClient::Response &r)
{
    if (r.transportError)
        return apiError(503, u"app_unavailable"_s, u"The app on this computer does not answer: %1"_s.arg(r.message));
    int status = 502;
    if (r.code == u"not_found")
        status = 404;
    else if (r.code == u"busy" || r.code == u"no_question")
        status = 409;
    else if (r.code == u"bad_option" || r.code == u"bad_params" || r.code == u"ambiguous_ref")
        status = 400;
    else if (r.code == u"refused" || r.code == u"unauthorized")
        status = 403;
    else if (r.code == u"not_ready")
        status = 503;
    return apiError(status, r.code.isEmpty() ? u"failed"_s : r.code, r.message.left(2000));
}

http::Response appUnavailable()
{
    return apiError(503, u"app_unavailable"_s,
                    u"The app on this computer does not answer (it shares its conversations while it runs)"_s);
}

/// A query's value (nullopt: not given; a malformed query: no value at all).
std::optional<QString> queryValue(const http::Request &request, const QString &name)
{
    const std::optional<QList<std::pair<QString, QString>>> form = http::parseForm(request.query);
    if (!form)
        return std::nullopt;
    for (const auto &[key, value] : *form) {
        if (key == name)
            return value;
    }
    return std::nullopt;
}

} // namespace

bool AcpHost::appLinked() const
{
    return m_app && m_app->isReady();
}

void AcpHost::relay(const QString &method, const QJsonObject &params, int timeoutMs, const http::Reply &reply,
                    std::function<http::Response(const QJsonObject &result)> answer)
{
    if (!appLinked()) {
        reply.send(appUnavailable());
        return;
    }
    m_app->call(method, params, timeoutMs, [reply, answer](const ipc::ControlClient::Response &r) {
        if (!reply.alive())
            return; // (the device left meanwhile)
        reply.send(r.ok ? answer(r.result.toObject()) : appError(r));
    });
}

void AcpHost::dropLeftPolls()
{
    m_chatPolls.removeIf([](QHash<quint64, ChatPoll>::iterator it) { return !it->reply.alive(); });
    if (m_chatPolls.isEmpty())
        m_pollCheck->stop();
}

http::Response AcpHost::chatPage(const http::Request &request) const
{
    const QString path = queryValue(request, u"path"_s).value_or(QString());
    if (!acp::isRemoteChatPath(path) || markdown::isEncryptedNotePath(path))
        return apiError(400, u"bad_path"_s, u"A conversation's page in the notebook's AI folder is needed"_s);
    const QString vaultPath = notebookPath(path);
    if (vaultPath.isEmpty())
        return apiError(400, u"bad_path"_s, u"A conversation's page in the notebook's AI folder is needed"_s);
    const QFileInfo file(QDir(m_root).filePath(vaultPath));
    if (file.exists() && file.size() > acp::MaxRemotePageBytes)
        return apiError(413, u"too_large"_s, u"The page is larger than %1 bytes"_s.arg(acp::MaxRemotePageBytes));
    const notebook::Notebook::Current current = m_notebook->read(vaultPath);
    if (!current.exists)
        return apiError(404, u"not_found"_s, u"No such conversation here: %1"_s.arg(vaultPath));
    if (current.bytes.size() > acp::MaxRemotePageBytes)
        return apiError(413, u"too_large"_s, u"The page is larger than %1 bytes"_s.arg(acp::MaxRemotePageBytes));
    return http::Response::json(200, QJsonObject{{u"path"_s, vaultPath}, {u"text"_s, current.text}});
}

void AcpHost::chatsRequest(const PairedDevice &device, const QStringList &parts, const http::Request &request,
                           const http::Reply &reply)
{
    if (!appLinked()) {
        reply.send(appUnavailable());
        return;
    }
    if (parts.size() == 2 && parts.at(1) == u"page") {
        reply.send(chatPage(request));
        return;
    }
    if (parts.size() == 1) {
        // GET chats?since=&wait=&path=: the app's chatLive, a long poll of `wait` seconds at most.
        if (!http::parseForm(request.query)) {
            reply.send(apiError(400, u"bad_request"_s, u"The query is malformed"_s));
            return;
        }
        bool ok = true;
        const QString sinceText = queryValue(request, u"since"_s).value_or(u"-1"_s);
        const qint64 since = sinceText.toLongLong(&ok);
        if (!ok || since < -1 || since > 1e15) {
            reply.send(apiError(400, u"bad_request"_s, u"since is a revision of an answer before (-1: none)"_s));
            return;
        }
        const int wait =
                std::clamp(queryValue(request, u"wait"_s).value_or(u"0"_s).toInt(), 0, m_options.maxWaitSeconds);
        const QString path = queryValue(request, u"path"_s).value_or(QString());
        if (!path.isEmpty() && !acp::isRemoteChatPath(path)) {
            reply.send(apiError(400, u"bad_path"_s, u"A conversation's page in the notebook's AI folder is needed"_s));
            return;
        }
        int mine = 0;
        for (const ChatPoll &p : std::as_const(m_chatPolls))
            mine += p.device == device.id ? 1 : 0;
        if (int(m_chatPolls.size()) >= m_options.maxChatPolls || mine >= m_options.maxChatPollsPerDevice) {
            reply.send(apiError(429, u"too_many_requests"_s, u"Too many requests wait for the app's conversations"_s));
            return;
        }
        QJsonObject params{{u"since"_s, double(since)}, {u"timeout"_s, wait * 1000}};
        if (!path.isEmpty())
            params.insert(u"path"_s, path);
        const quint64 serial = m_nextPoll++;
        m_chatPolls.insert(serial, ChatPoll{device.id, reply});
        if (!m_pollCheck->isActive())
            m_pollCheck->start();
        const QPointer<AcpHost> self(this);
        m_app->call(u"chatLive"_s, params, wait * 1000 + kAppCallMarginMs,
                    [self, serial, reply](const ipc::ControlClient::Response &r) {
                        if (self)
                            self->m_chatPolls.remove(serial);
                        if (!reply.alive())
                            return; // (its device left: dropped)
                        if (!r.ok) {
                            reply.send(appError(r));
                            return;
                        }
                        // (Passed on bounded: only what the device's side takes.)
                        const std::optional<acp::RemoteChats> chats = acp::RemoteChats::fromJson(r.result.toObject());
                        reply.send(chats ? http::Response::json(200, chats->toJson())
                                         : apiError(502, u"bad_answer"_s, u"The app's answer holds no revision"_s));
                    });
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(request.body);
    if (!doc.isObject()) {
        reply.send(apiError(400, u"bad_request"_s, u"A JSON object is needed"_s));
        return;
    }
    const QJsonObject body = doc.object();
    const QString action = parts.at(1);
    const QString path = body.value(u"path"_s).toString();
    if (action != u"start" && !acp::isRemoteChatPath(path)) {
        reply.send(apiError(400, u"bad_path"_s, u"A conversation's page in the notebook's AI folder is needed"_s));
        return;
    }
    const int timeout = m_options.appCallTimeoutMs + kAppCallMarginMs;
    const auto pathOf = [path](const QJsonObject &result) {
        const QString p = result.value(u"path"_s).toString();
        return http::Response::json(200, QJsonObject{{u"path"_s, p.isEmpty() ? path : p}});
    };
    if (action == u"send") {
        const QJsonValue text = body.value(u"text"_s);
        const QString mode = body.value(u"mode"_s).toString();
        if (!text.isString() || text.toString().trimmed().isEmpty()) {
            reply.send(apiError(400, u"bad_request"_s, u"The message (text) is needed"_s));
            return;
        }
        if (!mode.isEmpty() && mode != u"queue" && mode != u"steer") {
            reply.send(apiError(400, u"bad_mode"_s, u"mode is \"\", \"queue\" or \"steer\""_s));
            return;
        }
        QJsonObject params{{u"path"_s, path}, {u"text"_s, text.toString()}};
        if (!mode.isEmpty())
            params.insert(u"mode"_s, mode);
        say(u"%1 (%2) sends a message to %3%4"_s.arg(device.name, device.id, path,
                                                     mode.isEmpty() ? QString() : u" (" + mode + u')'));
        relay(u"chatSend"_s, params, timeout, reply, pathOf);
    } else if (action == u"start") {
        const QString project = body.value(u"project"_s).toString().trimmed();
        const QJsonValue text = body.value(u"text"_s);
        if (project.isEmpty() || !text.isString() || text.toString().trimmed().isEmpty()) {
            reply.send(apiError(400, u"bad_request"_s, u"The project and the first message (text) are needed"_s));
            return;
        }
        QJsonObject params{{u"project"_s, project}, {u"text"_s, text.toString()}};
        for (const QString &key : {u"partner"_s, u"worktree"_s, u"branch"_s}) {
            const QJsonValue v = body.value(key);
            if (!v.isUndefined() && !v.isNull() && !v.isString()) {
                reply.send(apiError(400, u"bad_request"_s, u"%1 is a string"_s.arg(key)));
                return;
            }
            if (!v.toString().trimmed().isEmpty())
                params.insert(key, v.toString().trimmed());
        }
        say(u"%1 (%2) starts a thread of %3 in the app"_s.arg(device.name, device.id, project));
        relay(u"chatStart"_s, params, timeout, reply, [](const QJsonObject &result) {
            const QString p = result.value(u"path"_s).toString();
            return acp::isRemoteChatPath(p)
                           ? http::Response::json(200, QJsonObject{{u"path"_s, p}})
                           : apiError(502, u"bad_answer"_s, u"The app's answer names no conversation"_s);
        });
    } else if (action == u"stop") {
        say(u"%1 (%2) stops the turn of %3"_s.arg(device.name, device.id, path));
        relay(u"chatStop"_s, {{u"path"_s, path}}, timeout, reply, [path](const QJsonObject &result) {
            return http::Response::json(200, QJsonObject{{u"path"_s, result.value(u"path"_s).toString(path)},
                                                         {u"stopped"_s, result.value(u"stopped"_s).toBool()}});
        });
    } else {
        const QJsonValue option = body.value(u"option"_s);
        if (!option.isString() || option.toString().isEmpty()) {
            reply.send(apiError(400, u"bad_option"_s, u"One of the question's options is needed"_s));
            return;
        }
        say(u"%1 (%2) answers the question of %3"_s.arg(device.name, device.id, path));
        relay(u"chatAnswer"_s, {{u"path"_s, path}, {u"option"_s, option.toString()}}, timeout, reply,
              [path](const QJsonObject &result) {
                  return http::Response::json(
                          200, QJsonObject{{u"path"_s, result.value(u"path"_s).toString(path)}, {u"answered"_s, true}});
              });
    }
}

QString AcpHost::tunnelSocket() const
{
    return m_tunnel ? m_tunnelKey : QString();
}

QString AcpHost::pairedName(const QString &device) const
{
    for (const PairedDevice &d : m_store->state().devices) {
        if (d.id == device)
            return d.name;
    }
    return {};
}

QJsonArray AcpHost::sessionServers(Stream *s, const std::optional<acp::AppSession> &asked, bool code)
{
    // (A thread's session without the app's tools: Brulion's server would have none.)
    if (m_mcpServers.isEmpty() || !m_tunnel || !s->app || !asked || asked->isEmpty())
        return code ? QJsonArray() : m_mcpServers;
    // Grants of the tunnel for this session's MCP server: its tokens only in the server's environment.
    const auto grant = [this, s, &asked](ipc::GrantScope scope) {
        const QString token = m_tunnel->addGrant(s->agent.name, scope);
        m_tunnelGrants.insert(token, TunnelGrant{s->id, asked->handle,
                                                 scope == ipc::GrantScope::Full ? QString(acp::AppScopeControl)
                                                                                : QString(acp::AppScopeChat),
                                                 s->agent.name});
        s->appGrants.append(token);
        return token;
    };
    QJsonObject server = toolsServer(m_options.toolsProgram, code ? QString() : m_root);
    QJsonArray args = server.value(u"args"_s).toArray();
    args.append(asked->control ? u"--app-control"_s : u"--app-chat"_s);
    const auto variable = [](const QString &name, const QString &value) {
        return QJsonObject{{u"name"_s, name}, {u"value"_s, value}};
    };
    QJsonArray env{variable(u"BRULION_APP_SOCKET"_s, m_tunnelKey), variable(u"BRULION_APP_CLIENT"_s, s->agent.name),
                   variable(u"BRULION_APP_DEVICE"_s, pairedName(s->device))};
    if (asked->control)
        env.append(variable(u"BRULION_APP_TOKEN"_s, grant(ipc::GrantScope::Full)));
    if (asked->chat)
        env.append(variable(u"BRULION_APP_CHAT_TOKEN"_s, grant(ipc::GrantScope::Chat)));
    // (A stream's sessions come and go: the grants of the oldest end.)
    while (s->appGrants.size() > kMaxAppGrants) {
        const QString old = s->appGrants.takeFirst();
        m_tunnelGrants.remove(old);
        m_tunnel->revokeGrant(old);
    }
    server.insert(u"args"_s, args);
    server.insert(u"env"_s, env);
    say(u"%1: a session asks for the app on the device (%2)"_s.arg(s->id, asked->control && asked->chat
                                                                                  ? u"its live tools and the chat's"_s
                                                                          : asked->control ? u"its live tools"_s
                                                                                           : u"the chat's tools"_s));
    return QJsonArray{server};
}

void AcpHost::revokeAppGrants(Stream *s)
{
    for (const QString &token : std::exchange(s->appGrants, {})) {
        m_tunnelGrants.remove(token);
        if (m_tunnel)
            m_tunnel->revokeGrant(token);
    }
}

void AcpHost::toDevice(Stream *s, const QJsonObject &message)
{
    const qint64 size = compact(message).size();
    s->out.append({s->nextSeq++, message});
    s->outSizes.append(size);
    s->outBytes += size;
    answerWaiter(s);
}

namespace {

/// How long the call `method` waits for the device: `base`, and the wait it asks for itself.
int appCallTimeout(const QString &method, const QJsonObject &params, int base)
{
    if (method == u"waitFor")
        return int(std::clamp<qint64>(params.value(u"ms"_s).toInteger(5000), 0, 120000)) + base;
    if (method == u"events")
        return int(std::clamp<qint64>(params.value(u"wait"_s).toInteger(0), 0, 30000)) + base;
    if (method == u"chatWait")
        return int(std::clamp<qint64>(params.value(u"ms"_s).toInteger(60000), 0, 600000)) + base;
    if (method == u"eval" || method == u"load")
        return 3 * base + 10000;
    return base;
}

QString controlCode(const char *code)
{
    return QString::fromLatin1(code);
}

} // namespace

void AcpHost::tunnelRequest(const ipc::ControlRequest &request, const ipc::ControlReply &reply)
{
    namespace E = ipc::ControlError;
    const auto found = m_tunnelGrants.constFind(request.grant);
    if (!request.granted || found == m_tunnelGrants.cend())
        return reply.fail(controlCode(E::Unauthorized), u"The token is no grant of this server"_s);
    const TunnelGrant grant = *found;
    Stream *s = m_streams.value(grant.stream);
    const QString device = s ? pairedName(s->device) : QString();
    if (request.method == u"hello") {
        // Answered here: the tools connect for every call.
        QJsonArray methods;
        for (const ipc::MethodSpec &m : ipc::controlMethods()) {
            if ((m.chat || !request.chatOnly) && m.name != u"quit")
                methods.append(m.name);
        }
        return reply.ok(QJsonObject{{u"protocol"_s, ipc::ControlProtocolVersion},
                                    {u"app"_s, u"Brulion"_s},
                                    {u"version"_s, QCoreApplication::applicationVersion()},
                                    {u"platform"_s, u"acp-tunnel"_s},
                                    {u"methods"_s, methods},
                                    {u"client"_s, grant.client},
                                    {u"granted"_s, true},
                                    {u"device"_s, device}});
    }
    if (!s || s->closed)
        return reply.fail(controlCode(E::NotReady), u"The agent's connection to the app on the device ended"_s,
                          {{u"reason"_s, u"stream_ended"_s}});
    if (s->retired)
        return reply.fail(controlCode(E::NotReady),
                          u"The app on %1 is not connected now (it was closed, or it lost the network): its tools "
                          u"reach it again in the next turn the app sends"_s.arg(device),
                          {{u"reason"_s, u"device_gone"_s}, {u"device"_s, device}});
    if (request.method == u"quit")
        return reply.fail(controlCode(E::Refused),
                          u"An agent on another computer does not quit the app on the device"_s);
    if (s->appCalls.size() >= m_options.maxAppCalls)
        return reply.fail(controlCode(E::Busy),
                          u"%1 calls of the app on %2 wait for its answer already: wait for them first"_s.arg(
                                  QString::number(m_options.maxAppCalls), device));
    QJsonObject params = request.params;
    QString path;
    if (request.method == u"screenshot") {
        // The picture comes back from the device and is written here (never a file of the device).
        path = params.take(u"path"_s).toString().trimmed();
        if (!path.isEmpty()) {
            if (!QDir::isAbsolutePath(path) || !path.endsWith(u".png"_s, Qt::CaseInsensitive))
                return reply.fail(controlCode(E::BadParams), u"The path is an absolute .png path"_s,
                                  {{u"param"_s, u"path"_s}});
            if (!QFileInfo(QFileInfo(path).absolutePath()).isDir())
                return reply.fail(controlCode(E::BadParams),
                                  u"The folder %1 does not exist"_s.arg(QFileInfo(path).absolutePath()),
                                  {{u"param"_s, u"path"_s}});
        }
    }
    acp::AppCall call;
    call.id = u"%1#%2-%3"_s.arg(acp::AppMethod, m_tunnelPrefix, QString::number(m_nextAppCall++));
    call.handle = grant.handle;
    call.scope = grant.scope;
    call.method = request.method;
    call.params = params;
    const QString key = idKey(call.id);
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    const QString stream = s->id;
    connect(timer, &QTimer::timeout, this, [this, stream, key] { appCallTimedOut(stream, key); });
    timer->start(appCallTimeout(request.method, params, m_options.appCallTimeoutMs));
    s->appCalls.insert(key, Stream::AppPending{reply, timer, request.method, path});
    say(u"%1: the agent's tools call the app on %2: %3"_s.arg(s->id, device, request.method));
    toDevice(s, call.toMessage());
}

void AcpHost::appCallTimedOut(const QString &stream, const QString &key)
{
    Stream *s = m_streams.value(stream);
    if (!s)
        return;
    const auto it = s->appCalls.find(key);
    if (it == s->appCalls.end())
        return;
    const Stream::AppPending pending = it.value();
    s->appCalls.erase(it);
    pending.timer->deleteLater();
    const QString device = pairedName(s->device);
    say(u"%1: the app on %2 did not answer %3 in time"_s.arg(s->id, device, pending.method));
    pending.reply.fail(controlCode(ipc::ControlError::NotReady),
                       u"The app on %1 did not answer in time: it may be in the background, asleep or offline — try "
                       u"again, or ask the user to open the app"_s.arg(device),
                       {{u"reason"_s, u"no_answer"_s}, {u"device"_s, device}});
}

void AcpHost::failAppCalls(Stream *s, const QString &code, const QString &why)
{
    for (const Stream::AppPending &pending : std::exchange(s->appCalls, {})) {
        delete pending.timer;
        pending.reply.fail(code, why);
    }
}

bool AcpHost::appAnswered(Stream *s, const QJsonObject &message)
{
    const QJsonValue id = message.value(u"id"_s);
    if (!message.value(u"method"_s).toString().isEmpty() || !id.isString() || m_tunnelPrefix.isEmpty()
        || !id.toString().startsWith(u"%1#%2-"_s.arg(acp::AppMethod, m_tunnelPrefix)))
        return false;
    const auto it = s->appCalls.find(idKey(id));
    if (it == s->appCalls.end()) {
        // (An answer that came too late: its call failed already. Never the agent's.)
        say(u"%1: a late answer of the app on the device dropped"_s.arg(s->id));
        return true;
    }
    const Stream::AppPending pending = it.value();
    s->appCalls.erase(it);
    delete pending.timer;
    acp::AppAnswer answer = acp::AppAnswer::fromResponse(message);
    if (answer.ok && pending.method == u"screenshot") {
        // The picture itself: written here, where the tool reads it (a PNG, its size bounded).
        const QJsonObject r = answer.result.toObject();
        const QByteArray png = QByteArray::fromBase64(r.value(u"png"_s).toString().toLatin1());
        QString file = pending.path;
        if (file.isEmpty()) {
#ifdef Q_OS_WIN
            const QString runtime = QDir::tempPath();
#else
            const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
#endif
            if (!runtime.isEmpty()) {
                const QString dir = QDir(runtime).filePath(u"brulion-screenshots"_s);
                QDir().mkpath(dir);
                QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
                file = QDir(dir).filePath(u"screenshot-%1-%2.png"_s.arg(
                        QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss-zzz"_s), s->id));
            }
        }
        QSaveFile out(file);
        if (png.size() > acp::MaxAppPngBytes || !png.startsWith(QByteArrayView("\x89PNG\r\n\x1a\n", 8)))
            answer = acp::AppAnswer::failure(controlCode(ipc::ControlError::Failed),
                                             u"The app on the device sent no picture"_s);
        else if (file.isEmpty())
            answer = acp::AppAnswer::failure(controlCode(ipc::ControlError::BadParams),
                                             u"There is no private runtime folder ($XDG_RUNTIME_DIR): give a path"_s,
                                             {{u"param"_s, u"path"_s}});
        else if (!out.open(QIODevice::WriteOnly) || out.write(png) != png.size() || !out.commit())
            answer = acp::AppAnswer::failure(controlCode(ipc::ControlError::Failed), u"Could not write %1"_s.arg(file),
                                             {{u"path"_s, file}});
        else
            answer = acp::AppAnswer::success(QJsonObject{{u"path"_s, file},
                                                         {u"width"_s, r.value(u"width"_s)},
                                                         {u"height"_s, r.value(u"height"_s)},
                                                         {u"devicePixelRatio"_s, r.value(u"devicePixelRatio"_s)},
                                                         {u"device"_s, pairedName(s->device)}});
    }
    if (answer.ok)
        pending.reply.ok(answer.result);
    else
        pending.reply.fail(answer.code, answer.message, answer.details);
    return true;
}

// ---- The agent ----------------------------------------------------------------------------------------

void AcpHost::startAgent(Stream *s)
{
    const QStringList parts = QProcess::splitCommand(s->agent.command);
    auto *proc = new QProcess(this);
    s->process = proc;
    proc->setProgram(parts.value(0));
    proc->setArguments(parts.mid(1));
    proc->setWorkingDirectory(m_root);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    const QString id = s->id;
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, id, proc] {
        Stream *st = m_streams.value(id);
        if (!st || st->process != proc)
            return;
        QByteArray bytes = proc->readAllStandardOutput();
        qsizetype start = 0;
        for (;;) {
            const qsizetype end = bytes.indexOf('\n', start);
            if (end < 0) {
                if (!st->skipping)
                    st->partial += bytes.mid(start);
                if (st->partial.size() > acp::MaxLineBytes) {
                    st->partial.clear();
                    st->skipping = true;
                    say(u"%1: a line of the agent longer than %2 MiB dropped"_s.arg(id).arg(acp::MaxLineBytes >> 20));
                }
                break;
            }
            QByteArray line = st->partial + bytes.mid(start, end - start);
            st->partial.clear();
            start = end + 1;
            if (std::exchange(st->skipping, false))
                continue;
            if (!line.trimmed().isEmpty())
                fromAgent(st, line);
            if (!m_streams.contains(id) || st->closed)
                return;
        }
        if (!st->out.isEmpty())
            answerWaiter(st);
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, id, proc] {
        Stream *st = m_streams.value(id);
        if (!st || st->process != proc)
            return;
        const QString text = QString::fromUtf8(proc->readAllStandardError());
        st->errors = QString(st->errors + text).right(kMaxErrorChars);
        for (const QString &line : text.split(u'\n', Qt::SkipEmptyParts))
            say(u"%1 %2: %3"_s.arg(id, st->agent.name, line.left(500)));
    });
    connect(proc, &QProcess::errorOccurred, this, [this, id, proc](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        Stream *st = m_streams.value(id);
        if (st && st->process == proc)
            closeStream(st, u"it could not be started (%1)"_s.arg(proc->errorString()));
    });
    connect(proc, &QProcess::finished, this, [this, id, proc](int code, QProcess::ExitStatus exit) {
        Stream *st = m_streams.value(id);
        if (!st || st->process != proc)
            return;
        const QString line = lastLine(st->errors);
        const QString why = exit == QProcess::CrashExit ? u"it crashed"_s : u"it ended with code %1"_s.arg(code);
        closeStream(st, line.isEmpty() ? why : why + u": "_s + line);
    });
    say(u"%1: starting %2 in %3"_s.arg(id, s->agent.command, m_root));
    proc->start();
}

void AcpHost::fromAgent(Stream *s, const QByteArray &line)
{
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parse);
    if (!doc.isObject()) {
        say(u"%1: a line of the agent that is no JSON-RPC message dropped"_s.arg(s->id));
        return;
    }
    const QJsonObject o = doc.object();
    if (servedHere(o)) {
        serve(s, o);
        return;
    }
    const QString method = o.value(u"method"_s).toString();
    // The answer to a thread's session/new: its session works where the request said.
    if (method.isEmpty() && o.contains(u"id"_s) && !s->pendingCode.isEmpty()) {
        if (const auto it = s->pendingCode.constFind(idKey(o.value(u"id"_s))); it != s->pendingCode.cend()) {
            const QString session = o.value(u"result"_s).toObject().value(u"sessionId"_s).toString();
            if (!session.isEmpty() && session.size() <= 500) {
                s->codeSessions.insert(session, *it);
                s->codeOrder.removeAll(session);
                s->codeOrder.append(session);
            }
            s->pendingCode.erase(it);
        }
    }
    if (method == u"session/update") {
        turnUpdate(s, o.value(u"params"_s).toObject());
    } else if (method == u"session/request_permission" && o.contains(u"id"_s)) {
        if (s->retired && m_options.questionWaitMs <= 0) {
            // Nobody is there to answer.
            respond(s, o.value(u"id"_s), QJsonObject{{u"outcome"_s, QJsonObject{{u"outcome"_s, u"cancelled"_s}}}});
            return;
        }
        const QJsonObject params = o.value(u"params"_s).toObject();
        const QString session = params.value(u"sessionId"_s).toString();
        s->asked.append({idKey(o.value(u"id"_s)), o.value(u"id"_s), session, params, now()});
        questionsChanged(s, session);
        if (s->retired)
            return; // (it waits for the device with its turn: GET sessions/<id>/turn)
    } else if (method.isEmpty() && o.contains(u"id"_s) && s->prompts.remove(idKey(o.value(u"id"_s)))) {
        // A turn ended: what the agent changed goes to the other devices.
        turnEnded(s, idKey(o.value(u"id"_s)), o);
        if (m_sync)
            m_sync->syncNow();
        if (s->retired && s->prompts.isEmpty()) {
            closeStream(s, s->retiredWhy);
            removeLater(s);
            return;
        }
    }
    if (s->retired) {
        // Nobody fetches it; a request of the agent hears so at once instead of waiting forever.
        if (!method.isEmpty() && o.contains(u"id"_s))
            respondError(s, o.value(u"id"_s), -32603, u"The device is gone"_s);
        return;
    }
    const qint64 size = compact(o).size();
    s->out.append({s->nextSeq++, o});
    s->outSizes.append(size);
    s->outBytes += size;
    if (s->outBytes > m_options.maxBufferBytes)
        closeStream(s, u"the device fetched nothing for too long (%1 MiB waited for it)"_s.arg(m_options.maxBufferBytes
                                                                                               >> 20));
}

void AcpHost::toAgent(Stream *s, const QJsonObject &message)
{
    // The device's answer to a call of the app (never the agent's business).
    if (appAnswered(s, message))
        return;
    // What a session asks of the device's app, and the project a thread's session works in: taken
    // out, whatever the stream may do.
    QJsonObject own = message;
    const std::optional<acp::AppSession> appAsked = acp::takeAppSession(own);
    bool malformed = false;
    const std::optional<acp::CodeSession> code = acp::takeCodeSession(own, &malformed);
    if (code || malformed) {
        codeSessionRequest(s, own, code, appAsked);
        return;
    }
    const QString method = own.value(u"method"_s).toString();
    // (A session the device asks for again without a project is the notebook's now.)
    if (method == u"session/load" || method == u"session/resume")
        s->codeSessions.remove(own.value(u"params"_s).toObject().value(u"sessionId"_s).toString());
    const std::optional<QJsonObject> rewritten = rewriteForAgent(own, m_root, sessionServers(s, appAsked));
    if (!rewritten) {
        say(u"%1: a message of the device that is no JSON-RPC message dropped"_s.arg(s->id));
        return;
    }
    if (method.isEmpty() && rewritten->contains(u"id"_s)) {
        // The device's answer to a question of the agent.
        const QString key = idKey(rewritten->value(u"id"_s));
        if (const auto asked = std::ranges::find(s->asked, key, &Stream::Asked::key); asked != s->asked.cend()) {
            const QString session = asked->session;
            s->asked.erase(asked);
            questionsChanged(s, session);
        }
    }
    if (method == u"session/prompt" && rewritten->contains(u"id"_s)) {
        s->prompts.insert(idKey(rewritten->value(u"id"_s)));
        turnStarted(s, rewritten->value(u"params"_s).toObject(), idKey(rewritten->value(u"id"_s)));
        if (m_sync && !s->holding) {
            // The device's latest notes here before the agent reads them (a while at most).
            s->holding = true;
            if (!s->holdTimer) {
                s->holdTimer = new QTimer(this);
                s->holdTimer->setSingleShot(true);
                const QString id = s->id;
                connect(s->holdTimer, &QTimer::timeout, this, [this, id] {
                    if (Stream *found = m_streams.value(id)) {
                        say(u"%1: the turn starts before the sync ended"_s.arg(id));
                        releaseHeld(found);
                    }
                });
            }
            s->holdTimer->start(m_options.syncBeforeTurnMs);
            const QString id = s->id;
            m_sync->syncNow([this, id](bool) {
                if (Stream *found = m_streams.value(id))
                    releaseHeld(found);
            });
        }
        // A thread's turn: its checkpoint first.
        const QString session = rewritten->value(u"params"_s).toObject().value(u"sessionId"_s).toString();
        if (s->codeSessions.contains(session)) {
            checkpointThenPrompt(s, *rewritten, session);
            return;
        }
    }
    deliverInOrder(s, *rewritten);
}

void AcpHost::deliverInOrder(Stream *s, const QJsonObject &message)
{
    if (s->holding || !s->held.isEmpty()) {
        s->held.append({message, 0});
        return;
    }
    deliver(s, message);
}

void AcpHost::flushHeld(Stream *s)
{
    if (s->holding)
        return;
    while (!s->held.isEmpty() && s->held.constFirst().ticket == 0) {
        const QJsonObject m = s->held.takeFirst().message;
        if (!m.isEmpty())
            deliver(s, m);
    }
}

void AcpHost::releaseHeld(Stream *s)
{
    if (!s->holding)
        return;
    s->holding = false;
    if (s->holdTimer)
        s->holdTimer->stop();
    flushHeld(s);
}

// ---- Code mode's threads (acp/codehost.h) ----------------------------------------------------------------

gitcli::Client *AcpHost::git()
{
    if (!m_git) {
        m_git = std::make_unique<gitcli::Client>();
        if (!m_options.gitProgram.isEmpty())
            m_git->setGitProgram(m_options.gitProgram);
    }
    return m_git.get();
}

void AcpHost::codePlace(const acp::CodeSession &code, std::function<void(const CodePlace &, const QString &)> done)
{
    const std::optional<acp::HostProject> project = m_projects ? m_projects->find(code.project) : std::nullopt;
    if (!project) {
        done({}, u"This computer has no project %1"_s.arg(code.project));
        return;
    }
    const QString projectFolder = QFileInfo(project->path).canonicalFilePath();
    if (projectFolder.isEmpty() || !QFileInfo(projectFolder).isDir()) {
        done({}, u"The folder of the project %1 is not there on this computer"_s.arg(project->name));
        return;
    }
    CodePlace place{project->id, projectFolder, QString()};
    if (code.worktree.isEmpty()) {
        done(place, {});
        return;
    }
    // A worktree of the project: as it really is (no "..", no link out), under the project's
    // worktrees root, or one git lists among the project's.
    const QString folder = QFileInfo(code.worktree).canonicalFilePath();
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        done({}, u"The worktree %1 is not there on this computer"_s.arg(code.worktree));
        return;
    }
    place.folder = folder;
    place.branch = code.branch;
    if (folder == projectFolder) {
        done(place, {});
        return;
    }
    const QString root = QFileInfo(project->worktreesRoot.isEmpty() ? acp::defaultWorktreesRoot() : project->worktreesRoot)
                                 .canonicalFilePath();
    if (!root.isEmpty() && folder.startsWith(root + u'/' + project->id + u'/')) {
        done(place, {});
        return;
    }
    const QString worktree = code.worktree;
    git()->worktrees(projectFolder, [done, place, worktree](const gitcli::WorktreeList &list) {
        for (const gitcli::Worktree &w : list.worktrees) {
            if (!w.bare && QFileInfo(w.path).canonicalFilePath() == place.folder) {
                done(place, {});
                return;
            }
        }
        done({}, u"%1 is no worktree of the project"_s.arg(worktree));
    });
}

void AcpHost::codeSessionRequest(Stream *s, const QJsonObject &message, const std::optional<acp::CodeSession> &code,
                                 const std::optional<acp::AppSession> &appAsked)
{
    const QJsonValue id = message.value(u"id"_s);
    const QString method = message.value(u"method"_s).toString();
    const QString streamId = s->id;
    const auto refuse = [this, streamId, id, method](const QString &why) {
        Stream *st = m_streams.value(streamId);
        if (!st || st->closed)
            return;
        say(u"%1: a thread's %2 refused: %3"_s.arg(streamId, method, why));
        // (Answered here, as the agent would: the device's turn fails with why.)
        if (!id.isUndefined() && !id.isNull())
            toDevice(st, QJsonObject{{u"jsonrpc"_s, u"2.0"_s},
                                     {u"id"_s, id},
                                     {u"error"_s, QJsonObject{{u"code"_s, acp::InvalidParams}, {u"message"_s, why}}}});
    };
    if (!code) {
        refuse(u"The request's project is none (brulion/code)"_s);
        return;
    }
    if (!m_projects) {
        refuse(u"This computer runs no threads of Code mode (acp serve without --projects)"_s);
        return;
    }
    // Held until its place is known (git may have to say), so what the device sends after it waits.
    const int ticket = m_nextTicket++;
    s->held.append({QJsonObject(), ticket});
    const acp::CodeSession asked = *code;
    codePlace(asked, [this, streamId, ticket, message, appAsked, refuse](const CodePlace &place, const QString &error) {
        Stream *st = m_streams.value(streamId);
        if (!st || st->closed)
            return;
        const auto held =
                std::ranges::find_if(st->held, [ticket](const Stream::Held &h) { return h.ticket == ticket; });
        if (held == st->held.end())
            return; // (the stream let them go meanwhile)
        if (!error.isEmpty()) {
            st->held.erase(held);
            refuse(error);
            flushHeld(st);
            return;
        }
        const std::optional<QJsonObject> rewritten =
                rewriteForAgent(message, place.folder, sessionServers(st, appAsked, true));
        held->ticket = 0;
        held->message = rewritten.value_or(QJsonObject());
        const QString method = message.value(u"method"_s).toString();
        const QString session = message.value(u"params"_s).toObject().value(u"sessionId"_s).toString();
        if (method == u"session/new") {
            st->pendingCode.insert(idKey(message.value(u"id"_s)), place);
        } else if (!session.isEmpty() && session.size() <= 500) {
            st->codeSessions.insert(session, place);
            st->codeOrder.removeAll(session);
            st->codeOrder.append(session);
        }
        // (A stream's sessions come and go: the oldest threads' are forgotten.)
        while (st->codeOrder.size() > 256) {
            const QString old = st->codeOrder.takeFirst();
            st->codeSessions.remove(old);
            st->checkpoints.remove(old);
        }
        while (st->pendingCode.size() > 64)
            st->pendingCode.erase(st->pendingCode.begin());
        say(u"%1: a session of the project %2 in %3"_s.arg(st->id, place.project, place.folder));
        flushHeld(st);
    });
}

void AcpHost::checkpointThenPrompt(Stream *s, const QJsonObject &message, const QString &session)
{
    const CodePlace place = s->codeSessions.value(session);
    const int ticket = m_nextTicket++;
    s->held.append({message, ticket});
    const QString turn = message.value(u"params"_s).toObject().value(u"_meta"_s).toObject().value(acp::TurnMetaKey)
                                 .toString()
                                 .left(200);
    const QString id = gitcli::Client::newCheckpointId();
    const QString streamId = s->id;
    const QString device = s->device;
    const QString prompt = idKey(message.value(u"id"_s));
    git()->checkpoint(place.folder, id, [this, streamId, device, ticket, session, turn, id, place, prompt](
                                                const gitcli::Outcome &outcome) {
        Stream *st = m_streams.value(streamId);
        if (!st || st->closed)
            return;
        const auto held =
                std::ranges::find_if(st->held, [ticket](const Stream::Held &h) { return h.ticket == ticket; });
        if (held == st->held.end())
            return;
        if (outcome.ok) {
            const acp::RemoteCheckpoint c{session, turn, id, timeText(now())};
            // The turn kept knows it (a device that lost the stream hears it there) …
            const auto kept = m_turns.find(turnKey(device, session));
            if (kept != m_turns.end() && kept->prompt == prompt && kept->stream == streamId) {
                kept->checkpoint = id;
                kept->checkpointAt = c.at;
            }
            // … and the device is told before the agent hears the prompt.
            if (!st->retired)
                toDevice(st, acp::checkpointNotification(c));
            QStringList &list = st->checkpoints[session];
            list.append(id);
            QStringList dropped;
            while (list.size() > std::max(1, m_options.maxCheckpoints))
                dropped.append(list.takeFirst());
            if (!dropped.isEmpty())
                git()->dropCheckpoints(place.folder, dropped, [](const gitcli::Outcome &) { });
            say(u"%1: checkpoint %2 of %3 before a turn"_s.arg(streamId, id, place.folder));
        } else {
            // (No git there, a folder git refuses: the turn goes on without one.)
            say(u"%1: no checkpoint of %2: %3"_s.arg(streamId, place.folder, outcome.message.left(300)));
        }
        held->ticket = 0;
        flushHeld(st);
    });
}

AcpHost::CodePlace AcpHost::codeSessionOf(const Stream *s, const QJsonObject &params) const
{
    return s->codeSessions.value(params.value(u"sessionId"_s).toString());
}

void AcpHost::requestPlace(const QString &project, const QString &worktree,
                           std::function<void(const CodePlace &, const http::Response &)> done)
{
    if (!m_projects) {
        done({}, apiError(404, u"no_projects"_s, u"This computer offers no projects (acp serve without --projects)"_s));
        return;
    }
    if (!m_projects->find(project)) {
        done({}, apiError(404, u"unknown_project"_s, u"This computer has no project %1"_s.arg(project.left(100))));
        return;
    }
    const std::optional<acp::CodeSession> code =
            acp::CodeSession::fromJson(QJsonObject{{u"project"_s, project}, {u"worktree"_s, worktree}});
    if (!code) {
        done({}, apiError(400, u"bad_worktree"_s, u"The worktree is no absolute folder"_s));
        return;
    }
    codePlace(*code, [done](const CodePlace &place, const QString &error) {
        done(place, error.isEmpty() ? http::Response() : apiError(403, u"not_a_worktree"_s, error));
    });
}

void AcpHost::projectsRequest(const QStringList &parts, const http::Request &request, const http::Reply &reply)
{
    if (parts.size() == 1) {
        reply.send(projectList());
        return;
    }
    const QString project = parts.at(1);
    if (parts.at(2) == u"worktrees")
        createWorktree(project, request, reply);
    else if (parts.at(2) == u"changes")
        changes(project, request, reply);
    else
        diff(project, request, reply);
}

http::Response AcpHost::projectList()
{
    if (!m_projects)
        return apiError(404, u"no_projects"_s, u"This computer offers no projects (acp serve without --projects)"_s);
    QJsonArray list;
    for (const acp::HostProject &p : m_projects->projects())
        list.append(QJsonObject{{u"id"_s, p.id}, {u"name"_s, p.name}, {u"path"_s, p.path}});
    return http::Response::json(200, QJsonObject{{u"projects"_s, list}});
}

void AcpHost::createWorktree(const QString &project, const http::Request &request, const http::Reply &reply)
{
    const QJsonDocument doc = QJsonDocument::fromJson(request.body);
    const QJsonObject body = doc.object();
    const QString title = body.value(u"title"_s).toString().simplified();
    const QString from = body.value(u"base"_s).toString().trimmed();
    if (!doc.isObject() || title.isEmpty() || title.size() > acp::MaxWorktreeTitleChars) {
        reply.send(apiError(400, u"bad_title"_s, u"A title of 1 to %1 characters is needed"_s.arg(acp::MaxWorktreeTitleChars)));
        return;
    }
    if (!from.isEmpty() && !gitcli::isValidBranchName(from)) {
        reply.send(apiError(400, u"bad_base"_s, u"That is no branch"_s));
        return;
    }
    const std::optional<acp::HostProject> p = m_projects ? m_projects->find(project) : std::nullopt;
    if (!p) {
        reply.send(m_projects ? apiError(404, u"unknown_project"_s, u"This computer has no project %1"_s.arg(project.left(100)))
                              : apiError(404, u"no_projects"_s,
                                         u"This computer offers no projects (acp serve without --projects)"_s));
        return;
    }
    const QString folder = QFileInfo(p->path).canonicalFilePath();
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        reply.send(apiError(409, u"no_folder"_s, u"The folder of the project is not there on this computer"_s));
        return;
    }
    // The name the app gives one (CodeProjects::worktreeFor): a free folder of the project's root.
    const acp::WorktreePlace place = acp::worktreePlace(p->worktreesRoot, p->id, title,
                                                        [](const QString &f) { return QFileInfo::exists(f); });
    if (!QDir().mkpath(QFileInfo(place.folder).absolutePath())) {
        reply.send(apiError(500, u"cannot_write"_s, u"The worktrees' folder cannot be made"_s));
        return;
    }
    say(u"a worktree of %1 for a thread: %2 on %3"_s.arg(p->id, place.folder, place.branch));
    QPointer<AcpHost> self(this);
    git()->addWorktree(folder, place.folder, place.branch, from.isEmpty() ? u"HEAD"_s : from,
                       [self, reply, place](const gitcli::Outcome &outcome) {
                           if (!self)
                               return;
                           if (!outcome.ok) {
                               self->say(u"worktree not made: %1"_s.arg(outcome.message.left(300)));
                               reply.send(apiError(409, u"git_failed"_s, outcome.message.left(1000)));
                               return;
                           }
                           reply.send(http::Response::json(
                                   201, QJsonObject{{u"folder"_s, place.folder}, {u"branch"_s, place.branch}}));
                       });
}

void AcpHost::changes(const QString &project, const http::Request &request, const http::Reply &reply)
{
    const QUrlQuery query(QString::fromLatin1(request.query));
    const QString since = query.queryItemValue(u"since"_s, QUrl::FullyDecoded);
    if (!since.isEmpty() && !gitcli::isValidCheckpointId(since)) {
        reply.send(apiError(400, u"bad_checkpoint"_s, u"That is no checkpoint"_s));
        return;
    }
    QPointer<AcpHost> self(this);
    requestPlace(project, query.queryItemValue(u"worktree"_s, QUrl::FullyDecoded),
                 [self, reply, since](const CodePlace &place, const http::Response &refusal) {
                     if (!self)
                         return;
                     if (place.folder.isEmpty()) {
                         reply.send(refusal);
                         return;
                     }
                     const auto answer = [reply](bool ok, const QString &error, const QList<gitcli::FileChange> &all) {
                         if (!ok) {
                             reply.send(apiError(409, u"git_failed"_s, error.left(1000)));
                             return;
                         }
                         QJsonArray list;
                         for (const gitcli::FileChange &c : all) {
                             if (list.size() >= acp::MaxRemoteChanges)
                                 break;
                             QJsonObject o{{u"path"_s, c.path},
                                           {u"change"_s, gitcli::changeName(c.change)},
                                           {u"additions"_s, c.additions},
                                           {u"deletions"_s, c.deletions},
                                           {u"binary"_s, c.binary}};
                             if (!c.oldPath.isEmpty())
                                 o.insert(u"oldPath"_s, c.oldPath);
                             list.append(o);
                         }
                         reply.send(http::Response::json(
                                 200, QJsonObject{{u"changes"_s, list},
                                                  {u"truncated"_s, all.size() > acp::MaxRemoteChanges}}));
                     };
                     if (since.isEmpty())
                         self->git()->status(place.folder, [answer](const gitcli::StatusReport &r) {
                             answer(r.ok, r.error, r.changes);
                         });
                     else
                         self->git()->changesSince(place.folder, since, [answer](const gitcli::ChangesReport &r) {
                             answer(r.ok, r.error, r.changes);
                         });
                 });
}

void AcpHost::diff(const QString &project, const http::Request &request, const http::Reply &reply)
{
    const QUrlQuery query(QString::fromLatin1(request.query));
    const QString since = query.queryItemValue(u"since"_s, QUrl::FullyDecoded);
    const QString path = query.queryItemValue(u"path"_s, QUrl::FullyDecoded);
    const QString oldPath = query.queryItemValue(u"oldPath"_s, QUrl::FullyDecoded);
    if (!since.isEmpty() && !gitcli::isValidCheckpointId(since)) {
        reply.send(apiError(400, u"bad_checkpoint"_s, u"That is no checkpoint"_s));
        return;
    }
    // (A path of the work tree as git writes it: nothing outside it, nothing of .git.)
    if (!gitcli::isSafeRelativePath(path) || (!oldPath.isEmpty() && !gitcli::isSafeRelativePath(oldPath))) {
        reply.send(apiError(400, u"bad_path"_s, u"That is no path of the project"_s));
        return;
    }
    QPointer<AcpHost> self(this);
    requestPlace(project, query.queryItemValue(u"worktree"_s, QUrl::FullyDecoded),
                 [self, reply, since, path, oldPath](const CodePlace &place, const http::Response &refusal) {
                     if (!self)
                         return;
                     if (place.folder.isEmpty()) {
                         reply.send(refusal);
                         return;
                     }
                     // (A folder of the path that leads out of the project — a link git never names
                     // a file under: the file is read from disk.)
                     const QString dir = QFileInfo(path).path();
                     if (codeFilePath(place.folder, dir == u"." ? place.folder : dir, false).isEmpty()) {
                         reply.send(apiError(400, u"bad_path"_s, u"That is no path of the project"_s));
                         return;
                     }
                     gitcli::FileTextsRequest r;
                     r.path = path;
                     r.oldPath = oldPath;
                     r.base = since.isEmpty() ? u"HEAD"_s : gitcli::Client::checkpointRef(since);
                     self->git()->fileTexts(place.folder, r, [reply](const gitcli::FileTexts &t) {
                         if (!t.ok) {
                             reply.send(apiError(409, u"git_failed"_s, t.error.left(1000)));
                             return;
                         }
                         reply.send(http::Response::json(200, QJsonObject{{u"path"_s, t.path},
                                                                          {u"oldText"_s, t.oldText},
                                                                          {u"newText"_s, t.newText},
                                                                          {u"oldExists"_s, t.oldExists},
                                                                          {u"newExists"_s, t.newExists},
                                                                          {u"binary"_s, t.binary},
                                                                          {u"tooLarge"_s, t.tooLarge}}));
                     });
                 });
}

void AcpHost::deliver(Stream *s, const QJsonObject &message)
{
    if (s->process && s->process->state() != QProcess::NotRunning)
        s->process->write(compact(message) + '\n');
}

void AcpHost::respond(Stream *s, const QJsonValue &id, const QJsonValue &result)
{
    deliver(s, QJsonObject{{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, id}, {u"result"_s, result}});
}

void AcpHost::respondError(Stream *s, const QJsonValue &id, int code, const QString &message)
{
    deliver(s, QJsonObject{{u"jsonrpc"_s, u"2.0"_s},
                           {u"id"_s, id},
                           {u"error"_s, QJsonObject{{u"code"_s, code}, {u"message"_s, message}}}});
}

void AcpHost::serve(Stream *s, const QJsonObject &request)
{
    const QJsonValue id = request.value(u"id"_s);
    const QString method = request.value(u"method"_s).toString();
    const QJsonObject params = request.value(u"params"_s).toObject();
    if (method == u"fs/read_text_file")
        readFile(s, id, params);
    else if (method == u"fs/write_text_file")
        writeFile(s, id, params);
    else if (method.startsWith(u"terminal/"_s))
        terminalRequest(s, id, method, params);
    else
        respondError(s, id, acp::MethodNotFound, u"Method not found: %1"_s.arg(method));
}

QString AcpHost::notebookPath(const QString &file) const
{
    if (m_root.isEmpty() || file.isEmpty() || file.contains(QChar(u'\0')))
        return {};
    const QString full = QDir::cleanPath(QDir(m_root).absoluteFilePath(file));
    if (!full.startsWith(m_root + u'/'))
        return {};
    // (A link out of the notebook is outside it too.)
    const QString canonicalRoot = QFileInfo(m_root).canonicalFilePath();
    QFileInfo info(full);
    while (!info.exists() && info.absolutePath() != info.absoluteFilePath())
        info = QFileInfo(info.absolutePath());
    const QString real = info.canonicalFilePath();
    if (!canonicalRoot.isEmpty() && !real.isEmpty() && real != canonicalRoot && !real.startsWith(canonicalRoot + u'/'))
        return {};
    const VaultPath path = paths::normalize(full.mid(m_root.size() + 1));
    const QString first = path.section(u'/', 0, 0);
    if (path.isEmpty() || first == u".brulion" || first == vaultTrashFolder() || paths::isIgnored(path))
        return {};
    return path;
}

void AcpHost::readFile(Stream *s, const QJsonValue &id, const QJsonObject &params)
{
    if (const CodePlace place = codeSessionOf(s, params); !place.folder.isEmpty()) {
        readCodeFile(s, id, place, params);
        return;
    }
    const QString path = notebookPath(params.value(u"path"_s).toString());
    if (path.isEmpty()) {
        respondError(s, id, acp::InvalidParams, u"Only files of the notebook can be read: %1"_s.arg(m_root));
        return;
    }
    const notebook::Notebook::Current current = m_notebook->read(path);
    if (!current.exists) {
        respondError(s, id, acp::ResourceNotFound, u"No such file: %1"_s.arg(path));
        return;
    }
    respond(s, id,
            QJsonObject{{u"content"_s, acp::textLines(current.text, params.value(u"line"_s).toInt(),
                                                      params.value(u"limit"_s).toInt())}});
}

void AcpHost::writeFile(Stream *s, const QJsonValue &id, const QJsonObject &params)
{
    if (const CodePlace place = codeSessionOf(s, params); !place.folder.isEmpty()) {
        writeCodeFile(s, id, place, params);
        return;
    }
    const QString path = notebookPath(params.value(u"path"_s).toString());
    if (path.isEmpty()) {
        respondError(s, id, acp::InvalidParams, u"Only files of the notebook can be written: %1"_s.arg(m_root));
        return;
    }
    if (markdown::isEncryptedNotePath(path)) {
        respondError(s, id, acp::InvalidParams, u"%1 is an encrypted page: it is changed only in Brulion"_s.arg(path));
        return;
    }
    QString lockError;
    const std::unique_ptr<QLockFile> lock = m_notebook->lock(kLockTimeoutMs, &lockError);
    if (!lock) {
        respondError(s, id, -32603, u"%1 could not be written: %2"_s.arg(path, lockError));
        return;
    }
    const QString content = params.value(u"content"_s).toString();
    notebook::Notebook::WriteOptions options;
    options.mayCreate = true;
    const notebook::Notebook::WriteResult result = m_notebook->edit(
            path,
            [&content](const QString &, bool, QString *next) {
                *next = content;
                return true;
            },
            options);
    if (!result.ok()) {
        respondError(
                s, id, -32603,
                u"%1 could not be written%2"_s.arg(path, result.error.isEmpty() ? QString() : u": "_s + result.error));
        return;
    }
    if (result.status == notebook::Notebook::WriteResult::Written) {
        say(u"%1 wrote %2"_s.arg(s->id, path));
        if (m_sync)
            m_sync->changed();
    }
    respond(s, id, QJsonValue());
}

void AcpHost::readCodeFile(Stream *s, const QJsonValue &id, const CodePlace &place, const QJsonObject &params)
{
    // A thread's files: those of its folder alone.
    const QString file = codeFilePath(place.folder, params.value(u"path"_s).toString(), false);
    if (file.isEmpty()) {
        respondError(s, id, acp::InvalidParams, u"Only files of the project's folder can be read: %1"_s.arg(place.folder));
        return;
    }
    QFile f(file);
    if (!QFileInfo(file).isFile() || !f.open(QIODevice::ReadOnly)) {
        respondError(s, id, acp::ResourceNotFound, u"No such file: %1"_s.arg(file));
        return;
    }
    if (f.size() > kMaxCodeFileBytes) {
        respondError(s, id, acp::InvalidParams, u"%1 is too big to read so (%2 MiB at most)"_s.arg(file).arg(kMaxCodeFileBytes >> 20));
        return;
    }
    respond(s, id,
            QJsonObject{{u"content"_s, acp::textLines(QString::fromUtf8(f.readAll()), params.value(u"line"_s).toInt(),
                                                      params.value(u"limit"_s).toInt())}});
}

void AcpHost::writeCodeFile(Stream *s, const QJsonValue &id, const CodePlace &place, const QJsonObject &params)
{
    const QString file = codeFilePath(place.folder, params.value(u"path"_s).toString(), true);
    if (file.isEmpty()) {
        respondError(s, id, acp::InvalidParams,
                     u"Only files of the project's folder (not of its .git) can be written: %1"_s.arg(place.folder));
        return;
    }
    const QByteArray bytes = params.value(u"content"_s).toString().toUtf8();
    QSaveFile f(file);
    if (!QDir().mkpath(QFileInfo(file).absolutePath()) || !f.open(QIODevice::WriteOnly)
        || f.write(bytes) != bytes.size() || !f.commit()) {
        respondError(s, id, -32603, u"%1 could not be written"_s.arg(file));
        return;
    }
    say(u"%1 wrote %2"_s.arg(s->id, file));
    respond(s, id, QJsonValue());
}

void AcpHost::terminalRequest(Stream *s, const QJsonValue &id, const QString &method, const QJsonObject &params)
{
    if (method == u"terminal/create") {
        const QString command = params.value(u"command"_s).toString();
        if (command.isEmpty() || s->terminals.size() >= kMaxTerminals) {
            respondError(s, id, acp::InvalidParams, command.isEmpty() ? u"No command"_s : u"Too many terminals"_s);
            return;
        }
        // A thread's commands run in its folder (or a folder inside it).
        const CodePlace place = codeSessionOf(s, params);
        QString home = m_root;
        if (!place.folder.isEmpty()) {
            const QString cwd = params.value(u"cwd"_s).toString();
            home = cwd.isEmpty() ? place.folder : codeFilePath(place.folder, cwd, false);
            if (home.isEmpty() || !QFileInfo(home).isDir()) {
                respondError(s, id, acp::InvalidParams,
                             u"A command runs in the project's folder or a folder inside it: %1"_s.arg(place.folder));
                return;
            }
        }
        QStringList args;
        for (const QJsonValue &a : params.value(u"args"_s).toArray())
            args << a.toString();
        auto *t = new Terminal;
        t->limit = std::max<qint64>(0, static_cast<qint64>(params.value(u"outputByteLimit"_s).toDouble()));
        if (t->limit == 0)
            t->limit = kDefaultOutputLimit;
        t->process = new QProcess(this);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const QJsonValue &v : params.value(u"env"_s).toArray())
            env.insert(v.toObject().value(u"name"_s).toString(), v.toObject().value(u"value"_s).toString());
        t->process->setProcessEnvironment(env);
        const QString cwd = params.value(u"cwd"_s).toString();
        t->process->setWorkingDirectory(!place.folder.isEmpty()                 ? home
                                        : cwd.isEmpty() || !QDir(cwd).exists() ? m_root
                                                                                : cwd);
        t->process->setProcessChannelMode(QProcess::MergedChannels);
        t->process->setProgram(command);
        t->process->setArguments(args);
        const QString terminalId = u"term-%1"_s.arg(m_nextTerminal++);
        s->terminals.insert(terminalId, t);
        const QString streamId = s->id;
        connect(t->process, &QProcess::readyReadStandardOutput, this, [t] {
            bool cut = false;
            t->output = acp::keepLast(t->output + t->process->readAllStandardOutput(), t->limit, &cut);
            t->truncated = t->truncated || cut;
        });
        const auto ended = [this, t, streamId](const QJsonValue &code, const QString &signal) {
            if (t->ended)
                return;
            t->ended = true;
            t->exitCode = code;
            t->signal = signal;
            Stream *owner = m_streams.value(streamId);
            for (const QJsonValue &waiter : std::exchange(t->waiting, {})) {
                if (owner)
                    respond(owner, waiter,
                            QJsonObject{{u"exitCode"_s, code},
                                        {u"signal"_s, signal.isEmpty() ? QJsonValue() : QJsonValue(signal)}});
            }
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
        say(u"%1 %2 runs %3"_s.arg(s->id, s->agent.name, (QStringList{command} + args).join(u' ').left(300)));
        t->process->start();
        respond(s, id, QJsonObject{{u"terminalId"_s, terminalId}});
        return;
    }
    const QString terminalId = params.value(u"terminalId"_s).toString();
    Terminal *t = s->terminals.value(terminalId);
    if (!t) {
        respondError(s, id, acp::InvalidParams, u"No such terminal: %1"_s.arg(terminalId));
        return;
    }
    const QJsonValue exit =
            t->ended
                    ? QJsonValue(QJsonObject{{u"exitCode"_s, t->exitCode},
                                             {u"signal"_s, t->signal.isEmpty() ? QJsonValue() : QJsonValue(t->signal)}})
                    : QJsonValue();
    if (method == u"terminal/output") {
        respond(s, id,
                QJsonObject{{u"output"_s, QString::fromUtf8(t->output)},
                            {u"truncated"_s, t->truncated},
                            {u"exitStatus"_s, exit}});
    } else if (method == u"terminal/wait_for_exit") {
        if (t->ended)
            respond(s, id, exit);
        else
            t->waiting << id;
    } else if (method == u"terminal/kill") {
        if (t->process->state() != QProcess::NotRunning)
            t->process->kill();
        respond(s, id, QJsonValue());
    } else if (method == u"terminal/release") {
        s->terminals.remove(terminalId);
        t->process->disconnect(this);
        if (t->process->state() != QProcess::NotRunning) {
            connect(t->process, &QProcess::finished, t->process, &QObject::deleteLater);
            t->process->kill();
        } else {
            t->process->deleteLater();
        }
        for (const QJsonValue &waiter : std::as_const(t->waiting))
            respond(s, waiter, QJsonObject{{u"exitCode"_s, QJsonValue()}, {u"signal"_s, u"SIGKILL"_s}});
        delete t;
        respond(s, id, QJsonValue());
    } else {
        respondError(s, id, acp::MethodNotFound, u"Method not found: %1"_s.arg(method));
    }
}

void AcpHost::endTerminals(Stream *s)
{
    for (Terminal *t : std::as_const(s->terminals)) {
        t->process->disconnect(this);
        if (t->process->state() != QProcess::NotRunning) {
            connect(t->process, &QProcess::finished, t->process, &QObject::deleteLater);
            t->process->kill();
        } else {
            t->process->deleteLater();
        }
        delete t;
    }
    s->terminals.clear();
}

// ---- The commands ---------------------------------------------------------------------------------------

namespace {

QString hostStateFile(Context &context, const QJsonObject &args, std::optional<Error> *error)
{
    const QString given = args.value(u"stateFile"_s).toString().trimmed();
    if (!given.isEmpty()) {
        const QString expanded = given.startsWith(u"~/") ? QDir::homePath() + given.mid(1) : given;
        return QDir::cleanPath(QFileInfo(expanded).absoluteFilePath());
    }
    if (auto e = context.resolveVault()) {
        *error = e;
        return {};
    }
    const QString state = context.stateDir();
    if (state.isEmpty()) {
        *error = Error{
                ExitUsage, u"state_file_required"_s,
                QCoreApplication::translate(
                        "brulion::cli", "Without device state (--no-history) give the state file with --state-file")};
        return {};
    }
    return QDir(state).filePath(QString::fromLatin1(kStateFileName));
}

Result acpServeCommand(Call &call)
{
    // run() starts the server itself (it serves until stopped).
    return call.fail(
            ExitUsage, u"not_a_tool"_s,
            QCoreApplication::translate("brulion::cli", "acp serve starts the server; it is no tool of its own"));
}

Result acpPair(Call &call)
{
    std::optional<Error> error;
    const QString file = hostStateFile(call.ctx, call.args, &error);
    if (error)
        return failWith(*error);
    const QString code = newPairingCode();
    const QDateTime expires = QDateTime::currentDateTimeUtc().addSecs(kPairingMinutes * 60);
    if (call.dryRun())
        return call.ok(QJsonObject{{u"stateFile"_s, file}, {u"dryRun"_s, true}});
    HostStateStore store(file);
    QString writeError;
    if (!store.update(
                [&](HostState &s) {
                    s.pairingHash = secretHash(code);
                    s.pairingExpires = expires;
                    s.pairingAttempts = 0;
                    return true;
                },
                &writeError))
        return call.fail(ExitUnavailable, u"cannot_write"_s,
                         QCoreApplication::translate("brulion::cli", "Cannot write %1: %2").arg(file, writeError),
                         {{u"path"_s, file}});
    const QString shown = formatPairingCode(code);
    const QString url = store.state().publicUrl;
    const QString link = pairingLink(url, shown);
    QString text;
    if (!link.isEmpty()) {
        text += terminalQrCode(link);
        text += QCoreApplication::translate("brulion::cli",
                                            "Scan it with the phone's camera (Brulion opens the pairing, filled in), "
                                            "or open: %1")
                        .arg(link)
                + u"\n\n"_s;
    }
    text += QCoreApplication::translate("brulion::cli", "Pairing code: %1 (valid for %2 minutes, once)")
                    .arg(shown)
                    .arg(kPairingMinutes)
            + u'\n';
    if (!url.isEmpty())
        text += QCoreApplication::translate("brulion::cli", "Address: %1").arg(url) + u'\n';
    text += QCoreApplication::translate("brulion::cli",
                                        "In Brulion on the device: Settings → Extensions → AI agents → Add → "
                                        "An agent on another computer.")
            + u'\n';
    return call.ok(QJsonObject{{u"stateFile"_s, file},
                               {u"code"_s, shown},
                               {u"expires"_s, timeText(expires)},
                               {u"url"_s, url},
                               {u"link"_s, link}},
                   text);
}

Result acpClients(Call &call)
{
    std::optional<Error> error;
    const QString file = hostStateFile(call.ctx, call.args, &error);
    if (error)
        return failWith(*error);
    HostStateStore store(file);
    QString readError;
    if (!store.refresh(&readError))
        return call.fail(ExitUnavailable, u"cannot_read"_s,
                         QCoreApplication::translate("brulion::cli", "Cannot read %1: %2").arg(file, readError),
                         {{u"path"_s, file}});
    QJsonArray devices;
    QString text;
    for (const PairedDevice &d : store.state().devices) {
        devices.append(QJsonObject{{u"id"_s, d.id},
                                   {u"name"_s, d.name},
                                   {u"paired"_s, timeText(d.created)},
                                   {u"lastUsed"_s, timeText(d.lastUsed)}});
        text += u"%1  %2  %3  %4\n"_s.arg(
                d.id, d.name,
                QCoreApplication::translate("brulion::cli", "paired %1")
                        .arg(QLocale().toString(d.created.toLocalTime(), QLocale::ShortFormat)),
                QCoreApplication::translate("brulion::cli", "last used %1")
                        .arg(QLocale().toString(d.lastUsed.toLocalTime(), QLocale::ShortFormat)));
    }
    if (devices.isEmpty())
        text = QCoreApplication::translate("brulion::cli", "No devices are paired.") + u'\n';
    const HostState &s = store.state();
    const bool waiting = !s.pairingHash.isEmpty() && s.pairingExpires > QDateTime::currentDateTimeUtc();
    return call.ok(QJsonObject{{u"stateFile"_s, file},
                               {u"devices"_s, devices},
                               {u"pairingCodeValid"_s, waiting},
                               {u"url"_s, s.publicUrl}},
                   text);
}

Result acpRevoke(Call &call)
{
    std::optional<Error> error;
    const QString file = hostStateFile(call.ctx, call.args, &error);
    if (error)
        return failWith(*error);
    const QString target = call.str(u"device"_s).trimmed();
    const bool all = call.flag(u"all"_s);
    if (target.isEmpty() == !all)
        return call.fail(ExitUsage, u"revoke_what"_s,
                         QCoreApplication::translate("brulion::cli", "Name a device (acp clients), or give --all"));
    HostStateStore store(file);
    QStringList revoked;
    QString writeError;
    const bool written = store.update(
            [&](HostState &s) {
                s.devices.removeIf([&](const PairedDevice &d) {
                    const bool match = all || d.id == target;
                    if (match)
                        revoked.append(d.id);
                    return match;
                });
                if (all) {
                    s.pairingHash.clear(); // (a code shown meanwhile is no use either)
                    return true;
                }
                return !revoked.isEmpty();
            },
            &writeError);
    if (!written)
        return call.fail(ExitUnavailable, u"cannot_write"_s,
                         QCoreApplication::translate("brulion::cli", "Cannot write %1: %2").arg(file, writeError),
                         {{u"path"_s, file}});
    if (!all && revoked.isEmpty())
        return call.fail(ExitNotFound, u"not_found"_s,
                         QCoreApplication::translate("brulion::cli", "No device %1").arg(target),
                         {{u"device"_s, target}});
    return call.ok(QJsonObject{{u"stateFile"_s, file}, {u"revoked"_s, QJsonArray::fromStringList(revoked)}},
                   QCoreApplication::translate("brulion::cli", "Devices unpaired: %1").arg(revoked.size()) + u'\n');
}

Command make(const QString &id, const char *summary, Handler run, QList<Arg> args, const char *help)
{
    Command c;
    c.id = id;
    c.summary = cliText(summary);
    c.help = cliText(help);
    c.run = std::move(run);
    c.args = std::move(args);
    c.needsVault = false; // the notebook gives the state file's default place only
    c.mcp = false;
    return c;
}

Arg stateFileOption()
{
    return option(u"stateFile"_s, ArgType::String,
                  QT_TRANSLATE_NOOP("brulion::cli",
                                    "The server's state file (paired devices, the pairing code); default: "
                                    "acp-remote.json in the notebook's device state"));
}

QStringList listArgument(const QJsonObject &args, const QString &name)
{
    QStringList out;
    for (const QString &part : args.value(name).toString().split(u',', Qt::SkipEmptyParts)) {
        if (const QString t = part.trimmed(); !t.isEmpty())
            out.append(t);
    }
    return out;
}

QString readSecretFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return {};
    }
    QString secret = QString::fromUtf8(file.read(4096));
    while (secret.endsWith(u'\n') || secret.endsWith(u'\r'))
        secret.chop(1);
    return secret;
}

/// The agents of --agent and --agents (a JSON file: [{id, name, command}]).
QList<HostAgent> agentsOf(const QJsonObject &args, QString *error)
{
    QList<HostAgent> out;
    const auto add = [&out, error](const HostAgent &a) {
        if (std::ranges::any_of(out, [&a](const HostAgent &b) { return b.id == a.id; })) {
            *error = u"the agent %1 is given twice"_s.arg(a.id);
            return false;
        }
        out.append(a);
        return true;
    };
    if (const QString file = args.value(u"agents"_s).toString().trimmed(); !file.isEmpty()) {
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) {
            *error = u"cannot read %1: %2"_s.arg(file, f.errorString());
            return {};
        }
        const QJsonDocument doc = QJsonDocument::fromJson(f.read(1024 * 1024));
        if (!doc.isArray()) {
            *error = u"%1 is no JSON list of agents ([{id, name, command}])"_s.arg(file);
            return {};
        }
        for (const QJsonValue &v : doc.array()) {
            const QJsonObject o = v.toObject();
            QString spec = o.value(u"id"_s).toString();
            if (const QString name = o.value(u"name"_s).toString(); !name.isEmpty())
                spec += u':' + name;
            if (const QString command = o.value(u"command"_s).toString(); !command.isEmpty())
                spec += u'=' + command;
            const std::optional<HostAgent> a = parseHostAgent(spec, error);
            if (!a || !add(*a))
                return {};
        }
    }
    for (const QString &spec : args.value(u"agent"_s).toString().split(u';', Qt::SkipEmptyParts)) {
        if (spec.trimmed().isEmpty())
            continue;
        const std::optional<HostAgent> a = parseHostAgent(spec, error);
        if (!a || !add(*a))
            return {};
    }
    return out;
}

} // namespace

void addAcpHostCommands(QList<Command> &out)
{
    Command c = make(
            u"acp.serve"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "Run AI agents of this computer for Brulion on other devices (a phone)"),
            acpServeCommand,
            {option(u"agent"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "An agent: claude, codex, gemini or opencode, or id[:name]=command (several: "
                                      "separated by ';')")),
             option(u"agents"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli", "A JSON file with the agents: [{id, name, command}]")),
             option(u"projects"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "A JSON file with Code mode's projects of this computer: [{id, name, path, "
                                      "worktreesRoot}] — the devices' threads work in them (read again when it "
                                      "changes)")),
             option(u"listen"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli", "The address to listen on (default 127.0.0.1)")),
             option(u"port"_s, ArgType::Int,
                    QT_TRANSLATE_NOOP("brulion::cli", "The port (default 8790; 0: any free one)")),
             option(u"publicUrl"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "The address the devices reach it at (https://agent.example.com, behind a "
                                      "reverse proxy)")),
             stateFileOption(),
             flag(u"noTools"_s, QT_TRANSLATE_NOOP("brulion::cli", "Sessions get no Brulion MCP server")),
             flag(u"noAppTools"_s,
                  QT_TRANSLATE_NOOP("brulion::cli",
                                    "The agents' tools never reach the app on a device (its live tools and chat)")),
             option(u"questionWait"_s, ArgType::Int,
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "Seconds an agent's question waits for a device that is gone before it is "
                                      "answered \"cancelled\" (default 3600; 0: at once)")),
             option(u"nextcloud"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "Keep the notebook in sync with this Nextcloud (its address, or the WebDAV "
                                      "folder's)")),
             option(u"nextcloudUser"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "Nextcloud: the user")),
             option(u"nextcloudFolder"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli", "Nextcloud: the notebook's folder there")),
             option(u"nextcloudPasswordFile"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli", "Nextcloud: a file with an app password")),
             option(u"syncInterval"_s, ArgType::Int,
                    QT_TRANSLATE_NOOP("brulion::cli", "Nextcloud: seconds between runs (default 300)")),
             option(u"deviceName"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "Nextcloud: this computer's name in conflict copies (default: its host name)")),
             option(u"tlsCert"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli", "HTTPS: the certificate chain (PEM), reloaded when it changes")),
             option(u"tlsKey"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "HTTPS: its private key (PEM)")),
             option(u"allowedHost"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "More host names it answers to (a proxy that rewrites Host; comma-separated)")),
             option(u"allowedOrigin"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "Brulion in a browser at these sites may use it (origins, comma-separated)")),
             flag(u"tailscale"_s,
                  QT_TRANSLATE_NOOP("brulion::cli",
                                    "This computer's tailnet: listen on its tailnet address, give the devices its "
                                    "tailnet name, and pair its own user's devices there without a code")),
             flag(u"tailscaleServe"_s,
                  QT_TRANSLATE_NOOP("brulion::cli",
                                    "Behind Tailscale Serve: listen on 127.0.0.1, give the devices "
                                    "https://<tailnet name>, and pair the devices of the user Serve names without a "
                                    "code")),
             option(u"tailnetUser"_s, ArgType::String,
                    QT_TRANSLATE_NOOP(
                            "brulion::cli",
                            "More tailnet users (logins, comma-separated) whose devices pair without a code")),
             flag(u"noTailnetTrust"_s,
                  QT_TRANSLATE_NOOP("brulion::cli", "In the tailnet too, every device pairs with a code")),
             flag(u"allowHttp"_s,
                  QT_TRANSLATE_NOOP("brulion::cli",
                                    "Allow a plain http:// public URL in your own network (not encrypted on the way)")),
             flag(u"stopOnEof"_s,
                  QT_TRANSLATE_NOOP("brulion::cli",
                                    "Stop when standard input ends (a program running the server stops it so, and "
                                    "when it ends itself)"))},
            QT_TRANSLATE_NOOP("brulion::cli",
                              "The app on a device pairs with a code of acp pair, then talks to an agent here over "
                              "the Agent Client Protocol: the agent works in this notebook (its files, commands and "
                              "Brulion's MCP tools are this computer's), signs in by itself, and what it says waits "
                              "for a device that sleeps. Plain HTTP on 127.0.0.1 behind a reverse proxy that adds "
                              "HTTPS in your own network (the agents run commands here), or on this computer's "
                              "tailnet address with --tailscale. Prints one JSON line when it listens, logs on "
                              "standard error and serves until stopped."));
    c.needsVault = true;
    out.append(c);
    out.append(make(u"acp.pair"_s, QT_TRANSLATE_NOOP("brulion::cli", "A pairing code for a device of acp serve"),
                    acpPair, {stateFileOption()},
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "Valid for 10 minutes, for one device, five wrong tries at most; a new code "
                                      "replaces the last one. The running server takes it at once.")));
    out.append(make(u"acp.clients"_s, QT_TRANSLATE_NOOP("brulion::cli", "The devices paired with acp serve"),
                    acpClients, {stateFileOption()},
                    QT_TRANSLATE_NOOP("brulion::cli", "Each device: its name, when it was paired and last used.")));
    out.append(make(u"acp.revoke"_s, QT_TRANSLATE_NOOP("brulion::cli", "Unpair devices of acp serve"), acpRevoke,
                    {positional(u"device"_s, ArgType::String,
                                QT_TRANSLATE_NOOP("brulion::cli", "A device (dev_…) from acp clients"), false),
                     flag(u"all"_s, QT_TRANSLATE_NOOP("brulion::cli", "Unpair every device")), stateFileOption()},
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "Its token stops working at once, also in a running server, and its agents "
                                      "stop; the device has to pair again.")));
}

int serveAcpHost(Context &context, const Command &command, const QJsonObject &args,
                 const std::function<int(const Command *, const Result &)> &output)
{
    const auto fail = [&](int exit, const QString &code, const QString &message, const QJsonObject &details = {}) {
        Result r;
        r.error = Error{exit, code, message, details};
        return output(&command, r);
    };
    context.serving = true;
    // The app that started it (its agent sharing) shares its own conversations: its control socket
    // and grant, taken out of the environment at once — the agents and their terminals never see them.
    AcpHostOptions options;
    if (const QString token = qEnvironmentVariable(ipc::ControlClient::ShareTokenVariable).trimmed();
        !token.isEmpty()) {
        options.appToken = token;
        options.appSocket = qEnvironmentVariable(ipc::ControlClient::SocketVariable).trimmed();
        qunsetenv(ipc::ControlClient::ShareTokenVariable);
        qunsetenv(ipc::ControlClient::SocketVariable);
    }
    if (auto e = context.resolveVault()) {
        Result r;
        r.error = e;
        return output(&command, r);
    }
    const bool tailnet = args.value(u"tailscale"_s).toBool();
    const bool behindServe = args.value(u"tailscaleServe"_s).toBool();
    if (tailnet && behindServe)
        return fail(ExitUsage, u"bad_tailscale"_s,
                    QCoreApplication::translate("brulion::cli", "--tailscale and --tailscale-serve exclude each other"));
    const bool listenGiven = !args.value(u"listen"_s).toString().trimmed().isEmpty();
    const QString listen = args.value(u"listen"_s).toString(u"127.0.0.1"_s).trimmed();
    if (listen == u"localhost")
        options.address = QHostAddress(QHostAddress::LocalHost);
    else if (listen == u"*" || listen == u"0.0.0.0")
        options.address = QHostAddress(QHostAddress::AnyIPv4);
    else if (listen == u"::")
        options.address = QHostAddress(QHostAddress::Any);
    else
        options.address = QHostAddress(listen);
    if (options.address.isNull())
        return fail(ExitUsage, u"bad_listen"_s,
                    QCoreApplication::translate("brulion::cli", "--listen takes an IP address"),
                    {{u"listen"_s, listen}});
    const QJsonValue portValue = args.value(u"port"_s);
    bool portOk = true;
    const qint64 port = portValue.isUndefined() ? 8790
                        : portValue.isDouble()  ? portValue.toInteger(-1)
                                                : portValue.toString().toLongLong(&portOk);
    if (!portOk || port < 0 || port > 65535)
        return fail(ExitUsage, u"bad_port"_s, QCoreApplication::translate("brulion::cli", "--port takes 0 … 65535"));
    options.port = quint16(port);
    options.publicUrl = args.value(u"publicUrl"_s).toString();
    options.tlsCertificate = args.value(u"tlsCert"_s).toString();
    options.tlsKey = args.value(u"tlsKey"_s).toString();
    options.allowedHosts = listArgument(args, u"allowedHost"_s);
    options.allowedOrigins = listArgument(args, u"allowedOrigin"_s);
    options.tools = !args.value(u"noTools"_s).toBool();
    options.appTools = !args.value(u"noAppTools"_s).toBool();
    options.toolsProgram = toolsProgram();
    if (const QJsonValue wait = args.value(u"questionWait"_s); !wait.isUndefined()) {
        bool waitOk = true;
        const qint64 seconds = wait.isDouble() ? wait.toInteger(-1) : wait.toString().toLongLong(&waitOk);
        if (!waitOk || seconds < 0 || seconds > 7LL * 24 * 60 * 60)
            return fail(ExitUsage, u"bad_question_wait"_s,
                        QCoreApplication::translate("brulion::cli", "--question-wait takes 0 … 604800 seconds"));
        options.questionWaitMs = seconds * 1000;
    }
    options.allowHttp = args.value(u"allowHttp"_s).toBool();
    if (const QString file = args.value(u"projects"_s).toString().trimmed(); !file.isEmpty()) {
        const QString expanded = file.startsWith(u"~/") ? QDir::homePath() + file.mid(1) : file;
        options.projectsFile = QDir::cleanPath(QFileInfo(expanded).absoluteFilePath());
    }
    QString agentError;
    options.agents = agentsOf(args, &agentError);
    if (!agentError.isEmpty())
        return fail(ExitUsage, u"bad_agent"_s, agentError);
    std::optional<Error> error;
    options.stateFile = hostStateFile(context, args, &error);
    if (error) {
        Result r;
        r.error = error;
        return output(&command, r);
    }
    std::optional<Error> openError;
    notebook::Notebook *nb = context.notebook(&openError);
    if (!nb) {
        Result r;
        r.error = openError;
        return output(&command, r);
    }
    std::unique_ptr<HostSync> sync;
    if (const QString server = args.value(u"nextcloud"_s).toString().trimmed(); !server.isEmpty()) {
        HostSyncOptions s;
        s.server = server;
        s.user = args.value(u"nextcloudUser"_s).toString();
        s.folder = args.value(u"nextcloudFolder"_s).toString();
        s.device = args.value(u"deviceName"_s).toString().trimmed();
        if (s.device.isEmpty())
            s.device = QSysInfo::machineHostName();
        const QJsonValue interval = args.value(u"syncInterval"_s);
        if (!interval.isUndefined())
            s.intervalSeconds = interval.isDouble() ? interval.toInt() : interval.toString().toInt();
        if (const QString file = args.value(u"nextcloudPasswordFile"_s).toString(); !file.isEmpty()) {
            QString readError;
            s.password = readSecretFile(file, &readError);
            if (!readError.isEmpty())
                return fail(ExitUnavailable, u"cannot_read"_s,
                            QCoreApplication::translate("brulion::cli", "Cannot read %1: %2").arg(file, readError));
        }
        sync = std::make_unique<HostSync>(nb, s);
        sync->log = logLine;
        QString syncError;
        if (!sync->start(&syncError))
            return fail(ExitUsage, u"bad_sync"_s, syncError);
    }
    // This computer's tailnet (--tailscale, --tailscale-serve): its address, its name, its user.
    std::unique_ptr<tailscale::LocalApi> tailscaleApi;
    std::unique_ptr<TailnetTrust> trust;
    std::optional<tailscale::Status> self;
    QJsonObject tailnetInfo;
    if (tailnet || behindServe) {
        tailscaleApi = std::make_unique<tailscale::LocalApi>();
        QString statusError;
        QEventLoop wait;
        bool answered = false;
        tailscaleApi->status([&](const std::optional<tailscale::Status> &s, const QString &e) {
            self = s;
            statusError = e;
            answered = true;
            wait.quit();
        });
        if (!answered)
            wait.exec();
        if (!self || !self->running() || self->dnsName.isEmpty() || self->addresses.isEmpty()) {
            const QString why = !self ? statusError
                                : !self->running()
                                        ? u"tailscaled is %1"_s.arg(self->backendState.isEmpty() ? u"not running"_s
                                                                                                  : self->backendState)
                                        : u"this computer has no tailnet name (MagicDNS) or address"_s;
            return fail(ExitUnavailable, u"no_tailscale"_s,
                        QCoreApplication::translate("brulion::cli", "Tailscale is not usable here: %1").arg(why));
        }
        if (behindServe) {
            if (listenGiven && !options.address.isLoopback())
                return fail(ExitUsage, u"bad_listen"_s,
                            QCoreApplication::translate("brulion::cli",
                                                        "--tailscale-serve listens on this computer only (127.0.0.1)"));
            if (!listenGiven)
                options.address = QHostAddress(QHostAddress::LocalHost);
            if (options.publicUrl.trimmed().isEmpty())
                options.publicUrl = u"https://"_s + self->dnsName;
        } else {
            if (!listenGiven)
                options.address = self->ipv4().isNull() ? self->addresses.first() : self->ipv4();
            if (options.publicUrl.trimmed().isEmpty())
                options.publicHost = self->dnsName;
        }
        // (A proxy or a client may name the computer by its tailnet name without the port.)
        options.allowedHosts.append(self->dnsName);
        const bool loopbackOnly = options.address.isLoopback();
        const bool trusted = !args.value(u"noTailnetTrust"_s).toBool();
        TailnetTrustOptions trustOptions;
        trustOptions.users = listArgument(args, u"tailnetUser"_s);
        trustOptions.behindServe = behindServe;
        trustOptions.loopbackOnly = loopbackOnly;
        trustOptions.thisNode = self->dnsName;
        if (trusted)
            trust = std::make_unique<TailnetTrust>(tailscaleApi.get(), self->login, trustOptions);
        tailnetInfo = QJsonObject{{u"name"_s, self->dnsName},
                                  {u"login"_s, self->login},
                                  {u"serve"_s, behindServe},
                                  {u"trust"_s, trusted},
                                  {u"users"_s, QJsonArray::fromStringList(trust ? trust->allowedLogins() : QStringList())}};
    } else if (!args.value(u"tailnetUser"_s).toString().trimmed().isEmpty()) {
        return fail(ExitUsage, u"bad_tailscale"_s,
                    QCoreApplication::translate("brulion::cli", "--tailnet-user needs --tailscale or --tailscale-serve"));
    }
    nb->index();
    AcpHost host(context, nb, options);
    host.log = logLine;
    host.setSync(sync.get());
    if (trust) {
        TailnetTrust *t = trust.get();
        host.setPairTrust([t](const http::Request &request, std::function<void(const PairTrust &)> done) {
            t->check(request, std::move(done));
        });
    }
    QString startError;
    if (!host.start(&startError))
        return fail(ExitUnavailable, u"cannot_serve"_s, startError);
    QJsonArray agents;
    for (const HostAgent &a : std::as_const(options.agents))
        agents.append(QJsonObject{{u"id"_s, a.id}, {u"name"_s, a.name}, {u"command"_s, a.command}});
    const QJsonObject started{
            {u"ok"_s, true},
            {u"command"_s, command.id},
            {u"vault"_s, context.vaultRoot()},
            {u"result"_s,
             QJsonObject{
                     {u"url"_s, host.publicUrl()},
                     {u"endpoint"_s, host.endpoint()},
                     {u"port"_s, host.port()},
                     {u"address"_s, options.address.toString()},
                     {u"tls"_s, host.server()->tls()},
                     {u"agents"_s, agents},
                     {u"tools"_s, options.tools},
                     {u"appTools"_s, !host.tunnelSocket().isEmpty()},
                     {u"appChats"_s, !options.appSocket.isEmpty()},
                     {u"sync"_s, sync != nullptr},
                     {u"projects"_s, options.projectsFile.isEmpty() ? QJsonValue() : QJsonValue(options.projectsFile)},
                     {u"tailnet"_s, tailnetInfo.isEmpty() ? QJsonValue() : QJsonValue(tailnetInfo)},
                     {u"stateFile"_s, options.stateFile}}},
            {u"warnings"_s, QJsonArray()}};
    const QByteArray line = QJsonDocument(started).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fflush(stdout);
    logLine(u"serving %1 at %2 (listening on %3 port %4%5%6)"_s.arg(
            context.vaultRoot(), host.publicUrl(), options.address.toString(), QString::number(host.port()),
            host.server()->tls() ? u", HTTPS"_s : QString(), sync ? u", synced with Nextcloud"_s : QString()));
    if (trust)
        logLine(u"devices of %1 in the tailnet pair without a code"_s.arg(trust->allowedLogins().join(u", "_s)));
    if (args.value(u"stopOnEof"_s).toBool()) {
        // (A program that runs the server — the app's "Share agents with the phone" — closes its
        // standard input to stop it, and so does its own end, a crash too: the server never outlives
        // it. The reader is left blocked on its read when the server stops for another reason.)
        QCoreApplication *app = QCoreApplication::instance();
        std::thread([app] {
            char buffer[256];
            while (std::fread(buffer, 1, sizeof buffer, stdin) > 0) { }
            QMetaObject::invokeMethod(app, [] { QCoreApplication::quit(); }, Qt::QueuedConnection);
        }).detach();
    }
    return QCoreApplication::exec();
}

} // namespace brulion::cli
