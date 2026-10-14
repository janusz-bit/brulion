/**
 * Shared UI components (named exports only). Styles: src/app/styles/components.css (+ tokens
 * in theme.css). Strings: src/core/i18n/messages/components.ts. Visual QA: src/app/dev/Gallery.tsx.
 */
export { LocaleContext, translatorFor, useLocale, useT } from './i18n';
export { Icon, ICONS, isIconName, renderIcon } from './Icon';
export type { IconName, IconProps, IconSlot } from './Icon';
export { cx, NARROW_QUERY, useControllable, useMediaQuery, useNow, useScrollLock } from './hooks';

export { Button, IconButton } from './Button';
export type { ButtonProps, ButtonSize, ButtonVariant, IconButtonProps } from './Button';
export { ProgressBar, Spinner } from './Spinner';
export type { ProgressBarProps, SpinnerProps } from './Spinner';
export { Checkbox, Field, SearchInput, Select, Switch, TextArea, TextInput, Toggle } from './forms';
export type {
  CheckboxProps,
  ControlSize,
  FieldProps,
  SearchInputProps,
  SelectOption,
  SelectProps,
  SwitchProps,
  TextAreaProps,
  TextInputProps,
  ToggleProps,
} from './forms';
export { formatShortcut, isApplePlatform, Kbd, splitShortcut } from './Kbd';
export type { KbdProps } from './Kbd';
export { Badge, Chip } from './Badge';
export type { BadgeProps, BadgeVariant, ChipProps } from './Badge';
export { rovingIndex, SegmentedControl, TabPanel, tabPanelId, Tabs, tabId } from './Tabs';
export type { SegmentedControlProps, SegmentOption, TabItem, TabPanelProps, TabsProps } from './Tabs';
export { EmptyState } from './EmptyState';
export type { EmptyStateProps } from './EmptyState';
export { Tooltip } from './Tooltip';
export type { TooltipProps } from './Tooltip';
export { ToastStack, ToastView } from './ToastView';
export type { ToastData, ToastKind, ToastViewProps } from './ToastView';

export { computePosition } from './position';
export type { Placement, PositionResult } from './position';
export { Popover, resolveAnchor } from './Popover';
export type { AnchorLike, AnchorProp, PopoverCloseReason, PopoverProps, VirtualAnchor } from './Popover';
export { Menu, MenuButton, menuNavIndex, typeaheadIndex } from './Menu';
export type { MenuAction, MenuButtonProps, MenuItem, MenuLabel, MenuProps, MenuSeparator } from './Menu';
export { ConfirmDialog, Modal } from './Modal';
export type { ConfirmDialogProps, ModalProps, ModalSize } from './Modal';
export { getFocusable, trapTabKey } from './focus';
export { layerCount, pushLayer } from './layers';
export type { Layer } from './layers';

export { DatePicker } from './DatePicker';
export type { DatePickerProps } from './DatePicker';
export {
  dayLabel,
  formatPreview,
  monthGrid,
  monthTitle,
  moveFocusDate,
  naturalPreview,
  parseTimeInput,
  weekdayHeaders,
} from './datepicker';
export type { DayMark, DayMarks, MonthView, NaturalPreview } from './datepicker';
export { ReminderDialog, ReminderFields, reminderResult, useReminderDraft } from './ReminderDialog';
export type { ReminderDialogProps, ReminderDraftState, ReminderFieldsProps } from './ReminderDialog';
export { DEFAULT_REPEAT, describeDraft, draftToRule, quickPicks, REPEAT_KINDS, ruleToDraft, toggleWeekday } from './reminder';
export type { QuickPick, QuickPickId, ReminderValue, RepeatDraft, RepeatKind, RepeatUnit } from './reminder';

export { DiffView } from './DiffView';
export type { DiffMode, DiffViewProps } from './DiffView';
export { collapseUnchanged, diffLines, diffWords, splitLines, toSplitRows } from './diff';
export type { DiffKind, DiffLine, DiffResult, Segment, SplitRow } from './diff';
export { MergeView } from './MergeView';
export type { MergeViewProps } from './MergeView';
export { buildMerged, conflictIndexes, hunksFromDiff3, resolvedLines, unresolvedCount } from './merge';
export type { Diff3Region, MergeHunk, MergePick, Resolution, Resolutions } from './merge';

export { clampSize, keyResize, Splitter } from './Splitter';
export type { SplitterProps } from './Splitter';
export { VirtualList } from './VirtualList';
export type { VirtualListHandle, VirtualListProps } from './VirtualList';
export { fixedRange, OffsetIndex, scrollTargetFor, visibleRange } from './virtual';
export type { Range, ScrollAlign } from './virtual';
