/**
 * A small but realistic vault for index/query tests: Polish journals and pages, aliases
 * (frontmatter and Logseq `alias::`), block ids and references, tasks with dates and
 * priorities, reminders/alarms, tags (frontmatter, inline, nested, Logseq `tags::`),
 * attachments, a duplicate page name, links to a missing page and code that must be ignored.
 *
 * "Now" for the fixture: Wednesday 2026-10-14 12:00 local time (weeks start on Monday, so
 * this-week = 2026-10-12 … 2026-10-18).
 */
import { VaultIndex } from './vaultIndex';

export const NOW = new Date(2026, 9, 14, 12, 0);

export const VAULT: Record<string, string> = {
  'journals/2026-10-12.md': [
    '- Spotkanie z [[Jan Kowalski]] w sprawie [[Projekty/Dom|domu]] #praca',
    '  - [ ] Wysłać ofertę 📅 2026-10-13',
    '  - notatka o żółwiu i zółwiach',
    '- Przeczytałem artykuł o remont w gazecie',
  ].join('\n'),
  'journals/2026-10-13.md': [
    '- [x] Zadzwonić do hydraulika 📅 2026-10-13 ✅ 2026-10-13',
    '- [ ] Kupić farbę ⏳ 2026-10-15 🔼 #zakupy',
    '- Pomysł na ((plan01))',
    '- DOING Pisanie raportu [#A] 📅 2026-10-20 #praca/raporty',
  ].join('\n'),
  'journals/2026-10-14.md': [
    '- [ ] Odebrać paczkę 📅 2026-10-14 ⏰ 2026-10-14 17:30',
    '- [/] Malowanie salonu 📅 2026-10-16 🛫 2026-10-14 #dom',
    '- Dzisiaj rozmawiałem z Jan Kowalski o dom i Dom Jana.',
    '- Kod: `Dom` oraz https://example.com/Dom i #Dom',
    '- ⏰! 2026-10-15 06:30 Pobudka na lotnisko',
    '- Wczorajsze przypomnienie ⏰ 2026-10-13 08:00',
  ].join('\n'),
  'Projekty/Dom.md': [
    '---',
    'aliases: [Remont, Dom Jana]',
    'tags: [Projekt]',
    '---',
    '# Dom',
    '',
    'Plan remontu domu w Krakowie. ^plan01',
    '',
    '- [ ] Kupić płytki 📅 2026-10-10 ⏫ #zakupy',
    '- [ ] Zamówić ekipę 📅 2026-10-18 ➕ 2026-10-01',
    '- [-] Anulowane zadanie 📅 2026-10-01',
    '- Lista: [[Budżet]] i [[Nieistniejąca strona]]',
    '- ![[salon.png]] oraz ![[plan.pdf]] i ![[brak.png]]',
    '- Sekcja #Remont/Łazienka',
    '  - [ ] Wybrać baterię 📅 2026-10-25',
  ].join('\n'),
  'Projekty/Budżet.md': [
    '# Budżet',
    '',
    '```calc',
    'farba = 120',
    '[[Dom]] #nietag',
    '```',
    '',
    '- Koszty [[Dom]] razem',
    '- Zobacz [[Projekty/Dom#^plan01]]',
    '- Farba: [lista](../Zakupy.md) ^koszty',
  ].join('\n'),
  'Zakupy.md': ['tags:: zakupy', '', '- [ ] Mleko', '- [ ] Chleb 🔽', '- Żółw dla dzieci', '- Wstęp: ((koszty))'].join('\n'),
  'Ludzie/Jan Kowalski.md': ['alias:: Janek', '', '- Telefon: 123 456 789', '- Pracuje przy [[Remont|remoncie]]'].join('\n'),
  'Archiwum/Stare/Dom.md': ['# Stary dom', '', '- Stare notatki o domu'].join('\n'),
  'Notatki.md': [
    '- Zobacz [[dom]], [[Remont]] i [[Janek]]',
    '- Zdjęcie ![](assets/salon.png)',
    '- Kolejny link do [[Nieistniejąca strona]]',
    '- [ ] Bez daty #praca',
  ].join('\n'),
};

export const ATTACHMENTS = ['assets/salon.png', 'plan.pdf'];

/** A fresh index with the whole fixture vault. */
export function buildIndex(): VaultIndex {
  const index = new VaultIndex();
  for (const [path, content] of Object.entries(VAULT)) index.update(path, content);
  for (const p of ATTACHMENTS) index.updateAttachment(p);
  return index;
}

/** Text of a block-ish ref for compact assertions. */
export function texts(items: { block: { text: string } }[]): string[] {
  return items.map((i) => i.block.text);
}
