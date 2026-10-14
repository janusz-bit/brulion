/**
 * Syntax highlighting: maps highlight tags to `b-md-*` / `b-tok-*` classes styled with theme
 * variables in src/app/styles/editor.css (so light/dark follow the theme without JS).
 */
import { HighlightStyle, syntaxHighlighting } from '@codemirror/language';
import type { Extension } from '@codemirror/state';
import { tags as t } from '@lezer/highlight';
import { brulionTags as b } from './syntax';

export const brulionHighlightStyle = HighlightStyle.define([
  // Markdown structure
  { tag: t.heading1, class: 'b-md-h b-md-h1' },
  { tag: t.heading2, class: 'b-md-h b-md-h2' },
  { tag: t.heading3, class: 'b-md-h b-md-h3' },
  { tag: [t.heading4, t.heading5, t.heading6], class: 'b-md-h b-md-h4' },
  { tag: t.strong, class: 'b-md-strong' },
  { tag: t.emphasis, class: 'b-md-em' },
  { tag: t.strikethrough, class: 'b-md-strike' },
  { tag: t.monospace, class: 'b-md-code' },
  { tag: t.quote, class: 'b-md-quote' },
  { tag: t.contentSeparator, class: 'b-md-hr' },
  { tag: [t.processingInstruction, t.meta], class: 'b-md-mark' },
  { tag: [t.link, t.url], class: 'b-md-link' },
  // Brulion dialect
  { tag: [b.wikiLink, b.wikiTarget, b.embed], class: 'b-md-wikilink' },
  { tag: b.blockRef, class: 'b-md-blockref' },
  { tag: b.tag, class: 'b-md-tag' },
  { tag: b.highlight, class: 'b-md-highlight' },
  { tag: b.comment, class: 'b-md-comment' },
  { tag: b.blockId, class: 'b-md-blockid' },
  { tag: [b.taskToken, b.reminder], class: 'b-md-token' },
  { tag: b.priority, class: 'b-md-priority' },
  { tag: b.taskMarker, class: 'b-md-taskmarker' },
  { tag: b.propertyKey, class: 'b-md-propkey' },
  { tag: b.frontmatter, class: 'b-md-frontmatter' },
  // Code in fenced blocks
  { tag: [t.keyword, t.controlKeyword, t.operatorKeyword, t.modifier], class: 'b-tok-keyword' },
  { tag: [t.string, t.special(t.string), t.regexp], class: 'b-tok-string' },
  { tag: [t.number, t.bool, t.null, t.atom], class: 'b-tok-number' },
  { tag: [t.comment, t.lineComment, t.blockComment, t.docComment], class: 'b-tok-comment' },
  { tag: [t.function(t.variableName), t.function(t.propertyName)], class: 'b-tok-function' },
  { tag: [t.typeName, t.className, t.namespace], class: 'b-tok-type' },
  { tag: [t.propertyName, t.attributeName], class: 'b-tok-property' },
  { tag: [t.tagName, t.angleBracket], class: 'b-tok-tag' },
  { tag: t.invalid, class: 'b-tok-invalid' },
]);

export function brulionHighlighting(): Extension {
  return syntaxHighlighting(brulionHighlightStyle);
}
