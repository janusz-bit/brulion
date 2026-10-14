import { describe, expect, it } from 'vitest';
import { DEFAULT_VAULT_CONFIG } from '../types';
import { CONFIG_PATH, mergeVaultConfig, parseVaultConfig, serializeVaultConfig } from './config';
import { FsError, isFsError, isNotFoundError, requireEntryPath } from './errors';
import { KeyedLock } from './locks';
import { ancestors, insertBeforeExtension, isStrictlyWithin, isWithin, rebase } from './pathUtil';
import { isTextPath } from './textFiles';

const sleep = (ms: number): Promise<void> => new Promise((r) => setTimeout(r, ms));

describe('KeyedLock', () => {
  it('serialises tasks on the same key in call order', async () => {
    const lock = new KeyedLock();
    const log: string[] = [];
    const a = lock.run(['k'], async () => {
      log.push('a start');
      await sleep(10);
      log.push('a end');
      return 1;
    });
    const b = lock.run(['k'], async () => {
      log.push('b');
      return 2;
    });
    expect(await Promise.all([a, b])).toEqual([1, 2]);
    expect(log).toEqual(['a start', 'a end', 'b']);
  });

  it('runs tasks on different keys concurrently', async () => {
    const lock = new KeyedLock();
    const log: string[] = [];
    const a = lock.run(['x'], async () => {
      await sleep(15);
      log.push('x');
    });
    const b = lock.run(['y'], async () => {
      log.push('y');
    });
    await Promise.all([a, b]);
    expect(log).toEqual(['y', 'x']);
  });

  it('a multi-key task waits for all its keys', async () => {
    const lock = new KeyedLock();
    const log: string[] = [];
    let releaseA!: () => void;
    let releaseB!: () => void;
    const gateA = new Promise<void>((r) => (releaseA = r));
    const gateB = new Promise<void>((r) => (releaseB = r));
    const a = lock.run(['a'], async () => {
      await gateA;
      log.push('a');
    });
    const b = lock.run(['b'], async () => {
      await gateB;
      log.push('b');
    });
    const ab = lock.run(['a', 'b'], () => log.push('ab'));
    releaseB();
    await b;
    await sleep(0);
    expect(log).toEqual(['b']);
    releaseA();
    await Promise.all([a, ab]);
    expect(log).toEqual(['b', 'a', 'ab']);
  });

  it('keeps going after a failing task and propagates its error', async () => {
    const lock = new KeyedLock();
    const failed = lock.run(['k'], async () => {
      throw new Error('nope');
    });
    const next = lock.run(['k'], () => 'ok');
    await expect(failed).rejects.toThrow('nope');
    expect(await next).toBe('ok');
  });

  it('synchronous throws become rejections', async () => {
    const lock = new KeyedLock();
    await expect(
      lock.run(['k'], () => {
        throw new Error('sync');
      }),
    ).rejects.toThrow('sync');
  });

  it('wait() and idle() observe queued work; keys are released afterwards', async () => {
    const lock = new KeyedLock();
    let done = false;
    void lock.run(['k'], async () => {
      await sleep(5);
      done = true;
    });
    expect(lock.isBusy(['k'])).toBe(true);
    expect(lock.isBusy(['other'])).toBe(false);
    await lock.wait(['k']);
    expect(done).toBe(true);
    await lock.idle();
    expect(lock.size).toBe(0);
    await lock.wait(['never-used']);
  });

  it('duplicate keys in one call do not deadlock', async () => {
    const lock = new KeyedLock();
    expect(await lock.run(['k', 'k', 'k'], () => 5)).toBe(5);
  });

  it('shared holders of a key run concurrently; an exclusive holder waits for them', async () => {
    const lock = new KeyedLock();
    const log: string[] = [];
    let release!: () => void;
    const gate = new Promise<void>((r) => (release = r));
    const s1 = lock.runWith({ exclusive: ['f:1'], shared: ['d'] }, async () => {
      log.push('s1 start');
      await gate;
      log.push('s1 end');
    });
    const s2 = lock.runWith({ exclusive: ['f:2'], shared: ['d'] }, () => log.push('s2'));
    await s2;
    expect(log).toEqual(['s1 start', 's2']);
    const x = lock.runWith({ exclusive: ['d'] }, () => log.push('x'));
    const s3 = lock.runWith({ exclusive: ['f:3'], shared: ['d'] }, () => log.push('s3'));
    await sleep(0);
    expect(log).toEqual(['s1 start', 's2']);
    release();
    await Promise.all([s1, x, s3]);
    expect(log).toEqual(['s1 start', 's2', 's1 end', 'x', 's3']);
    expect(lock.size).toBe(0);
  });

  it('waitExclusive() ignores shared holders; wait() waits for both', async () => {
    const lock = new KeyedLock();
    let release!: () => void;
    const gate = new Promise<void>((r) => (release = r));
    const shared = lock.runWith({ exclusive: ['f:1'], shared: ['d'] }, () => gate);
    let waitedAll = false;
    const all = lock.wait(['d']).then(() => (waitedAll = true));
    await lock.waitExclusive(['d', 'f:2']);
    expect(waitedAll).toBe(false);
    release();
    await Promise.all([shared, all]);
    expect(waitedAll).toBe(true);
  });

  it('a key that is both shared and exclusive in one call is exclusive', async () => {
    const lock = new KeyedLock();
    const log: string[] = [];
    let release!: () => void;
    const gate = new Promise<void>((r) => (release = r));
    const s = lock.runWith({ exclusive: ['f'], shared: ['d'] }, async () => {
      await gate;
      log.push('s');
    });
    const x = lock.runWith({ exclusive: ['d'], shared: ['d'] }, () => log.push('x'));
    await sleep(0);
    release();
    await Promise.all([s, x]);
    expect(log).toEqual(['s', 'x']);
  });
});

describe('path helpers', () => {
  it('ancestors', () => {
    expect(ancestors('a/b/c.md')).toEqual(['a', 'a/b']);
    expect(ancestors('c.md')).toEqual([]);
    expect(ancestors('Żółw/Łąka/x.md')).toEqual(['Żółw', 'Żółw/Łąka']);
  });

  it('isWithin / isStrictlyWithin', () => {
    expect(isWithin('a/b.md', 'a')).toBe(true);
    expect(isWithin('a', 'a')).toBe(true);
    expect(isWithin('ab/c.md', 'a')).toBe(false);
    expect(isWithin('x', '')).toBe(true);
    expect(isStrictlyWithin('a', 'a')).toBe(false);
    expect(isStrictlyWithin('a/b', 'a')).toBe(true);
    expect(isStrictlyWithin('x', '')).toBe(true);
    expect(isStrictlyWithin('', '')).toBe(false);
  });

  it('rebase', () => {
    expect(rebase('a/x/y.md', 'a', 'b/c')).toBe('b/c/x/y.md');
    expect(rebase('a', 'a', 'z')).toBe('z');
    expect(rebase('a/x.md', 'a', '')).toBe('x.md');
  });

  it('insertBeforeExtension', () => {
    expect(insertBeforeExtension('a/b.md', ' 2')).toBe('a/b 2.md');
    expect(insertBeforeExtension('a/b', ' 2')).toBe('a/b 2');
    expect(insertBeforeExtension('a.d/b', ' 2')).toBe('a.d/b 2');
    expect(insertBeforeExtension('.hidden', ' 2')).toBe('.hidden 2');
    expect(insertBeforeExtension('dir/v1.2', ' (x)', true)).toBe('dir/v1.2 (x)');
    expect(insertBeforeExtension('Notatka.tar.gz', ' 2')).toBe('Notatka.tar 2.gz');
  });
});

describe('errors', () => {
  it('FsError carries code and path', () => {
    const e = new FsError('ENOENT', 'a/ż.md');
    expect(e).toBeInstanceOf(Error);
    expect(e.code).toBe('ENOENT');
    expect(e.path).toBe('a/ż.md');
    expect(e.name).toBe('FsError');
    expect(e.message).toContain('a/ż.md');
    expect(isFsError(e)).toBe(true);
    expect(isFsError(e, 'EEXIST')).toBe(false);
    expect(isFsError(new Error('x'))).toBe(false);
  });

  it('isNotFoundError recognises errors from all backends', () => {
    expect(isNotFoundError(new FsError('ENOENT', 'x'))).toBe(true);
    expect(isNotFoundError(new FsError('EEXIST', 'x'))).toBe(false);
    expect(isNotFoundError(new DOMException('x', 'NotFoundError'))).toBe(true);
    expect(isNotFoundError('No such file or directory (os error 2)')).toBe(true);
    expect(isNotFoundError(new Error('ENOENT: no such file'))).toBe(true);
    expect(isNotFoundError(new Error('permission denied'))).toBe(false);
    expect(isNotFoundError(null)).toBe(false);
    expect(isNotFoundError(undefined)).toBe(false);
  });

  it('requireEntryPath normalises and rejects the root', () => {
    expect(requireEntryPath('./a//b.md')).toBe('a/b.md');
    expect(() => requireEntryPath('')).toThrow(FsError);
    expect(() => requireEntryPath('/./')).toThrow(FsError);
  });
});

describe('isTextPath', () => {
  it('detects text formats case-insensitively', () => {
    for (const p of ['a.md', 'B.MD', 'x.markdown', 'c.txt', '.brulion/config.json', 'k.canvas', 'cal.ics', 'd.csv', 'i.svg']) {
      expect(isTextPath(p)).toBe(true);
    }
    for (const p of ['img.png', 'doc.pdf', 'a.zip', 'noext', 'photo.JPG']) expect(isTextPath(p)).toBe(false);
  });
});

describe('vault config', () => {
  it('merges partial config over the defaults', () => {
    const { config, raw, warnings } = parseVaultConfig('{"journalsFolder": "dziennik", "weekStartsOn": 0}');
    expect(config).toEqual({ ...DEFAULT_VAULT_CONFIG, journalsFolder: 'dziennik', weekStartsOn: 0 });
    expect(raw).toEqual({ journalsFolder: 'dziennik', weekStartsOn: 0 });
    expect(warnings).toEqual([]);
  });

  it('falls back to defaults for invalid JSON or non-objects', () => {
    for (const text of ['{oops', '', '[1,2]', '"text"', 'null', '42']) {
      const r = parseVaultConfig(text);
      expect(r.config).toEqual(DEFAULT_VAULT_CONFIG);
      expect(r.raw).toEqual({});
      expect(r.warnings.length).toBe(1);
    }
    expect(parseVaultConfig('{oops').warnings[0]).toContain(CONFIG_PATH);
  });

  it('rejects values of the wrong type individually', () => {
    const { config, warnings } = parseVaultConfig(
      JSON.stringify({ weekStartsOn: 3, defaultReminderTime: '25:00', journalFileFormat: '', version: 2, attachmentsFolder: 5, templatesFolder: 'szablony/' }),
    );
    expect(config.weekStartsOn).toBe(1);
    expect(config.defaultReminderTime).toBe('09:00');
    expect(config.journalFileFormat).toBe('yyyy-MM-dd');
    expect(config.version).toBe(1);
    expect(config.attachmentsFolder).toBe('assets');
    expect(config.templatesFolder).toBe('szablony');
    expect(warnings).toHaveLength(5);
  });

  it('normalises times and folders, tolerates a BOM', () => {
    const { config } = parseVaultConfig('\uFEFF{"defaultReminderTime": "7:30", "journalsFolder": "/Dziennik/Żółty/", "journalTemplate": "./szablony/dzień.md"}');
    expect(config.defaultReminderTime).toBe('07:30');
    expect(config.journalsFolder).toBe('Dziennik/Żółty');
    expect(config.journalTemplate).toBe('szablony/dzień.md');
  });

  it('mergeVaultConfig ignores unknown keys', () => {
    expect(mergeVaultConfig({ foo: 1 }).config).toEqual(DEFAULT_VAULT_CONFIG);
  });

  it('serialises known keys in canonical order and keeps unknown keys', () => {
    const text = serializeVaultConfig({ ...DEFAULT_VAULT_CONFIG, journalsFolder: 'Dziennik' }, { futureSetting: { a: 1 }, journalsFolder: 'old' });
    expect(text.endsWith('\n')).toBe(true);
    const parsed = JSON.parse(text) as Record<string, unknown>;
    expect(Object.keys(parsed)).toEqual([...Object.keys(DEFAULT_VAULT_CONFIG), 'futureSetting']);
    expect(parsed.journalsFolder).toBe('Dziennik');
    expect(parsed.futureSetting).toEqual({ a: 1 });
    expect(parseVaultConfig(text).config.journalsFolder).toBe('Dziennik');
  });

  it('keeps unknown keys named like Object.prototype members without prototype pollution', () => {
    const parsed = parseVaultConfig('{"constructor":"x","toString":1,"__proto__":{"polluted":true},"journalsFolder":"Dziennik"}');
    expect(parsed.config.journalsFolder).toBe('Dziennik');
    const back = JSON.parse(serializeVaultConfig(parsed.config, parsed.raw)) as Record<string, unknown>;
    expect(Object.keys(back)).toEqual(expect.arrayContaining(['constructor', 'toString', '__proto__', 'journalsFolder']));
    expect(back.constructor).toBe('x');
    expect(({} as Record<string, unknown>).polluted).toBeUndefined();
  });
});
