/** Small React hooks shared by the components. */
import { useCallback, useEffect, useLayoutEffect, useRef, useState, useSyncExternalStore } from 'react';

/** useLayoutEffect that does not warn when rendered outside a browser. */
export const useIsoLayoutEffect = typeof document !== 'undefined' ? useLayoutEffect : useEffect;

/** Keep a ref pointing at the latest value (for stable callbacks). */
export function useLatest<T>(value: T): { readonly current: T } {
  const ref = useRef(value);
  useIsoLayoutEffect(() => {
    ref.current = value;
  });
  return ref;
}

/**
 * Controlled / uncontrolled state: when `value` is provided it wins and `onChange` is
 * expected to update it; otherwise the component keeps its own state.
 */
export function useControllable<T>(
  value: T | undefined,
  defaultValue: T,
  onChange?: (v: T) => void,
): [T, (v: T) => void] {
  const [inner, setInner] = useState(defaultValue);
  const controlled = value !== undefined;
  const current = controlled ? value : inner;
  const onChangeRef = useLatest(onChange);
  const set = useCallback(
    (v: T) => {
      if (!controlled) setInner(v);
      onChangeRef.current?.(v);
    },
    [controlled, onChangeRef],
  );
  return [current, set];
}

/**
 * The current time, refreshed every `everyMs` while mounted, or `fixed` when given (tests,
 * gallery). For UI that shows relative times ("in 10 min", "tonight") and may stay open.
 */
export function useNow(fixed?: Date, everyMs = 30_000): Date {
  const [now, setNow] = useState(() => fixed ?? new Date());
  useEffect(() => {
    if (fixed) return;
    const id = setInterval(() => setNow(new Date()), everyMs);
    return () => clearInterval(id);
  }, [fixed, everyMs]);
  return fixed ?? now;
}

/** Subscribe to a CSS media query (false outside the browser). */
export function useMediaQuery(query: string): boolean {
  const subscribe = useCallback(
    (cb: () => void) => {
      if (typeof window === 'undefined' || typeof window.matchMedia !== 'function') return () => {};
      const mql = window.matchMedia(query);
      mql.addEventListener('change', cb);
      return () => mql.removeEventListener('change', cb);
    },
    [query],
  );
  const get = () =>
    typeof window !== 'undefined' && typeof window.matchMedia === 'function' ? window.matchMedia(query).matches : false;
  return useSyncExternalStore(subscribe, get, () => false);
}

/** Phone-sized layout (dialogs become bottom sheets). Matches the CSS breakpoint. */
export const NARROW_QUERY = '(max-width: 639.98px)';

let scrollLocks = 0;
let savedOverflow = '';

/** Prevent the page behind an overlay from scrolling while `active`. Ref-counted. */
export function useScrollLock(active: boolean): void {
  useEffect(() => {
    if (!active || typeof document === 'undefined') return;
    if (scrollLocks === 0) {
      savedOverflow = document.body.style.overflow;
      document.body.style.overflow = 'hidden';
    }
    scrollLocks++;
    return () => {
      scrollLocks--;
      if (scrollLocks === 0) document.body.style.overflow = savedOverflow;
    };
  }, [active]);
}

/** Read/write a JSON value in localStorage (errors ignored: private mode, quota, ...). */
export function readStored<T>(key: string, fallback: T): T {
  try {
    const raw = globalThis.localStorage?.getItem(key);
    return raw == null ? fallback : (JSON.parse(raw) as T);
  } catch {
    return fallback;
  }
}

export function writeStored(key: string, value: unknown): void {
  try {
    globalThis.localStorage?.setItem(key, JSON.stringify(value));
  } catch {
    // ignore
  }
}

/** Join class names, skipping falsy values. */
export function cx(...parts: (string | false | null | undefined)[]): string {
  return parts.filter(Boolean).join(' ');
}
