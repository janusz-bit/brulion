// @vitest-environment jsdom
import { useRef, useState } from 'react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { Button } from './Button';
import { ConfirmDialog, Modal } from './Modal';
import { Popover } from './Popover';
import { layerCount } from './layers';
import { byText, cleanup, click, key, render } from './render.testutil';

afterEach(cleanup);
// jsdom + React renders are slow on the Raspberry Pi dev machine under load.
vi.setConfig({ testTimeout: 30_000 });

function Harness({ onClose = () => {} }: { onClose?: () => void }) {
  const [open, setOpen] = useState(false);
  return (
    <>
      <button id="trigger" onClick={() => setOpen(true)}>
        open
      </button>
      <Modal
        open={open}
        onClose={() => {
          onClose();
          setOpen(false);
        }}
        title="Zmień nazwę"
        description="Opis okna"
        footer={
          <>
            <Button id="cancel">Anuluj</Button>
            <Button id="ok">OK</Button>
          </>
        }
      >
        <input id="name" defaultValue="Plan" />
      </Modal>
    </>
  );
}

describe('Modal', () => {
  it('renders an accessible modal dialog and focuses the first field', () => {
    const r = render(<Harness />);
    const trigger = r.container.querySelector<HTMLButtonElement>('#trigger')!;
    trigger.focus();
    click(trigger);
    const dialog = document.querySelector('[role="dialog"]')!;
    expect(dialog).toBeTruthy();
    expect(dialog.getAttribute('aria-modal')).toBe('true');
    const title = document.getElementById(dialog.getAttribute('aria-labelledby')!);
    expect(title?.textContent).toBe('Zmień nazwę');
    expect(document.getElementById(dialog.getAttribute('aria-describedby')!)?.textContent).toBe('Opis okna');
    // Initial focus skips the × button.
    expect(document.activeElement?.id).toBe('name');
    expect(document.body.style.overflow).toBe('hidden');
  });

  it('traps Tab and Shift+Tab inside the dialog', () => {
    const r = render(<Harness />);
    click(r.container.querySelector('#trigger'));
    const ok = document.getElementById('ok')!;
    ok.focus();
    key(ok, 'Tab');
    // Wraps to the first focusable element (the × button in the header).
    expect(document.activeElement?.getAttribute('aria-label')).toBe('Zamknij okno');
    key(document.activeElement, 'Tab', { shiftKey: true });
    expect(document.activeElement?.id).toBe('ok');
    // Tab in the middle is left to the browser.
    const name = document.getElementById('name')!;
    name.focus();
    const ev = key(name, 'Tab');
    expect(ev.defaultPrevented).toBe(false);
  });

  it('closes on Escape and gives focus back to the trigger', () => {
    const onClose = vi.fn();
    const r = render(<Harness onClose={onClose} />);
    const trigger = r.container.querySelector<HTMLButtonElement>('#trigger')!;
    trigger.focus();
    click(trigger);
    expect(layerCount()).toBe(1);
    key(document.activeElement, 'Escape');
    expect(onClose).toHaveBeenCalledTimes(1);
    expect(document.querySelector('[role="dialog"]')).toBeNull();
    expect(document.activeElement).toBe(trigger);
    expect(layerCount()).toBe(0);
    expect(document.body.style.overflow).toBe('');
  });

  it('closes on backdrop click and the × button', () => {
    const onClose = vi.fn();
    const r = render(<Harness onClose={onClose} />);
    click(r.container.querySelector('#trigger'));
    click(document.querySelector('.b-modal__backdrop'));
    expect(onClose).toHaveBeenCalledTimes(1);
    click(r.container.querySelector('#trigger'));
    click(document.querySelector('[aria-label="Zamknij okno"]'));
    expect(onClose).toHaveBeenCalledTimes(2);
  });

  it('Escape closes only the topmost layer (popover inside a modal)', () => {
    const modalClose = vi.fn();
    const popClose = vi.fn();
    function Nested() {
      const anchor = useRef<HTMLButtonElement | null>(null);
      const [pop, setPop] = useState(true);
      return (
        <Modal open onClose={modalClose} title="Okno">
          <button ref={anchor}>kotwica</button>
          <Popover
            open={pop}
            anchor={anchor}
            onClose={(reason) => {
              popClose(reason);
              setPop(false);
            }}
            aria-label="Wybór"
          >
            <button id="inside">w środku</button>
          </Popover>
        </Modal>
      );
    }
    render(<Nested />);
    expect(layerCount()).toBe(2);
    key(document.getElementById('inside'), 'Escape');
    expect(popClose).toHaveBeenCalledWith('escape');
    expect(modalClose).not.toHaveBeenCalled();
    key(document.activeElement, 'Escape');
    expect(modalClose).toHaveBeenCalledTimes(1);
  });

  it('a press inside the modal closes a popover but not the modal', () => {
    const modalClose = vi.fn();
    const popClose = vi.fn();
    function Nested() {
      const anchor = useRef<HTMLButtonElement | null>(null);
      return (
        <Modal open onClose={modalClose} title="Okno">
          <button ref={anchor}>kotwica</button>
          <p id="text">tekst</p>
          <Popover open anchor={anchor} onClose={popClose} autoFocus={false}>
            <span>treść</span>
          </Popover>
        </Modal>
      );
    }
    render(<Nested />);
    click(document.getElementById('text'));
    expect(popClose).toHaveBeenCalledWith('outside');
    expect(modalClose).not.toHaveBeenCalled();
  });

  it('renders inline without a portal, focus changes or layers', () => {
    const r = render(
      <Modal open inline onClose={() => {}} title="Podgląd">
        <input id="field" />
      </Modal>,
    );
    expect(r.container.querySelector('.b-modal-root.is-inline [role="dialog"]')).toBeTruthy();
    expect(document.activeElement).toBe(document.body);
    expect(layerCount()).toBe(0);
  });
});

describe('ConfirmDialog', () => {
  it('focuses Cancel for destructive actions and waits for async confirm', async () => {
    let resolve: () => void = () => {};
    const onConfirm = vi.fn(() => new Promise<void>((r) => (resolve = r)));
    const onCancel = vi.fn();
    render(<ConfirmDialog open danger title="Usunąć?" body="Na pewno?" confirmLabel="Usuń" onConfirm={onConfirm} onCancel={onCancel} />);
    const dialog = document.querySelector('[role="alertdialog"]')!;
    expect(dialog).toBeTruthy();
    expect(document.activeElement?.textContent).toBe('Anuluj');
    const del = byText(dialog, 'button', 'Usuń')!;
    click(del);
    expect(onConfirm).toHaveBeenCalledTimes(1);
    expect(del.getAttribute('aria-busy')).toBe('true');
    const { act } = await import('react');
    await act(async () => {
      resolve();
    });
    expect(del.getAttribute('aria-busy')).toBeNull();
    click(byText(dialog, 'button', 'Anuluj'));
    expect(onCancel).toHaveBeenCalledTimes(1);
  });
});
