// Publishing (docs/CLI.md "Publishing", docs/UI.md "Publishing"): the notebook's publishing targets
// on this device — GitHub Pages, a WebDAV folder, Nextcloud with a public link, a folder — what they
// publish, the dry run, publishing and unpublishing, over the core module publish. Targets and
// manifests live in the notebook's device state folder, shared with the app; secrets in the app's
// owner-only files (the GitHub token the app keeps; `publish secret` keeps a target's password).
#include "commands_p.h"

#include "index/vaultindex.h"
#include "publish/publish.h"
#include "sync/http.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QTextStream>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

using namespace brulion::publish;

/// The transport of the publish commands (tests: none — the folder target needs no network).
sync::HttpTransport *transport()
{
    static sync::NetworkHttpTransport *network = new sync::NetworkHttpTransport(QCoreApplication::instance());
    return network;
}

std::optional<PublishStore> storeOf(Call &call)
{
    const QString state = call.ctx.stateDir();
    if (state.isEmpty()) {
        call.setError(ExitUsage, u"no_state"_s,
                      tr("Publishing keeps its targets in the notebook's device state: run it without --no-history"));
        return std::nullopt;
    }
    return PublishStore(state);
}

/// A target by its id or name (any letter case).
std::optional<PublishTarget> findTarget(Call &call, const PublishStore &store, const QString &ref)
{
    const QList<PublishTarget> all = store.targets();
    QList<PublishTarget> found;
    for (const PublishTarget &t : all) {
        if (t.id == ref)
            return t;
        if (t.name.compare(ref.trimmed(), Qt::CaseInsensitive) == 0)
            found.append(t);
    }
    if (found.size() == 1)
        return found.first();
    QJsonArray names;
    for (const PublishTarget &t : all)
        names.append(QJsonObject{{u"id"_s, t.id}, {u"name"_s, t.name}});
    if (found.size() > 1) {
        call.setError(ExitConflict, u"ambiguous_target"_s, tr("Several targets are named %1: use the id").arg(ref),
                      {{u"targets"_s, names}});
        return std::nullopt;
    }
    call.setError(ExitNotFound, u"target_not_found"_s, tr("No publishing target %1").arg(ref), {{u"targets"_s, names}});
    return std::nullopt;
}

QString secretOf(Call &call, const PublishTarget &t, const QString &which)
{
    const char *variable = which == u"share"   ? "BRULION_PUBLISH_SHARE_PASSWORD"
                           : which == u"pages" ? "BRULION_PUBLISH_PAGES_PASSWORD"
                                               : "BRULION_PUBLISH_PASSWORD";
    if (const QString fromEnv = qEnvironmentVariable(variable); !fromEnv.isEmpty())
        return fromEnv;
    return readSecret(secretFile(call.ctx.appDataDir(), t.id, which));
}

PublishCredentials credentialsOf(Call &call, const PublishTarget &t)
{
    PublishCredentials c;
    c.token = qEnvironmentVariable("BRULION_GITHUB_TOKEN");
    if (c.token.isEmpty())
        c.token = readSecret(githubTokenFile(call.ctx.appDataDir()));
    c.password = secretOf(call, t, u"password"_s);
    c.sharePassword = secretOf(call, t, u"share"_s);
    return c;
}

QJsonObject statusJson(const PublishStatus &s)
{
    QJsonObject o = toJson(s);
    if (!s.errorCode.isEmpty())
        o.insert(u"message"_s, errorText(s.errorCode, s.error));
    return o;
}

QJsonObject targetJson(Call &call, const PublishTarget &t)
{
    QJsonObject o = toJson(t);
    o.insert(u"kindText"_s, kindText(t.kind));
    o.insert(u"address"_s, describe(t));
    if (t.kind == TargetKind::WebDav || t.kind == TargetKind::Nextcloud)
        o.insert(u"hasPassword"_s, !secretOf(call, t, u"password"_s).isEmpty());
    if (t.kind == TargetKind::Nextcloud && t.sharePassword)
        o.insert(u"hasSharePassword"_s, !secretOf(call, t, u"share"_s).isEmpty());
    if (t.protect)
        o.insert(u"hasPagesPassword"_s, !secretOf(call, t, u"pages"_s).isEmpty());
    if (t.last.isValid())
        o.insert(u"last"_s, statusJson(t.last));
    o.remove(u"scope"_s);
    QJsonArray items;
    for (const PublishItem &i : t.scope.items)
        items.append(QJsonObject{{u"kind"_s, itemKindName(i.kind)}, {u"value"_s, i.value}});
    o.insert(u"items"_s, items);
    o.insert(u"linkedPages"_s, t.scope.linkedPages);
    o.insert(u"onlyMarked"_s, t.scope.onlyMarked);
    o.insert(u"extensionPages"_s, t.scope.extensionPages);
    o.insert(u"aiConversations"_s, t.scope.aiConversations);
    return o;
}

QString itemText(const PublishItem &i)
{
    switch (i.kind) {
    case PublishItem::Kind::Page: return paths::pageTitle(i.value);
    case PublishItem::Kind::Folder: return i.value + u'/';
    case PublishItem::Kind::Tag: return u'#' + i.value;
    case PublishItem::Kind::Notebook: return tr("the whole notebook");
    }
    return i.value;
}

Result publishTargets(Call &call)
{
    auto store = storeOf(call);
    if (!store)
        return call.failed();
    QJsonArray list;
    QString text;
    for (const QList<PublishTarget> all = store->targets(); const PublishTarget &t : all) {
        list.append(targetJson(call, t));
        QStringList items;
        for (const PublishItem &i : t.scope.items)
            items.append(itemText(i));
        text += t.name + u"  ("_s + t.id + u")\n  "_s + kindText(t.kind) + u": "_s + describe(t) + u'\n';
        if (!items.isEmpty())
            text += u"  "_s + items.join(u", "_s) + u'\n';
        if (t.last.isValid())
            text += u"  "_s
                    + (t.last.ok ? tr("published %1").arg(t.last.at.toString(u"yyyy-MM-dd HH:mm"_s))
                                 : tr("failed %1: %2")
                                           .arg(t.last.at.toString(u"yyyy-MM-dd HH:mm"_s),
                                                errorText(t.last.errorCode, t.last.error)))
                    + (t.last.url.isEmpty() ? QString() : u" · "_s + t.last.url) + u'\n';
    }
    if (list.isEmpty())
        text = tr("No publishing targets yet: publish add …") + u'\n';
    return call.ok(QJsonObject{{u"targets"_s, list}}, text);
}

/// The fields of `add` / `set` given on the command line, into `t`.
void applyFields(Call &call, PublishTarget &t)
{
    const auto take = [&call](const QString &arg, QString &field) {
        if (call.args.contains(arg))
            field = call.str(arg).trimmed();
    };
    take(u"repository"_s, t.repository);
    take(u"branch"_s, t.branch);
    take(u"folder"_s, t.folder);
    take(u"url"_s, t.url);
    take(u"server"_s, t.server);
    take(u"username"_s, t.username);
    take(u"siteUrl"_s, t.siteUrl);
    take(u"title"_s, t.title);
    if (call.args.contains(u"path"_s))
        t.path = QDir::cleanPath(QFileInfo(call.str(u"path"_s).trimmed()).absoluteFilePath());
    if (call.args.contains(u"content"_s))
        t.content = contentFromName(call.str(u"content"_s));
    if (call.args.contains(u"share"_s))
        t.share = call.str(u"share"_s) != u"off"_s;
    if (call.args.contains(u"sharePassword"_s))
        t.sharePassword = call.str(u"sharePassword"_s) == u"on"_s;
    if (call.args.contains(u"protect"_s))
        t.protect = call.str(u"protect"_s) == u"on"_s;
    if (call.args.contains(u"shareExpires"_s)) {
        const QString when = call.str(u"shareExpires"_s).trimmed();
        if (when.isEmpty() || when == u"never"_s || when == u"nigdy"_s) {
            t.shareExpires = {};
        } else if (const auto day = call.date(when)) {
            t.shareExpires = *day;
        }
    }
}

std::optional<Result> checkedOrFail(Call &call, const PublishTarget &t)
{
    if (const QString problem = checkTarget(t); !problem.isEmpty())
        return call.fail(ExitUsage, u"bad_target"_s, errorText(problem), {{u"problem"_s, problem}});
    return std::nullopt;
}

Result publishAdd(Call &call)
{
    auto store = storeOf(call);
    if (!store)
        return call.failed();
    PublishTarget t;
    t.id = newTargetId();
    t.kind = kindFromName(call.str(u"kind"_s)).value_or(TargetKind::GitHubPages);
    t.name = call.str(u"name"_s).trimmed();
    applyFields(call, t);
    if (call.error)
        return call.failed();
    for (const PublishTarget &other : store->targets()) {
        if (other.name.compare(t.name, Qt::CaseInsensitive) == 0)
            return call.fail(ExitConflict, u"target_exists"_s, tr("A target named %1 exists already").arg(t.name),
                             {{u"id"_s, other.id}});
    }
    if (auto bad = checkedOrFail(call, t))
        return *bad;
    if (!call.dryRun() && !store->saveTarget(t))
        return call.fail(ExitInternal, u"io_error"_s, tr("The target could not be saved"));
    return call.ok(targetJson(call, t), tr("Added %1 (%2): %3").arg(t.name, t.id, describe(t)) + u'\n');
}

Result publishSet(Call &call)
{
    auto store = storeOf(call);
    if (!store)
        return call.failed();
    auto t = findTarget(call, *store, call.str(u"target"_s));
    if (!t)
        return call.failed();
    if (call.has(u"name"_s))
        t->name = call.str(u"name"_s).trimmed();
    applyFields(call, *t);
    if (call.error)
        return call.failed();
    if (auto bad = checkedOrFail(call, *t))
        return *bad;
    if (!call.dryRun() && !store->saveTarget(*t))
        return call.fail(ExitInternal, u"io_error"_s, tr("The target could not be saved"));
    return call.ok(targetJson(call, *t));
}

Result publishRemove(Call &call)
{
    auto store = storeOf(call);
    if (!store)
        return call.failed();
    auto t = findTarget(call, *store, call.str(u"target"_s));
    if (!t)
        return call.failed();
    if (!call.dryRun()) {
        store->removeTarget(t->id);
        writeSecret(secretFile(call.ctx.appDataDir(), t->id, u"password"_s), {});
        writeSecret(secretFile(call.ctx.appDataDir(), t->id, u"share"_s), {});
        writeSecret(secretFile(call.ctx.appDataDir(), t->id, u"pages"_s), {});
    }
    return call.ok(QJsonObject{{u"removed"_s, t->id}, {u"name"_s, t->name}},
                   tr("Removed %1: what it published stays where it is (publish unpublish removes it)").arg(t->name)
                           + u'\n');
}

Result publishSecret(Call &call)
{
    auto store = storeOf(call);
    if (!store)
        return call.failed();
    auto t = findTarget(call, *store, call.str(u"target"_s));
    if (!t)
        return call.failed();
    if (call.flag(u"share"_s) && call.flag(u"pages"_s))
        return call.fail(ExitUsage, u"bad_argument"_s, tr("--share and --pages name different passwords: give one"));
    const QString which = call.flag(u"share"_s) ? u"share"_s : call.flag(u"pages"_s) ? u"pages"_s : u"password"_s;
    QString value = call.text(u"password"_s);
    while (value.endsWith(u'\n') || value.endsWith(u'\r'))
        value.chop(1);
    const QString file = secretFile(call.ctx.appDataDir(), t->id, which);
    if (!call.dryRun() && !writeSecret(file, value))
        return call.fail(ExitInternal, u"io_error"_s, tr("The secret could not be kept"));
    return call.ok(QJsonObject{{u"target"_s, t->id}, {u"which"_s, which}, {u"set"_s, !value.isEmpty()}},
                   (value.isEmpty() ? tr("Forgot the secret of %1") : tr("Kept the secret of %1")).arg(t->name)
                           + u'\n');
}

/// The items named by --page, --folder, --tag, --notebook.
QList<PublishItem> itemsOf(Call &call)
{
    QList<PublishItem> out;
    if (call.has(u"page"_s)) {
        const auto page = call.page(call.str(u"page"_s), true);
        if (!page)
            return {};
        out.append({PublishItem::Kind::Page, page->path});
    }
    if (call.has(u"folder"_s)) {
        const auto folder = call.vaultPath(call.str(u"folder"_s));
        if (!folder)
            return {};
        out.append({PublishItem::Kind::Folder, *folder});
    }
    if (call.has(u"tag"_s))
        out.append({PublishItem::Kind::Tag, call.str(u"tag"_s)});
    if (call.flag(u"notebook"_s))
        out.append({PublishItem::Kind::Notebook, {}});
    return out;
}

void applyOptions(Call &call, PublishScope &scope)
{
    const auto onOff = [&call](const QString &arg, bool &field) {
        if (call.args.contains(arg))
            field = call.str(arg) == u"on"_s;
    };
    onOff(u"linkedPages"_s, scope.linkedPages);
    onOff(u"onlyMarked"_s, scope.onlyMarked);
    onOff(u"extensionPages"_s, scope.extensionPages);
    onOff(u"aiConversations"_s, scope.aiConversations);
}

Result publishInclude(Call &call, bool include)
{
    auto store = storeOf(call);
    if (!store)
        return call.failed();
    auto t = findTarget(call, *store, call.str(u"target"_s));
    if (!t)
        return call.failed();
    const QList<PublishItem> items = itemsOf(call);
    if (call.error)
        return call.failed();
    int changed = 0;
    for (const PublishItem &i : items) {
        if (include) {
            changed += t->scope.add(i) ? 1 : 0;
        } else {
            PublishScope one;
            one.add(i);
            const PublishItem clean = one.items.value(0, i);
            changed += int(t->scope.items.removeAll(clean));
        }
    }
    if (include)
        applyOptions(call, t->scope);
    if (!call.dryRun() && !store->saveTarget(*t))
        return call.fail(ExitInternal, u"io_error"_s, tr("The target could not be saved"));
    QJsonObject o = targetJson(call, *t);
    o.insert(u"changed"_s, changed);
    return call.ok(o);
}

QString humanSize(qint64 bytes)
{
    return QLocale::c().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QJsonObject planJson(const PublishReport &r, const PublishSelection &selection, const SiteBuild &site)
{
    QJsonArray files;
    for (const PlannedFile &f : r.plan.files) {
        QJsonObject o{{u"path"_s, f.path}, {u"action"_s, actionName(f.action)}, {u"size"_s, f.size}};
        if (f.action == FileAction::Conflict)
            o.insert(u"deletion"_s, f.deletion);
        files.append(o);
    }
    QJsonArray leftOut;
    for (const LeftOut &l : selection.leftOut)
        leftOut.append(QJsonObject{
                {u"path"_s, l.path}, {u"reason"_s, reasonName(l.reason)}, {u"reasonText"_s, reasonText(l.reason)}});
    QJsonArray missing;
    for (const PublishItem &i : selection.missingItems)
        missing.append(QJsonObject{{u"kind"_s, itemKindName(i.kind)}, {u"value"_s, i.value}});
    QJsonArray fragments;
    for (const VaultPath &p : site.pagesWithFragmentsRemoved)
        fragments.append(p);
    QJsonObject counts{{u"upload"_s, r.plan.count(FileAction::Upload)},
                       {u"update"_s, r.plan.count(FileAction::Update)},
                       {u"unchanged"_s, r.plan.count(FileAction::Unchanged)},
                       {u"delete"_s, r.plan.count(FileAction::Delete)},
                       {u"conflict"_s, r.plan.count(FileAction::Conflict)}};
    return QJsonObject{{u"files"_s, files},
                       {u"counts"_s, counts},
                       {u"remoteChecked"_s, r.plan.remoteChecked},
                       {u"pages"_s, site.pages},
                       {u"attachments"_s, site.attachments},
                       {u"bytes"_s, site.bytes},
                       {u"leftOut"_s, leftOut},
                       {u"missingItems"_s, missing},
                       {u"encryptedFragmentsRemoved"_s, site.fragmentsRemoved},
                       {u"protected"_s, site.protectedPages},
                       {u"pagesWithFragmentsRemoved"_s, fragments}};
}

QString planText(const PublishTarget &t, const PublishReport &r, const PublishSelection &selection,
                 const SiteBuild &site, bool preview)
{
    QString out = t.name + (r.url.isEmpty() ? QString() : u" → "_s + r.url) + u'\n';
    if (r.operation == u"unpublish") {
        out += tr("%1 removed, %2 kept (changed there since)").arg(r.deleted).arg(r.conflicts.size()) + u'\n';
        for (const PlannedFile &f : r.plan.files)
            out += u"  "_s + (f.action == FileAction::Conflict ? u"! "_s : u"- "_s) + f.path + u'\n';
        return out;
    }
    const int up = preview ? r.plan.count(FileAction::Upload) : r.uploaded;
    const int changed = preview ? r.plan.count(FileAction::Update) : r.updated;
    const int removed = preview ? r.plan.count(FileAction::Delete) : r.deleted;
    out += tr("%1 page(s), %2 attachment(s), %3 · %4 new, %5 changed, %6 unchanged, %7 removed")
                   .arg(site.pages)
                   .arg(site.attachments)
                   .arg(humanSize(site.bytes))
                   .arg(up)
                   .arg(changed)
                   .arg(r.plan.count(FileAction::Unchanged))
                   .arg(removed)
           + u'\n';
    if (!r.plan.remoteChecked && preview)
        out += tr("The target was not checked (%1): the plan comes from the last publish.")
                       .arg(errorText(r.remoteErrorCode, r.remoteError))
               + u'\n';
    for (const PlannedFile &f : r.plan.files) {
        if (f.action == FileAction::Unchanged)
            continue;
        const QString mark = f.action == FileAction::Upload   ? u"+"_s
                             : f.action == FileAction::Update ? u"~"_s
                             : f.action == FileAction::Delete ? u"-"_s
                                                              : u"!"_s;
        out += u"  "_s + mark + u' ' + f.path
               + (f.action == FileAction::Conflict ? u"  ("_s + actionText(f.action, f.deletion) + u')' : QString())
               + u'\n';
    }
    if (!selection.leftOut.isEmpty()) {
        out += tr("Left out:") + u'\n';
        for (const LeftOut &l : selection.leftOut)
            out += u"  "_s + l.path + u"  ("_s + reasonText(l.reason) + u")\n"_s;
    }
    for (qsizetype i = 0; i < r.warningCodes.size(); ++i)
        out += tr("Note: %1").arg(warningText(r.warningCodes.at(i), r.warningDetails.value(i))) + u'\n';
    return out;
}

int exitFor(const QString &code)
{
    if (code == u"conflicts" || code == u"not-fast-forward")
        return ExitConflict;
    if (code == u"not-found" || code == u"nothing")
        return ExitNotFound;
    if (code == u"network" || code == u"timeout" || code == u"auth" || code == u"no-token" || code == u"no-password"
        || code == u"no-page-password" || code == u"rate-limited" || code == u"server")
        return ExitUnavailable;
    if (code == u"no-push" || code == u"forbidden" || code == u"archived")
        return ExitRefused;
    return ExitInternal;
}

enum class Operation { Preview, Publish, Unpublish };

Result operate(Call &call, Operation operation)
{
    auto store = storeOf(call);
    if (!store)
        return call.failed();
    auto t = findTarget(call, *store, call.str(u"target"_s));
    if (!t)
        return call.failed();
    const bool force = call.flag(u"force"_s);
    if (force && !call.ctx.globals.allowDestructive)
        return call.fail(ExitRefused, u"destructive_refused"_s,
                         tr("--force replaces files changed at the target: run it with --allow-destructive"));
    if (call.dryRun() && operation == Operation::Publish)
        operation = Operation::Preview;
    VaultIndex *index = call.index();
    Vault *vault = call.vault();
    if (!index || !vault)
        return call.failed();
    const QDateTime now = call.ctx.now();
    if (operation != Operation::Unpublish && t->protect && secretOf(call, *t, u"pages"_s).isEmpty())
        return call.fail(ExitUnavailable, u"no_page_password"_s, errorText(u"no-page-password"_s));
    PreparedSite prepared = prepareSite(
            *index, *t,
            [vault](const VaultPath &path) -> std::optional<QByteArray> {
                bool ok = false;
                QByteArray data = vault->readBinary(path, &ok);
                return ok ? std::optional<QByteArray>(std::move(data)) : std::nullopt;
            },
            QFileInfo(call.ctx.vaultRoot()).fileName(), now);
    for (const VaultPath &p : prepared.unreadable)
        call.warn(u"unreadable"_s, tr("%1 could not be read and is left out").arg(p), {{u"path"_s, p}});
    if (operation != Operation::Unpublish && prepared.selection.pages.isEmpty())
        return call.fail(ExitNotFound, u"nothing_to_publish"_s, errorText(u"nothing"_s),
                         {{u"leftOut"_s, int(prepared.selection.leftOut.size())}});
    if (prepared.input.protect)
        prepared.input.protection.password = secretOf(call, *t, u"pages"_s);
    const SiteBuild site = operation == Operation::Unpublish ? SiteBuild{} : buildSite(prepared.input);
    if (!site.errorCode.isEmpty())
        return call.fail(exitFor(site.errorCode), u"publish_failed"_s, errorText(site.errorCode));
    const PublishManifest manifest = store->manifest(t->id);
    Publisher publisher(transport());
    publisher.setClock([now] { return now; });
    if (call.ctx.globals.verbose) {
        QObject::connect(&publisher, &Publisher::progress, &publisher, [](const QString &phase, int done, int total) {
            QTextStream err(stderr);
            err << phase << ' ' << done << '/' << total << '\n';
        });
    }
    std::optional<PublishReport> report;
    QEventLoop loop;
    const auto done = [&report, &loop](const PublishReport &r) {
        report = r;
        loop.quit();
    };
    const PublishCredentials credentials = credentialsOf(call, *t);
    switch (operation) {
    case Operation::Preview: publisher.preview(*t, site, manifest, credentials, done); break;
    case Operation::Publish: publisher.publish(*t, site, manifest, credentials, force, done); break;
    case Operation::Unpublish: publisher.unpublish(*t, manifest, credentials, force, done); break;
    }
    if (!report)
        loop.exec();
    const PublishReport &r = *report;
    if (operation != Operation::Preview) {
        if (r.manifestChanged)
            store->saveManifest(t->id, r.manifest);
        PublishStatus status;
        status.at = now;
        status.operation = operation == Operation::Unpublish ? u"unpublish"_s : u"publish"_s;
        status.ok = r.ok;
        status.url = r.url;
        status.pages = site.pages;
        status.uploaded = r.uploaded + r.updated;
        status.deleted = r.deleted;
        status.unchanged = r.unchanged;
        status.conflicts = int(r.conflicts.size());
        status.commit = r.commit;
        status.errorCode = r.errorCode;
        status.error = r.error;
        store->setStatus(t->id, status);
    }
    for (qsizetype i = 0; i < r.warningCodes.size(); ++i)
        call.warn(r.warningCodes.at(i), warningText(r.warningCodes.at(i), r.warningDetails.value(i)));
    if (site.fragmentsRemoved > 0)
        call.warn(u"encrypted_fragments_left_out"_s,
                  tr("%1 encrypted fragment(s) were left out of the published pages").arg(site.fragmentsRemoved));
    QJsonObject o = planJson(r, prepared.selection, site);
    o.insert(u"target"_s, QJsonObject{{u"id"_s, t->id}, {u"name"_s, t->name}, {u"kind"_s, kindName(t->kind)}});
    o.insert(u"operation"_s, r.operation);
    o.insert(u"url"_s, r.url);
    if (!r.commit.isEmpty()) {
        o.insert(u"commit"_s, r.commit);
        o.insert(u"commitUrl"_s, r.commitUrl);
    }
    if (!r.remoteErrorCode.isEmpty())
        o.insert(u"remoteError"_s, QJsonObject{{u"code"_s, r.remoteErrorCode},
                                               {u"message"_s, errorText(r.remoteErrorCode, r.remoteError)}});
    if (operation != Operation::Preview) {
        o.insert(u"done"_s, QJsonObject{{u"uploaded"_s, r.uploaded},
                                        {u"updated"_s, r.updated},
                                        {u"deleted"_s, r.deleted},
                                        {u"unchanged"_s, r.unchanged}});
    }
    QJsonArray failures;
    for (const FileFailure &f : r.failures)
        failures.append(QJsonObject{{u"path"_s, f.path}, {u"code"_s, f.code}, {u"message"_s, f.message}});
    if (!failures.isEmpty())
        o.insert(u"failures"_s, failures);
    if (!r.ok) {
        o.insert(u"errorCode"_s, r.errorCode);
        return call.fail(exitFor(r.errorCode), r.errorCode == u"conflicts"_s ? u"conflicts"_s : u"publish_failed"_s,
                         errorText(r.errorCode, r.error), o);
    }
    return call.ok(o, planText(*t, r, prepared.selection, site, operation == Operation::Preview));
}

Command make(const QString &id, const char *summary, Handler run, QList<Arg> args, const char *help = nullptr)
{
    Command c;
    c.id = id;
    c.summary = cliText(summary);
    if (help)
        c.help = cliText(help);
    c.run = std::move(run);
    c.args = std::move(args);
    return c;
}

} // namespace

void addPublishCommands(QList<Command> &out)
{
    const Arg target = positional(u"target"_s, ArgType::String,
                                  QT_TRANSLATE_NOOP("brulion::cli", "A publishing target: its name or id"));
    const QList<Arg> fields{
            option(u"repository"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "GitHub: owner/name")),
            option(u"branch"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "GitHub: the branch (default gh-pages, made when missing)")),
            option(u"folder"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "GitHub: the folder in the branch (\"\" or docs); Nextcloud: the "
                                                     "folder of the account")),
            option(u"url"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "WebDAV: the folder's address")),
            option(u"server"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "Nextcloud: the server's address")),
            option(u"username"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "WebDAV, Nextcloud: the user name")),
            option(u"path"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "Folder: a folder on this device")),
            option(u"siteUrl"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "The address people open (a custom domain, a web server's)")),
            option(u"title"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "The site's title (default: the notebook's name)")),
            choice(u"content"_s, {u"site"_s, u"document"_s},
                   QT_TRANSLATE_NOOP("brulion::cli", "Web pages (default) or one HTML document")),
            choice(u"share"_s, {u"on"_s, u"off"_s},
                   QT_TRANSLATE_NOOP("brulion::cli", "Nextcloud: a public link (default on)")),
            choice(u"sharePassword"_s, {u"on"_s, u"off"_s},
                   QT_TRANSLATE_NOOP("brulion::cli",
                                     "Nextcloud: the link asks for a password (kept with publish secret --share)")),
            option(u"shareExpires"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "Nextcloud: the link's expiry date (never: none)")),
            choice(u"protect"_s, {u"on"_s, u"off"_s},
                   QT_TRANSLATE_NOOP("brulion::cli", "Password-protected pages: every page encrypted, opened with the "
                                                     "password in the browser (kept with publish secret --pages)")),
    };
    const QList<Arg> items{
            option(u"page"_s, ArgType::Ref, QT_TRANSLATE_NOOP("brulion::cli", "A page")),
            option(u"folder"_s, ArgType::Path, QT_TRANSLATE_NOOP("brulion::cli", "A folder with its subfolders")),
            option(u"tag"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "The pages with a tag (and its sub-tags)")),
            flag(u"notebook"_s, QT_TRANSLATE_NOOP("brulion::cli", "The whole notebook")),
    };
    const QList<Arg> options{
            choice(u"linkedPages"_s, {u"on"_s, u"off"_s},
                   QT_TRANSLATE_NOOP("brulion::cli", "The pages they link to are published too")),
            choice(u"onlyMarked"_s, {u"on"_s, u"off"_s},
                   QT_TRANSLATE_NOOP("brulion::cli",
                                     "Only pages marked publish: true (a chosen page is published anyway)")),
            choice(u"extensionPages"_s, {u"on"_s, u"off"_s},
                   QT_TRANSLATE_NOOP("brulion::cli", "Extension pages (their code) may be published")),
            choice(u"aiConversations"_s, {u"on"_s, u"off"_s},
                   QT_TRANSLATE_NOOP("brulion::cli", "Conversations with AI (AI/) may be published")),
    };

    Command c = make(u"publish.targets"_s,
                     QT_TRANSLATE_NOOP("brulion::cli", "The notebook's publishing targets on this device"),
                     publishTargets, {});
    out.append(c);

    QList<Arg> addArgs{choice(u"kind"_s, {u"github"_s, u"webdav"_s, u"nextcloud"_s, u"folder"_s},
                              QT_TRANSLATE_NOOP("brulion::cli", "Where it publishes"), true, true),
                       positional(u"name"_s, ArgType::String,
                                  QT_TRANSLATE_NOOP("brulion::cli", "Its name (\"Blog\", \"Przepisy\")"), true, true)};
    addArgs += fields;
    c = make(u"publish.add"_s, QT_TRANSLATE_NOOP("brulion::cli", "Add a publishing target"), publishAdd, addArgs,
             QT_TRANSLATE_NOOP("brulion::cli",
                               "A target is this device's, kept in the notebook's device state (as the app's); "
                               "what it publishes comes with publish include. Passwords never go on the command "
                               "line: publish secret reads them from standard input. GitHub uses the token the "
                               "app keeps (or BRULION_GITHUB_TOKEN)."));
    c.writes = true;
    c.mcp = false;
    out.append(c);

    QList<Arg> setArgs{target, option(u"name"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "A new name"))};
    setArgs += fields;
    c = make(u"publish.set"_s, QT_TRANSLATE_NOOP("brulion::cli", "Change a publishing target"), publishSet, setArgs);
    c.writes = true;
    c.mcp = false;
    out.append(c);

    QList<Arg> includeArgs{target};
    includeArgs += items;
    includeArgs += options;
    c = make(
            u"publish.include"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "Publish a page, a folder, a tag or the notebook with a target"),
            [](Call &call) { return publishInclude(call, true); }, includeArgs,
            QT_TRANSLATE_NOOP("brulion::cli",
                              "Never published: hidden folders, conflict copies, encrypted pages, files and "
                              "fragments, pages marked publish: false; extension pages, conversations with AI and "
                              "templates only when chosen."));
    c.writes = true;
    c.mcp = false;
    out.append(c);

    QList<Arg> excludeArgs{target};
    excludeArgs += items;
    c = make(
            u"publish.exclude"_s, QT_TRANSLATE_NOOP("brulion::cli", "Stop publishing an item with a target"),
            [](Call &call) { return publishInclude(call, false); }, excludeArgs,
            QT_TRANSLATE_NOOP("brulion::cli", "Its pages go from the site with the next publish."));
    c.writes = true;
    c.mcp = false;
    out.append(c);

    c = make(u"publish.secret"_s, QT_TRANSLATE_NOOP("brulion::cli", "Keep a target's password on this device"),
             publishSecret,
             {target,
              stdinable(option(u"password"_s, ArgType::Text,
                               QT_TRANSLATE_NOOP("brulion::cli", "The password (better: --stdin); empty forgets it"))),
              flag(u"share"_s, QT_TRANSLATE_NOOP("brulion::cli", "Nextcloud: the public link's password")),
              flag(u"pages"_s, QT_TRANSLATE_NOOP("brulion::cli", "The password of protected pages (--protect on)"))},
             QT_TRANSLATE_NOOP("brulion::cli",
                               "Kept like the app's Nextcloud password, in an owner-only file of the app's data "
                               "folder, never in the notebook. BRULION_PUBLISH_PASSWORD, "
                               "BRULION_PUBLISH_SHARE_PASSWORD and BRULION_PUBLISH_PAGES_PASSWORD override them."));
    c.writes = true;
    c.mcp = false;
    out.append(c);

    c = make(u"publish.remove"_s, QT_TRANSLATE_NOOP("brulion::cli", "Forget a publishing target"), publishRemove,
             {target},
             QT_TRANSLATE_NOOP("brulion::cli", "What it published stays: publish unpublish removes that first."));
    c.writes = true;
    c.destructive = true;
    c.mcp = false;
    out.append(c);

    c = make(
            u"publish.preview"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "What a publish would upload, change and remove (nothing written)"),
            [](Call &call) { return operate(call, Operation::Preview); }, {target},
            QT_TRANSLATE_NOOP("brulion::cli",
                              "The target is looked at (offline: the plan from the last publish); the pages left "
                              "out are listed with the reason."));
    out.append(c);

    c = make(
            u"publish.run"_s, QT_TRANSLATE_NOOP("brulion::cli", "Publish a target's pages"),
            [](Call &call) { return operate(call, Operation::Publish); },
            {target, flag(u"force"_s, QT_TRANSLATE_NOOP("brulion::cli",
                                                        "Replace files changed at the target since the last publish "
                                                        "(needs --allow-destructive)"))},
            QT_TRANSLATE_NOOP("brulion::cli",
                              "Only what changed is sent (GitHub: one commit). A file changed at the target since "
                              "the last publish stops it (exit 4 conflicts) until --force. --dry-run is publish "
                              "preview."));
    c.writes = true;
    out.append(c);

    c = make(
            u"publish.unpublish"_s, QT_TRANSLATE_NOOP("brulion::cli", "Remove what a target published"),
            [](Call &call) { return operate(call, Operation::Unpublish); },
            {target, flag(u"force"_s, QT_TRANSLATE_NOOP("brulion::cli", "Also files changed at the target since"))},
            QT_TRANSLATE_NOOP("brulion::cli",
                              "Its files go (GitHub: in one commit), a Nextcloud link too; files the target had "
                              "of its own stay."));
    c.writes = true;
    c.destructive = true;
    out.append(c);
}

} // namespace brulion::cli
