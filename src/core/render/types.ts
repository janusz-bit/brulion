/**
 * Public types of `@/core/render` (docs/ARCHITECTURE.md) plus the per-render environment that
 * the markdown-it plugins share.
 */
import type { LocalDate, Locale, VaultPath } from '../types';

/** Translator compatible with `createTranslator()` from `@/core/i18n`. */
export type RenderTranslator = (key: string, params?: Record<string, string | number>) => string;

/**
 * Everything the renderer needs from its host (app, exporter, tests). Only the first six
 * members are required (the documented contract); the rest are optional refinements.
 */
export interface RenderContext {
  /** Vault path of the note being rendered (links resolve relative to it). */
  path: VaultPath;
  locale: Locale;
  /** Resolve a link target (SYNTAX §2) to a vault path, or null when it does not exist. */
  resolveLink(target: string, from: VaultPath): VaultPath | null;
  /**
   * URL for a link. `path` is the resolved file (null when unresolved, and for tag links where
   * `target` is `#tag`); `target` is the link target as written.
   */
  href(path: VaultPath | null, target: string, heading?: string, blockId?: string): string;
  /** URL under which an attachment can be loaded (`asset:`, `blob:`, `data:`, http...), or null. */
  assetUrl(path: VaultPath): string | null;
  /**
   * HTML of an embedded note / section / block (usually another `renderMarkdown` call), or null
   * when it does not exist. `target` is the resolved vault path when the link resolves (the
   * current note for same-page embeds); `depth` is the nesting depth of the embed (1 = embedded
   * directly in the top-level note). Called synchronously.
   */
  embed?(target: string, heading: string | undefined, blockId: string | undefined, depth: number): string | null;
  /** Look up a block by id (vault-wide) for `((id))` references. */
  blockRef?(id: string): { path: VaultPath; text: string } | null;
  /** HTML of the results of a ```` ```query ```` block, or null to show a placeholder. */
  query?(src: string): string | null;
  /** Reference time for relative dates and overdue detection (default: now). */
  now?: Date;
  /** Translator for user-visible strings; keys missing there fall back to built-in texts. */
  t?: RenderTranslator;
  /** Render frontmatter as a properties table (default true). */
  showProperties?: boolean;
  /** Task checkboxes are enabled (the host handles clicks via `data-line`); default false. */
  interactive?: boolean;
  /** Embed depth of this render (0 = top-level note); used for the embed depth limit. */
  depth?: number;
  /** Single line breaks become `<br>` (Obsidian default); default true. */
  breaks?: boolean;
  /** Prefix for generated element ids (footnotes), to keep embedded notes unique. */
  idPrefix?: string;
  /** Added to every `data-line` value (when rendering an excerpt of a larger file). */
  lineOffset?: number;
}

/** Frontmatter found at the top of the rendered content. */
export interface FrontmatterInfo {
  /** 0-based line of the closing `---`. */
  endLine: number;
  data: Record<string, unknown>;
}

/** A footnote definition `[^label]: text`. */
export interface FootnoteDef {
  label: string;
  content: string;
  line: number;
}

/** Per-render state shared by all plugins (stored in the markdown-it env). */
export interface RenderEnv {
  ctx: RenderContext;
  /** The full source given to `renderMarkdown` (calc scopes are computed from it). */
  source: string;
  locale: Locale;
  now: Date;
  today: LocalDate;
  /** Embed depth of this render. */
  depth: number;
  lineOffset: number;
  idPrefix: string;
  frontmatter: FrontmatterInfo | null;
  /** URLs returned by `ctx.assetUrl` during this render (always kept by the sanitizer). */
  assetUrls: Set<string>;
  /** Heading anchors already used (for de-duplication). */
  headingAnchors: Map<string, number>;
  footnotes: Map<string, FootnoteDef>;
  /** Calc scopes by line (memoised `pageScope` results). */
  scopes: Map<number, Record<string, unknown>>;
  /** Block references currently being expanded (cycle guard). */
  blockRefStack: string[];
}
