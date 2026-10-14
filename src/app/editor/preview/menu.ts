/**
 * A small anchored menu for widgets (task status, reminder actions). Plain DOM styled with the
 * design system's popover/menu classes (components.css), keyboard accessible (arrows, Home/End,
 * Enter, Escape), closes on outside pointer down, scroll or blur. Browser-only.
 */
import { computePosition } from '@/app/components';
import { h, icon, type PreviewIcon } from './dom';

export interface PreviewMenuItem {
  label: string;
  icon?: PreviewIcon;
  danger?: boolean;
  checked?: boolean;
  run(): void;
}

let open: { close(): void } | null = null;

/** Close the open preview menu (if any). */
export function closePreviewMenu(): void {
  open?.close();
}

/** Open a menu next to `anchor`; `onClose` runs after it closed (restore editor focus there). */
export function openPreviewMenu(anchor: Element, label: string, items: PreviewMenuItem[], onClose?: () => void): void {
  closePreviewMenu();
  const themed = anchor.closest('[data-theme]');
  const menu = h('div', { class: 'b-menu', role: 'menu', 'aria-label': label });
  const pop = h('div', { class: 'b-popover b-lp-menu', 'data-theme': themed?.getAttribute('data-theme') ?? null }, menu);
  const buttons = items.map((item, i) => {
    const b = h(
      'button',
      {
        type: 'button',
        class: `b-menu__item${item.danger ? ' is-danger' : ''}`,
        role: item.checked === undefined ? 'menuitem' : 'menuitemradio',
        'aria-checked': item.checked === undefined ? null : item.checked ? 'true' : 'false',
        tabindex: i === 0 ? '0' : '-1',
      },
      h('span', { class: 'b-menu__icon' }, item.icon ? icon(item.icon, 'b-lp-icon') : null),
      h('span', { class: 'b-menu__text' }, h('span', { class: 'b-menu__item-label' }, item.label)),
      item.checked ? h('span', { class: 'b-lp-menu__check' }, icon('check', 'b-lp-icon')) : null,
    );
    b.addEventListener('click', () => {
      close();
      item.run();
    });
    menu.appendChild(b);
    return b;
  });

  const focusAt = (i: number) => {
    const n = buttons.length;
    const b = buttons[((i % n) + n) % n];
    buttons.forEach((x) => x.setAttribute('tabindex', x === b ? '0' : '-1'));
    b?.focus();
  };
  menu.addEventListener('keydown', (e) => {
    const i = buttons.indexOf(document.activeElement as HTMLButtonElement);
    switch (e.key) {
      case 'ArrowDown':
        focusAt(i + 1);
        break;
      case 'ArrowUp':
        focusAt(i - 1);
        break;
      case 'Home':
        focusAt(0);
        break;
      case 'End':
        focusAt(buttons.length - 1);
        break;
      case 'Escape':
      case 'Tab':
        close();
        break;
      default:
        return;
    }
    e.preventDefault();
    e.stopPropagation();
  });

  const onDown = (e: Event) => {
    if (!pop.contains(e.target as Node)) close();
  };
  const onScroll = (e: Event) => {
    if (!pop.contains(e.target as Node)) close();
  };
  let closed = false;
  function close() {
    if (closed) return;
    closed = true;
    document.removeEventListener('pointerdown', onDown, true);
    window.removeEventListener('scroll', onScroll, true);
    window.removeEventListener('resize', close);
    pop.remove();
    if (open === handle) open = null;
    onClose?.();
  }
  const handle = { close };
  open = handle;

  document.body.appendChild(pop);
  const r = anchor.getBoundingClientRect();
  const size = pop.getBoundingClientRect();
  const pos = computePosition(
    { top: r.top, left: r.left, width: r.width, height: r.height },
    { width: size.width, height: size.height },
    { width: window.innerWidth, height: window.innerHeight },
    'bottom-start',
  );
  pop.style.top = `${pos.top}px`;
  pop.style.left = `${pos.left}px`;
  pop.dataset.placement = pos.placement;
  pop.classList.add('is-ready');
  document.addEventListener('pointerdown', onDown, true);
  window.addEventListener('scroll', onScroll, true);
  window.addEventListener('resize', close);
  focusAt(0);
}
