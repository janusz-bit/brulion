#include "notebook/journals.h"

#include "markdown/journal.h"
#include "markdown/templates.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace brulion::notebook {

dates::UiLocale contentLocale(QStringView language)
{
    return language == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish;
}

QString appLanguage(const QString &saved)
{
    if (saved == u"pl" || saved == u"en")
        return saved;
    return QLocale().name().startsWith(u"en"_s) ? u"en"_s : u"pl"_s;
}

VaultPath existingJournal(const Vault &vault, QDate date)
{
    const VaultConfig config = vault.config();
    const QString preferred = config.journalsFolder.isEmpty() ? QString() : config.journalsFolder + u'/';
    const auto inPreferred = [&](const QString &path) { return !preferred.isEmpty() && path.startsWith(preferred); };
    QString found;
    for (const FileStat &f : vault.markdownFiles()) {
        if (markdown::journalDateFromPath(f.path, config) != date)
            continue;
        // Several (e.g. "journals/" and "Journals/"): the configured spelling, else the first.
        if (found.isEmpty() || (inPreferred(f.path) && !inPreferred(found)))
            found = f.path;
    }
    return found;
}

VaultPath journalFileFor(const Vault &vault, QDate date)
{
    if (!date.isValid())
        return {};
    const QString path = markdown::journalPathForDate(date, vault.config());
    // Also check the disk: a note that just arrived (Syncthing) may not be known yet.
    if (vault.exists(path) || QFileInfo::exists(vault.absolutePath(path)))
        return path;
    // The day may have a note under another name the journal folder accepts: Logseq's
    // "2026_10_14.md", "Journals/" capitalised on another device, the previous file name format.
    // Never a second note for the same day next to it.
    const QString existing = existingJournal(vault, date);
    return existing.isEmpty() ? path : existing;
}

QString templateText(Vault &vault, const VaultPath &templatePath, const QDateTime &when, const QString &title,
                     dates::UiLocale locale)
{
    bool ok = false;
    const QString tpl = vault.readText(paths::normalize(templatePath), &ok);
    if (!ok)
        return {};
    markdown::TemplateVars vars;
    vars.now = when;
    vars.title = title;
    vars.locale = locale;
    return markdown::applyTemplate(tpl, vars).text;
}

QString newJournalText(Vault &vault, QDate date, const QDateTime &now, dates::UiLocale locale)
{
    const QString tpl = paths::normalize(vault.config().journalTemplate);
    if (!date.isValid() || tpl.isEmpty() || !vault.exists(tpl))
        return {};
    return templateText(vault, tpl, QDateTime(date, now.time()), paths::pageTitle(journalFileFor(vault, date)), locale);
}

EnsuredJournal ensureJournal(Vault &vault, QDate date, const QDateTime &now, dates::UiLocale locale,
                             ChangeSource source)
{
    EnsuredJournal out;
    const QString path = journalFileFor(vault, date);
    if (path.isEmpty())
        return out;
    if (vault.exists(path) || QFileInfo::exists(vault.absolutePath(path))) {
        out.path = path;
        return out;
    }
    const QString content = newJournalText(vault, date, now, locale);
    // A note created by another program in the meantime stays as it is.
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

QString appendBlock(const QString &content, const QString &block)
{
    static const QRegularExpression emptyBullet(u"^\\s*[-*+]\\s*$"_s);
    if (block.trimmed().isEmpty())
        return content;
    const QString eol = text::detectEol(content);
    // A template often ends with an empty bullet ("- "): fill that instead of adding below it.
    QList<text::Line> existing = text::splitLines(content);
    while (!existing.isEmpty() && existing.last().text.trimmed().isEmpty())
        existing.removeLast();
    if (!existing.isEmpty() && emptyBullet.match(existing.last().text).hasMatch())
        existing.removeLast();
    QString out = text::joinLines(existing);
    if (!out.isEmpty() && !out.endsWith(u'\n') && !out.endsWith(u'\r'))
        out += eol;
    // A leading BOM survives the trimming above (it is part of the first line's text).
    out += QString(block).remove(u'\r').replace(u'\n', eol) + eol;
    return out;
}

QString blockFromText(const QString &text)
{
    static const QRegularExpression listMarker(u"^([-*+]|\\d+[.)])\\s"_s);
    QStringList lines = QString(text).remove(u'\r').split(u'\n');
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty())
        lines.removeFirst();
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    if (lines.isEmpty())
        return {};
    QString first = lines.first().trimmed();
    if (!listMarker.match(first).hasMatch())
        first.prepend(u"- "_s);
    QString block = first;
    for (qsizetype i = 1; i < lines.size(); ++i)
        block += u"\n  "_s + lines.at(i).trimmed();
    return block;
}

} // namespace brulion::notebook
