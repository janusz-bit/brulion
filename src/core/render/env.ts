/**
 * Per-render environment: creation, access from markdown-it rules, safe calls into the host
 * context, and the stack of renders in progress (nested renders happen synchronously when
 * `ctx.embed` renders another note), used for embed depth / cycle detection and to share
 * asset URLs with the outer render's sanitizer.
 */
import type { Env } from 'markdown-it';
import { findFrontmatter, parseFrontmatter } from '@/core/markdown';
import { todayLocal } from '@/core/dates';
import type { VaultPath } from '../types';
import { translate, type RenderMessageKey } from './messages';
import type { FrontmatterInfo, RenderContext, RenderEnv } from './types';

/** Key of the Brulion render environment inside the markdown-it `env` object. */
const ENV_KEY = Symbol.for('brulion.render.env');

/** Maximum nesting depth of note/block embeds (deeper embeds render a placeholder). */
export const MAX_EMBED_DEPTH = 3;

/** Renders in progress, outermost first. */
const renderStack: RenderEnv[] = [];
/** Keys of the notes / sections / blocks being rendered, outermost first (embed cycle guard). */
const embedKeys: string[] = [];

/** The Brulion environment stored in a markdown-it env, if any. */
export function getEnv(env: Env | null | undefined): RenderEnv | undefined {
  return env ? (env[ENV_KEY] as RenderEnv | undefined) : undefined;
}

/** Wrap a render environment into a markdown-it env object. */
export function toMdEnv(env: RenderEnv): Env {
  return { [ENV_KEY]: env };
}

function frontmatterOf(content: string): FrontmatterInfo | null {
  const block = findFrontmatter(content);
  if (!block) return null;
  let data: Record<string, unknown> = {};
  try {
    data = parseFrontmatter(content).data;
  } catch {
    // Malformed YAML: hide the block, show no properties.
  }
  return { endLine: block.endLine, data };
}

function validDate(d: Date | undefined): Date {
  return d instanceof Date && !Number.isNaN(d.getTime()) ? d : new Date();
}

/** Create the environment for rendering `content` (a whole note or an excerpt). */
export function createEnv(content: string, ctx: RenderContext, opts: { frontmatter?: boolean } = {}): RenderEnv {
  const now = validDate(ctx.now);
  const parentDepth = renderStack.length > 0 ? renderStack[renderStack.length - 1]!.depth + 1 : 0;
  const depth = Math.max(Number.isFinite(ctx.depth) ? Math.max(0, Math.floor(ctx.depth!)) : 0, parentDepth);
  return {
    ctx,
    source: content,
    locale: ctx.locale === 'en' ? 'en' : 'pl',
    now,
    today: todayLocal(now),
    depth,
    lineOffset: Number.isFinite(ctx.lineOffset) ? Math.floor(ctx.lineOffset!) : 0,
    idPrefix: typeof ctx.idPrefix === 'string' ? ctx.idPrefix.replace(/[^A-Za-z0-9_-]/g, '') : '',
    frontmatter: opts.frontmatter === false ? null : frontmatterOf(content),
    assetUrls: new Set(),
    headingAnchors: new Map(),
    footnotes: new Map(),
    scopes: new Map(),
    blockRefStack: [],
  };
}

/**
 * Run `fn` with `env` registered as the innermost render in progress. A top-level render also
 * registers its note as the root of the embed chain.
 */
export function withRender<T>(env: RenderEnv, fn: () => T): T {
  const root = renderStack.length === 0;
  if (root) embedKeys.push(noteEmbedKey(env.ctx.path));
  renderStack.push(env);
  try {
    return fn();
  } finally {
    renderStack.pop();
    if (root) embedKeys.pop();
  }
}

/** Embed chain key of a whole note or a section of it. */
export function noteEmbedKey(path: VaultPath, heading?: string): string {
  return heading ? `note:${path}#${heading.normalize('NFC').toLowerCase()}` : `note:${path}`;
}

/** Embed chain key of a block. */
export function blockEmbedKey(id: string): string {
  return `block:${id}`;
}

/** Is `key` being rendered right now (embedding it again would loop)? */
export function isEmbedInProgress(key: string): boolean {
  return embedKeys.includes(key);
}

/** Run `fn` with `key` pushed on the embed chain. */
export function withEmbedKey<T>(key: string, fn: () => T): T {
  embedKeys.push(key);
  try {
    return fn();
  } finally {
    embedKeys.pop();
  }
}

/** Call a host callback; exceptions become `fallback` (a broken host must not break rendering). */
export function safeCall<T>(fn: () => T, fallback: T): T {
  try {
    return fn();
  } catch {
    return fallback;
  }
}

/** `ctx.resolveLink` guarded against exceptions and non-string results. */
export function resolveLink(env: RenderEnv, target: string): VaultPath | null {
  const r = safeCall(() => env.ctx.resolveLink(target, env.ctx.path), null);
  return typeof r === 'string' && r !== '' ? r : null;
}

/** `ctx.href` guarded against exceptions (falls back to `#`). */
export function linkHref(env: RenderEnv, path: VaultPath | null, target: string, heading?: string, blockId?: string): string {
  const r = safeCall(() => env.ctx.href(path, target, heading, blockId), '#');
  return typeof r === 'string' ? r : '#';
}

/** `ctx.assetUrl`, recording the URL as trusted for the sanitizers of all renders in progress. */
export function assetUrl(env: RenderEnv, path: VaultPath): string | null {
  const url = safeCall(() => env.ctx.assetUrl(path), null);
  if (typeof url !== 'string' || url === '') return null;
  env.assetUrls.add(url);
  for (const e of renderStack) e.assetUrls.add(url);
  return url;
}

/** Translate a renderer message for this render. */
export function tr(env: RenderEnv, key: RenderMessageKey, params?: Record<string, string | number>): string {
  return translate(env.locale, key, params, env.ctx.t);
}

/** Source line as written into `data-line` attributes. */
export function dataLine(env: RenderEnv, line: number): number {
  return line + env.lineOffset;
}
