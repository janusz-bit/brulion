// Shared fixtures of the integration tests: a realistic Polish sample vault (journals, pages,
// tasks with recurrence, reminders and an alarm, calc blocks, table formulas, block references,
// an attachment, Logseq page properties, a CRLF note) and an Indexer that keeps a VaultIndex in
// step with a Vault the way the app's Workspace does. Every test pins "now" (2026-10-14 08:00,
// a Wednesday) and the time zone (Europe/Warsaw, set in initMain()).
#pragma once

#include "index/index.h"
#include "markdown/markdown.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QByteArray>
#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace integration {

using namespace brulion;
using namespace Qt::StringLiterals;

/// The fixed "now" of every scenario.
inline QDateTime now()
{
    return QDateTime(QDate(2026, 10, 14), QTime(8, 0));
}

inline QDate today()
{
    return now().date();
}

inline const QString kJournal13 = u"journals/2026-10-13.md"_s;
inline const QString kJournal14 = u"journals/2026-10-14.md"_s;
inline const QString kKitchen = u"Projekty/Remont kuchni.md"_s;
inline const QString kJan = u"Ludzie/Jan Kowalski.md"_s;
inline const QString kChores = u"Zadania domowe.md"_s;
inline const QString kShopping = u"Zakupy.md"_s;
inline const QString kPhoto = u"assets/kuchnia.png"_s;
inline const QString kConfig = u".brulion/config.json"_s;
inline constexpr int kArchiveNotes = 12;

inline QString archiveNote(int i)
{
    return u"Archiwum/Stara notatka %1.md"_s.arg(i, 2, 10, QLatin1Char('0'));
}

/// A few bytes that look like a PNG (never decoded by the core).
inline QByteArray photoBytes()
{
    QByteArray png("\x89PNG\r\n\x1a\n", 8);
    for (int i = 0; i < 256; ++i)
        png.append(char(i));
    return png;
}

inline const QString kJournal14Text = u"- Środa — plan dnia #dziennik\n"
                                      u"\t- [ ] Podlać kwiaty 📅 2026-10-14 🔁 co tydzień w środę ⏰ 2026-10-14 18:00\n"
                                      u"\t- [ ] Zadzwonić do mamy ⏰ 2026-10-14 12:30 #rodzina\n"
                                      u"- Notatki ze spotkania o [[Kuchnia|kuchni]]\n"
                                      u"\t- Ustalenia: płytki szare, fugi białe ^ustalenia\n"
                                      u"\t- Koszt robocizny ustalony z [[Jan Kowalski|Jankiem]]\n"_s;

inline const QString kKitchenText = u"---\n"
                                    u"aliases: [Kuchnia]\n"
                                    u"tags: [dom, remont]\n"
                                    u"---\n"
                                    u"# Remont kuchni\n"
                                    u"\n"
                                    u"Plan prac i koszty. Ustalenia z dziennika: ((ustalenia))\n"
                                    u"\n"
                                    u"| Produkt | Cena | Ilość | Razem |\n"
                                    u"| --- | ---: | ---: | ---: |\n"
                                    u"| Płytki | 45,50 | 12 | =[Cena]*[Ilość] |\n"
                                    u"| Klej | 32 | 3 | =[Cena]*[Ilość] |\n"
                                    u"| Suma | | | =sum(above) |\n"
                                    u"\n"
                                    u"```calc\n"
                                    u"płytki = 45,50 * 12\n"
                                    u"klej = 32 * 3\n"
                                    u"robocizna = 1200\n"
                                    u"łącznie = płytki + klej + robocizna\n"
                                    u"rabat = 10%\n"
                                    u"łącznie - rabat\n"
                                    u"```\n"
                                    u"\n"
                                    u"Do zapłaty po rabacie: `= łącznie - rabat` zł\n"
                                    u"\n"
                                    u"- [ ] Kupić płytki 📅 2026-10-16 ⏫ #zakupy\n"
                                    u"- [/] Zamówić fachowca ⏳ 2026-10-15\n"
                                    u"- [ ] Odebrać zamówienie ⏰! 2026-10-15 07:30\n"
                                    u"- Zdjęcie obecnej kuchni: ![[kuchnia.png]]\n"_s;

/// The sample vault: vault path -> file content.
inline QMap<QString, QByteArray> sampleVault()
{
    QMap<QString, QByteArray> files;
    const auto text = [&files](const QString &path, const QString &content) { files.insert(path, content.toUtf8()); };
    text(kConfig,
         u"{\n  \"version\": 1,\n  \"journalsFolder\": \"journals\",\n  \"defaultReminderTime\": \"09:00\"\n}\n"_s);
    text(kJournal13, u"- Spotkanie z [[Jan Kowalski]] w sprawie remontu #praca\n"
                     u"- [x] Wysłać raport kwartalny ✅ 2026-10-13 #praca\n"
                     u"- Pomysł: ogródek na balkonie #dom\n"_s);
    text(kJournal14, kJournal14Text);
    text(kJan, u"alias:: Janek\n"
               u"tags:: znajomi\n"
               u"\n"
               u"- Telefon: 600 100 200\n"
               u"- Pomaga przy [[Projekty/Remont kuchni]]\n"
               u"- DONE Oddać wiertarkę\n"
               u"- TODO Kupić prezent na urodziny\n"
               u"  SCHEDULED: <2026-10-20 Tue +1y>\n"_s);
    text(kKitchen, kKitchenText);
    text(kChores, u"# Zadania domowe\r\n"
                  u"\r\n"
                  u"- [ ] Wynieść śmieci 📅 2026-10-15 🔁 every week on Monday, Thursday ⏰ 2026-10-15 07:00\r\n"
                  u"- [ ] Zapłacić czynsz 📅 2026-11-10 🔁 every month on the 10th 🔼\r\n"
                  u"- Wieczorny przegląd ⏰ 2026-10-14 20:00\r\n"
                  u"- Wizyta u dentysty (@2026-10-16 10:00)\r\n"_s);
    text(kShopping, u"- [ ] Mleko #zakupy\n"
                    u"- [ ] Chleb #zakupy\n"
                    u"- [x] Kawa #zakupy ✅ 2026-10-12\n"
                    u"- Lista dla [[Projekty/Remont kuchni#Remont kuchni|remontu]]\n"_s);
    text(u"templates/journal.md"_s, u"- {{date}} — {{weekday}}\n- \n"_s);
    for (int i = 0; i < kArchiveNotes; ++i)
        text(archiveNote(i), u"- Stara notatka %1 o [[Zakupy]] #archiwum\n"_s.arg(i));
    files.insert(kPhoto, photoBytes());
    return files;
}

/// Keeps a VaultIndex in step with a Vault (like the app's Workspace): everything indexed at
/// construction, then every Vault::changed() applied.
class Indexer {
public:
    explicit Indexer(Vault &vault) : m_vault(vault), index(vault.config())
    {
        reindexAll();
        QObject::connect(&vault, &Vault::changed, &index,
                         [this](const QList<VaultChange> &changes) { apply(changes); });
        QObject::connect(&vault, &Vault::configChanged, &index, [this] { index.setConfig(m_vault.config()); });
    }

    void reindexAll()
    {
        VaultIndex::Batch batch(index);
        index.clear();
        for (const FileStat &file : m_vault.files())
            indexFile(file.path);
    }

private:
    void indexFile(const VaultPath &path)
    {
        if (paths::isIgnored(path))
            return;
        if (!paths::isMarkdown(path)) {
            index.update(path, QString());
            return;
        }
        bool ok = false;
        const QString content = m_vault.readText(path, &ok);
        if (ok)
            index.update(path, content);
    }

    void apply(const QList<VaultChange> &changes)
    {
        VaultIndex::Batch batch(index);
        for (const VaultChange &change : changes) {
            switch (change.type) {
            case ChangeType::Created:
            case ChangeType::Modified: indexFile(change.path); break;
            case ChangeType::Deleted: index.remove(change.path); break;
            case ChangeType::Renamed:
                index.remove(change.oldPath);
                indexFile(change.path);
                break;
            }
        }
    }

    Vault &m_vault;

public:
    VaultIndex index;
};

/// "Kupić płytki" -> the task (first match by text prefix) in the index.
inline std::optional<TaskRef> findTask(const VaultIndex &index, const QString &textPrefix)
{
    for (const TaskRef &ref : index.tasks()) {
        if (ref.task.text.startsWith(textPrefix))
            return ref;
    }
    return std::nullopt;
}

inline QStringList taskTexts(const VaultIndex &index, const VaultPath &path)
{
    QStringList out;
    for (const TaskRef &ref : index.tasks()) {
        if (ref.path == path)
            out.append(ref.task.text);
    }
    return out;
}

} // namespace integration
