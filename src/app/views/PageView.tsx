// PLACEHOLDER — replaced by the views agent
/**
 * One page: title (rename), properties, the editor, backlinks below.
 * Contract: named export `PageView` (= file name), props `ViewProps<'page'>` from
 * '@/app/state/store' ({ route: { path, line?, blockId? }, pane }); App.tsx lazy-loads it.
 */
import { NoteEditor } from '@/app/editor';
import type { ViewProps } from '@/app/state/store';
import { pageTitle } from '@/core/paths';

export function PageView({ route, pane }: ViewProps<'page'>) {
  return (
    <div className="b-placeholder-page">
      <h1>{pageTitle(route.path)}</h1>
      <NoteEditor path={route.path} line={route.line} blockId={route.blockId} compact autoFocus={pane !== 'side'} />
    </div>
  );
}
