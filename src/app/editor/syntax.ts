/**
 * Brulion dialect (docs/SYNTAX.md) as @lezer/markdown extensions, so editor extensions (live
 * preview, autocomplete, commands) can work on syntax nodes instead of regexes.
 *
 * Syntax node names (stable API for src/app/editor/preview):
 *
 * | Node            | Children                                        | Source                      |
 * |-----------------|-------------------------------------------------|-----------------------------|
 * | `WikiLink`      | `WikiMark` (`[[`, `|`, `]]`), `WikiTarget`, `WikiAlias` | `[[Page#Head|alias]]` |
 * | `Embed`         | `WikiMark` (`![[`, `|`, `]]`), `WikiTarget`, `WikiAlias` | `![[photo.png]]`     |
 * | `BlockRef`      | `BlockRefMark` (`((`, `))`), `BlockRefId`       | `((abc123))`                |
 * | `BlockEmbed`    | `BlockRefMark` (`!((`, `))`), `BlockRefId`      | `!((abc123))`               |
 * | `Tag`           | `TagMark` (`#`, or `#[[` + `]]`), `TagName`     | `#tag`, `#[[multi word]]`   |
 * | `Highlight`     | `HighlightMark` (`==`)                          | `==text==`                  |
 * | `Comment`       | `CommentMark` (`%%`)                            | `%%hidden%%` (inline)       |
 * | `CommentBlock`  | `CommentMark`                                   | `%%` lines … `%%`           |
 * | `BlockId`       | —                                               | `^abc-123` at a line end    |
 * | `TaskToken`     | `TaskTokenMark` (emoji, `⏰!`), `TaskTokenValue`| `📅 2026-10-15`, `⏰ … 09:00`, `🔁 every week` |
 * | `PriorityToken` | —                                               | `🔺` `⏫` `🔼` `🔽` `⏬`      |
 * | `PropertyLine`  | `PropertyKey`, `PropertyMark` (`::`), `PropertyValue` (inline content) | `key:: value` |
 * | `Frontmatter`   | `FrontmatterMark` (`---`), `FrontmatterContent` | YAML at the start of a file |
 *
 * GFM's `Task` / `TaskMarker` also cover Brulion's `[/]`, `[-]` and `[>]` states.
 * WikiTarget covers everything between `[[` and `|`/`]]`, including `#Heading` / `#^id`.
 * A `TaskToken`'s kind is its mark text (`📅` due, `⏳` scheduled, `🛫` start, `✅` done,
 * `❌` cancelled, `➕` created, `🔁` recurrence, `⏰` reminder, `⏰!` alarm).
 *
 * Standard CommonMark/GFM node names (Link, Image, URL, InlineCode, FencedCode, CodeInfo,
 * Table, Task, TaskMarker, ListItem, BulletList, OrderedList, ListMark, Blockquote, QuoteMark,
 * ATXHeading1…6, HeaderMark, Emphasis, StrongEmphasis, Strikethrough, HorizontalRule, …) are
 * unchanged. Subscript, superscript and `:emoji:` shortcodes are not part of the dialect and
 * are removed.
 */
import { styleTags, tags, Tag as HighlightTag } from '@lezer/highlight';
import type {
  BlockContext,
  Element,
  InlineContext,
  LeafBlock,
  LeafBlockParser,
  Line,
  MarkdownConfig,
  MarkdownExtension,
} from '@lezer/markdown';

/** Highlight tags for the dialect's nodes (used by highlight.ts, available to others). */
export const brulionTags = {
  wikiLink: HighlightTag.define(tags.link),
  wikiTarget: HighlightTag.define(tags.link),
  embed: HighlightTag.define(tags.link),
  blockRef: HighlightTag.define(tags.link),
  tag: HighlightTag.define(tags.labelName),
  highlight: HighlightTag.define(),
  comment: HighlightTag.define(tags.comment),
  blockId: HighlightTag.define(tags.meta),
  taskToken: HighlightTag.define(tags.annotation),
  reminder: HighlightTag.define(tags.annotation),
  priority: HighlightTag.define(tags.annotation),
  propertyKey: HighlightTag.define(tags.propertyName),
  frontmatter: HighlightTag.define(tags.meta),
  taskMarker: HighlightTag.define(),
};

// ---------------------------------------------------------------------------------------------
// Character helpers
// ---------------------------------------------------------------------------------------------

const CH = {
  tab: 9,
  newline: 10,
  space: 32,
  bang: 33,
  hash: 35,
  percent: 37,
  lparen: 40,
  rparen: 41,
  equals: 61,
  lbracket: 91,
  rbracket: 93,
  caret: 94,
} as const;

function isSpace(code: number): boolean {
  return code === CH.space || code === CH.tab;
}

/** End of the current line within the inline section (position of '\n' or section end). */
function lineEndOf(cx: InlineContext, pos: number): number {
  const rel = cx.text.indexOf('\n', pos - cx.offset);
  return rel < 0 ? cx.end : cx.offset + rel;
}

/** Whether `pos` is the first non-blank position of its line within the inline section. */
function atLineStart(cx: InlineContext, pos: number): boolean {
  let p = pos - 1;
  while (p >= cx.offset && isSpace(cx.char(p))) p--;
  return p < cx.offset || cx.char(p) === CH.newline;
}

// ---------------------------------------------------------------------------------------------
// [[wiki links]] and ![[embeds]]
// ---------------------------------------------------------------------------------------------

/** Parse `[[...]]` starting at `open` (index of the first '['); returns elements or null. */
function wikiElements(cx: InlineContext, start: number, open: number, node: 'WikiLink' | 'Embed'): Element | null {
  const innerFrom = open + 2;
  const lineEnd = lineEndOf(cx, innerFrom);
  const rest = cx.slice(innerFrom, lineEnd);
  const close = rest.indexOf(']]');
  if (close <= 0) return null;
  const inner = rest.slice(0, close);
  if (inner.includes('[[') || inner.trim() === '') return null;
  const innerTo = innerFrom + close;
  const children: Element[] = [cx.elt('WikiMark', start, innerFrom)];
  const pipe = inner.indexOf('|');
  if (pipe >= 0) {
    if (pipe > 0) children.push(cx.elt('WikiTarget', innerFrom, innerFrom + pipe));
    children.push(cx.elt('WikiMark', innerFrom + pipe, innerFrom + pipe + 1));
    if (innerFrom + pipe + 1 < innerTo) children.push(cx.elt('WikiAlias', innerFrom + pipe + 1, innerTo));
  } else {
    children.push(cx.elt('WikiTarget', innerFrom, innerTo));
  }
  children.push(cx.elt('WikiMark', innerTo, innerTo + 2));
  return cx.elt(node, start, innerTo + 2, children);
}

const WikiLinks: MarkdownConfig = {
  defineNodes: [
    { name: 'WikiLink', style: brulionTags.wikiLink },
    { name: 'Embed', style: brulionTags.embed },
    { name: 'WikiMark', style: tags.processingInstruction },
    { name: 'WikiTarget', style: brulionTags.wikiTarget },
    { name: 'WikiAlias', style: brulionTags.wikiLink },
  ],
  parseInline: [
    {
      name: 'Embed',
      before: 'Image',
      parse(cx, next, pos) {
        if (next !== CH.bang || cx.char(pos + 1) !== CH.lbracket || cx.char(pos + 2) !== CH.lbracket) return -1;
        const el = wikiElements(cx, pos, pos + 1, 'Embed');
        return el ? cx.addElement(el) : -1;
      },
    },
    {
      name: 'WikiLink',
      before: 'Link',
      parse(cx, next, pos) {
        if (next !== CH.lbracket || cx.char(pos + 1) !== CH.lbracket) return -1;
        const el = wikiElements(cx, pos, pos, 'WikiLink');
        return el ? cx.addElement(el) : -1;
      },
    },
  ],
};

// ---------------------------------------------------------------------------------------------
// ((block refs)) and !((block embeds))
// ---------------------------------------------------------------------------------------------

const BLOCK_ID_RE = /^[A-Za-z0-9-]+$/;

function blockRefElement(cx: InlineContext, start: number, open: number, node: 'BlockRef' | 'BlockEmbed'): Element | null {
  const idFrom = open + 2;
  const lineEnd = lineEndOf(cx, idFrom);
  const rest = cx.slice(idFrom, lineEnd);
  const close = rest.indexOf('))');
  if (close <= 0) return null;
  const id = rest.slice(0, close);
  if (!BLOCK_ID_RE.test(id)) return null;
  const idTo = idFrom + close;
  return cx.elt(node, start, idTo + 2, [
    cx.elt('BlockRefMark', start, idFrom),
    cx.elt('BlockRefId', idFrom, idTo),
    cx.elt('BlockRefMark', idTo, idTo + 2),
  ]);
}

const BlockRefs: MarkdownConfig = {
  defineNodes: [
    { name: 'BlockRef', style: brulionTags.blockRef },
    { name: 'BlockEmbed', style: brulionTags.blockRef },
    { name: 'BlockRefMark', style: tags.processingInstruction },
    { name: 'BlockRefId', style: brulionTags.blockRef },
  ],
  parseInline: [
    {
      name: 'BlockEmbed',
      before: 'Image',
      parse(cx, next, pos) {
        if (next !== CH.bang || cx.char(pos + 1) !== CH.lparen || cx.char(pos + 2) !== CH.lparen) return -1;
        const el = blockRefElement(cx, pos, pos + 1, 'BlockEmbed');
        return el ? cx.addElement(el) : -1;
      },
    },
    {
      name: 'BlockRef',
      before: 'Link',
      parse(cx, next, pos) {
        if (next !== CH.lparen || cx.char(pos + 1) !== CH.lparen) return -1;
        const el = blockRefElement(cx, pos, pos, 'BlockRef');
        return el ? cx.addElement(el) : -1;
      },
    },
  ],
};

// ---------------------------------------------------------------------------------------------
// #tags (SYNTAX §3)
// ---------------------------------------------------------------------------------------------

/** Tag body characters: Unicode letters/digits (incl. combining marks), `_`, `-`, `/`. */
const TAG_BODY_RE = /^[\p{L}\p{N}\p{M}_\-/]+/u;

/** Length of a tag body at the start of `s` (0 when not a valid tag: needs a non-digit). */
export function tagBodyLength(s: string): number {
  const m = TAG_BODY_RE.exec(s);
  if (!m) return 0;
  let body = m[0];
  // A trailing '/' or '-' is punctuation more often than part of the tag ("#tag/").
  while (body.endsWith('/')) body = body.slice(0, -1);
  if (!/[^\p{N}]/u.test(body)) return 0;
  return body.length;
}

const Tags: MarkdownConfig = {
  defineNodes: [
    { name: 'Tag', style: brulionTags.tag },
    { name: 'TagMark', style: brulionTags.tag },
    { name: 'TagName', style: brulionTags.tag },
  ],
  parseInline: [
    {
      name: 'Tag',
      before: 'Link',
      parse(cx, next, pos) {
        if (next !== CH.hash) return -1;
        if (pos > cx.offset) {
          const prev = cx.char(pos - 1);
          if (!isSpace(prev) && prev !== CH.newline && prev !== CH.lparen) return -1;
        }
        // #[[multi word tag]]
        if (cx.char(pos + 1) === CH.lbracket && cx.char(pos + 2) === CH.lbracket) {
          const from = pos + 3;
          const rest = cx.slice(from, lineEndOf(cx, from));
          const close = rest.indexOf(']]');
          if (close <= 0 || rest.slice(0, close).includes('[')) return -1;
          return cx.addElement(
            cx.elt('Tag', pos, from + close + 2, [
              cx.elt('TagMark', pos, from),
              cx.elt('TagName', from, from + close),
              cx.elt('TagMark', from + close, from + close + 2),
            ]),
          );
        }
        const len = tagBodyLength(cx.slice(pos + 1, lineEndOf(cx, pos + 1)));
        if (len === 0) return -1;
        return cx.addElement(
          cx.elt('Tag', pos, pos + 1 + len, [cx.elt('TagMark', pos, pos + 1), cx.elt('TagName', pos + 1, pos + 1 + len)]),
        );
      },
    },
  ],
};

// ---------------------------------------------------------------------------------------------
// ==highlight== and %%comments%%
// ---------------------------------------------------------------------------------------------

const PUNCT = /[!-/:-@[-`{-~\p{P}\p{S}]/u;
const HighlightDelim = { resolve: 'Highlight', mark: 'HighlightMark' };

const Highlight: MarkdownConfig = {
  defineNodes: [
    { name: 'Highlight', style: { 'Highlight/...': brulionTags.highlight } },
    { name: 'HighlightMark', style: tags.processingInstruction },
  ],
  parseInline: [
    {
      name: 'Highlight',
      after: 'Emphasis',
      parse(cx, next, pos) {
        if (next !== CH.equals || cx.char(pos + 1) !== CH.equals || cx.char(pos + 2) === CH.equals) return -1;
        if (pos > cx.offset && cx.char(pos - 1) === CH.equals) return -1;
        const before = cx.slice(pos - 1, pos);
        const after = cx.slice(pos + 2, pos + 3);
        const sBefore = /\s|^$/.test(before);
        const sAfter = /\s|^$/.test(after);
        const pBefore = PUNCT.test(before);
        const pAfter = PUNCT.test(after);
        return cx.addDelimiter(
          HighlightDelim,
          pos,
          pos + 2,
          !sAfter && (!pAfter || sBefore || pBefore),
          !sBefore && (!pBefore || sAfter || pAfter),
        );
      },
    },
  ],
};

/** A line that opens (or closes) a block comment: `%%` alone, or `%%` + text without a close. */
function commentBlockStart(text: string): boolean {
  const t = text.trim();
  if (!t.startsWith('%%')) return false;
  return t.indexOf('%%', 2) < 0;
}

const Comments: MarkdownConfig = {
  defineNodes: [
    { name: 'Comment', style: brulionTags.comment },
    { name: 'CommentBlock', block: true, style: brulionTags.comment },
    { name: 'CommentMark', style: brulionTags.comment },
  ],
  parseInline: [
    {
      name: 'Comment',
      before: 'Emphasis',
      parse(cx, next, pos) {
        if (next !== CH.percent || cx.char(pos + 1) !== CH.percent) return -1;
        const rel = cx.text.indexOf('%%', pos - cx.offset + 2);
        if (rel < 0) return -1;
        const close = cx.offset + rel;
        return cx.addElement(
          cx.elt('Comment', pos, close + 2, [cx.elt('CommentMark', pos, pos + 2), cx.elt('CommentMark', close, close + 2)]),
        );
      },
    },
  ],
  parseBlock: [
    {
      name: 'CommentBlock',
      before: 'FencedCode',
      parse(cx: BlockContext, line: Line) {
        if (cx.depth !== 1 || line.next !== CH.percent || !commentBlockStart(line.text.slice(line.pos))) return false;
        const from = cx.lineStart + line.pos;
        const marks: Element[] = [cx.elt('CommentMark', from, from + 2)];
        let to = cx.lineStart + line.text.length;
        while (cx.nextLine()) {
          const close = line.text.indexOf('%%', line.pos);
          to = cx.lineStart + line.text.length;
          if (close >= 0) {
            marks.push(cx.elt('CommentMark', cx.lineStart + close, cx.lineStart + close + 2));
            to = cx.lineStart + line.text.length;
            cx.nextLine();
            break;
          }
        }
        cx.addElement(cx.elt('CommentBlock', from, to, marks));
        return true;
      },
    },
  ],
};

// ---------------------------------------------------------------------------------------------
// ^block-id at the end of a line
// ---------------------------------------------------------------------------------------------

const BlockIds: MarkdownConfig = {
  defineNodes: [{ name: 'BlockId', style: brulionTags.blockId }],
  parseInline: [
    {
      name: 'BlockId',
      before: 'Emphasis',
      parse(cx, next, pos) {
        if (next !== CH.caret) return -1;
        if (pos > cx.offset) {
          const prev = cx.char(pos - 1);
          if (!isSpace(prev) && prev !== CH.newline) return -1;
        }
        const lineEnd = lineEndOf(cx, pos);
        const m = /^\^[A-Za-z0-9-]+(?=[ \t]*$)/.exec(cx.slice(pos, lineEnd));
        if (!m) return -1;
        return cx.addElement(cx.elt('BlockId', pos, pos + m[0].length));
      },
    },
  ],
};

// ---------------------------------------------------------------------------------------------
// Task / reminder tokens (SYNTAX §5-6)
// ---------------------------------------------------------------------------------------------

const VS = '\\uFE0F?';
const DATE_TIME = String.raw`\d{4}-\d{2}-\d{2}(?:[ \t]+\d{1,2}:\d{2}(?![\d:]))?`;
const DATE_TOKEN_RE = new RegExp(`^((?:📅|📆|🗓|⏳|⌛|🛫|➕|✅|❌)${VS})([ \\t]*)(${DATE_TIME})`, 'u');
const REMINDER_TOKEN_RE = new RegExp(`^(⏰${VS}!?)([ \\t]*)(${DATE_TIME})`, 'u');
const RECURRENCE_RE = new RegExp(`^(🔁${VS})([ \\t]*)`, 'u');
const PRIORITY_RE = new RegExp(`^(?:🔺|⏫|🔼|🔽|⏬)${VS}`, 'u');
/** Where a recurrence rule ends (next token, tag, block id, comment). */
const RULE_STOP_RE = /📅|📆|🗓|⏳|⌛|🛫|➕|✅|❌|🔁|⏰|🔺|⏫|🔼|🔽|⏬|[ \t]#(?=[\p{L}\p{N}_[])|[ \t]\^[A-Za-z0-9-]+[ \t]*$|%%/u;

/** Lead code units of the token emoji (BMP ones and the surrogate lead of the astral ones). */
const TOKEN_LEADS = new Set(
  ['📅', '📆', '🗓', '⏳', '⌛', '🛫', '➕', '✅', '❌', '🔁', '⏰', '🔺', '⏫', '🔼', '🔽', '⏬'].map((s) => s.charCodeAt(0)),
);

const TaskTokens: MarkdownConfig = {
  defineNodes: [
    { name: 'TaskToken', style: brulionTags.taskToken },
    { name: 'TaskTokenMark', style: brulionTags.taskToken },
    { name: 'TaskTokenValue', style: brulionTags.taskToken },
    { name: 'PriorityToken', style: brulionTags.priority },
  ],
  parseInline: [
    {
      name: 'TaskToken',
      before: 'Emphasis',
      parse(cx, next, pos) {
        if (!TOKEN_LEADS.has(next)) return -1;
        const lineEnd = lineEndOf(cx, pos);
        const s = cx.slice(pos, lineEnd);
        const m = DATE_TOKEN_RE.exec(s) ?? REMINDER_TOKEN_RE.exec(s);
        if (m) {
          const markTo = pos + m[1]!.length;
          const valueFrom = markTo + m[2]!.length;
          const valueTo = valueFrom + m[3]!.length;
          return cx.addElement(
            cx.elt('TaskToken', pos, valueTo, [cx.elt('TaskTokenMark', pos, markTo), cx.elt('TaskTokenValue', valueFrom, valueTo)]),
          );
        }
        const r = RECURRENCE_RE.exec(s);
        if (r) {
          const markTo = pos + r[1]!.length;
          const valueFrom = markTo + r[2]!.length;
          const rest = s.slice(r[0].length);
          const stop = RULE_STOP_RE.exec(rest);
          const rule = (stop ? rest.slice(0, stop.index) : rest).trimEnd();
          const children = [cx.elt('TaskTokenMark', pos, markTo)];
          if (rule) children.push(cx.elt('TaskTokenValue', valueFrom, valueFrom + rule.length));
          return cx.addElement(cx.elt('TaskToken', pos, rule ? valueFrom + rule.length : markTo, children));
        }
        const p = PRIORITY_RE.exec(s);
        if (p) return cx.addElement(cx.elt('PriorityToken', pos, pos + p[0].length));
        return -1;
      },
    },
  ],
};

// ---------------------------------------------------------------------------------------------
// key:: value property lines
// ---------------------------------------------------------------------------------------------

const PROPERTY_RE = /^([A-Za-z0-9_-]+)(::)(?:([ \t]+)(.*))?$/;

const Properties: MarkdownConfig = {
  defineNodes: [
    { name: 'PropertyLine' },
    { name: 'PropertyKey', style: brulionTags.propertyKey },
    { name: 'PropertyMark', style: tags.processingInstruction },
    { name: 'PropertyValue' },
  ],
  parseInline: [
    {
      name: 'PropertyLine',
      before: 'Emphasis',
      parse(cx, next, pos) {
        // Keys start with [A-Za-z0-9_-]; cheap rejection first.
        const ok = (next >= 48 && next <= 57) || (next >= 65 && next <= 90) || (next >= 97 && next <= 122) || next === 95 || next === 45;
        if (!ok || !atLineStart(cx, pos)) return -1;
        const lineEnd = lineEndOf(cx, pos);
        const m = PROPERTY_RE.exec(cx.slice(pos, lineEnd));
        if (!m) return -1;
        const keyTo = pos + m[1]!.length;
        const children: Element[] = [cx.elt('PropertyKey', pos, keyTo), cx.elt('PropertyMark', keyTo, keyTo + 2)];
        const value = (m[4] ?? '').trimEnd();
        if (value) {
          const valueFrom = keyTo + 2 + (m[3]?.length ?? 0);
          children.push(cx.elt('PropertyValue', valueFrom, valueFrom + value.length, cx.parser.parseInline(value, valueFrom)));
        }
        return cx.addElement(cx.elt('PropertyLine', pos, lineEnd, children));
      },
    },
  ],
};

// ---------------------------------------------------------------------------------------------
// YAML frontmatter
// ---------------------------------------------------------------------------------------------

const Frontmatter: MarkdownConfig = {
  defineNodes: [
    { name: 'Frontmatter', block: true, style: brulionTags.frontmatter },
    { name: 'FrontmatterMark', style: tags.processingInstruction },
    { name: 'FrontmatterContent', style: brulionTags.frontmatter },
  ],
  parseBlock: [
    {
      name: 'Frontmatter',
      before: 'HorizontalRule',
      parse(cx, line) {
        if (cx.lineStart !== 0 || cx.depth !== 1 || line.text.trimEnd() !== '---') return false;
        const marks: Element[] = [cx.elt('FrontmatterMark', 0, 3)];
        const contentFrom = line.text.length + 1;
        let end = -1;
        let contentTo = contentFrom;
        while (cx.nextLine()) {
          const t = line.text.trimEnd();
          if (t === '---' || t === '...') {
            contentTo = cx.lineStart - 1;
            if (contentTo > contentFrom) marks.push(cx.elt('FrontmatterContent', contentFrom, contentTo));
            marks.push(cx.elt('FrontmatterMark', cx.lineStart, cx.lineStart + 3));
            end = cx.lineStart + line.text.length;
            cx.nextLine();
            break;
          }
        }
        if (end < 0) {
          // Unclosed: everything up to the end of the document is frontmatter.
          end = cx.lineStart + line.text.length;
          if (end > contentFrom) marks.push(cx.elt('FrontmatterContent', contentFrom, end));
        }
        cx.addElement(cx.elt('Frontmatter', 0, end, marks));
        return true;
      },
    },
  ],
};

// ---------------------------------------------------------------------------------------------
// Task states beyond GFM: [/] doing, [-] cancelled, [>] deferred (SYNTAX §5)
// ---------------------------------------------------------------------------------------------

class TaskStateParser implements LeafBlockParser {
  nextLine(): boolean {
    return false;
  }
  finish(cx: BlockContext, leaf: LeafBlock): boolean {
    cx.addLeafElement(
      leaf,
      cx.elt('Task', leaf.start, leaf.start + leaf.content.length, [
        cx.elt('TaskMarker', leaf.start, leaf.start + 3),
        ...cx.parser.parseInline(leaf.content.slice(3), leaf.start + 3),
      ]),
    );
    return true;
  }
}

const TaskStates: MarkdownConfig = {
  props: [styleTags({ TaskMarker: brulionTags.taskMarker })],
  parseBlock: [
    {
      name: 'BrulionTaskStates',
      before: 'TaskList',
      leaf(cx, leaf) {
        return /^\[[/>-]\][ \t]/.test(leaf.content) && cx.parentType().name === 'ListItem' ? new TaskStateParser() : null;
      },
    },
  ],
};

/** All Brulion extensions (GFM comes with the base `markdownLanguage`). */
export const brulionMarkdown: MarkdownExtension = [
  TaskStates,
  Frontmatter,
  Comments,
  WikiLinks,
  BlockRefs,
  Tags,
  Highlight,
  BlockIds,
  TaskTokens,
  Properties,
  { remove: ['Superscript', 'Subscript', 'Emoji'] },
];

/** Names of the dialect's syntax nodes (see the table above). */
export const BRULION_NODES = [
  'WikiLink',
  'Embed',
  'WikiMark',
  'WikiTarget',
  'WikiAlias',
  'BlockRef',
  'BlockEmbed',
  'BlockRefMark',
  'BlockRefId',
  'Tag',
  'TagMark',
  'TagName',
  'Highlight',
  'HighlightMark',
  'Comment',
  'CommentBlock',
  'CommentMark',
  'BlockId',
  'TaskToken',
  'TaskTokenMark',
  'TaskTokenValue',
  'PriorityToken',
  'PropertyLine',
  'PropertyKey',
  'PropertyMark',
  'PropertyValue',
  'Frontmatter',
  'FrontmatterMark',
  'FrontmatterContent',
] as const;
export type BrulionNodeName = (typeof BRULION_NODES)[number];
