#!/usr/bin/env node
// judge.mjs — the blind judging of the UI benchmark (docs/UI.md "UI benchmark (Brulion vs
// Obsidian)", rubric.md). Node ≥ 22, no npm packages.
//
//   node judge.mjs pack  --round DIR [--judges 3] [--batch 4] [--seed N] [--only-changed PREV]
//   node judge.mjs score --round DIR [--prev PREV] [--seed N]
//
// pack: a group = screen × size (both themes). Every group goes to `--judges` different judges, a
// judge (one batch) sees at most `--batch` groups. A/B is drawn per judge, group and theme from a
// seeded RNG, balanced over the panel (of 3 judges at least one sees Brulion as A and one as B).
// Images are copied to DIR/judging/batches/bNN/gNN-<theme>-<A|B>.png (names without the apps);
// DIR/judging/batches/bNN/batch.json lists them with each screen's context; the key lives outside
// the batches, DIR/judging-keys/key.json. --only-changed PREV: only groups whose Brulion shots
// differ from PREV's (> 0.1 % of the pixels by > 8/255; the graph: its texts and targets) are
// packed; the others keep PREV's votes (DIR/judging/carried.json).
//
// score: reads the judges' outputs DIR/judging/votes/bNN.json (the rubric's JSON), maps them
// through the key and writes DIR/scores.json and DIR/report.md: per vote J = mean of the 8
// criteria; per pair (screen, size, theme) J_B and J_O = median over the judges, agreement = share
// of judges whose J_B > J_O; O from DIR/metrics.json; P = 0.75·J + 0.25·O; margin m = P_B − P_O;
// overall means with a 95 % bootstrap interval of the margin; the win condition.
import { copyFileSync, existsSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { inflateSync } from 'node:zlib';

const CRITERIA = ['hierarchy', 'typography', 'spacing', 'color', 'consistency', 'calm', 'affordance', 'appeal'];
const here = new URL('.', import.meta.url).pathname;

function parseArgs(argv) {
  const [command, ...rest] = argv;
  const args = { command, judges: 3, batch: 4, seed: 1 };
  for (let i = 0; i < rest.length; i += 2) {
    const key = rest[i].replace(/^--/, '');
    const value = rest[i + 1];
    if (value === undefined) throw new Error(`${rest[i]} needs a value`);
    if (['judges', 'batch', 'seed'].includes(key)) args[key] = Number(value);
    else if (['round', 'only-changed', 'prev', 'screens'].includes(key)) args[key] = value;
    else throw new Error(`unknown option ${rest[i]}`);
  }
  if (!['pack', 'score'].includes(command) || !args.round) throw new Error('usage: judge.mjs pack|score --round DIR …');
  return args;
}

// mulberry32: a small seeded RNG (reproducible packs and bootstrap intervals).
function rng(seed) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function shuffle(list, random) {
  const a = [...list];
  for (let i = a.length - 1; i > 0; --i) {
    const j = Math.floor(random() * (i + 1));
    [a[i], a[j]] = [a[j], a[i]];
  }
  return a;
}

const readJson = (f, fallback) => (existsSync(f) ? JSON.parse(readFileSync(f, 'utf8')) : fallback);
const median = (v) => {
  const s = [...v].sort((a, b) => a - b);
  const n = s.length;
  return n ? (n % 2 ? s[(n - 1) / 2] : (s[n / 2 - 1] + s[n / 2]) / 2) : null;
};
const mean = (v) => (v.length ? v.reduce((a, b) => a + b, 0) / v.length : null);
const r3 = (n) => (n === null || n === undefined ? null : Math.round(n * 1000) / 1000);
const pad = (n) => String(n).padStart(2, '0');

/// The groups of a round: screens.json order, a group when both apps have a shot of it.
function groupsOf(round, screensFile) {
  const manifest = readJson(screensFile, null);
  const groups = [];
  let n = 0;
  for (const screen of manifest.screens) {
    for (const size of screen.sizes) {
      ++n;
      const g = manifest.sizes[size];
      const device = g.touch
        ? `phone ${g.width}×${g.height} at ${g.scale}× (${g.width * g.scale}×${g.height * g.scale} px)`
        : `desktop ${g.width}×${g.height}`;
      const pairs = {};
      for (const theme of manifest.themes) {
        const b = join(round, 'shots/brulion', `${screen.id}-${size}-${theme}.png`);
        const o = join(round, 'shots/obsidian', `${screen.id}-${size}-${theme}.png`);
        if (existsSync(b) && existsSync(o)) pairs[theme] = { brulion: resolve(b), obsidian: resolve(o) };
      }
      if (Object.keys(pairs).length)
        groups.push({ gid: `g${pad(n)}`, screen: screen.id, size, context: screen.context, device, pairs });
    }
  }
  return groups;
}

// ---- PNG comparison (--only-changed) ---------------------------------------------------------

function decodePng(file) {
  const buf = readFileSync(file);
  let pos = 8, width = 0, height = 0, colorType = 0, depth = 0;
  const idat = [];
  while (pos < buf.length) {
    const len = buf.readUInt32BE(pos);
    const type = buf.toString('ascii', pos + 4, pos + 8);
    const data = buf.subarray(pos + 8, pos + 8 + len);
    if (type === 'IHDR') {
      width = data.readUInt32BE(0); height = data.readUInt32BE(4); depth = data[8]; colorType = data[9];
      if (data[12] !== 0) throw new Error(`${file}: interlaced PNG`);
    } else if (type === 'IDAT') idat.push(data);
    else if (type === 'IEND') break;
    pos += 12 + len;
  }
  const channels = { 0: 1, 2: 3, 4: 2, 6: 4 }[colorType];
  if (depth !== 8 || !channels) throw new Error(`${file}: unsupported PNG (depth ${depth}, colour type ${colorType})`);
  const raw = inflateSync(Buffer.concat(idat));
  const stride = width * channels;
  const out = Buffer.alloc(stride * height);
  for (let y = 0; y < height; ++y) {
    const filter = raw[y * (stride + 1)];
    const line = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let x = 0; x < stride; ++x) {
      const a = x >= channels ? out[y * stride + x - channels] : 0;
      const b = y > 0 ? out[(y - 1) * stride + x] : 0;
      const c = x >= channels && y > 0 ? out[(y - 1) * stride + x - channels] : 0;
      let v = line[x];
      if (filter === 1) v += a;
      else if (filter === 2) v += b;
      else if (filter === 3) v += (a + b) >> 1;
      else if (filter === 4) {
        const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
        v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      }
      out[y * stride + x] = v & 0xff;
    }
  }
  return { width, height, channels, data: out };
}

/// True when the two PNGs differ in more than 0.1 % of the pixels by more than 8/255.
function pngChanged(a, b) {
  if (!existsSync(a) || !existsSync(b)) return true;
  try {
    const x = decodePng(a), y = decodePng(b);
    if (x.width !== y.width || x.height !== y.height || x.channels !== y.channels) return true;
    let differing = 0;
    const px = x.width * x.height;
    for (let i = 0; i < px; ++i) {
      for (let c = 0; c < Math.min(3, x.channels); ++c) {
        if (Math.abs(x.data[i * x.channels + c] - y.data[i * y.channels + c]) > 8) { ++differing; break; }
      }
    }
    return differing > 0.001 * px;
  } catch {
    return true;
  }
}

/// The graph settles from a random layout: compared by its texts and targets, not its pixels.
function dumpChanged(a, b) {
  const sig = (f) => {
    const d = readJson(f, null);
    if (!d) return null;
    return JSON.stringify([d.texts.map((t) => [t.text, t.px, t.color]).sort(), d.targets.map((t) => [t.label, t.kind]).sort()]);
  };
  const x = sig(a), y = sig(b);
  return x === null || x !== y;
}

// ---- pack ------------------------------------------------------------------------------------

function pack(args) {
  const round = resolve(args.round);
  const screens = args.screens ?? join(here, 'screens.json');
  let groups = groupsOf(round, screens);
  const carried = [];
  if (args['only-changed']) {
    const prev = resolve(args['only-changed']);
    groups = groups.filter((g) => {
      const changed = Object.keys(g.pairs).some((theme) => {
        const name = `${g.screen}-${g.size}-${theme}`;
        const now = join(round, 'shots/brulion', name);
        const before = join(prev, 'shots/brulion', name);
        return g.screen === 'graph' ? dumpChanged(`${now}.layout.json`, `${before}.layout.json`)
          : pngChanged(`${now}.png`, `${before}.png`);
      });
      if (!changed) carried.push(g.gid);
      return changed;
    });
  }
  const random = rng(args.seed);
  const judging = join(round, 'judging');
  const keys = join(round, 'judging-keys');
  rmSync(judging, { recursive: true, force: true });
  rmSync(keys, { recursive: true, force: true });
  mkdirSync(join(judging, 'batches'), { recursive: true });
  mkdirSync(join(judging, 'votes'), { recursive: true });
  mkdirSync(keys, { recursive: true });

  // A/B per group and theme over the panel: one judge sees Brulion as A, one as B, the rest random.
  const sides = {};
  for (const g of groups) {
    sides[g.gid] = {};
    for (const theme of Object.keys(g.pairs)) {
      const draw = [true, false];
      while (draw.length < args.judges) draw.push(random() < 0.5);
      sides[g.gid][theme] = shuffle(draw.slice(0, args.judges), random);
    }
  }
  // Panel p: every group once, in batches of `--batch` (so a group's judges are different batches).
  const key = { seed: args.seed, judges: args.judges, batch: args.batch, round, batches: {}, carried };
  let b = 0;
  for (let p = 0; p < args.judges; ++p) {
    const order = shuffle(groups, random);
    for (let i = 0; i < order.length; i += args.batch) {
      const id = `b${pad(++b)}`;
      const dir = join(judging, 'batches', id);
      mkdirSync(dir, { recursive: true });
      const batch = { batch: id, rubric: resolve(here, 'rubric.md'), groups: [] };
      key.batches[id] = { panel: p, groups: {} };
      for (const g of order.slice(i, i + args.batch)) {
        const images = {};
        key.batches[id].groups[g.gid] = { screen: g.screen, size: g.size };
        for (const [theme, pair] of Object.entries(g.pairs)) {
          const brulionIsA = sides[g.gid][theme][p];
          const a = brulionIsA ? pair.brulion : pair.obsidian;
          const bb = brulionIsA ? pair.obsidian : pair.brulion;
          const fa = join(dir, `${g.gid}-${theme}-A.png`), fb = join(dir, `${g.gid}-${theme}-B.png`);
          copyFileSync(a, fa);
          copyFileSync(bb, fb);
          images[theme] = { A: fa, B: fb };
          key.batches[id].groups[g.gid][theme] = { A: brulionIsA ? 'brulion' : 'obsidian', B: brulionIsA ? 'obsidian' : 'brulion' };
        }
        batch.groups.push({ gid: g.gid, context: g.context, device: g.device, images });
      }
      writeFileSync(join(dir, 'batch.json'), JSON.stringify(batch, null, 2) + '\n');
    }
  }
  writeFileSync(join(keys, 'key.json'), JSON.stringify(key, null, 2) + '\n');
  writeFileSync(join(judging, 'carried.json'), JSON.stringify({ from: args['only-changed'] ?? null, groups: carried }, null, 2) + '\n');
  console.log(`judge: ${groups.length} groups in ${b} batches (${args.judges} judges each)${carried.length ? `, ${carried.length} carried over` : ''}`);
}

// ---- score -----------------------------------------------------------------------------------

function validScores(s) {
  return s && CRITERIA.every((c) => Number.isInteger(s[c]) && s[c] >= 1 && s[c] <= 10);
}

/// Votes of a round as {screen, size, theme, judge, brulion: {…}, obsidian: {…}, guess, comments}.
function votesOf(round) {
  const key = readJson(join(round, 'judging-keys/key.json'), null);
  if (!key) return { votes: [], dropped: [], guesses: [] };
  const votes = [], dropped = [], guesses = [];
  const dir = join(round, 'judging/votes');
  const files = existsSync(dir) ? readdirSync(dir).filter((f) => f.endsWith('.json')).sort() : [];
  for (const f of files) {
    let out;
    try { out = JSON.parse(readFileSync(join(dir, f), 'utf8')); } catch { dropped.push({ file: f, reason: 'not JSON' }); continue; }
    const batchId = out.batch ?? f.replace(/\.json$/, '');
    const batch = key.batches[batchId];
    if (!batch) { dropped.push({ file: f, reason: `unknown batch ${batchId}` }); continue; }
    for (const g of out.groups ?? []) {
      const map = batch.groups[g.gid];
      if (!map) { dropped.push({ file: f, reason: `group ${g.gid} not in ${batchId}` }); continue; }
      for (const pair of g.pairs ?? []) {
        const sides = map[pair.theme];
        if (!sides) { dropped.push({ file: f, reason: `${g.gid} ${pair.theme}: no such pair` }); continue; }
        if (pair.invalid || !validScores(pair.A) || !validScores(pair.B)) {
          dropped.push({ file: f, reason: `${g.gid} ${pair.theme}: ${pair.invalid ? 'marked invalid' : 'malformed scores'}` });
          continue;
        }
        const bySide = { A: pair.A, B: pair.B };
        const appSide = sides.A === 'brulion' ? { brulion: 'A', obsidian: 'B' } : { brulion: 'B', obsidian: 'A' };
        votes.push({
          batch: batchId, screen: map.screen, size: map.size, theme: pair.theme,
          brulion: bySide[appSide.brulion], obsidian: bySide[appSide.obsidian],
          preferred: pair.preferred === appSide.brulion ? 'brulion' : pair.preferred === appSide.obsidian ? 'obsidian' : null,
          comments: {
            brulion: { strengths: pair[`strengths${appSide.brulion}`] ?? '', weaknesses: pair[`weaknesses${appSide.brulion}`] ?? '' },
            obsidian: { strengths: pair[`strengths${appSide.obsidian}`] ?? '', weaknesses: pair[`weaknesses${appSide.obsidian}`] ?? '' },
          },
        });
      }
    }
    for (const guess of out.identityGuess ?? []) {
      const sides = batch.groups[guess.gid]?.[guess.theme];
      if (!sides) continue;
      const truth = sides.A === 'obsidian' ? 'A' : 'B';
      guesses.push({ gid: guess.gid, theme: guess.theme, guess: guess.obsidian, correct: guess.obsidian === truth });
    }
  }
  return { votes, dropped, guesses, key };
}

const jOf = (s) => mean(CRITERIA.map((c) => s[c]));

function score(args) {
  const round = resolve(args.round);
  let { votes, dropped, guesses, key } = votesOf(round);
  // Groups not re-judged keep the previous round's votes (pack --only-changed).
  const carried = readJson(join(round, 'judging/carried.json'), { from: null, groups: [] });
  if (carried.from && carried.groups.length) {
    const prev = votesOf(resolve(carried.from));
    const carriedKeys = new Set();
    for (const b of Object.values(prev.key?.batches ?? {}))
      for (const [gid, g] of Object.entries(b.groups)) if (carried.groups.includes(gid)) carriedKeys.add(`${g.screen}|${g.size}`);
    votes = votes.concat(prev.votes.filter((v) => carriedKeys.has(`${v.screen}|${v.size}`)).map((v) => ({ ...v, carried: true })));
  }
  const metrics = readJson(join(round, 'metrics.json'), { pairs: [], means: {} });
  const prevScores = args.prev ? readJson(join(resolve(args.prev), 'scores.json'), null) : null;

  const pairKey = (v) => `${v.screen}|${v.size}|${v.theme}`;
  const byPair = new Map();
  for (const v of votes) {
    if (!byPair.has(pairKey(v))) byPair.set(pairKey(v), []);
    byPair.get(pairKey(v)).push(v);
  }
  const metricOf = new Map(metrics.pairs.map((p) => [`${p.screen}|${p.size}|${p.theme}`, p]));
  const keys = new Set([...byPair.keys(), ...metricOf.keys()]);
  const pairs = [];
  for (const k of [...keys].sort()) {
    const [screen, size, theme] = k.split('|');
    const vs = byPair.get(k) ?? [];
    const m = metricOf.get(k);
    const JB = median(vs.map((v) => jOf(v.brulion))), JO = median(vs.map((v) => jOf(v.obsidian)));
    const OB = m?.brulion?.O ?? null, OO = m?.obsidian?.O ?? null;
    const valid = vs.length >= 2 && OB !== null && OO !== null;
    const PB = valid ? 0.75 * JB + 0.25 * OB : null, PO = valid ? 0.75 * JO + 0.25 * OO : null;
    const agreement = vs.length ? vs.filter((v) => jOf(v.brulion) > jOf(v.obsidian)).length / vs.length : null;
    const margin = valid ? PB - PO : null;
    const prev = prevScores?.pairs?.find((p) => p.screen === screen && p.size === size && p.theme === theme);
    pairs.push({
      screen, size, theme, votes: vs.length, valid, rejudge: vs.length < 2 && OB !== null && OO !== null,
      JB: r3(JB), JO: r3(JO), OB: r3(OB), OO: r3(OO), PB: r3(PB), PO: r3(PO), margin: r3(margin), agreement: r3(agreement),
      win: valid && margin >= 0.2 && JB >= JO && agreement >= 2 / 3,
      delta: prev && valid && prev.valid ? { PB: r3(PB - prev.PB), JB: r3(JB - prev.JB), OB: r3(OB - prev.OB) } : null,
    });
  }
  const valid = pairs.filter((p) => p.valid);
  const overall = {
    pairs: valid.length,
    PB: r3(mean(valid.map((p) => p.PB))), PO: r3(mean(valid.map((p) => p.PO))),
    JB: r3(mean(valid.map((p) => p.JB))), JO: r3(mean(valid.map((p) => p.JO))),
    OB: r3(mean(valid.map((p) => p.OB))), OO: r3(mean(valid.map((p) => p.OO))),
  };
  overall.margin = r3(overall.PB - overall.PO);
  // Bootstrap: resample the pairs, and inside a pair its votes.
  const random = rng(args.seed ?? 1);
  const samples = [];
  for (let i = 0; i < 1000 && valid.length; ++i) {
    let sum = 0;
    for (let j = 0; j < valid.length; ++j) {
      const p = valid[Math.floor(random() * valid.length)];
      const vs = byPair.get(`${p.screen}|${p.size}|${p.theme}`);
      const re = vs.map(() => vs[Math.floor(random() * vs.length)]);
      const jb = median(re.map((v) => jOf(v.brulion))), jo = median(re.map((v) => jOf(v.obsidian)));
      sum += 0.75 * (jb - jo) + 0.25 * (p.OB - p.OO);
    }
    samples.push(sum / valid.length);
  }
  samples.sort((a, b) => a - b);
  overall.marginCI = samples.length ? [r3(samples[Math.floor(0.025 * samples.length)]), r3(samples[Math.ceil(0.975 * samples.length) - 1])] : null;
  overall.win = valid.length > 0 && valid.every((p) => p.win) && overall.margin >= 0.5 && overall.marginCI[0] > 0
    && overall.JB > overall.JO && overall.OB >= overall.OO;
  const criteria = {};
  for (const c of CRITERIA) {
    criteria[c] = { brulion: r3(mean(votes.map((v) => v.brulion[c]))), obsidian: r3(mean(votes.map((v) => v.obsidian[c]))) };
  }
  const blindness = {
    guesses: guesses.length,
    correct: guesses.filter((g) => g.correct).length,
    unsure: guesses.filter((g) => g.guess === 'unsure').length,
  };
  blindness.accuracy = guesses.length ? r3(blindness.correct / guesses.length) : null;
  const comments = {};
  for (const v of votes) {
    const k = `${v.screen}-${v.size}`;
    (comments[k] ??= []).push({ theme: v.theme, batch: v.batch, brulion: v.comments.brulion, obsidian: v.comments.obsidian });
  }
  const scores = { schema: 1, round, pairs, overall, criteria, blindness, dropped, metricMeans: metrics.means, comments };
  writeFileSync(join(round, 'scores.json'), JSON.stringify(scores, null, 2) + '\n');

  const lines = [];
  lines.push(`# UI benchmark — ${round.split('/').pop()}`, '');
  lines.push(`Overall (${overall.pairs} valid pairs): **P Brulion ${overall.PB} vs Obsidian ${overall.PO}** (margin ${overall.margin}, 95 % CI ${J(overall.marginCI)}); J ${overall.JB} vs ${overall.JO}; O ${overall.OB} vs ${overall.OO}. Win condition: **${overall.win ? 'met' : 'not met'}**.`, '');
  lines.push('| Screen | Size | Theme | Votes | J_B | J_O | O_B | O_O | P_B | P_O | Margin | Agreement | Win | ΔP_B |');
  lines.push('|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|');
  for (const p of pairs)
    lines.push(`| ${p.screen} | ${p.size} | ${p.theme} | ${p.votes} | ${p.JB ?? '–'} | ${p.JO ?? '–'} | ${p.OB ?? '–'} | ${p.OO ?? '–'} | ${p.PB ?? '–'} | ${p.PO ?? '–'} | ${p.margin ?? '–'} | ${p.agreement ?? '–'} | ${p.valid ? (p.win ? 'yes' : 'no') : (p.rejudge ? 're-judge' : 'n/a')} | ${p.delta?.PB ?? ''} |`);
  lines.push('', '## Criteria (mean over all votes)', '', '| Criterion | Brulion | Obsidian |', '|---|---:|---:|');
  for (const c of CRITERIA) lines.push(`| ${c} | ${criteria[c].brulion ?? '–'} | ${criteria[c].obsidian ?? '–'} |`);
  lines.push('', `## Blindness`, '', `${blindness.correct} of ${blindness.guesses} identity guesses right (${blindness.unsure} unsure).`);
  if (dropped.length) lines.push('', '## Dropped votes', '', ...dropped.map((d) => `- ${d.file}: ${d.reason}`));
  lines.push('', '## Comments', '');
  for (const [k, list] of Object.entries(comments)) {
    lines.push(`### ${k}`, '');
    for (const c of list)
      lines.push(`- ${c.theme} (${c.batch}) Brulion: + ${c.brulion.strengths} − ${c.brulion.weaknesses} / Obsidian: + ${c.obsidian.strengths} − ${c.obsidian.weaknesses}`);
    lines.push('');
  }
  writeFileSync(join(round, 'report.md'), lines.join('\n') + '\n');
  console.log(`judge: ${valid.length} valid pairs, margin ${overall.margin}, win ${overall.win}`);
}

const J = JSON.stringify;

try {
  const args = parseArgs(process.argv.slice(2));
  if (args.command === 'pack') pack(args);
  else score(args);
} catch (e) {
  console.error(`judge: ${e.message}`);
  process.exit(2);
}
