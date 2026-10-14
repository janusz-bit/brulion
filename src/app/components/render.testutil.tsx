/** Minimal React test helpers (no testing-library): render into jsdom with act(). */
import { act, type ReactNode } from 'react';
import { createRoot, type Root } from 'react-dom/client';

(globalThis as { IS_REACT_ACT_ENVIRONMENT?: boolean }).IS_REACT_ACT_ENVIRONMENT = true;

export interface Rendered {
  container: HTMLElement;
  root: Root;
  rerender(node: ReactNode): void;
  unmount(): void;
}

const mounted = new Set<Rendered>();

export function render(node: ReactNode): Rendered {
  const container = document.createElement('div');
  document.body.appendChild(container);
  const root = createRoot(container);
  act(() => root.render(node));
  const r: Rendered = {
    container,
    root,
    rerender(n) {
      act(() => root.render(n));
    },
    unmount() {
      act(() => root.unmount());
      container.remove();
      mounted.delete(r);
    },
  };
  mounted.add(r);
  return r;
}

/** Unmount everything rendered by `render` (call in afterEach). */
export function cleanup(): void {
  // Deleting the current entry while iterating a Set is safe.
  for (const r of mounted) r.unmount();
  document.body.innerHTML = '';
}

export function click(el: Element | null | undefined): void {
  if (!el) throw new Error('click: element not found');
  act(() => {
    el.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, cancelable: true }));
    (el as HTMLElement).click();
  });
}

export function key(el: Element | null | undefined, k: string, opts: KeyboardEventInit = {}): KeyboardEvent {
  const target = el ?? document.activeElement ?? document.body;
  const ev = new KeyboardEvent('keydown', { key: k, bubbles: true, cancelable: true, ...opts });
  act(() => {
    target.dispatchEvent(ev);
  });
  return ev;
}

/** Set an input's value the way React notices (native setter + input event). */
export function type(el: Element | null | undefined, value: string): void {
  if (!(el instanceof HTMLInputElement || el instanceof HTMLTextAreaElement)) throw new Error('type: not an input');
  const proto = el instanceof HTMLInputElement ? HTMLInputElement.prototype : HTMLTextAreaElement.prototype;
  const setter = Object.getOwnPropertyDescriptor(proto, 'value')?.set;
  act(() => {
    setter?.call(el, value);
    el.dispatchEvent(new Event('input', { bubbles: true }));
  });
}

export function byText(root: ParentNode, selector: string, text: string | RegExp): HTMLElement | undefined {
  return [...root.querySelectorAll<HTMLElement>(selector)].find((el) =>
    typeof text === 'string' ? el.textContent?.trim() === text : text.test(el.textContent ?? ''),
  );
}
