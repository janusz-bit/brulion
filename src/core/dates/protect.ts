/**
 * Spans of an input in which natural-language dates must never be recognised: URLs, e-mail
 * addresses, wiki links/embeds, Markdown links, inline code, tags and dates that already are
 * canonical Brulion tokens (`📅 2026-10-15`, `⏰! 2026-10-15 06:30`, `(@2026-10-15 09:00)`).
 *
 * Callers strip the recognised phrase from the text (quick capture), so a match inside
 * `https://blog.pl/2026/10/15/post` or `[[Spotkanie w piątek]]` would corrupt the URL or the
 * link target, and the dates inside them are not meant as input anyway.
 *
 * Every pattern is linear: each repeated character class stops at the next opening
 * delimiter, so unclosed brackets or backticks cannot cause quadratic rescans. E-mail
 * addresses are found by expanding around each "@" instead of a regex (a pattern like
 * /[\w.]+@/ rescans long words from every start position).
 */

/**
 * Sources of the protected-span patterns; they run on folded (lower-case) text. Compiled
 * lazily (see {@link patterns}): lookbehind in a regex literal would be a parse-time error on
 * older WebKit and break loading the whole module, not just this feature.
 */
const PROTECTED_SOURCES: readonly string[] = [
  // Bare URLs: "https://…", "mailto:…", "www.…" up to the next whitespace.
  String.raw`(?<![\p{L}\p{N}])(?:https?|ftp|file|mailto):[^\s<>]*`,
  String.raw`(?<![\p{L}\p{N}.])www\.[^\s<>]*`,
  // Wiki links and embeds ("[[Page]]", "![[Page#^id]]", tag "#[[multi word]]").
  String.raw`!?\[\[[^[\]\n]*\]\]`,
  // Markdown links / images: "[text](target)".
  String.raw`!?\[[^[\]\n]*\]\([^()\s]*\)`,
  // Inline code.
  '`[^`\\n]*`',
  // Tags (SYNTAX §3): after start, whitespace or "("; not all digits ("#1" is no tag).
  String.raw`(?<![^\s(])#(?!\p{N}+(?![\p{L}\p{N}_/-]))[\p{L}\p{N}_/-]+`,
  // Canonical date tokens: 📅 ⏳ 🛫 ➕ ✅ ❌ ⏰ (⏰! alarm), optional " HH:mm".
  String.raw`[\u{1F4C5}\u{23F3}\u{1F6EB}\u{2795}\u{2705}\u{274C}\u{23F0}]\u{FE0F}?!?[ \t]*\d{4}-\d{2}-\d{2}(?:[ Tt]\d{1,2}:\d{2})?`,
  // obsidian-reminder: "(@2026-10-15 09:00)".
  String.raw`\(@\d{4}-\d{2}-\d{2}(?:[ Tt]\d{1,2}:\d{2})?\)`,
];

let patternsCache: RegExp[] | null = null;

/** The compiled protected-span patterns (built on first use). */
function patterns(): RegExp[] {
  patternsCache ??= PROTECTED_SOURCES.map((src) => new RegExp(src, 'gu'));
  return patternsCache;
}

/** Characters of the local part / domain of an e-mail address. */
const EMAIL_LOCAL = /[\p{L}\p{N}._%+-]/u;
const EMAIL_DOMAIN = /[\p{L}\p{N}.-]/u;

/** [start, end) spans of e-mail addresses ("jan.jutro@firma.pl"). Linear: expansions stop at "@". */
function emailSpans(text: string): [number, number][] {
  const out: [number, number][] = [];
  for (let at = text.indexOf('@'); at !== -1; at = text.indexOf('@', at + 1)) {
    let start = at;
    while (start > 0 && EMAIL_LOCAL.test(text.charAt(start - 1))) start--;
    let end = at + 1;
    while (end < text.length && EMAIL_DOMAIN.test(text.charAt(end))) end++;
    // Trailing dots end the sentence, not the domain (a loop: /\.+$/ backtracks on dot runs).
    while (end > at + 1 && text.charAt(end - 1) === '.') end--;
    // A domain needs a dot ("a@b" is no address).
    if (start < at && text.slice(at + 1, end).includes('.')) out.push([start, end]);
  }
  return out;
}

/**
 * Mark protected spans of `text` (folded input) in a mask: `mask[i] === 1` means unit `i`
 * belongs to a URL, link, code span, tag, e-mail address or canonical date token.
 */
export function protectedMask(text: string): Uint8Array {
  const mask = new Uint8Array(text.length);
  for (const re of patterns()) {
    for (const m of text.matchAll(re)) mask.fill(1, m.index, m.index + m[0].length);
  }
  for (const [start, end] of emailSpans(text)) mask.fill(1, start, end);
  return mask;
}
