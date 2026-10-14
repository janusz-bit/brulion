import { addTokenToBlock, appendBlock, appendImportSection, buildCaptureBlock, eolOf, mimeForPath, pathForTitle } from './content';

const NOW = new Date(2026, 9, 14, 10, 30); // Wed 14 Oct 2026, 10:30 local

describe('buildCaptureBlock', () => {
  it('turns a Polish date phrase with a time into a reminder on a task', () => {
    expect(buildCaptureBlock('Kupić mleko jutro o 18', NOW)).toBe('- [ ] Kupić mleko ⏰ 2026-10-15 18:00');
  });

  it('turns a bare date into a due date', () => {
    expect(buildCaptureBlock('Zapłacić rachunek w piątek', NOW)).toBe('- [ ] Zapłacić rachunek 📅 2026-10-16');
  });

  it('understands English phrases too', () => {
    expect(buildCaptureBlock('Call mom tomorrow at 6pm', NOW, { locale: 'en' })).toBe('- [ ] Call mom ⏰ 2026-10-15 18:00');
  });

  it('keeps text without dates and keeps a phrase that is only a date', () => {
    expect(buildCaptureBlock('Zadzwonić do mamy', NOW)).toBe('- [ ] Zadzwonić do mamy');
    expect(buildCaptureBlock('jutro', NOW)).toBe('- [ ] jutro');
  });

  it('writes plain items and explicit reminders/alarms', () => {
    expect(buildCaptureBlock('Pomysł na prezent', NOW, { task: false })).toBe('- Pomysł na prezent');
    expect(buildCaptureBlock('Pobudka', NOW, { reminderAt: '2026-10-15T06:30', alarm: true })).toBe('- [ ] Pobudka ⏰! 2026-10-15 06:30');
    expect(buildCaptureBlock('Link https://example.com jutro', NOW, { parseDates: false, task: false })).toBe(
      '- Link https://example.com jutro',
    );
  });

  it('makes further lines child items', () => {
    expect(buildCaptureBlock('Zakupy\n mleko \n\nchleb', NOW, { task: false })).toBe('- Zakupy\n\t- mleko\n\t- chleb');
  });
});

describe('appendBlock', () => {
  it('appends to empty and non-empty notes', () => {
    expect(appendBlock('', '- [ ] a')).toEqual({ content: '- [ ] a\n', line: 0 });
    expect(appendBlock('- x\n', '- y')).toEqual({ content: '- x\n- y\n', line: 1 });
    expect(appendBlock('- x', '- y')).toEqual({ content: '- x\n- y\n', line: 1 });
  });

  it('replaces the empty item a journal template leaves and trailing blank lines', () => {
    expect(appendBlock('- \n', '- [ ] a').content).toBe('- [ ] a\n');
    expect(appendBlock('- plan\n\t- \n\n\n', '- b')).toEqual({ content: '- plan\n- b\n', line: 1 });
  });

  it('keeps CRLF line endings', () => {
    expect(appendBlock('- x\r\n- y\r\n', '- z\n\t- dziecko')).toEqual({ content: '- x\r\n- y\r\n- z\r\n\t- dziecko\r\n', line: 2 });
  });
});

describe('appendImportSection', () => {
  it('adds the imported content under a heading', () => {
    expect(appendImportSection('- moje\n', '- z Logseq\n', 'Logseq')).toBe('- moje\n\n## Import (Logseq)\n\n- z Logseq\n');
    expect(appendImportSection('', 'treść', 'Notion')).toBe('## Import (Notion)\n\ntreść\n');
    expect(appendImportSection('a\r\nb\r\n', 'x\ny', 'Bear')).toBe('a\r\nb\r\n\r\n## Import (Bear)\r\n\r\nx\r\ny\r\n');
  });
});

describe('addTokenToBlock', () => {
  const TOKEN = '⏰ 2026-10-15 09:00';

  it('adds the token before a block id', () => {
    expect(addTokenToBlock('- [ ] Zadanie ^abc123\n- inne', 0, TOKEN)).toBe(`- [ ] Zadanie ${TOKEN} ^abc123\n- inne`);
  });

  it('targets the first line of the block', () => {
    expect(addTokenToBlock('Akapit pierwszy\ndruga linia\n', 1, TOKEN)).toBe(`Akapit pierwszy ${TOKEN}\ndruga linia\n`);
    expect(addTokenToBlock('- rodzic\n\t- dziecko\n', 1, TOKEN)).toBe(`- rodzic\n\t- dziecko ${TOKEN}\n`);
  });

  it('leaves code blocks, frontmatter and invalid lines alone', () => {
    const code = '```js\nlet a = 1;\n```\n';
    expect(addTokenToBlock(code, 1, TOKEN)).toBe(code);
    const fm = '---\ntags: [a]\n---\n- x\n';
    expect(addTokenToBlock(fm, 1, TOKEN)).toBe(fm);
    expect(addTokenToBlock('- x', 5, TOKEN)).toBe('- x');
    expect(addTokenToBlock('- x', -1, TOKEN)).toBe('- x');
  });

  it('turns a blank line into an item and keeps CRLF', () => {
    expect(addTokenToBlock('- a\n\n- b', 1, TOKEN)).toBe(`- a\n- ${TOKEN}\n- b`);
    expect(addTokenToBlock('- a\r\n- b\r\n', 1, TOKEN)).toBe(`- a\r\n- b ${TOKEN}\r\n`);
  });
});

describe('pathForTitle', () => {
  it('builds paths with folders and sanitised names', () => {
    expect(pathForTitle('Zażółć gęślą jaźń')).toBe('Zażółć gęślą jaźń.md');
    expect(pathForTitle('Projekty/Dom', '')).toBe('Projekty/Dom.md');
    expect(pathForTitle('Plan: wakacje?', 'Notatki')).toBe('Notatki/Plan wakacje.md');
    expect(pathForTitle('../../etc/passwd')).toBe('etc/passwd.md');
    expect(pathForTitle('Już.md')).toBe('Już.md');
    expect(pathForTitle('   ')).toBe('Bez tytułu.md');
  });
});

describe('misc', () => {
  it('detects line endings and MIME types', () => {
    expect(eolOf('a\r\nb')).toBe('\r\n');
    expect(eolOf('a\nb')).toBe('\n');
    expect(eolOf('')).toBe('\n');
    expect(mimeForPath('assets/Zdjęcie.JPG')).toBe('image/jpeg');
    expect(mimeForPath('a/b.svg')).toBe('image/svg+xml');
    expect(mimeForPath('x.unknown')).toBe('application/octet-stream');
  });
});
