#include "extensions/share.h"

#include "extensions/builtin.h"
#include "index/vaultindex.h"
#include "markdown/parse.h"
#include "util/paths.h"
#include "vault/localstore.h"
#include "vault/vault.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace brulion::extensions {

QStringList requiredNames(const QString &code)
{
    // b.require("…"), b.require('…'), b.require "…" — a name written out, on one line.
    static const QRegularExpression call(uR"re(\bb\s*\.\s*require\s*\(?\s*(?:"([^"\n]+)"|'([^'\n]+)'))re"_s);
    QStringList out;
    for (auto it = call.globalMatch(code); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QString name = (m.hasCaptured(1) ? m.captured(1) : m.captured(2)).trimmed();
        if (!name.isEmpty() && !out.contains(name))
            out << name;
    }
    return out;
}

QString runIdOf(const PageExtension &page)
{
    const QString builtin = builtinFor(page.overrides.isEmpty() ? page.id : page.overrides);
    return builtin.isEmpty() ? page.id : builtin;
}

CopyReport copyExtension(const VaultIndex &index, const VaultPath &path, Vault &target, bool replace)
{
    CopyReport report;
    // The page and, breadth first, the extension pages its code requires (each once: a cycle of
    // requires is copied as it is).
    QStringList queue{paths::normalize(path)}, seen;
    while (!queue.isEmpty()) {
        const VaultPath current = queue.takeFirst();
        if (seen.contains(current))
            continue;
        seen << current;
        const ParsedNote *note = index.note(current);
        if (!note || !isExtensionPage(*note)) {
            if (current == seen.first()) {
                report.error = u"“%1” is no extension page"_s.arg(paths::pageTitle(current));
                return report;
            }
            continue; // (a required page that is no extension: b.require says so where it runs)
        }
        const QString content = index.content(current);
        const PageExtension page = readPageExtension(current, *note, content);
        for (const QString &name : requiredNames(page.code))
            if (const auto required = index.resolveLink(name, current); required && paths::isMarkdown(*required))
                queue << *required;
        bool ok = false;
        const QString there = target.exists(current) ? target.readText(current, &ok) : QString();
        if (ok && there == content) {
            report.unchanged << current;
            report.pages << page;
            continue;
        }
        if (ok && !replace) {
            report.conflicts << current;
            continue;
        }
        if (!target.writeText(current, content, ChangeSource::User)) {
            report.error = target.errorString();
            return report;
        }
        report.written << current;
        report.pages << page;
    }
    return report;
}

void approveInStore(LocalStore *store, const PageExtension &page)
{
    if (!store || page.hash.isEmpty())
        return;
    QStringList approved;
    for (const QJsonValue v : store->get(approvedStoreKey()).toArray())
        if (const QString hash = v.toString(); hash.size() == 64 && hash != page.hash && !approved.contains(hash))
            approved << hash;
    approved << page.hash;
    while (approved.size() > kMaxApprovals)
        approved.removeFirst();
    store->set(approvedStoreKey(), QJsonArray::fromStringList(approved));
    QJsonObject code{{u"hash"_s, page.hash},
                     {u"permissions"_s, QJsonArray::fromStringList(page.permissions)},
                     {u"title"_s, page.title}};
    // (Code too long to keep keeps its hash: the next approval shows all of the new code.)
    if (page.code.size() <= kMaxApprovedCode)
        code.insert(u"code"_s, page.code);
    store->set(approvedCodeStoreKey(runIdOf(page)), code);
}

QVariantMap approvedCode(const LocalStore *store, const QString &id)
{
    if (!store)
        return {};
    const QJsonObject object = store->get(approvedCodeStoreKey(id)).toObject();
    if (object.value(u"hash"_s).toString().size() != 64)
        return {};
    return object.toVariantMap();
}

} // namespace brulion::extensions
