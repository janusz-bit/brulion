/**
 * Command palette (Ctrl/⌘+K): fuzzy, diacritic-insensitive search over the command registry
 * (services/commands.ts), including commands for the focused editor.
 */
import { useMemo, useState, useSyncExternalStore } from 'react';
import { Kbd } from '@/app/components';
import { useAppStore, useT } from '@/app/state/hooks';
import { availableCommands, commandContext, commandTitle, runCommand, subscribeCommands, type Command, type CommandGroup } from '../services/commands';
import { fuzzyFilter } from './fuzzy';
import { Picker, type PickerItem } from './Picker';

const GROUP_ORDER: CommandGroup[] = ['editor', 'page', 'journal', 'create', 'navigation', 'view', 'sync', 'app'];

let commandsVersion = 0;
const bump = () => {
  commandsVersion++;
};
subscribeCommands(bump);

export function CommandPalette() {
  const open = useAppStore((s) => s.paletteOpen);
  if (!open) return null;
  return <PaletteBody />;
}

function PaletteBody() {
  const t = useT();
  const setOpen = useAppStore((s) => s.setPaletteOpen);
  const version = useSyncExternalStore(subscribeCommands, () => commandsVersion);
  const [query, setQuery] = useState('');
  // Context captured when the palette opens (focused editor, current page).
  const ctx = useMemo(() => commandContext(), []);

  const items = useMemo((): (PickerItem & { cmd: Command })[] => {
    void version;
    const cmds = availableCommands(ctx).filter((c) => c.id !== 'app.palette');
    const labelled = cmds.map((cmd) => ({ cmd, label: commandTitle(cmd, t) }));
    if (!query.trim()) {
      labelled.sort((a, b) => GROUP_ORDER.indexOf(a.cmd.group) - GROUP_ORDER.indexOf(b.cmd.group));
      return labelled.map(({ cmd, label }) => ({
        id: cmd.id,
        cmd,
        label,
        icon: cmd.icon ?? null,
        shortcut: cmd.shortcut,
        group: t(`shell.cmdGroup.${cmd.group}`),
      }));
    }
    return fuzzyFilter(labelled, query, (x) => [x.label, ...(x.cmd.keywords ?? []), x.cmd.id], 60).map(({ item, match }) => ({
      id: item.cmd.id,
      cmd: item.cmd,
      label: item.label,
      indices: match.indices,
      icon: item.cmd.icon ?? null,
      shortcut: item.cmd.shortcut,
      hint: t(`shell.cmdGroup.${item.cmd.group}`),
    }));
  }, [ctx, query, t, version]);

  return (
    <Picker
      open
      onClose={() => setOpen(false)}
      title={t('shell.palette.title')}
      placeholder={t('shell.palette.placeholder')}
      query={query}
      onQueryChange={setQuery}
      items={items}
      className="b-palette"
      onPick={(item) => {
        setOpen(false);
        // Let the dialog close (and focus return) before the command runs.
        requestAnimationFrame(() => void runCommand(item.id, commandContext()));
      }}
      empty={t('shell.palette.empty')}
      footer={
        <>
          <span>
            <Kbd keys="ArrowUp" /> <Kbd keys="ArrowDown" /> {t('shell.picker.move')}
          </span>
          <span>
            <Kbd keys="Enter" /> {t('shell.picker.run')}
          </span>
          <span>
            <Kbd keys="Escape" /> {t('shell.picker.close')}
          </span>
        </>
      }
    />
  );
}
