/**
 * `.brulion/config.json` parsing and serialisation. Missing or invalid keys fall back to
 * DEFAULT_VAULT_CONFIG; unknown keys are preserved so that a newer Brulion on another device
 * does not lose its settings when this device saves the file.
 */
import { normalizePath } from '../paths';
import { DEFAULT_VAULT_CONFIG, type VaultConfig } from '../types';

export const CONFIG_PATH = '.brulion/config.json';

export interface ParsedVaultConfig {
  config: VaultConfig;
  /** The raw JSON object (all keys, including unknown ones); `{}` when invalid. */
  raw: Record<string, unknown>;
  /** Human-readable problems (invalid JSON, wrong value types). */
  warnings: string[];
}

const FOLDER_KEYS = ['journalsFolder', 'newPagesFolder', 'attachmentsFolder', 'templatesFolder'] as const;

function isPlainObject(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

/** Validate one value against the type/shape expected for `key`; undefined = invalid. */
function validValue<K extends keyof VaultConfig>(key: K, value: unknown): VaultConfig[K] | undefined {
  switch (key) {
    case 'version':
      return (value === 1 ? 1 : undefined) as VaultConfig[K] | undefined;
    case 'weekStartsOn':
      return (value === 0 || value === 1 ? value : undefined) as VaultConfig[K] | undefined;
    case 'defaultReminderTime': {
      if (typeof value !== 'string') return undefined;
      const m = /^(\d{1,2}):(\d{2})$/.exec(value.trim());
      if (!m || Number(m[1]) > 23 || Number(m[2]) > 59) return undefined;
      return `${m[1]!.padStart(2, '0')}:${m[2]}` as VaultConfig[K];
    }
    case 'journalFileFormat':
      return (typeof value === 'string' && value.trim() !== '' ? value.trim() : undefined) as VaultConfig[K] | undefined;
    case 'journalTemplate':
      return (typeof value === 'string' ? normalizePath(value) : undefined) as VaultConfig[K] | undefined;
    default:
      if ((FOLDER_KEYS as readonly string[]).includes(key)) {
        return (typeof value === 'string' ? normalizePath(value) : undefined) as VaultConfig[K] | undefined;
      }
      return undefined;
  }
}

/** Merge a raw object over DEFAULT_VAULT_CONFIG, validating each known key. */
export function mergeVaultConfig(raw: Record<string, unknown>): { config: VaultConfig; warnings: string[] } {
  const config: VaultConfig = { ...DEFAULT_VAULT_CONFIG };
  const warnings: string[] = [];
  const target = config as unknown as Record<string, unknown>;
  for (const key of Object.keys(DEFAULT_VAULT_CONFIG) as (keyof VaultConfig)[]) {
    if (!Object.hasOwn(raw, key)) continue;
    const v = validValue(key, raw[key]);
    if (v === undefined) warnings.push(`Invalid value for "${key}": ${JSON.stringify(raw[key])}`);
    else target[key] = v;
  }
  return { config, warnings };
}

/** Parse config.json text. Invalid JSON or a non-object yields the defaults plus a warning. */
export function parseVaultConfig(text: string): ParsedVaultConfig {
  let data: unknown;
  try {
    data = JSON.parse(text.replace(/^\uFEFF/, ''));
  } catch (e) {
    return { config: { ...DEFAULT_VAULT_CONFIG }, raw: {}, warnings: [`Invalid JSON in ${CONFIG_PATH}: ${(e as Error).message}`] };
  }
  if (!isPlainObject(data)) {
    return { config: { ...DEFAULT_VAULT_CONFIG }, raw: {}, warnings: [`${CONFIG_PATH} must contain a JSON object`] };
  }
  const { config, warnings } = mergeVaultConfig(data);
  return { config, raw: data, warnings };
}

/** Serialise config (known keys in canonical order, then preserved unknown keys), 2-space JSON. */
export function serializeVaultConfig(config: VaultConfig, raw: Record<string, unknown> = {}): string {
  const { config: valid } = mergeVaultConfig(config as unknown as Record<string, unknown>);
  const out: Record<string, unknown> = { ...valid };
  for (const [k, v] of Object.entries(raw)) {
    if (Object.hasOwn(DEFAULT_VAULT_CONFIG, k)) continue;
    // defineProperty: keys like "__proto__" stay plain data properties (no prototype change).
    Object.defineProperty(out, k, { value: v, enumerable: true, writable: true, configurable: true });
  }
  return `${JSON.stringify(out, null, 2)}\n`;
}
