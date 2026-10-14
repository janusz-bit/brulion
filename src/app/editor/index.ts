/**
 * Note editor (CodeMirror 6). Public API for the shell and views; see docs/UI.md.
 */
export { NoteEditor } from './NoteEditor';
export type { NoteEditorProps } from './NoteEditor';
export {
  contextFor,
  DEFAULT_EDITOR_SETTINGS,
  editorContext,
  editorDialogs,
  editorNow,
  editorSettings,
  EditorHostContext,
  EditorHostProvider,
  getEditorContext,
  templatePaths,
  useEditorHost,
} from './context';
export type {
  DialogOpener,
  EditorContext,
  EditorDialogRequest,
  EditorHost,
  EditorIndex,
  EditorRoute,
  EditorSettings,
  EditorWorkspace,
  FileChange,
  PageSuggestion,
} from './context';
export { clearFocusedEditor, getFocusedEditor, setFocusedEditor, subscribeFocusedEditor, useFocusedEditor } from './focus';
export type { FocusedEditor } from './focus';
export { EditorSession, externalChange, lineOfBlockId } from './session';
export type { RevealTarget, SessionCallbacks, SessionStatus } from './session';
export { brulionLanguage, editorExtensions, reconfigureEditor } from './setup';
export type { EditorSetupOptions } from './setup';
export { BRULION_NODES, brulionMarkdown, brulionTags } from './syntax';
export type { BrulionNodeName } from './syntax';
export {
  cycleTask,
  indentItems,
  moveItemDown,
  moveItemUp,
  outdentItems,
  selectBlock,
  toggleFoldAll,
} from './outliner';
export { setZoom, toggleZoom, zoomField, zoomIn, zoomOut } from './zoom';
export { SLASH_COMMANDS, filterSlashCommands } from './slash';
export type { SlashCommand } from './slash';
export { brulionKeymap, toggleInline } from './keymap';
export { MobileToolbar } from './MobileToolbar';
export { FakeWorkspace, TinyIndex } from './fake-workspace';
export { linkAt, linkClicks, openLinkAtCursor, openTarget } from './links';
export type { LinkTarget } from './links';
