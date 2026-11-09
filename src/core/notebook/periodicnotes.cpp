#include "notebook/periodicnotes.h"

#include "markdown/templates.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace brulion::notebook {

VaultPath existingPeriodicNote(const Vault &vault, periodic::Period period, QDate date)
{
    const VaultConfig &config = vault.config();
    const QDate start = periodic::periodStart(period, date);
    if (!start.isValid())
        return {};
    const QString folder = periodic::folder(period, config);
    const QString preferred = folder.isEmpty() ? QString() : folder + u'/';
    const auto inPreferred = [&](const QString &path) { return preferred.isEmpty() || path.startsWith(preferred); };
    QString found;
    // const: markdownFiles() may share the vault's cached list, which a non-const loop would copy.
    for (const QList<FileStat> notes = vault.markdownFiles(); const FileStat &f : notes) {
        if (periodic::dateFromPath(period, f.path, config) != start)
            continue;
        if (found.isEmpty() || (inPreferred(f.path) && !inPreferred(found)))
            found = f.path;
    }
    return found;
}

VaultPath periodicFileFor(const Vault &vault, periodic::Period period, QDate date)
{
    const QString path = periodic::pathFor(period, date, vault.config());
    if (path.isEmpty())
        return {};
    if (vault.exists(path) || QFileInfo::exists(vault.absolutePath(path)))
        return path;
    const QString existing = existingPeriodicNote(vault, period, date);
    return existing.isEmpty() ? path : existing;
}

QString newPeriodicText(Vault &vault, periodic::Period period, QDate date, const QDateTime &now, dates::UiLocale locale)
{
    const QString tpl = periodic::templatePath(period, vault.config());
    const QDate start = periodic::periodStart(period, date);
    if (!start.isValid() || tpl.isEmpty() || !vault.exists(tpl))
        return {};
    bool ok = false;
    const QString text = vault.readText(tpl, &ok);
    if (!ok)
        return {};
    markdown::TemplateVars vars;
    vars.now = QDateTime(start, now.time());
    vars.title = paths::pageTitle(periodicFileFor(vault, period, date));
    vars.locale = locale;
    vars.end = periodic::periodEnd(period, date);
    return markdown::applyTemplate(text, vars).text;
}

EnsuredJournal ensurePeriodicNote(Vault &vault, periodic::Period period, QDate date, const QDateTime &now,
                                  dates::UiLocale locale, ChangeSource source)
{
    EnsuredJournal out;
    const QString path = periodicFileFor(vault, period, date);
    if (path.isEmpty())
        return out;
    if (vault.exists(path) || QFileInfo::exists(vault.absolutePath(path))) {
        out.path = path;
        return out;
    }
    const QString content = newPeriodicText(vault, period, date, now, locale);
    switch (vault.writeTextIfUnchanged(path, content, QString(), source)) {
    case WriteOutcome::Written:
        out.created = true;
        out.path = path;
        break;
    case WriteOutcome::Changed: out.path = path; break;
    case WriteOutcome::Failed: out.error = vault.errorString(); break;
    }
    return out;
}

} // namespace brulion::notebook
