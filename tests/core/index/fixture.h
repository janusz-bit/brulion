// A small but realistic vault for index/query tests (ported from the TypeScript v1 fixture):
// Polish journals and pages, aliases (frontmatter and Logseq `alias::`), block ids and
// references, tasks with dates and priorities, reminders/alarms, tags (frontmatter, inline,
// nested, Logseq `tags::`), attachments, a duplicate page name, links to a missing page and code
// that must be ignored.
//
// "Today" for the fixture: Wednesday 2026-10-14 (weeks start on Monday, so this-week =
// 2026-10-12 … 2026-10-18).
#pragma once

#include "index/index.h"

#include <QDate>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace fixture {

using namespace Qt::StringLiterals;

inline const QDate Today(2026, 10, 14);

inline QString lines(const QStringList &l)
{
    return l.join(u'\n');
}

inline QList<QPair<QString, QString>> vault()
{
    return {
            {u"journals/2026-10-12.md"_s,
             lines({u"- Spotkanie z [[Jan Kowalski]] w sprawie [[Projekty/Dom|domu]] #praca"_s,
                    u"  - [ ] Wysłać ofertę 📅 2026-10-13"_s, u"  - notatka o żółwiu i zółwiach"_s,
                    u"- Przeczytałem artykuł o remont w gazecie"_s})},
            {u"journals/2026-10-13.md"_s,
             lines({u"- [x] Zadzwonić do hydraulika 📅 2026-10-13 ✅ 2026-10-13"_s,
                    u"- [ ] Kupić farbę ⏳ 2026-10-15 🔼 #zakupy"_s, u"- Pomysł na ((plan01))"_s,
                    u"- DOING Pisanie raportu [#A] 📅 2026-10-20 #praca/raporty"_s})},
            {u"journals/2026-10-14.md"_s, lines({u"- [ ] Odebrać paczkę 📅 2026-10-14 ⏰ 2026-10-14 17:30"_s,
                                                 u"- [/] Malowanie salonu 📅 2026-10-16 🛫 2026-10-14 #dom"_s,
                                                 u"- Dzisiaj rozmawiałem z Jan Kowalski o dom i Dom Jana."_s,
                                                 u"- Kod: `Dom` oraz https://example.com/Dom i #Dom"_s,
                                                 u"- ⏰! 2026-10-15 06:30 Pobudka na lotnisko"_s,
                                                 u"- Wczorajsze przypomnienie ⏰ 2026-10-13 08:00"_s})},
            {u"Projekty/Dom.md"_s,
             lines({u"---"_s, u"aliases: [Remont, Dom Jana]"_s, u"tags: [Projekt]"_s, u"---"_s, u"# Dom"_s, u""_s,
                    u"Plan remontu domu w Krakowie. ^plan01"_s, u""_s, u"- [ ] Kupić płytki 📅 2026-10-10 ⏫ #zakupy"_s,
                    u"- [ ] Zamówić ekipę 📅 2026-10-18 ➕ 2026-10-01"_s, u"- [-] Anulowane zadanie 📅 2026-10-01"_s,
                    u"- Lista: [[Budżet]] i [[Nieistniejąca strona]]"_s,
                    u"- ![[salon.png]] oraz ![[plan.pdf]] i ![[brak.png]]"_s, u"- Sekcja #Remont/Łazienka"_s,
                    u"  - [ ] Wybrać baterię 📅 2026-10-25"_s})},
            {u"Projekty/Budżet.md"_s,
             lines({u"# Budżet"_s, u""_s, u"```calc"_s, u"farba = 120"_s, u"[[Dom]] #nietag"_s, u"```"_s, u""_s,
                    u"- Koszty [[Dom]] razem"_s, u"- Zobacz [[Projekty/Dom#^plan01]]"_s,
                    u"- Farba: [lista](../Zakupy.md) ^koszty"_s})},
            {u"Zakupy.md"_s, lines({u"tags:: zakupy"_s, u""_s, u"- [ ] Mleko"_s, u"- [ ] Chleb 🔽"_s,
                                    u"- Żółw dla dzieci"_s, u"- Wstęp: ((koszty))"_s})},
            {u"Ludzie/Jan Kowalski.md"_s,
             lines({u"alias:: Janek"_s, u""_s, u"- Telefon: 123 456 789"_s, u"- Pracuje przy [[Remont|remoncie]]"_s})},
            {u"Archiwum/Stare/Dom.md"_s, lines({u"# Stary dom"_s, u""_s, u"- Stare notatki o domu"_s})},
            {u"Notatki.md"_s,
             lines({u"- Zobacz [[dom]], [[Remont]] i [[Janek]]"_s, u"- Zdjęcie ![](assets/salon.png)"_s,
                    u"- Kolejny link do [[Nieistniejąca strona]]"_s, u"- [ ] Bez daty #praca"_s})},
    };
}

inline const QStringList Attachments = {u"assets/salon.png"_s, u"plan.pdf"_s};

/// Fill `index` with the whole fixture vault (in the order above, attachments last).
inline void build(brulion::VaultIndex &index)
{
    for (const auto &[path, content] : vault())
        index.update(path, content);
    for (const QString &p : Attachments)
        index.update(p, QString());
}

/// Text of the block an item points at.
inline QString blockText(const brulion::VaultIndex &index, const QString &path, int blockIndex)
{
    const brulion::ParsedNote *note = index.note(path);
    if (!note || blockIndex < 0 || blockIndex >= note->blocks.size())
        return {};
    return note->blocks[blockIndex].text;
}

} // namespace fixture
