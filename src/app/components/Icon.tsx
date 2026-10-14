/**
 * Icons: a curated name → lucide component map so call sites stay short (`<Icon name="bell"/>`)
 * and the bundle only contains icons we actually use. Views may also import lucide icons
 * directly and pass the component (`<Icon icon={Sparkles}/>`).
 */
import type { ReactNode } from 'react';
import {
  AlarmClock,
  ArrowLeft,
  ArrowRight,
  Bell,
  BellRing,
  BookOpen,
  Calendar,
  CalendarDays,
  Check,
  ChevronDown,
  ChevronLeft,
  ChevronRight,
  ChevronUp,
  CircleAlert,
  CircleCheck,
  Clock,
  Columns2,
  Copy,
  Ellipsis,
  Eraser,
  ExternalLink,
  FileText,
  Folder,
  GitMerge,
  GripVertical,
  Hash,
  Inbox,
  Info,
  Keyboard,
  Link,
  ListTodo,
  Menu,
  Minus,
  Monitor,
  Moon,
  Pencil,
  Plus,
  Redo2,
  Repeat,
  Rows2,
  Search,
  Settings,
  Sparkles,
  SquarePen,
  Star,
  Sun,
  Tag,
  Trash,
  TriangleAlert,
  Undo2,
  X,
  type LucideIcon,
} from 'lucide-react';

export const ICONS = {
  alarm: AlarmClock,
  'arrow-left': ArrowLeft,
  'arrow-right': ArrowRight,
  bell: Bell,
  'bell-ring': BellRing,
  book: BookOpen,
  calendar: Calendar,
  'calendar-days': CalendarDays,
  check: Check,
  'chevron-down': ChevronDown,
  'chevron-left': ChevronLeft,
  'chevron-right': ChevronRight,
  'chevron-up': ChevronUp,
  alert: CircleAlert,
  'check-circle': CircleCheck,
  clock: Clock,
  columns: Columns2,
  copy: Copy,
  more: Ellipsis,
  eraser: Eraser,
  external: ExternalLink,
  file: FileText,
  folder: Folder,
  merge: GitMerge,
  grip: GripVertical,
  hash: Hash,
  inbox: Inbox,
  info: Info,
  keyboard: Keyboard,
  link: Link,
  tasks: ListTodo,
  menu: Menu,
  minus: Minus,
  monitor: Monitor,
  moon: Moon,
  pencil: Pencil,
  plus: Plus,
  redo: Redo2,
  repeat: Repeat,
  rows: Rows2,
  search: Search,
  settings: Settings,
  sparkles: Sparkles,
  edit: SquarePen,
  star: Star,
  sun: Sun,
  tag: Tag,
  trash: Trash,
  warning: TriangleAlert,
  undo: Undo2,
  x: X,
} satisfies Record<string, LucideIcon>;

export type IconName = keyof typeof ICONS;

export interface IconProps {
  /** A name from {@link ICONS}… */
  name?: IconName;
  /** …or a lucide component. */
  icon?: LucideIcon;
  /** Pixel size (default 16; 1em follows the text). */
  size?: number | string;
  strokeWidth?: number;
  /** Accessible label; without it the icon is decorative (aria-hidden). */
  label?: string;
  className?: string;
}

export function Icon({ name, icon, size = 16, strokeWidth = 1.75, label, className }: IconProps) {
  const Cmp = icon ?? (name ? ICONS[name] : undefined);
  if (!Cmp) return null;
  return (
    <Cmp
      size={size}
      strokeWidth={strokeWidth}
      className={className ? `b-icon ${className}` : 'b-icon'}
      aria-hidden={label ? undefined : true}
      aria-label={label}
      role={label ? 'img' : undefined}
      focusable="false"
    />
  );
}

/** Icon slot used by components: an icon name, a lucide component, or any node. */
export type IconSlot = IconName | LucideIcon | ReactNode;

export function isIconName(v: unknown): v is IconName {
  return typeof v === 'string' && Object.prototype.hasOwnProperty.call(ICONS, v);
}

/** Render an {@link IconSlot}. */
export function renderIcon(slot: IconSlot, size: number | string = 16): ReactNode {
  if (slot === null || slot === undefined || slot === false) return null;
  if (isIconName(slot)) return <Icon name={slot} size={size} />;
  if (isLucideComponent(slot)) return <Icon icon={slot} size={size} />;
  return slot as ReactNode;
}

function isLucideComponent(v: unknown): v is LucideIcon {
  // lucide icons are forwardRef objects ({ $$typeof, render }) or functions.
  return (
    typeof v === 'function' ||
    (typeof v === 'object' && v !== null && 'render' in v && typeof (v as { render: unknown }).render === 'function')
  );
}
