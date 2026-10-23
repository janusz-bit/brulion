// The running app (desktop, this user session): `app open | today | capture` through the
// single-instance socket (ipc/appmessage.h; nothing to switch on), and the remote control —
// `app state | navigate | commands | command | screenshot | find | click | long-press |
// double-click | drag | scroll | type | key | wait | quit`, `app open` and `app capture <text>` —
// through the app's control socket (ipc/controlprotocol.h), which is there only while the app
// allows it (--remote-control, or Settings → Application → "For developers"). docs/CLI.md
// "Remote control".
#include "commands_p.h"

#include "appmessage.h"
#include "controlclient.h"
#include "controlprotocol.h"
#include "notebook/itemref.h"
#include "singleinstance.h"
#include "vault/vault.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

/// The exit code of a remote control error (ipc::ControlError).
int exitFor(const QString &code)
{
    namespace E = ipc::ControlError;
    for (const char *usage : {E::ParseError, E::InvalidRequest, E::UnknownMethod, E::BadParams}) {
        if (code == QLatin1String(usage))
            return ExitUsage;
    }
    if (code == QLatin1String(E::NotFound) || code == QLatin1String(E::Timeout) || code == QLatin1String(E::OffScreen))
        return ExitNotFound;
    if (code == QLatin1String(E::StaleHandle) || code == QLatin1String(E::Ambiguous))
        return ExitConflict;
    if (code == QLatin1String(E::NotReady))
        return ExitUnavailable;
    if (code == QLatin1String(E::Refused))
        return ExitRefused;
    return ExitInternal;
}

QString singleInstanceKey(const Call &call)
{
    return ipc::SingleInstance::keyFor(call.ctx.appDataDir());
}

QString remoteControlHint()
{
    // The Polish labels too: the app's interface is Polish unless chosen otherwise.
    return tr("Allow it in the app: Settings → Application → For developers → “Allow agents to control the app” (in "
              "Polish: Ustawienia → Aplikacja → Dla programistów → „Pozwól agentom sterować aplikacją”), or start "
              "Brulion with --remote-control");
}

/// The answer of the app's remote control to `method`; nullopt and the call's error set when no
/// app runs (5 app_not_running), it does not allow remote control (5 remote_control_off) or it
/// refused the request (its error code, exitFor()).
std::optional<QJsonValue> askApp(Call &call, const QString &method, const QJsonObject &params, int timeoutMs = 20000)
{
    ipc::ControlClient client(ipc::controlKeyFor(singleInstanceKey(call)));
    if (!client.connect()) {
        if (!client.errorString().isEmpty()) {
            // Another user's socket at the app's name: nothing was sent to it.
            call.setError(ExitUnavailable, u"app_not_reached"_s,
                          tr("The running app did not answer: %1").arg(client.errorString()),
                          {{u"reason"_s, u"foreign_socket"_s}});
            return std::nullopt;
        }
        const bool running = ipc::SingleInstance(singleInstanceKey(call)).isPrimaryRunning();
        if (!running)
            call.setError(ExitUnavailable, u"app_not_running"_s,
                          tr("Brulion is not running on this desktop (the app must run in this user session)"),
                          {{u"gui"_s, QJsonObject{{u"running"_s, false}, {u"remoteControl"_s, false}}},
                           {u"hint"_s, tr("Start Brulion with --remote-control")}});
        else
            call.setError(ExitUnavailable, u"remote_control_off"_s,
                          tr("The running app does not allow remote control. %1").arg(remoteControlHint()),
                          {{u"gui"_s, QJsonObject{{u"running"_s, true}, {u"remoteControl"_s, false}}},
                           {u"hint"_s, remoteControlHint()}});
        return std::nullopt;
    }
    const ipc::ControlClient::Response response = client.call(method, params, timeoutMs);
    if (response.transportError) {
        call.setError(ExitUnavailable, u"app_not_reached"_s,
                      tr("The running app did not answer: %1").arg(response.message), {{u"reason"_s, response.code}});
        return std::nullopt;
    }
    if (!response.ok) {
        call.setError(exitFor(response.code), response.code, response.message, response.details);
        return std::nullopt;
    }
    return response.result;
}

/// askApp() as the command's result; with --dry-run only what would be asked.
Result appResult(Call &call, const QString &method, const QJsonObject &params, int timeoutMs = 20000)
{
    if (call.dryRun())
        return call.ok(QJsonObject{{u"dryRun"_s, true}, {u"method"_s, method}, {u"params"_s, params}});
    const auto result = askApp(call, method, params, timeoutMs);
    if (!result)
        return call.failed();
    return call.ok(*result);
}

/// A target from the command line: `<target>` ("h12" a handle from find, "120,340" a point in
/// window coordinates, anything else the text of an item) and the options --handle, --x/--y,
/// --text, --object-name, --type, --exact. `prefix`: "from"/"to" of drag. {} when none is given.
std::optional<QJsonObject> targetOf(Call &call, const QString &positionalName, bool required)
{
    QJsonObject target;
    const QString word = call.str(positionalName).trimmed();
    static const QRegularExpression handle(u"^h\\d+$"_s);
    static const QRegularExpression point(u"^\\s*(-?\\d+(?:\\.\\d+)?)\\s*[,x ]\\s*(-?\\d+(?:\\.\\d+)?)\\s*$"_s);
    if (!word.isEmpty()) {
        if (handle.match(word).hasMatch()) {
            target.insert(u"handle"_s, word);
        } else if (const auto m = point.match(word); m.hasMatch()) {
            target.insert(u"x"_s, m.captured(1).toDouble());
            target.insert(u"y"_s, m.captured(2).toDouble());
        } else {
            target.insert(u"text"_s, word);
        }
    }
    if (call.has(u"handle"_s))
        target.insert(u"handle"_s, call.str(u"handle"_s));
    if (call.has(u"x"_s) || call.has(u"y"_s)) {
        target.insert(u"x"_s, call.integer(u"x"_s, 0));
        target.insert(u"y"_s, call.integer(u"y"_s, 0));
    }
    for (const QString &key : {u"text"_s, u"objectName"_s, u"type"_s}) {
        if (call.has(key))
            target.insert(key, call.str(key));
    }
    if (call.flag(u"exact"_s))
        target.insert(u"exact"_s, true);
    if (const QString why = ipc::checkTarget(target, required); !why.isEmpty()) {
        call.setError(
                ExitUsage, u"bad_target"_s, why,
                {{u"examples"_s, QJsonArray{u"h12"_s, u"120,340"_s, u"Zapisz"_s, u"--object-name shellPalette"_s}}});
        return std::nullopt;
    }
    return target;
}

/// The pointer options shared by click, long-press, double-click, drag and scroll.
void pointerOptions(Call &call, QJsonObject &params)
{
    if (call.has(u"button"_s))
        params.insert(u"button"_s, call.str(u"button"_s));
    if (call.has(u"modifiers"_s))
        params.insert(u"modifiers"_s, call.str(u"modifiers"_s));
    if (call.has(u"device"_s))
        params.insert(u"device"_s, call.str(u"device"_s));
}

// ---- Through the single-instance socket -----------------------------------------------------

/// Send `message` to the running app; {sent, gui: {running}}.
Result sendToApp(Call &call, const QByteArray &message, QJsonObject result)
{
    ipc::SingleInstance app(singleInstanceKey(call));
    // Another user's socket at the app's name: nothing was sent to it.
    const auto foreign = [&call, &app] {
        return call.fail(ExitUnavailable, u"app_not_reached"_s,
                         tr("The running app did not answer: %1").arg(app.errorString()),
                         {{u"reason"_s, u"foreign_socket"_s}});
    };
    if (!app.isPrimaryRunning()) {
        if (!app.errorString().isEmpty())
            return foreign();
        return call.fail(ExitUnavailable, u"app_not_running"_s,
                         tr("Brulion is not running on this desktop (the app must run in this user session)"),
                         {{u"gui"_s, QJsonObject{{u"running"_s, false}}}});
    }
    if (!app.sendToPrimary(message)) {
        if (!app.errorString().isEmpty())
            return foreign();
        return call.fail(ExitUnavailable, u"app_not_reached"_s, tr("The running app did not take the message"),
                         {{u"gui"_s, QJsonObject{{u"running"_s, true}}}});
    }
    result.insert(u"gui"_s, QJsonObject{{u"running"_s, true}});
    result.insert(u"via"_s, u"message"_s);
    return call.ok(result);
}

/// `app open` without the remote control: the route resolved here, sent as an `open` message.
Result openByMessage(Call &call, const QString &target)
{
    if (auto e = call.ctx.resolveVault())
        return failWith(*e);
    const auto ref = call.page(target, false);
    if (!ref)
        return call.failed();
    QJsonObject route;
    const bool place = call.has(u"line"_s) || call.has(u"blockId"_s) || call.has(u"heading"_s);
    if (ref->journal && !ref->item && !(place && ref->exists)) {
        route = {{u"view"_s, u"journal"_s}, {u"date"_s, ref->journal->toString(Qt::ISODate)}, {u"path"_s, ref->path}};
    } else {
        if (!ref->exists)
            return call.fail(ExitNotFound, u"page_not_found"_s, tr("No page %1").arg(ref->path),
                             {{u"path"_s, ref->path}});
        route = {{u"view"_s, u"page"_s}, {u"path"_s, ref->path}};
        if (ref->item && ref->item->hasBlock()) {
            route.insert(u"blockId"_s, ref->item->blockId);
        } else if (ref->item && ref->item->hasLine()) {
            notebook::ItemRef item = *ref->item;
            const notebook::ResolvedLine line =
                    notebook::resolve(call.nb()->read(ref->path).text, item, call.vault()->config());
            route.insert(u"line"_s, line.line >= 0 ? line.line : item.line);
        }
        if (place) {
            route.remove(u"line"_s);
            route.remove(u"blockId"_s);
        }
        if (call.has(u"line"_s))
            route.insert(u"line"_s, call.integer(u"line"_s, 0));
        if (call.has(u"blockId"_s))
            route.insert(u"blockId"_s, call.str(u"blockId"_s).remove(u'^'));
        if (call.has(u"heading"_s))
            route.insert(u"heading"_s, call.str(u"heading"_s));
    }
    const QString current = call.ctx.settingsValue(u"vaults/current"_s);
    if (!current.isEmpty() && Vault::idForPath(current) != Vault::idForPath(call.ctx.vaultRoot()))
        call.warn(u"other_notebook"_s,
                  tr("The app's notebook is %1: it will not open a page of %2").arg(current, call.ctx.vaultRoot()));
    return sendToApp(call, ipc::openMessage(route, call.ctx.vaultRoot()),
                     {{u"sent"_s, u"open"_s}, {u"route"_s, route}});
}

// ---- Commands -----------------------------------------------------------------------------------

Result appOpen(Call &call)
{
    const QString target = call.str(u"target"_s).trimmed();
    if (target == u"capture")
        return sendToApp(call, ipc::encodeMessage("capture"), {{u"sent"_s, u"capture"_s}});
    ipc::ControlClient probe(ipc::controlKeyFor(singleInstanceKey(call)));
    if (!probe.connect(500)) {
        // No remote control: the single-instance message (the route resolved here).
        if (target.isEmpty() || target == u"today")
            return sendToApp(call, ipc::encodeMessage("today"), {{u"sent"_s, u"today"_s}});
        return openByMessage(call, target);
    }
    // The app resolves the ref in the notebook it shows (the same refs as here).
    QJsonObject params{{u"ref"_s, target.isEmpty() ? u"today"_s : target}};
    if (call.has(u"line"_s))
        params.insert(u"line"_s, call.integer(u"line"_s, 0));
    if (call.has(u"blockId"_s))
        params.insert(u"blockId"_s, call.str(u"blockId"_s));
    if (call.has(u"heading"_s))
        params.insert(u"heading"_s, call.str(u"heading"_s));
    if (call.flag(u"side"_s))
        params.insert(u"side"_s, true);
    // A notebook named on the command line must be the one the app shows.
    if (!call.ctx.globals.vault.isEmpty() || qEnvironmentVariableIsSet("BRULION_VAULT")) {
        if (auto e = call.ctx.resolveVault())
            return failWith(*e);
        const auto state = askApp(call, u"state"_s, {});
        if (!state)
            return call.failed();
        const QString appVault = state->toObject().value(u"vault"_s).toObject().value(u"path"_s).toString();
        if (!appVault.isEmpty() && Vault::idForPath(appVault) != Vault::idForPath(call.ctx.vaultRoot()))
            return call.fail(ExitConflict, u"other_notebook"_s,
                             tr("The app shows the notebook %1, not %2").arg(appVault, call.ctx.vaultRoot()),
                             {{u"appVault"_s, appVault}, {u"vault"_s, call.ctx.vaultRoot()}});
    }
    if (call.dryRun())
        return appResult(call, u"open"_s, params);
    const auto result = askApp(call, u"open"_s, params);
    if (!result) {
        // A notebook still opening: the message, which the app keeps until the notebook is ready.
        if (call.error && call.error->code == QLatin1String(ipc::ControlError::NotReady)) {
            call.error.reset();
            if (target.isEmpty() || target == u"today")
                return sendToApp(call, ipc::encodeMessage("today"), {{u"sent"_s, u"today"_s}});
            return openByMessage(call, target);
        }
        return call.failed();
    }
    QJsonObject out = result->toObject();
    out.insert(u"via"_s, u"control"_s);
    out.insert(u"gui"_s, QJsonObject{{u"running"_s, true}, {u"remoteControl"_s, true}});
    return call.ok(out);
}

Result appToday(Call &call)
{
    return sendToApp(call, ipc::encodeMessage("today"), {{u"sent"_s, u"today"_s}});
}

Result appCapture(Call &call)
{
    const QString text = call.text(u"text"_s);
    if (text.trimmed().isEmpty()) {
        if (call.dryRun())
            return call.ok(QJsonObject{{u"dryRun"_s, true}, {u"sent"_s, u"capture"_s}});
        return sendToApp(call, ipc::encodeMessage("capture"), {{u"sent"_s, u"capture"_s}});
    }
    return appResult(call, u"capture"_s, {{u"text"_s, text}});
}

Result appState(Call &call)
{
    return appResult(call, u"state"_s, {});
}

Result appNavigate(Call &call)
{
    const QString raw = call.str(u"route"_s).trimmed();
    QJsonObject params;
    if (raw.startsWith(u'{')) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            return call.fail(ExitUsage, u"bad_route"_s,
                             tr("The route is a view name or a JSON object: %1").arg(error.errorString()));
        params.insert(u"route"_s, doc.object());
    } else {
        params.insert(u"route"_s, raw);
    }
    if (call.flag(u"side"_s))
        params.insert(u"side"_s, true);
    return appResult(call, u"navigate"_s, params);
}

Result appCommands(Call &call)
{
    return appResult(call, u"commands"_s, {});
}

Result appCommand(Call &call)
{
    return appResult(call, u"command"_s, {{u"id"_s, call.str(u"id"_s).trimmed()}});
}

Result appScreenshot(Call &call)
{
    QJsonObject params;
    if (const QString path = call.str(u"path"_s).trimmed(); !path.isEmpty()) {
        // --read-only (an agent's `mcp --read-only --app-control`): only the runtime folder — a path
        // could replace any .png, a picture in the notebook too.
        if (call.ctx.globals.readOnly)
            return call.fail(ExitRefused, u"read_only"_s,
                             tr("A screenshot path writes a file there: refused in read-only mode (without a path the "
                                "screenshot goes to the runtime folder)"));
        // Relative to where the command runs (the app's own folder is somewhere else).
        const QString expanded = path.startsWith(u"~/"_s) ? QDir::homePath() + path.mid(1) : path;
        params.insert(u"path"_s, QDir::cleanPath(QFileInfo(expanded).absoluteFilePath()));
    }
    return appResult(call, u"screenshot"_s, params);
}

Result appFind(Call &call)
{
    QJsonObject params;
    for (const QString &key : {u"text"_s, u"objectName"_s, u"type"_s, u"within"_s}) {
        if (call.has(key) && !call.str(key).isEmpty())
            params.insert(key, call.str(key));
    }
    if (call.flag(u"exact"_s))
        params.insert(u"exact"_s, true);
    if (call.flag(u"includeHidden"_s))
        params.insert(u"visibleOnly"_s, false);
    if (call.has(u"limit"_s))
        params.insert(u"limit"_s, call.integer(u"limit"_s, 50));
    if (!params.contains(u"text"_s) && !params.contains(u"objectName"_s) && !params.contains(u"type"_s))
        return call.fail(ExitUsage, u"missing_argument"_s, tr("find needs a text, --object-name or --type"));
    return appResult(call, u"find"_s, params);
}

Result pointerCommand(Call &call, const QString &method, bool targetRequired)
{
    const auto target = targetOf(call, u"target"_s, targetRequired);
    if (!target)
        return call.failed();
    QJsonObject params = *target;
    pointerOptions(call, params);
    if (method == u"longPress" && call.has(u"ms"_s))
        params.insert(u"ms"_s, call.integer(u"ms"_s, 800));
    if (method == u"scroll") {
        if (!call.has(u"dx"_s) && !call.has(u"dy"_s))
            return call.fail(ExitUsage, u"missing_argument"_s,
                             tr("scroll needs --dy (or --dx): pixels, positive down"));
        params.insert(u"dx"_s, call.integer(u"dx"_s, 0));
        params.insert(u"dy"_s, call.integer(u"dy"_s, 0));
    }
    return appResult(call, method, params);
}

Result appClick(Call &call)
{
    return pointerCommand(call, u"click"_s, true);
}

Result appLongPress(Call &call)
{
    return pointerCommand(call, u"longPress"_s, true);
}

Result appDoubleClick(Call &call)
{
    return pointerCommand(call, u"doubleClick"_s, true);
}

Result appScroll(Call &call)
{
    return pointerCommand(call, u"scroll"_s, false);
}

Result appDrag(Call &call)
{
    QJsonObject params;
    for (const QString &end : {u"from"_s, u"to"_s}) {
        QJsonObject target;
        const QString word = call.str(end).trimmed();
        static const QRegularExpression handle(u"^h\\d+$"_s);
        static const QRegularExpression point(u"^\\s*(-?\\d+(?:\\.\\d+)?)\\s*[,x ]\\s*(-?\\d+(?:\\.\\d+)?)\\s*$"_s);
        if (handle.match(word).hasMatch()) {
            target.insert(u"handle"_s, word);
        } else if (const auto m = point.match(word); m.hasMatch()) {
            target.insert(u"x"_s, m.captured(1).toDouble());
            target.insert(u"y"_s, m.captured(2).toDouble());
        } else if (!word.isEmpty()) {
            target.insert(u"text"_s, word);
        }
        if (const QString why = ipc::checkTarget(target, true); !why.isEmpty())
            return call.fail(ExitUsage, u"bad_target"_s, end + u": "_s + why);
        params.insert(end, target);
    }
    pointerOptions(call, params);
    if (call.has(u"holdMs"_s))
        params.insert(u"holdMs"_s, call.integer(u"holdMs"_s, 0));
    return appResult(call, u"drag"_s, params);
}

Result appType(Call &call)
{
    const QString text = call.text(u"text"_s);
    if (text.isEmpty())
        return call.fail(ExitUsage, u"missing_argument"_s, tr("Nothing to type"));
    // The app types about one key in 10 ms (and waits for the window to settle).
    return appResult(call, u"type"_s, {{u"text"_s, text}}, int(qMin<qsizetype>(30000 + text.size() * 30, 900000)));
}

Result appKey(Call &call)
{
    QJsonObject params{{u"sequence"_s, call.str(u"sequence"_s)}};
    if (call.has(u"count"_s))
        params.insert(u"count"_s, call.integer(u"count"_s, 1));
    return appResult(call, u"key"_s, params);
}

Result appWait(Call &call)
{
    QJsonObject params;
    if (call.has(u"route"_s)) {
        const QString raw = call.str(u"route"_s).trimmed();
        if (raw.startsWith(u'{')) {
            const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
            if (!doc.isObject())
                return call.fail(ExitUsage, u"bad_route"_s, tr("The route is a view name or a JSON object"));
            params.insert(u"route"_s, doc.object());
        } else {
            params.insert(u"route"_s, raw);
        }
    }
    for (const QString &key : {u"text"_s, u"objectName"_s, u"type"_s, u"handle"_s}) {
        if (call.has(key))
            params.insert(key, call.str(key));
    }
    for (const QString &key : {u"exact"_s, u"idle"_s, u"gone"_s}) {
        if (call.flag(key))
            params.insert(key, true);
    }
    const int ms = qBound(0, call.integer(u"timeout"_s, 5000), 120000);
    params.insert(u"ms"_s, ms);
    return appResult(call, u"waitFor"_s, params, ms + 10000);
}

Result appQuit(Call &call)
{
    return appResult(call, u"quit"_s, {});
}

Command make(const QString &id, const char *summary, Handler run, QList<Arg> args, bool appControl = true)
{
    Command c;
    c.id = id;
    c.summary = cliText(summary);
    c.run = std::move(run);
    c.args = std::move(args);
    c.needsVault = false;
    c.appControl = appControl;
    return c;
}

/// The options of a target (`<target>` or these).
QList<Arg> targetArgs()
{
    return {positional(
                    u"target"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "h12 (a handle from app find), 120,340 (window coordinates) or an item's text"),
                    false, true),
            option(u"handle"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "A handle from app find or app state")),
            option(u"x"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "Window x coordinate (with --y)")),
            option(u"y"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "Window y coordinate (with --x)")),
            option(u"text"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "The topmost shown item with this text (any case)")),
            option(u"objectName"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "An item's objectName")),
            option(u"type"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "An item's QML type (BButton, TasksRow, TextEdit …)")),
            flag(u"exact"_s, QT_TRANSLATE_NOOP("brulion::cli", "The text exactly, not a part of it"))};
}

QList<Arg> pointerArgs(bool button, bool scroll = false)
{
    QList<Arg> out;
    if (button) {
        out.append(choice(u"button"_s, {u"left"_s, u"right"_s, u"middle"_s},
                          QT_TRANSLATE_NOOP("brulion::cli", "The mouse button (left)")));
        out.append(option(u"modifiers"_s, ArgType::String,
                          QT_TRANSLATE_NOOP("brulion::cli", "Keys held: ctrl, shift, alt, meta (ctrl+shift)")));
    }
    out.append(choice(
            u"device"_s, {u"mouse"_s, u"touch"_s},
            scroll ? QT_TRANSLATE_NOOP("brulion::cli", "The mouse wheel (default) or a finger")
                   : QT_TRANSLATE_NOOP("brulion::cli", "Mouse or finger (default: the app's: touch with --touch)")));
    return out;
}

} // namespace

void addAppCommands(QList<Command> &out)
{
    // Through the single-instance socket (and the remote control when it is on).
    Command c =
            make(u"app.open"_s,
                 QT_TRANSLATE_NOOP("brulion::cli",
                                   "Show the running app at a page, a line, a block, a daily note (today) or quick "
                                   "capture"),
                 appOpen,
                 {positional(u"target"_s, ArgType::Ref,
                             QT_TRANSLATE_NOOP("brulion::cli",
                                               "A page ref (Dom, Dom.md#L12@hash, ^id), a date (today, jutro), or "
                                               "capture"),
                             false, true),
                  option(u"line"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "A line of the page (0-based)")),
                  option(u"blockId"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "A block of the page")),
                  option(u"heading"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "A heading of the page")),
                  flag(u"side"_s, QT_TRANSLATE_NOOP("brulion::cli", "In the side panel (remote control only)"))},
                 false);
    c.help = cliText(QT_TRANSLATE_NOOP("brulion::cli",
                                       "Desktop only, the app of this user session (exit 5 with gui.running false when "
                                       "none runs). With the app's remote control on, the app resolves the ref in the "
                                       "notebook it shows and says what it opened; else the route goes as a message "
                                       "and the app opens pages of the notebook it shows."));
    out.append(c);
    out.append(make(u"app.today"_s, QT_TRANSLATE_NOOP("brulion::cli", "Show the running app at today's daily note"),
                    appToday, {}, false));
    c = make(u"app.capture"_s,
             QT_TRANSLATE_NOOP("brulion::cli",
                               "Quick capture in the running app: open it, or save a text (remote control)"),
             appCapture,
             {stdinable(positional(u"text"_s, ArgType::Text,
                                   QT_TRANSLATE_NOOP("brulion::cli",
                                                     "Saved as the app's quick capture does (today's journal; a date "
                                                     "makes a task); none: the dialog opens"),
                                   false, true))},
             false);
    c.writes = true;
    out.append(c);

    // The remote control (`brulion-cli mcp --app-control` offers these as tools).
    c = make(u"app.state"_s,
             QT_TRANSLATE_NOOP("brulion::cli",
                               "What the running app shows: status, notebook, route, focus, the edited block, toasts, "
                               "window"),
             appState, {});
    c.help = cliText(QT_TRANSLATE_NOOP("brulion::cli",
                                       "The app commands below need the app's remote control: Settings → Application → "
                                       "For developers, or Brulion started with --remote-control (exit 5 "
                                       "remote_control_off otherwise)."));
    out.append(c);
    out.append(
            make(u"app.navigate"_s,
                 QT_TRANSLATE_NOOP("brulion::cli", "Show a view in the running app (tasks, calendar, a route as JSON)"),
                 appNavigate,
                 {positional(u"route"_s, ArgType::String,
                             QT_TRANSLATE_NOOP("brulion::cli",
                                               "A view (journals, tasks, calendar, search, settings …) or a route "
                                               "{\"view\": …}"),
                             true, true),
                  flag(u"side"_s, QT_TRANSLATE_NOOP("brulion::cli", "In the side panel (wide windows)"))}));
    out.append(make(u"app.commands"_s,
                    QT_TRANSLATE_NOOP("brulion::cli", "The command palette's commands (id, title, enabled)"),
                    appCommands, {}));
    c = make(u"app.command"_s, QT_TRANSLATE_NOOP("brulion::cli", "Run a command of the palette by its id"), appCommand,
             {positional(u"id"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "A command id (app commands)"))});
    c.writes = true;
    out.append(c);
    out.append(make(
            u"app.screenshot"_s, QT_TRANSLATE_NOOP("brulion::cli", "Save the app's window as a PNG and give its path"),
            appScreenshot,
            {positional(u"path"_s, ArgType::String,
                        QT_TRANSLATE_NOOP("brulion::cli", "Where (a .png file); default: the user's runtime folder"),
                        false)}));
    out.append(make(
            u"app.find"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "Items of the app's window by text, objectName or type, with handles and "
                                              "geometry"),
            appFind,
            {positional(u"text"_s, ArgType::String,
                        QT_TRANSLATE_NOOP("brulion::cli", "Text the item shows (a part, any case)"), false, true),
             option(u"objectName"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "An item's objectName")),
             option(u"type"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli", "An item's QML type (BButton, TasksRow, TextEdit …)")),
             flag(u"exact"_s, QT_TRANSLATE_NOOP("brulion::cli", "The text exactly, not a part of it")),
             flag(u"includeHidden"_s, QT_TRANSLATE_NOOP("brulion::cli", "Also items that are not shown")),
             option(u"within"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "Only inside this handle")),
             option(u"limit"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "At most N items (50)"))}));
    c = make(u"app.click"_s, QT_TRANSLATE_NOOP("brulion::cli", "Click (or tap) an item or a point of the app"),
             appClick, targetArgs() + pointerArgs(true));
    c.writes = true;
    out.append(c);
    c = make(u"app.long-press"_s, QT_TRANSLATE_NOOP("brulion::cli", "Press and hold an item or a point of the app"),
             appLongPress,
             targetArgs() + pointerArgs(false)
                     + QList<Arg>{option(u"ms"_s, ArgType::Int,
                                         QT_TRANSLATE_NOOP("brulion::cli", "How long, in ms (800)"))});
    c.writes = true;
    out.append(c);
    c = make(u"app.double-click"_s,
             QT_TRANSLATE_NOOP("brulion::cli", "Double-click (or double-tap) an item or a point"), appDoubleClick,
             targetArgs() + pointerArgs(true));
    c.writes = true;
    out.append(c);
    c = make(u"app.drag"_s, QT_TRANSLATE_NOOP("brulion::cli", "Drag from one item or point of the app to another"),
             appDrag,
             QList<Arg>{positional(u"from"_s, ArgType::String,
                                   QT_TRANSLATE_NOOP("brulion::cli", "Where it starts: h12, 120,340 or a text")),
                        positional(u"to"_s, ArgType::String,
                                   QT_TRANSLATE_NOOP("brulion::cli", "Where it ends: h12, 120,340 or a text")),
                        option(u"holdMs"_s, ArgType::Int,
                               QT_TRANSLATE_NOOP("brulion::cli", "Held at the end before letting go, in ms"))}
                     + pointerArgs(true));
    c.writes = true;
    out.append(c);
    out.append(make(u"app.scroll"_s,
                    QT_TRANSLATE_NOOP("brulion::cli", "Scroll at an item or a point (default: the window's middle)"),
                    appScroll,
                    targetArgs()
                            + QList<Arg>{option(u"dy"_s, ArgType::Int,
                                                QT_TRANSLATE_NOOP("brulion::cli", "Pixels down (negative: up)")),
                                         option(u"dx"_s, ArgType::Int,
                                                QT_TRANSLATE_NOOP("brulion::cli", "Pixels right (negative: left)"))}
                            + pointerArgs(false, true)));
    c = make(
            u"app.type"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "Type text into the app's focused item, key by key (Polish letters too)"),
            appType,
            {stdinable(positional(u"text"_s, ArgType::Text,
                                  QT_TRANSLATE_NOOP("brulion::cli", "The text (a line break is Return)"), true,
                                  true))});
    c.writes = true;
    out.append(c);
    c = make(u"app.key"_s, QT_TRANSLATE_NOOP("brulion::cli", "Press keys in the app: Ctrl+K, Escape, \"Down, Return\""),
             appKey,
             {positional(u"sequence"_s, ArgType::String,
                         QT_TRANSLATE_NOOP("brulion::cli", "Keys as written in menus, several separated by commas"),
                         true, true),
              option(u"count"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "Press them N times"))});
    c.writes = true;
    out.append(c);
    c = make(
            u"app.wait"_s,
            QT_TRANSLATE_NOOP("brulion::cli",
                              "Wait until the app shows a route or an item, an item is gone, or the app is idle"),
            appWait,
            {option(u"route"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "A view, or a route as JSON")),
             option(u"text"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "An item showing this text")),
             option(u"objectName"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "An item's objectName")),
             option(u"type"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "An item's QML type")),
             flag(u"exact"_s, QT_TRANSLATE_NOOP("brulion::cli", "The text exactly")),
             option(u"handle"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "The item of a handle")),
             flag(u"gone"_s, QT_TRANSLATE_NOOP("brulion::cli", "Until the item is no longer shown")),
             flag(u"idle"_s,
                  QT_TRANSLATE_NOOP("brulion::cli", "Until nothing moves, nothing is indexed and every edit is saved")),
             option(u"timeout"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "At most this long, in ms (5000)"))});
    c.help = cliText(QT_TRANSLATE_NOOP("brulion::cli",
                                       "Exit 3 (timeout) when the condition did not hold in time. Several conditions "
                                       "must all hold."));
    out.append(c);
    c = make(u"app.quit"_s, QT_TRANSLATE_NOOP("brulion::cli", "Quit the running app"), appQuit, {});
    c.writes = true;
    out.append(c);
}

} // namespace brulion::cli
