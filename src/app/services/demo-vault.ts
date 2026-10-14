/**
 * "Przykładowy brulion": a Polish demo / guide vault. A guide page explains every feature with
 * live examples; journals, projects, a reading list and recipes look like a real notebook.
 * All dates are relative to `now` when seeding, so tasks and reminders are always current.
 */
import { addDaysLocal, endOfMonthLocal, todayLocal, weekdayOf } from '@/core/dates';
import { journalPathForDate } from '@/core/markdown';
import { DEFAULT_VAULT_CONFIG, type LocalDate, type VaultConfig, type VaultPath } from '@/core/types';

/** Location id of the demo vault in the browser / app storage. */
export const DEMO_VAULT_ID = 'demo';
export const GUIDE_PAGE = 'Witaj w Brulionie.md';

const SKETCH_SVG = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 300" width="480" height="300" font-family="system-ui, sans-serif" font-size="14">
  <rect width="480" height="300" fill="#fbf7ee"/>
  <g stroke="#9fc3e6" stroke-width="0.6" opacity="0.7">
    ${Array.from({ length: 23 }, (_, i) => `<line x1="${(i + 1) * 20}" y1="0" x2="${(i + 1) * 20}" y2="300"/>`).join('')}
    ${Array.from({ length: 14 }, (_, i) => `<line x1="0" y1="${(i + 1) * 20}" x2="480" y2="${(i + 1) * 20}"/>`).join('')}
  </g>
  <line x1="40" y1="0" x2="40" y2="300" stroke="#e2574c" stroke-width="1.5"/>
  <g fill="none" stroke="#1f2430" stroke-width="3" stroke-linejoin="round">
    <rect x="60" y="30" width="380" height="240"/>
    <line x1="240" y1="30" x2="240" y2="170"/>
    <line x1="60" y1="170" x2="190" y2="170"/>
    <line x1="290" y1="170" x2="440" y2="170"/>
  </g>
  <g fill="#1f2430">
    <text x="110" y="105">Kuchnia</text>
    <text x="300" y="105">Łazienka</text>
    <text x="205" y="230">Salon</text>
  </g>
  <g fill="#f2b33d" opacity="0.85"><rect x="250" y="40" width="70" height="40" rx="4"/></g>
  <text x="257" y="65" font-size="11" fill="#1f2430">nowe płytki</text>
</svg>
`;

function nextWeekday(from: LocalDate, weekday: number): LocalDate {
  const diff = (weekday - weekdayOf(from) + 7) % 7 || 7;
  return addDaysLocal(from, diff);
}

/** All files of the demo vault, path → content (text, or bytes for attachments). */
export function demoVaultFiles(now: Date, config: VaultConfig = DEFAULT_VAULT_CONFIG): Record<VaultPath, string | Uint8Array> {
  const today = todayLocal(now);
  const d = (n: number): LocalDate => addDaysLocal(today, n);
  const j = (date: LocalDate): VaultPath => journalPathForDate(date, config);
  const sunday = nextWeekday(today, 0);
  const monthEnd = endOfMonthLocal(today);
  const files: Record<VaultPath, string | Uint8Array> = {};

  files[GUIDE_PAGE] = `---
tags: [przewodnik]
aliases: [Start, Przewodnik]
---
# Witaj w Brulionie

Brulion to notatnik, który działa **bez internetu** i trzyma wszystko w zwykłych plikach Markdown — w folderze, który możesz synchronizować, kopiować i otwierać w Obsidianie czy Logseq. Ta strona pokazuje wszystkie funkcje na żywych przykładach. Śmiało je zmieniaj: to Twój brulion.

> [!tip] Od czego zacząć?
> Otwórz [[${today}|dzisiejszy dziennik]] (\`Ctrl+J\`) i zapisz pierwszą myśl. Szybką notatkę dodasz też przyciskiem ✏️ na górnym pasku albo w telefonie przyciskiem w prawym dolnym rogu.

## Konspekt (outliner)
- Każdy punkt listy to **blok**.
	- \`Tab\` zagnieżdża blok, \`Shift+Tab\` go wyciąga.
	- \`Alt+↑\` i \`Alt+↓\` przesuwają blok razem z dziećmi.
	- Kliknięcie kropki przybliża blok, \`Ctrl+.\` zwija i rozwija.
- Piszesz jak w Logseq, a plik pozostaje zwykłym Markdownem.

## Linki i tagi
- Link do strony: [[Remont mieszkania]], z własnym opisem: [[Książki 2026|moja lista lektur]].
- Link do nagłówka: [[Przepisy#Pierogi ruskie]].
- Strona, której jeszcze nie ma, powstanie po kliknięciu: [[Pomysły na weekend]].
- Tagi grupują notatki z całego brulionu: #przewodnik #pomysł #dom/remont
- Wszystkie odnośniki do bieżącej strony zobaczysz w panelu **Odnośniki** po prawej.
- \`Ctrl+O\` szybko otwiera dowolną stronę, \`Ctrl+K\` otwiera paletę poleceń.

## Odwołania do bloków
- Ten blok ma własny identyfikator: kawa smakuje najlepiej o poranku ☕ ^kawa
- Odwołanie pokazuje jego treść w innym miejscu: ((kawa))
- A tak wygląda osadzony blok:
	- !((kawa))

## Zadania
- [ ] Zwykłe zadanie — odhacz je kliknięciem albo \`Ctrl+Enter\`
- [/] Zadanie w toku
- [x] Zadanie wykonane ✅ ${d(-1)}
- [ ] Zadanie z terminem 📅 ${d(2)}
- [ ] Zadanie zaplanowane na konkretny dzień ⏳ ${d(1)}
- [ ] Pilne zadanie ⏫ 📅 ${d(5)}
- [ ] Podlać kwiaty 🔁 every week 📅 ${d(3)}
- Po odhaczeniu zadania cyklicznego Brulion sam dopisze jego następne wystąpienie.
- Wszystkie zadania z całego brulionu znajdziesz w widoku **Zadania**.

## Przypomnienia i alarmy
- [ ] Zwykłe przypomnienie ⏰ ${d(1)} 09:00
- [ ] Alarm — głośny, dzwoni aż do wyłączenia ⏰! ${d(2)} 07:00
- Przypomnienie może mieć każdy blok, np. urodziny [[Anna Kowalska|Ani]] ⏰ ${d(10)} 10:00 🔁 every year
- W edytorze wpisz \`/przypomnienie\` albo użyj przycisku ⏰ na pasku narzędzi. Daty możesz pisać słownie: „jutro o 18”, „w piątek 9:30”, „za 2 dni”.
- Na Androidzie przypomnienia działają także przy zamkniętej aplikacji; na komputerze Brulion czeka w zasobniku systemowym.

## Obliczenia
Blok \`calc\` liczy linijka po linijce, zna zmienne, procenty i jednostki:

\`\`\`calc
kawa = 14
ciastko = 9,50
kawa * 2 + ciastko
ans + 10%
20% z 150
5 km to mi
\`\`\`

Obliczenia działają też w tekście: dwie kawy kosztują \`= kawa * 2\` zł.

## Tabele z formułami
Komórka zaczynająca się od \`=\` to formuła:

| Produkt | Ilość | Cena | Razem |
| --- | --- | --- | --- |
| Mąka | 2 | 4.50 | =[Ilość]*[Cena] |
| Jajka | 10 | 1.20 | =[Ilość]*[Cena] |
| Masło | 1 | 8.99 | =[Ilość]*[Cena] |
| **Suma** | | | =sum(above) |

## Zapytania
Blok \`query\` zbiera bloki z całego brulionu i odświeża się sam. Otwarte zadania na najbliższy tydzień:

\`\`\`query
type:tasks status:open due:<=+7d sort:due
\`\`\`

Zadania z tagiem #dom:

\`\`\`query
type:tasks status:open tag:dom
\`\`\`

## Osadzenia
Fragment innej strony:

![[Przepisy#Szarlotka]]

Obrazek z folderu \`assets\`:

![[szkic-mieszkania.svg]]

## Callouty
> [!note] Notatka
> Callouty wyróżniają ważne fragmenty. Działają tak samo jak w Obsidianie.

> [!warning] Uwaga
> Rodzaje: note, tip, info, warning, danger, todo, quote.

Możesz też ==zaznaczać tekst==, dodawać %%ukryte komentarze%% i przypisy[^1].

[^1]: Przypis wyświetla się na dole strony.

## Szablony
- Szablony leżą w folderze \`templates\`. [[templates/journal|Szablon dziennika]] dostaje każdy nowy dzień.
- \`/szablon\` w edytorze wstawia dowolny szablon, np. [[templates/Spotkanie|Spotkanie]]. Zmienne: \`{{date}}\`, \`{{time}}\`, \`{{title}}\`, \`{{weekday}}\`, \`{{cursor}}\`.

## Kalendarz
- Widok **Kalendarz** pokazuje dzienniki, terminy zadań i przypomnienia w układzie miesiąca, tygodnia lub listy.
- Kliknięcie dnia otwiera jego dziennik — także dla dni w przyszłości, żeby coś zaplanować.

## Synchronizacja
### Syncthing
- Na komputerze wskaż folder Syncthing jako skarbiec (Ustawienia → Skarbiec → Zmień skarbiec).
- Na Androidzie zainstaluj Syncthing-Fork, udostępnij ten sam folder (np. \`/storage/emulated/0/Brulion\`) i nadaj Brulionowi dostęp do plików.
- Konflikty (pliki \`.sync-conflict-…\`) znajdziesz w widoku **Synchronizacja**, gdzie scalisz je jednym kliknięciem.

### Nextcloud
- W widoku **Synchronizacja** wpisz adres serwera, login i hasło aplikacji.
- Brulion synchronizuje w tle, a zmiany z dwóch urządzeń scala automatycznie.

## Import i eksport
- Import z Obsidiana, Logseq, Notion, Evernote, Google Keep, Joplin, Simplenote, Roam, Bear, plików Markdown, HTML i kalendarzy ICS.
- Eksport do archiwum Markdown, strony HTML, JSON, formatu Logseq, kalendarza ICS i OPML.
- Wszystko w widoku **Import i eksport**.

## Historia i kosz
- Każda zmiana pliku zapisuje poprzednią wersję na tym urządzeniu: menu ⋯ → **Historia zmian**.
- Usunięte strony trafiają do **Kosza**, skąd można je przywrócić.

## Skróty klawiszowe
| Skrót | Działanie |
| --- | --- |
| \`Ctrl+K\` | paleta poleceń |
| \`Ctrl+O\` | szybkie otwieranie stron |
| \`Ctrl+J\` | dzisiejszy dziennik |
| \`Ctrl+Shift+F\` | wyszukiwanie |
| \`Alt+←\` / \`Alt+→\` | wstecz / dalej |
| \`Ctrl+Enter\` | odhacz zadanie |
| \`Tab\` / \`Shift+Tab\` | wcięcie / wysunięcie bloku |
| \`Alt+↑\` / \`Alt+↓\` | przesuń blok |
| \`Ctrl+.\` | zwiń / rozwiń |
| \`Ctrl+,\` | ustawienia |
`;

  files[j(today)] = `- Poranna kawa i plan dnia #plan
	- zajrzeć do [[Witaj w Brulionie]] i poznać skróty
- [ ] Zadzwonić do hydraulika w sprawie [[Remont mieszkania|remontu]] ⏰ ${today} 16:00
- [ ] Kupić mleko i chleb
- Spotkanie zespołu o 11:00 #praca
	- ustalić termin wdrożenia
	- [ ] wysłać notatkę ze spotkania 📅 ${d(1)}
- Przeczytałam 30 stron „Lalki” — scena w sklepie Wokulskiego jest świetna. [[Książki 2026]]
`;

  files[j(d(-1))] = `- Długi spacer po parku, prawie 7 km #zdrowie
- [x] Zapłacić rachunek za prąd ✅ ${d(-1)}
- [x] Odebrać paczkę z paczkomatu ✅ ${d(-1)}
- Pomysł: w weekend zrobić [[Przepisy#Pierogi ruskie|pierogi ruskie]] dla całej rodziny #pomysł
`;

  files[j(d(-2))] = `- Rozmowa z [[Anna Kowalska|Anią]] o płytkach do łazienki #dom/remont
	- poleca ekipę, z którą robiła swoje mieszkanie
- [x] Zamówić próbki płytek ✅ ${d(-2)}
- Wieczorem kino: „Seksmisja” po raz dziesiąty i wciąż bawi
`;

  files[j(d(-7))] = `- Ruszamy z remontem! Plan i budżet w [[Remont mieszkania]] #dom/remont
- [x] Wybrać ekipę remontową ✅ ${d(-7)}
- Skończyłam „Solaris” — piątka z plusem #książki
- Zakupy na targu: jabłka na [[Przepisy#Szarlotka|szarlotkę]], ogórki, koperek
`;

  files['Projekty/Remont mieszkania.md'] = `---
tags: [projekt, dom]
status: w toku
budżet: 25000
---
# Remont mieszkania

> [!info] Cel
> Odświeżyć łazienkę i kuchnię do końca miesiąca, bez przekraczania budżetu.

## Plan prac
- Łazienka #dom/remont
	- [x] Skuć stare płytki ✅ ${d(-3)}
	- [ ] Hydraulik: nowe podejście pod pralkę 📅 ${d(1)} ⏰ ${d(1)} 08:00
	- [ ] Położyć płytki 📅 ${d(6)}
- Kuchnia #dom/remont
	- [ ] Wybrać blat 🔼 📅 ${d(4)}
	- [ ] Zamówić oświetlenie LED pod szafki 📅 ${d(9)}
- Po remoncie
	- [ ] Wielkie sprzątanie ⏳ ${monthEnd}

## Budżet
| Pozycja | Ilość | Cena | Razem |
| --- | --- | --- | --- |
| Płytki (m²) | 12 | 89 | =[Ilość]*[Cena] |
| Klej i fuga | 6 | 45 | =[Ilość]*[Cena] |
| Blat dębowy | 1 | 2400 | =[Ilość]*[Cena] |
| Robocizna | 1 | 6500 | =[Ilość]*[Cena] |
| **Suma** | | | =sum(above) |

\`\`\`calc
budżet = 25000
wydane = 12 * 89 + 6 * 45 + 2400 + 6500
zostało = budżet - wydane
zostało / budżet * 100
\`\`\`

## Otwarte zadania
\`\`\`query
type:tasks status:open path:Projekty sort:due
\`\`\`

## Szkic
![[szkic-mieszkania.svg]]
`;

  files['Książki 2026.md'] = `---
tags: [książki]
cel: 24
---
# Książki 2026

Cel na ten rok: \`= cel\` książki, czyli \`= cel / 12\` miesięcznie.

## Czytam teraz
- „Lalka” — Bolesław Prus #książki/klasyka
	- strona:: 214
	- [ ] Skończyć do końca miesiąca 📅 ${monthEnd}

## Przeczytane
| Tytuł | Autor | Ocena |
| --- | --- | --- |
| Solaris | Stanisław Lem | 5 |
| Bieguni | Olga Tokarczuk | 4 |
| Ferdydurke | Witold Gombrowicz | 4 |
| Zły | Leopold Tyrmand | 5 |
| **Średnia** | | =avg(above) |

## Do przeczytania
- [ ] „Księgi Jakubowe” — Olga Tokarczuk
- [ ] „Dukla” — Andrzej Stasiuk
- [ ] „Pan Tadeusz” — wreszcie w całości 🔽
`;

  files['Przepisy.md'] = `---
tags: [przepisy]
---
# Przepisy

## Pierogi ruskie
- Składniki (ok. 40 sztuk):
	- ciasto: 500 g mąki, 250 ml ciepłej wody, 2 łyżki oleju, szczypta soli
	- farsz: 1 kg ziemniaków, 300 g twarogu, 2 cebule, sól, pieprz
- Ciasto zagnieść i odstawić na 30 minut pod ściereczką.
- Cebulę zeszklić na maśle, połowę dodać do farszu.
- Gotować 2–3 minuty od wypłynięcia, podawać z resztą cebulki.

\`\`\`calc
pierogi = 40
osoby = 6
pierogi / osoby
\`\`\`

## Szarlotka
- 5 kwaśnych jabłek, 300 g mąki, 200 g masła, 100 g cukru, 2 żółtka
- Jabłka zetrzeć, podsmażyć z cynamonem.
- Piec 50 minut w 180 °C.
- [ ] Upiec na niedzielę ⏳ ${sunday}
`;

  files['Ludzie/Anna Kowalska.md'] = `---
aliases: [Ania]
tags: [ludzie]
---
# Anna Kowalska
telefon:: 600 100 200
- Poleca ekipę od płytek — [[Remont mieszkania]]
- Urodziny ⏰ ${d(10)} 10:00 🔁 every year
`;

  files['templates/journal.md'] = `- {{cursor}}
`;

  files['templates/Spotkanie.md'] = `## Spotkanie {{date}} {{time}}
uczestnicy::
- Notatki
	- {{cursor}}
- Ustalenia
	- [ ]
`;

  files['assets/szkic-mieszkania.svg'] = new TextEncoder().encode(SKETCH_SVG);
  return files;
}

/** Something that can receive the demo files (the Workspace). */
export interface DemoTarget {
  readonly config: VaultConfig;
  exists(path: VaultPath): boolean;
  write(path: VaultPath, content: string, source?: 'system'): Promise<void>;
  readonly vault: { writeBinary(path: VaultPath, data: Uint8Array, source?: 'system'): Promise<void> };
}

/** Write the demo files that do not exist yet. Returns the paths written. */
export async function seedDemoVault(target: DemoTarget, now: Date = new Date()): Promise<VaultPath[]> {
  const written: VaultPath[] = [];
  for (const [path, content] of Object.entries(demoVaultFiles(now, target.config))) {
    if (target.exists(path)) continue;
    if (typeof content === 'string') await target.write(path, content, 'system');
    else await target.vault.writeBinary(path, content, 'system');
    written.push(path);
  }
  return written;
}
