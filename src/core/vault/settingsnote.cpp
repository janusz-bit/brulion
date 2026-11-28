// The notebook's settings in a note (settingsnote.h).
#include "vault/settingsnote.h"

#include "markdown/frontmatter.h"

#include <QJsonDocument>

using namespace Qt::StringLiterals;

namespace brulion::settingsnote {

namespace {

constexpr auto MarkKey = "brulion"_L1;

QByteArray jsonOf(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }

/// What a new note says, in the notebook's language.
QString newNoteText(QStringView language)
{
    if (language == u"pl")
        return u"---\nbrulion: settings\n---\n# Ustawienia notatnika\n\nUstawienia, które dzielą wszystkie "
               u"urządzenia tego notatnika (docs: Ustawienia notatnika). Zmieniaj je tutaj albo w Ustawieniach "
               u"→ Notatnik.\n"_s;
    return u"---\nbrulion: settings\n---\n# Notebook settings\n\nThe settings every device of this notebook "
           u"shares. Change them here or in Settings → Notebook.\n"_s;
}

} // namespace

QStringList noteNames() { return {u"Notebook settings.md"_s, u"Ustawienia notatnika.md"_s}; }

QString noteName(QStringView language)
{
    return language == u"pl" ? u"Ustawienia notatnika.md"_s : u"Notebook settings.md"_s;
}

bool isSettingsNote(QStringView content)
{
    const QVariantMap data = markdown::parseFrontmatter(content).data;
    return data.value(QString(MarkKey)).toString().trimmed().compare(u"settings"_s, Qt::CaseInsensitive) == 0;
}

QJsonObject noteSettings(QStringView content)
{
    QVariantMap data = markdown::parseFrontmatter(content).data;
    data.remove(QString(MarkKey));
    return QJsonObject::fromVariantMap(data);
}

ParsedVaultConfig mergedConfig(QByteArrayView configJson, QStringView noteContent)
{
    return mergedOver(parseVaultConfig(configJson), noteContent);
}

QJsonObject changedSettings(const VaultConfig &before, const VaultConfig &after)
{
    const QJsonObject a = vaultConfigToJson(before), b = vaultConfigToJson(after);
    QJsonObject out;
    for (auto it = b.constBegin(); it != b.constEnd(); ++it)
        if (a.value(it.key()) != it.value())
            out.insert(it.key(), it.value());
    return out;
}

ParsedVaultConfig mergedOver(const ParsedVaultConfig &base, QStringView noteContent)
{
    if (!isSettingsNote(noteContent))
        return base;
    // (The settings as they are: config.json's keys — or, without it, those taken from .obsidian/, the
    // ones that differ from the defaults: a key left out keeps following another, as weeklyFolder
    // follows journalsFolder — then the note's.)
    QJsonObject combined = base.raw;
    if (combined.isEmpty()) {
        const QJsonObject known = vaultConfigToJson(base.config), defaults = vaultConfigToJson(VaultConfig{});
        for (auto it = known.constBegin(); it != known.constEnd(); ++it)
            if (defaults.value(it.key()) != it.value())
                combined.insert(it.key(), it.value());
    }
    QStringList warnings = base.warnings;
    QStringList seen = parseVaultConfig(jsonOf(combined)).warnings;
    const QJsonObject note = noteSettings(noteContent);
    for (auto it = note.constBegin(); it != note.constEnd(); ++it) {
        QJsonObject candidate = combined;
        candidate.insert(it.key(), it.value());
        const QStringList now = parseVaultConfig(jsonOf(candidate)).warnings;
        // (An invalid value: config.json's — or the default — kept, never a reset by a typo.)
        if (now.size() > seen.size()) {
            for (const QString &w : now)
                if (!seen.contains(w))
                    warnings << u"Notebook settings: "_s + w;
            continue;
        }
        combined = candidate;
        seen = now;
    }
    ParsedVaultConfig out = parseVaultConfig(jsonOf(combined));
    out.warnings = warnings;
    return out;
}

QString migratedNote(QByteArrayView configJson, const QString &note, QStringView language)
{
    const ParsedVaultConfig parsed = parseVaultConfig(configJson);
    const QJsonObject defaults = vaultConfigToJson(VaultConfig{});
    QString out = note.isEmpty() ? newNoteText(language) : note;
    if (!isSettingsNote(out))
        out = withSetting(out, QString(MarkKey), QJsonValue(u"settings"_s));
    const QJsonObject has = noteSettings(out);
    // The keys config.json says (not its version), where they differ from the defaults; a key the
    // note has keeps the note's value.
    for (auto it = parsed.raw.constBegin(); it != parsed.raw.constEnd(); ++it) {
        if (it.key() == u"version" || has.contains(it.key()) || defaults.value(it.key()) == it.value())
            continue;
        out = withSetting(out, it.key(), it.value());
    }
    return out;
}

QString withSetting(const QString &note, const QString &key, const QJsonValue &value)
{
    if (value.isUndefined())
        return markdown::removeFrontmatterValue(note, key);
    return markdown::setFrontmatterValue(note, key, value.toVariant());
}

} // namespace brulion::settingsnote
