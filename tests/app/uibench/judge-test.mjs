#!/usr/bin/env node
// judge-test.mjs <work-dir> — part of ctest tst_uibench_bench_dryrun (dryrun-test.sh): judge.mjs
// over three synthetic rounds of two screens. Round 0 is judged in full; rounds 1 and 2 change
// only screen b (pack --only-changed with a relative path), so screen a keeps round 0's votes
// through both (carried twice); a malformed vote is dropped; packing over cast votes is refused
// without --force; a re-judged batch (bNN-retry.json) replaces its votes instead of adding to
// them; a vote file whose "batch" names another batch is dropped. Node ≥ 22, no packages.
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { dirname, isAbsolute, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const work = process.argv[2];
if (!work) throw new Error('usage: judge-test.mjs <work-dir>');
rmSync(work, { recursive: true, force: true });
mkdirSync(work, { recursive: true });

let failed = 0;
const check = (ok, what) => {
  if (!ok) {
    console.error(`judge-test: FAIL ${what}`);
    ++failed;
  }
};

// ---- A minimal PNG writer (8-bit RGB, one colour) -------------------------------------------
const crcTable = Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
const crc32 = (buf) => {
  let c = 0xffffffff;
  for (const b of buf) c = crcTable[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
};
function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body));
  return Buffer.concat([len, body, crc]);
}
function png(file, [r, g, b], w = 8, h = 8) {
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; ++y)
    for (let x = 0; x < w; ++x) raw.set([r, g, b], y * (w * 3 + 1) + 1 + x * 3);
  mkdirSync(dirname(file), { recursive: true });
  writeFileSync(file, Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
}

// ---- The rounds -----------------------------------------------------------------------------
const screensFile = join(work, 'screens.json');
writeFileSync(screensFile, JSON.stringify({
  schema: 1, sizes: { desktop: { width: 8, height: 8, scale: 1, touch: false } }, themes: ['light', 'dark'],
  screens: ['a', 'b'].map((id) => ({ id, sizes: ['desktop'], context: `Ekran ${id}`, brulion: { args: [], delayMs: 0 },
    obsidian: { steps: [] } })),
}));

function makeRound(name, colourOfB) {
  const round = join(work, name);
  for (const theme of ['light', 'dark']) {
    png(join(round, `shots/brulion/a-desktop-${theme}.png`), [200, 200, 200]);
    png(join(round, `shots/brulion/b-desktop-${theme}.png`), colourOfB);
    for (const id of ['a', 'b']) png(join(round, `shots/obsidian/${id}-desktop-${theme}.png`), [50, 50, 50]);
  }
  const pairs = [];
  for (const screen of ['a', 'b'])
    for (const theme of ['light', 'dark'])
      pairs.push({ screen, size: 'desktop', theme, brulion: { O: 8 }, obsidian: { O: 7 } });
  writeFileSync(join(round, 'metrics.json'), JSON.stringify({ schema: 1, pairs, means: {} }));
  return round;
}

function judge(args, expectFailure = false) {
  const r = spawnSync(process.execPath, [join(here, 'judge.mjs'), ...args, '--screens', screensFile],
    { cwd: work, encoding: 'utf8' });
  if (expectFailure) return r;
  if (r.status !== 0) throw new Error(`judge.mjs ${args.join(' ')}: ${r.stderr}`);
  return r;
}

const all = (b) => Object.fromEntries(['hierarchy', 'typography', 'spacing', 'color', 'consistency', 'calm', 'affordance', 'appeal'].map((c) => [c, b]));

/// One vote file per batch: Brulion scored `brulion` on every criterion, Obsidian 6.
function vote(round, brulion, { fileOf = (id) => `${id}.json`, only = null, mutate = null } = {}) {
  const key = JSON.parse(readFileSync(join(round, 'judging-keys/key.json'), 'utf8'));
  for (const [id, batch] of Object.entries(key.batches)) {
    if (only && !only.includes(id)) continue;
    const out = { batch: id, judge: batch.panel, groups: [], identityGuess: [] };
    for (const [gid, g] of Object.entries(batch.groups)) {
      const pairs = [];
      for (const theme of ['light', 'dark']) {
        const sides = g[theme];
        pairs.push({ theme, invalid: false, A: all(sides.A === 'brulion' ? brulion : 6), B: all(sides.B === 'brulion' ? brulion : 6),
                     preferred: sides.A === 'brulion' ? 'A' : 'B', strengthsA: 's', weaknessesA: 'w', strengthsB: 's', weaknessesB: 'w' });
        out.identityGuess.push({ gid, theme, obsidian: sides.A === 'obsidian' ? 'A' : 'B' });
      }
      out.groups.push({ gid, pairs });
    }
    if (mutate) mutate(id, out);
    writeFileSync(join(round, 'judging/votes', fileOf(id)), JSON.stringify(out));
  }
}

const scores = (round) => JSON.parse(readFileSync(join(round, 'scores.json'), 'utf8'));
const pairOf = (s, screen, theme) => s.pairs.find((p) => p.screen === screen && p.theme === theme);

// Round 0: everything judged; one malformed pair (a criterion of 11) is dropped.
const r0 = makeRound('round-00', [200, 200, 200]);
judge(['pack', '--round', 'round-00', '--judges', '3', '--batch', '1', '--seed', '7']);
let malformedDone = false;
vote(r0, 8, { mutate: (id, out) => {
  const g = out.groups.find((x) => x.gid === 'g01');
  if (g && !malformedDone) { g.pairs[0].A.hierarchy = 11; malformedDone = true; }
} });
judge(['score', '--round', 'round-00']);
let s = scores(r0);
check(s.overall.pairs === 4, `round 0: 4 valid pairs (${s.overall.pairs})`);
check(s.dropped.some((d) => /malformed/.test(d.reason)), 'round 0: the malformed vote is dropped');
check(pairOf(s, 'a', 'light').votes === 2, `round 0: a light has 2 votes (${pairOf(s, 'a', 'light').votes})`);
check(pairOf(s, 'b', 'dark').JB === 8 && pairOf(s, 'b', 'dark').JO === 6, 'round 0: J_B 8, J_O 6');
check(s.blindness.guesses === 12 && s.blindness.correct === 12, `round 0: 12 identity guesses, all right (${s.blindness.correct}/${s.blindness.guesses})`);

// Packing again over cast votes is refused; --force allows it.
const refused = judge(['pack', '--round', 'round-00', '--judges', '3', '--batch', '1'], true);
check(refused.status === 2 && /--force/.test(refused.stderr), 'pack over cast votes is refused without --force');
check(readdirSync(join(r0, 'judging/votes')).length === 6, 'the refused pack kept the votes');

// Round 1: only b changed; a is carried from round 0 (a relative path given).
const r1 = makeRound('round-01', [120, 30, 30]);
judge(['pack', '--round', 'round-01', '--judges', '3', '--batch', '1', '--seed', '8', '--only-changed', 'round-00']);
const carried1 = JSON.parse(readFileSync(join(r1, 'judging/carried.json'), 'utf8'));
check(isAbsolute(carried1.from) && carried1.from === r0, `round 1: carried.json names round 0 by its absolute path (${carried1.from})`);
check(carried1.groups.length === 1 && carried1.groups[0].screen === 'a', 'round 1: group a carried');
vote(r1, 9);
judge(['score', '--round', 'round-01']);
s = scores(r1);
check(s.overall.pairs === 4, `round 1: 4 valid pairs (${s.overall.pairs})`);
check(pairOf(s, 'a', 'dark').votes === 3 && pairOf(s, 'a', 'dark').JB === 8, 'round 1: a keeps round 0\'s votes');
check(pairOf(s, 'b', 'light').JB === 9, 'round 1: b judged again');

// Round 2: b changed again; a is carried from round 1, which carried it from round 0.
const r2 = makeRound('round-02', [30, 120, 30]);
judge(['pack', '--round', 'round-02', '--judges', '3', '--batch', '1', '--seed', '9', '--only-changed', 'round-01']);
vote(r2, 7);
judge(['score', '--round', 'round-02']);
s = scores(r2);
check(s.overall.pairs === 4, `round 2: 4 valid pairs — a carried twice (${s.overall.pairs})`);
check(pairOf(s, 'a', 'light').votes === 2 && pairOf(s, 'a', 'dark').votes === 3, 'round 2: a has round 0\'s votes');
check(pairOf(s, 'b', 'dark').JB === 7, 'round 2: b judged again');

// A re-judged batch replaces its votes; a file naming another batch is dropped.
const firstBatch = Object.keys(JSON.parse(readFileSync(join(r2, 'judging-keys/key.json'), 'utf8')).batches)[0];
vote(r2, 10, { only: [firstBatch], fileOf: (id) => `${id}-retry.json` });
vote(r2, 10, { only: [firstBatch], fileOf: () => 'b99.json', mutate: (id, out) => { out.batch = firstBatch; } });
judge(['score', '--round', 'round-02']);
s = scores(r2);
check(pairOf(s, 'b', 'light').votes === 3, `round 2: the retry replaced its batch's votes (${pairOf(s, 'b', 'light').votes} votes)`);
check(s.dropped.some((d) => /replaced by/.test(d.reason)), 'round 2: the replaced votes are listed');
check(s.dropped.some((d) => d.file === 'b99.json' && /says batch/.test(d.reason)), 'round 2: a file naming another batch is dropped');

// Unchanged shots from another Obsidian: re-judged (both apps' pictures count).
const r3 = makeRound('round-03', [30, 120, 30]);
png(join(r3, 'shots/obsidian/a-desktop-dark.png'), [60, 90, 60]);
const packed = judge(['pack', '--round', 'round-03', '--judges', '3', '--batch', '1', '--only-changed', 'round-02']).stdout;
const carried3 = JSON.parse(readFileSync(join(r3, 'judging/carried.json'), 'utf8'));
check(carried3.groups.length === 1 && carried3.groups[0].screen === 'b', `round 3: a re-judged after Obsidian's picture changed (${packed.trim()})`);

if (failed) process.exit(1);
console.log('judge-test: ok');
