#include "suggestionmodel.h"

#include "blocktext.h"
#include "editorhost.h"

#include "dates/format.h"
#include "dates/local.h"
#include "dates/natural.h"
#include "markdown/journal.h"
#include "text/text.h"
#include "util/paths.h"

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

QVariantMap item(const QString &kind, const QString &title, const QString &subtitle, const QString &icon,
                 const QString &insertText)
{
    return {{u"kind"_s, kind},         {u"title"_s, title},  {u"subtitle"_s, subtitle}, {u"icon"_s, icon},
            {u"insertText"_s, insertText}, {u"hint"_s, QString()}, {u"command"_s, QString()}, {u"path"_s, QString()},
            {u"line"_s, -1},           {u"blockId"_s, QString()}};
}

} // namespace

SuggestionModel::SuggestionModel(QObject *parent) : QAbstractListModel(parent) {}

int SuggestionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QHash<int, QByteArray> SuggestionModel::roleNames() const
{
    return {{TitleRole, "title"},     {SubtitleRole, "subtitle"}, {IconRole, "icon"},     {InsertTextRole, "insertText"},
            {KindRole, "kind"},       {HintRole, "hint"},         {CommandRole, "command"}, {PathRole, "path"},
            {LineRole, "line"},       {BlockIdRole, "blockId"}};
}

QVariant SuggestionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const QVariantMap m = m_items[index.row()].toMap();
    if (role == Qt::DisplayRole)
        role = TitleRole;
    const QByteArray name = roleNames().value(role);
    return name.isEmpty() ? QVariant() : m.value(QString::fromLatin1(name));
}

void SuggestionModel::setHost(EditorHost *host)
{
    if (m_host == host)
        return;
    if (m_host)
        disconnect(m_host, nullptr, this, nullptr);
    m_host = host;
    if (m_host)
        connect(m_host, &EditorHost::indexChanged, this, &SuggestionModel::refresh);
    Q_EMIT hostChanged();
    refresh();
}

void SuggestionModel::setMode(const QString &mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    Q_EMIT modeChanged();
    refresh();
}

void SuggestionModel::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    Q_EMIT filterChanged();
    refresh();
}

void SuggestionModel::setToken(const QString &token)
{
    if (m_token == token)
        return;
    m_token = token;
    Q_EMIT tokenChanged();
    refresh();
}

void SuggestionModel::setLocale(const QString &locale)
{
    const bool english = locale == u"en";
    if (english == m_english)
        return;
    m_english = english;
    Q_EMIT localeChanged();
    refresh();
}

void SuggestionModel::setNow(const QString &now)
{
    if (m_fixedNow == now)
        return;
    m_fixedNow = now;
    Q_EMIT nowChanged();
    refresh();
}

void SuggestionModel::setCurrentPath(const QString &path)
{
    if (m_currentPath == path)
        return;
    m_currentPath = path;
    Q_EMIT currentPathChanged();
}

void SuggestionModel::setLimit(int limit)
{
    limit = std::max(1, limit);
    if (m_limit == limit)
        return;
    m_limit = limit;
    Q_EMIT limitChanged();
    refresh();
}

QVariantMap SuggestionModel::detect(const QString &text, int cursor) const
{
    return completionTriggerAt(text, cursor).toMap();
}

void SuggestionModel::setTrigger(const QVariantMap &trigger)
{
    const QString mode = trigger.value(u"mode"_s).toString();
    const QString filter = trigger.value(u"filter"_s).toString();
    const QString token = trigger.value(u"token"_s).toString();
    const bool changed = mode != m_mode || filter != m_filter || token != m_token;
    if (!changed)
        return;
    const bool modeChange = mode != m_mode;
    const bool filterChange = filter != m_filter;
    const bool tokenChange = token != m_token;
    m_mode = mode;
    m_filter = filter;
    m_token = token;
    if (modeChange)
        Q_EMIT modeChanged();
    if (filterChange)
        Q_EMIT filterChanged();
    if (tokenChange)
        Q_EMIT tokenChanged();
    refresh();
}

QVariantMap SuggestionModel::get(int row) const
{
    return row >= 0 && row < m_items.size() ? m_items[row].toMap() : QVariantMap();
}

void SuggestionModel::refresh()
{
    QVariantList items;
    if (m_mode == u"pages")
        items = pageItems();
    else if (m_mode == u"blocks")
        items = blockItems();
    else if (m_mode == u"tags")
        items = tagItems();
    else if (m_mode == u"slash")
        items = slashItems();
    else if (m_mode == u"date")
        items = dateItems();
    else if (m_mode == u"templates")
        items = templateItems();
    if (items.size() > m_limit)
        items = items.mid(0, m_limit);
    if (items == m_items)
        return;
    const int oldCount = int(m_items.size());
    beginResetModel();
    m_items = items;
    endResetModel();
    if (oldCount != m_items.size())
        Q_EMIT countChanged();
}

QDateTime SuggestionModel::currentDateTime() const
{
    if (!m_fixedNow.isEmpty()) {
        if (const auto spec = dates::parseDateSpec(m_fixedNow))
            return spec->toLocalDateTime();
    }
    return QDateTime::currentDateTime();
}

QVariantList SuggestionModel::pageItems() const
{
    QVariantList out;
    if (!m_host)
        return out;
    const QString typed = m_filter.trimmed();
    const QString typedKey = text::foldKey(typed);
    bool exact = false;
    for (const QVariant &v : m_host->suggestPages(typed, m_limit)) {
        const QVariantMap p = v.toMap();
        const QString title = p.value(u"title"_s).toString();
        const QString alias = p.value(u"alias"_s).toString();
        const QString path = p.value(u"path"_s).toString();
        QString folder = p.value(u"folder"_s).toString();
        if (folder.isEmpty())
            folder = paths::dirname(path);
        if (text::foldKey(title) == typedKey || (!alias.isEmpty() && text::foldKey(alias) == typedKey))
            exact = true;
        // The title alone may name another page of that name (the shortest path wins, docs/SYNTAX.md
        // §2): then the link names this one by its shortest unique path ("Projekty/Dom").
        QString target = path.isEmpty() ? title : m_host->linkTarget(path, m_currentPath);
        if (target.isEmpty())
            target = title;
        QVariantMap it = alias.isEmpty()
                                 ? item(u"page"_s, title, folder, u"file-text"_s, target)
                                 : item(u"alias"_s, alias, u"→ "_s + title, u"file-text"_s, target + u'|' + alias);
        it[u"path"_s] = path;
        out.append(it);
    }
    if (!typed.isEmpty() && !exact) {
        QVariantMap it = item(u"create"_s, typed, tr("New page"), u"plus"_s, typed);
        out.append(it);
    }
    return out;
}

QVariantList SuggestionModel::blockItems() const
{
    QVariantList out;
    if (!m_host)
        return out;
    for (const QVariant &v : m_host->suggestBlocks(m_filter.trimmed(), m_limit)) {
        const QVariantMap b = v.toMap();
        const QString path = b.value(u"path"_s).toString();
        QString t = b.value(u"text"_s).toString().simplified();
        if (t.size() > 90)
            t = t.left(89) + u'…';
        QVariantMap it = item(u"block"_s, t, paths::pageTitle(path), u"link"_s, QString());
        it[u"path"_s] = path;
        it[u"line"_s] = b.value(u"line"_s, -1).toInt();
        it[u"blockId"_s] = b.value(u"blockId"_s).toString();
        out.append(it);
    }
    return out;
}

QVariantList SuggestionModel::tagItems() const
{
    QVariantList out;
    if (!m_host)
        return out;
    const QString typed = m_filter.trimmed().normalized(QString::NormalizationForm_C);
    // Tags in use and pages (a tag names a page, docs/SYNTAX.md §3); the one already typed out
    // (in any letter case) has nothing to complete.
    for (const QVariant &v : m_host->suggestTags(m_filter.trimmed(), m_limit)) {
        const QVariantMap t = v.toMap();
        const QString tag = t.value(u"tag"_s).toString();
        if (tag.isEmpty() || tag.normalized(QString::NormalizationForm_C).compare(typed, Qt::CaseInsensitive) == 0)
            continue;
        const int count = t.value(u"count"_s).toInt();
        const QString path = t.value(u"path"_s).toString();
        const QString title = t.value(u"title"_s).toString();
        QString subtitle;
        QString icon = u"hash"_s;
        if (t.value(u"kind"_s).toString() == u"page") {
            const QString folder = t.value(u"folder"_s).toString();
            subtitle = folder.isEmpty() ? tr("Page") : tr("Page in %1").arg(folder);
            icon = u"file-text"_s;
        } else {
            subtitle = count > 0 ? tr("%n block(s)", "", count) : QString();
            // The page the tag opens, when its name says something else (an alias, a folder).
            if (!title.isEmpty() && title.compare(paths::basename(tag), Qt::CaseInsensitive) != 0)
                subtitle = subtitle.isEmpty() ? QString(u"→ "_s + title) : QString(subtitle + u" · → "_s + title);
        }
        QVariantMap it = item(u"tag"_s, u"#"_s + tag, subtitle, icon, tag);
        it[u"path"_s] = path;
        out.append(it);
    }
    return out;
}

QVariantList SuggestionModel::slashItems() const
{
    QVariantList out;
    for (const SlashCommand *c : filterSlashCommands(m_filter)) {
        // Subtitle: what the command writes ("⏰ data godzina"), not the name in the other language.
        QVariantMap it = item(u"command"_s, m_english ? c->nameEn : c->namePl, m_english ? c->syntaxEn : c->syntaxPl,
                              c->icon, QString());
        it[u"hint"_s] = c->hint;
        it[u"command"_s] = c->id;
        out.append(it);
    }
    return out;
}

QVariantList SuggestionModel::dateItems() const
{
    QVariantList out;
    const QDateTime now = currentDateTime();
    const VaultConfig config = vaultConfigFromMap(m_host ? m_host->vaultConfig() : QVariantMap());
    const dates::UiLocale locale = m_english ? dates::UiLocale::English : dates::UiLocale::Polish;
    const bool reminderToken = m_token.startsWith(u"⏰");
    const bool at = m_token == u"@";

    const auto addSpec = [&](DateSpec spec, const QString &phrase) {
        if (reminderToken && !spec.time)
            spec.time = config.defaultReminderTime;
        const QString value = spec.toString();
        const QString human = dates::formatHuman(spec, locale, now.date());
        const QString weekday = dates::formatDateShort(spec.date, locale, now.date().year());
        const QString subtitle = human == weekday || human.startsWith(weekday) ? human : human + u" · "_s + weekday;
        if (at) {
            const QString journal =
                    u"[["_s + paths::pageTitle(markdown::journalPathForDate(spec.date, config)) + u"]]"_s;
            QVariantMap j = item(u"journal"_s, journal, subtitle, u"book-open"_s, journal);
            j[u"hint"_s] = phrase;
            out.append(j);
            QVariantMap d = item(u"date"_s, value, subtitle, u"calendar"_s, u"📅 "_s + value);
            out.append(d);
            if (spec.time)
                out.append(item(u"reminder"_s, u"⏰ "_s + value, subtitle, u"bell"_s, u"⏰ "_s + value));
            return;
        }
        QVariantMap d = item(u"date"_s, value, subtitle, reminderToken ? u"bell"_s : u"calendar"_s, value);
        d[u"hint"_s] = phrase;
        out.append(d);
    };

    const QString phrase = m_filter.trimmed();
    if (!phrase.isEmpty()) {
        if (const auto found = dates::parseNaturalDate(phrase, now, config.weekStartsOn, locale);
            found && found->start == 0 && found->length == phrase.size()) {
            addSpec(found->spec, phrase);
            return out;
        }
    }
    if (at && phrase.isEmpty())
        return out; // "@" alone: wait for a phrase (e-mail addresses, mentions)
    // Suggestions: common phrases matching what was typed so far.
    const QStringList phrases = m_english
                                        ? QStringList{u"today"_s, u"tomorrow"_s, u"day after tomorrow"_s, u"next monday"_s,
                                                      u"next week"_s, u"in 2 hours"_s}
                                        : QStringList{u"dziś"_s, u"jutro"_s, u"pojutrze"_s, u"w poniedziałek"_s,
                                                      u"za tydzień"_s, u"za 2 godziny"_s};
    const QString key = text::foldKey(phrase);
    for (const QString &p : phrases) {
        if (!key.isEmpty() && !text::foldKey(p).startsWith(key))
            continue;
        if (!reminderToken && p.contains(u"2"_s))
            continue; // hours only make sense for reminders
        if (const auto found = dates::parseNaturalDate(p, now, config.weekStartsOn, locale))
            addSpec(found->spec, p);
    }
    return out;
}

QVariantList SuggestionModel::templateItems() const
{
    QVariantList out;
    if (!m_host)
        return out;
    const QString key = text::foldKey(m_filter.trimmed());
    for (const QVariant &v : m_host->templates()) {
        const QVariantMap t = v.toMap();
        const QString title = t.value(u"title"_s).toString();
        if (!key.isEmpty() && !text::foldKey(title).contains(key))
            continue;
        QVariantMap it = item(u"template"_s, title, t.value(u"path"_s).toString(), u"sparkles"_s, QString());
        it[u"path"_s] = t.value(u"path"_s).toString();
        out.append(it);
    }
    return out;
}

} // namespace brulion::editor
