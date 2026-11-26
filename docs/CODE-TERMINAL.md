# The Code mode's terminal

The terminal panel of the Code mode (the coding-agent workspace: projects, agent threads, a git
changes panel and this terminal) is a real terminal emulator: an xterm-compatible parser and screen
model in core, a pseudo-terminal per platform, and a Qt Quick view that paints only what changed.

```
src/core/term/        brulion::term — GUI-free (QtCore): Parser, Screen, Terminal, keys, widths,
                      the text as lines and its search (screentext.h), links without OSC 8 (links.h)
src/platform/pty.h    brulion::platform::Pty — pty_posix.cpp (Linux, macOS, BSDs) / pty_none.cpp
src/app/terminal/     brulion::terminal — TerminalSession, TerminalGroup (a tab: splits),
                      TerminalThread (a thread's tabs), TerminalRegistry (every thread's), TerminalView;
                      terminalwiring.cpp (in the app: the registry follows the notebook)
src/app/qml/terminal/ TerminalPanel.qml (a thread's tabs, splits, find, the menu), TerminalTab.qml,
                      TerminalPane.qml (one terminal), TerminalFindBar.qml
```

Data flow: the program writes to the pty → `Pty::dataReceived` → `TerminalSession` feeds
`term::Terminal` (parser → screen; replies to queries go back through `Terminal::reply` →
`Pty::write`) → `TerminalSession::contentChanged` → `TerminalView` asks for the rectangles of the
dirty rows and paints them on the next frame. Keys go the other way: `TerminalView` →
`term::encodeKey()` → `TerminalSession::writeBytes()` → `Pty::write()`.

## The emulator (`src/core/term`)

### Parser (`parser.h`)

Paul Williams' DEC-compatible state machine (vt100.net/emu/dec_ansi_parser): ground, escape,
escape intermediate, CSI entry / param / intermediate / ignore, DCS entry / param / intermediate /
passthrough / ignore, OSC string, SOS/PM/APC string. It feeds a `ParserHandler` with print,
execute, ESC, CSI and OSC actions.

* UTF-8 is decoded in the ground state only, and survives sequences split across reads. Invalid
  bytes, overlong forms, surrogates, code points beyond U+10FFFF and sequences cut short by another
  byte become U+FFFD, one per maximal invalid subpart (as the Unicode Standard recommends). 8-bit
  C1 controls are not recognised (in UTF-8 those bytes are parts of characters); C1 code points that
  arrive as UTF-8 characters are dropped.
* CSI parameters: at most 32 (more are dropped, the sequence is still dispatched), each saturating
  at 65535; `:` separates sub-parameters (`38:2::r:g:b`). More than two intermediates: the sequence
  is dropped. C0 controls inside a CSI are executed as they come; CAN and SUB cancel a sequence;
  ESC starts a new one anywhere.
* OSC strings end with BEL or ST and are cut at 4096 bytes (the rest is consumed). DCS and
  SOS/PM/APC strings are consumed and ignored — DECRQSS, XTGETTCAP, sixel and kitty graphics get
  no answer.

### Screen (`screen.h`, `cell.h`)

* A grid of `Cell`s (16 bytes): a code point (0 for the right half of a wide character), foreground
  and background (`Color`: default, 256 indexed — 0-15 the view's ANSI palette —, 24-bit RGB),
  SGR attributes (bold, dim, italic, underline, double underline, blink, inverse, invisible,
  strikethrough, overline) and an OSC 8 hyperlink id. A code point with combining marks becomes a
  cluster kept by its `Line` (at most 32 UTF-16 units: a flood of marks stays bounded).
* Character widths (`charwidth.h`) are wcwidth()'s, so the cursor stays where shells and editors
  expect it: 2 for East Asian Wide and Fullwidth (CJK, Hangul syllables, emoji with an emoji
  presentation) and the CJK planes 2 and 3, 0 for Mn, Me, Cf (ZWJ; the variation selectors are Mn)
  and Hangul U+1160-U+11FF, 1 otherwise. Tables from Unicode 16 (`scripts/term-width-table.py`
  regenerates `charwidth_table_p.h` from Python's unicodedata). No grapheme clustering of emoji
  sequences: a ZWJ sequence takes the columns of its parts, a flag two cells of width 1.
* A wide character that does not fit at the right margin wraps first, leaving the last cell blank;
  writing over either half of a wide character blanks the other half.
* Main and alternate screen; the main screen has a scrollback of N lines (default 5000, a ring;
  trailing blank cells of its lines are dropped). Lines go into it when they leave the top of a
  scroll region that starts at the top of the screen (LF, IND, SU, wrapping) — a status line under
  the region stays. DL never adds to it. ED 3 clears it.
* Auto-wrap (DECAWM) with xterm's pending-wrap state; a line continued by wrapping is marked
  `wrapped` (selection and copying join it). Origin mode, insert mode, LNM, reverse video, tab stops
  (every 8 columns by default), G0/G1 with DEC Special Graphics and UK, DECSC/DECRC per screen.
* Erasing and scrolled-in lines use the current background (xterm's bce, which xterm-256color
  promises).
* Resize keeps the content without reflow: lines are cut or padded (a wide character cut at the new
  edge goes), the cursor clamped. A shorter main screen pushes its top rows into the scrollback when
  the cursor would fall off (else drops rows below the cursor); a taller one takes lines back from
  the scrollback. The scroll region resets. Sizes are clamped to 1-1000 columns, 1-500 rows.
* Dirty rows: `isRowDirty()` / `clearDirty()`, and `scrolledIntoHistory()` — how many lines went
  into the scrollback since, so a view scrolled back can keep showing the same lines.
* `clearKeepingCursorLine()` — the panel's "Clear": the scrollback goes and, on the main screen, the
  rows above the cursor's; the cursor's row (the prompt being typed) becomes the first, the rows
  under it are blanked. The shell is not told: its line editor works relative to the cursor. The
  alternate screen (a full-screen program draws it) stays.

### The text as the user reads it (`screentext.h`, `links.h`)

* `logicalLine(screen, line, withCells)`: the rows of one line of text — the row and those it
  wrapped into (`Line::wrapped`) — as one string, and for each UTF-16 unit its cell and width (a
  wide character's right half adds nothing, a cluster all its units to one cell). `indexAt(cell)`
  goes back (the right half of a wide character is the character).
* `findText(screen, needle, case, limit)`: every match in the scrollback and the screen, oldest
  first, apart, at most `limit`; one may span wrapped rows, never two lines. The text of a line is
  built first, its cell map only for lines that hold a match.
* `findLinks(text)`: links of one line that no OSC 8 marked — `http://`, `https://`, `file://` URLs
  (up to a space, a quote or an angle bracket; trailing `.,;:!?` and closing brackets they do not
  open are not theirs) and file path candidates: words of letters (any script), digits and `_ - . /
  ~ + @` with a `/` or a short extension (`name.ext`, no number like `3.14.2`), followed by
  `:line`, `:line:column`, `(line)`, `(line,column)` or Python's `", line N"`; a sentence's full
  stop after it is not part of it. `resolvePath(path, workingDirectory, home)` says which candidate
  is a file: `~/…` in the home folder, an absolute path as it is, anything else relative to the
  directory — and without a diff's `a/` / `b/` when nothing is there; folders and URLs are no files.

### Supported sequences (`terminal.h`)

| Kind | Sequences |
|---|---|
| C0 | BEL (`bell()`), BS, HT, LF / VT / FF, CR, SO, SI |
| ESC | DECSC `7`, DECRC `8`, IND `D`, NEL `E`, HTS `H`, RI `M`, RIS `c` (scrollback and title stay), DECKPAM `=`, DECKPNM `>`, DECID `Z`, DECALN `#8`, charsets `( ) 0 A B`, ST (ignored) |
| Cursor | CUU `A`, CUD `B`, CUF `C`, CUB `D`, CNL `E`, CPL `F`, CHA `G`, CUP `H`, HVP `f`, VPA `d`, HPA `` ` ``, HPR `a`, VPR `e`, CHT `I`, CBT `Z`, SCOSC `s`, SCORC `u` |
| Editing | ED `J` (0-3), EL `K` (0-2), DECSED / DECSEL (`?J`, `?K`, as ED / EL), ICH `@`, DCH `P`, ECH `X`, IL `L`, DL `M`, SU `S`, SD `T`, REP `b`, TBC `g` (0, 3) |
| Modes | SM / RM `h` / `l`: 4 IRM, 20 LNM. DECSET / DECRST `?h` / `?l`: 1 DECCKM, 5 DECSCNM, 6 DECOM, 7 DECAWM, 9 X10 mouse, 12 cursor blink, 25 DECTCEM, 47 / 1047 / 1049 alternate screen, 1048 save cursor, 1000 / 1002 / 1003 mouse, 1004 focus events, 1006 SGR mouse, 2004 bracketed paste, 2026 synchronized output |
| Style | SGR `m`: 0-9, 21-29, 30-39, 40-49, 53, 55, 90-97, 100-107; 38 / 48 with `;5;n`, `;2;r;g;b`, `:5:n`, `:2::r:g:b`, `:2:r:g:b`; 58 (underline colour) parsed and not drawn; `4:0`-`4:5` underline styles (double drawn double, the rest single). DECSCUSR `SP q` (0-6), DECSTR `!p` |
| Replies | DSR `5n` → `CSI 0 n`; DSR `6n` → CPR (origin-relative); `?6n` → DECXCPR; DA `c` → `CSI ? 62 ; 22 c` (VT220 with ANSI colour); DA2 `>c` → `CSI > 1 ; 10 ; 0 c`; DECRQM `$p` / `?$p` for the modes above; XTWINOPS `18t` → `CSI 8 ; rows ; cols t`; OSC 10 / 11 / 12 `?` → the view's colours |
| OSC | 0 / 2 title (control characters dropped, cut at 256), 1 ignored, 7 working directory (`file://host/path`, percent-decoded; other schemes ignored), 8 hyperlinks (stored per cell — `Screen::hyperlink(id)`, at most 4096 distinct URIs of up to 2048 characters), 10 / 11 / 12 queries answered (setting colours ignored), 52 ignored, anything else ignored |

Not supported: DECSLRM / left and right margins, DECCOLM (132 columns), double-width and
double-height lines, sixel and other graphics, DECRQSS, XTGETTCAP, the kitty keyboard protocol,
modifyOtherKeys, title stacks (`22t` / `23t`), palette changes (OSC 4 / 104), the UTF-8 (1005) and
urxvt (1015) mouse encodings, blinking text (drawn steady), reverse wrap.

### Input (`keys.h`)

Pure functions of the event and the program's `InputModes`; Qt's key codes and modifiers are
QtCore types, so this stays GUI-free.

* Arrows, Home, End: `CSI A`… (`SS3 A`… with DECCKM), `CSI 1 ; m A` with modifiers (m = 1 + Shift 1
  + Alt 2 + Ctrl 4 + Meta 8). Insert, Delete, Page Up / Down, F5-F12: `CSI n ~` (`CSI n ; m ~`);
  F1-F4: `SS3 P`… (`CSI 1 ; m P`).
* Enter: CR; Backspace: DEL (Ctrl: BS); Tab: HT; Shift+Tab: `CSI Z`; Escape: ESC; Ctrl+Tab sends
  nothing (focus moves on).
* Ctrl with a letter, `@ [ \ ] ^ _ ?`, Space and xterm's digits 2-8: the C0 code. Alt: ESC before
  what the key sends. Anything else: the UTF-8 of the key's text.
* macOS: Qt's ControlModifier is Command, MetaModifier is Control; the view swaps them, so
  Control+C is ^C there too, and Option with a character is no Alt (it composes ą, ż).
* Pastes (`encodePaste`): CR LF and LF become CR; control characters other than TAB and line breaks
  (with ESC, DEL, C1) are dropped; with bracketed paste the text goes between `CSI 200 ~` and
  `CSI 201 ~` — it cannot end the bracket itself.
* Input method text (`encodeText`): its UTF-8, the same characters dropped.
* Mouse (`encodeMouse`): X10 (presses), 1000 (presses, releases, wheel), 1002 (+ motion with a
  button), 1003 (+ all motion); SGR encoding with ?1006, else xterm's `CSI M b x y` up to column
  and row 223 (beyond: nothing). Focus (`encodeFocus`): `CSI I` / `CSI O` with ?1004.

## The pseudo-terminal (`src/platform/pty.h`)

`platform::Pty` (QObject): `available()`, `defaultEnvironment()`, `userShell()`,
`start(program, arguments, workingDirectory, environment, columns, rows, &error)`, `write()`,
`resize()` (TIOCSWINSZ → SIGWINCH), `terminate(graceMs)`, `isRunning()`, `processId()`; signals
`dataReceived(QByteArray)`, `finished(exitCode, crashed)`.

* `pty_posix.cpp` (Linux, macOS, the BSDs): `posix_openpt()` / `grantpt()` / `unlockpt()` /
  `ptsname_r()` (no libutil, nothing to link). The termios gets IUTF8 (the line editor erases whole
  UTF-8 characters) and VERASE = DEL.
* `fork()` then, in the child, only async-signal-safe calls: `setsid()` (its own session and
  process group), `TIOCSCTTY` (the pty becomes its controlling terminal), the pty on 0, 1, 2, every
  other descriptor closed (`close_range()` on Linux, a loop elsewhere), all signal dispositions
  back to default, an empty signal mask, `chdir()`, `execve()`. Everything the child needs (argv,
  envp, the program's path from the environment's PATH) is prepared before the fork. An exec
  failure comes back through a close-on-exec pipe, so `start()` reports it ("Exec format error")
  instead of a program that dies at once.
* Output: a `QSocketNotifier` on the non-blocking master, at most 4 × 64 KB a turn so a flood
  never starves the event loop. Input: written at once, queued while the pty is full (a write
  notifier flushes it), at most 16 MB queued.
* The child's end: a pidfd (Linux 5.3+) in a `QSocketNotifier`, else `waitpid(WNOHANG)` every
  250 ms (and at once on EOF). There is no SIGCHLD handler — that belongs to QProcess. The output
  the child wrote before it ended is delivered before `finished()`; the child is always reaped (no
  zombies — tested).
* `terminate()`: SIGHUP and SIGCONT to the child's process group and to the terminal's foreground
  process group (a job the shell runs), SIGKILL after the grace period (3 s) if it is still there.
  The destructor does the same with a 200 ms grace and waits for the child.
* `hasForegroundJob()`: the terminal's foreground process group (`tcgetpgrp()` on the master) is not
  the child's own — a job of the shell's runs (`npm run dev`, `vim`; the shell gives the terminal to
  a job's group while it runs and takes it back for its prompt).
* `currentDirectory()`: the foreground program's directory (the job's group leader, else the child)
  where the system tells it — Linux: `/proc/<pid>/cwd`; "" elsewhere and when nothing runs. Relative
  paths in the output resolve against it.
* `pty_none.cpp` (Windows, Android, the browser): `available()` is false, `start()` fails with
  "There is no terminal on this device". A Windows backend would replace it with ConPTY
  (`CreatePseudoConsole`, overlapped pipes, `ResizePseudoConsole`, a job object for terminate)
  behind the same class; `src/platform/CMakeLists.txt` chooses the file.

## The session, the view, the panel (`src/app/terminal`, `qml/terminal`)

### TerminalSession

QML type `TerminalSession`. Properties: `folder` (rw), `command` (rw; "" = the user's shell),
`label` (rw), `title` (ro: the program's OSC title, else `label`, else the command, else the
shell's name), `workingDirectory` (ro, OSC 7), `currentDirectory` (ro: `Pty::currentDirectory()`,
else OSC 7's, else `folder`, else the home folder), `running`, `hasExited`, `exitCode` (128 + signal
when a signal ended it; -1 before), `errorString`, `available` (constant), `columns`, `rows`,
`busy`, `activity`, `bellRung`. Methods: `start()` (again after the end: on a clean screen),
`stop()`, `write(text)` (as is), `paste(text)` (encodePaste, bracketed when asked),
`resize(columns, rows)`, `clear()` (`Screen::clearKeepingCursorLine()`), `checkBusy()`. Signals:
`bell()`, `started()`, `exited(code)`, `contentChanged()`, `modesChanged()`, `cleared()` and the
properties' changes. C++: `terminal()`, `writeBytes()`, `commandLine()`, `setShown()`.

* `busy`: a program runs in it — `Pty::hasForegroundJob()`, or the session's own command (`$SHELL
  -lc` may exec it: no job of its own). Looked at 120 ms after output or input (one timer, not per
  read) and every 2 s while busy (a job that prints nothing ends unseen otherwise); idle, a session
  becomes busy only after input or with output, so nothing polls then.
* `activity` / `bellRung`: output came, or the program rang the bell, while no view showed the
  session (`setShown()`: TerminalView counts itself while it is visible in a window; two views
  may show one for a moment). Both go when a view shows it again; output while shown sets none.

The program: `command` empty → the user's shell (`$SHELL` when it is an executable file, else the
account's login shell, else `/bin/sh`) as an interactive, non-login shell with no arguments;
otherwise that shell runs it as `$SHELL -lc command` (the shell is the program — Brulion never
builds a shell string of its own). The child gets the app's environment with `TERM=xterm-256color`,
`COLORTERM=truecolor`, `PWD` = the folder, a UTF-8 `LC_CTYPE` when it names no locale at all;
`COLUMNS`, `LINES`, `TERMCAP` and the AppImage runtime's own `APPIMAGE`, `APPDIR`, `ARGV0`, `OWD` are
removed. A folder that does not exist: the home folder.

### Threads, tabs and splits (`TerminalRegistry`, `TerminalThread`, `TerminalGroup`)

A thread's terminals live outside the view, as in T3 Code: showing another thread shows its
terminals, and the first one's — tabs, splits, scrollback, the programs running in them — wait in
the registry until it is shown again. CodeView is made anew when Code mode is left and entered
again; the registry is not.

* **`TerminalRegistry`** (QML singleton; `TerminalRegistry::instance()`, a child of the
  application): one `TerminalThread` a key. Keys: a thread's page path (`AI/2026-11-26 Napraw
  testy.md`), a new thread's folder until its first message makes the page — CodeView then calls
  `rekey(folder, path)` (it sees ChatView's `path` change before the route does). `thread(key,
  folder)` (made when missing), `find(key)`, `shown(key)` (the panel shows it: the most recently
  shown), `rekey(from, to)`, `close(key)`, `closeIdle(key)`, `isBusy(key)`, `closeAll()`;
  properties `keys` (threads with terminals, the most recently shown first), `busyKeys`,
  `sessionCount`, `available`; signal `busyChanged(key, busy)` — for the sidebar's "a program
  runs" mark (not wired into CodeSidebar by this branch).
* It follows the notebook (`terminalwiring.cpp`, made with the QML singleton): a thread renamed or
  moved (`Workspace::pathRenamed`) — `rekey()`; deleted, to the trash or by another program
  (`Workspace::pathRemoved`) — `close()`; archived (`WorkspaceChats::archivedChanged`, also the
  automatic archiving of untouched threads) — `closeIdle()`, as T3 Code's settling closes a thread's
  idle terminals; the app quitting (`aboutToQuit`) — `hangUpAll()`: SIGHUP to every program at once,
  and the application's end deletes the registry (its child), which reaps them; the waits overlap.
* **Limits**: `MaxThreads` = 10 threads with terminals, `MaxSessions` = 40 terminals in all. When a
  thread is shown beyond the first, the least recently shown other threads' idle terminals end;
  when a new terminal would be the 41st, idle ones of the least recently shown other threads end,
  one at a time, until there is room. A terminal in which a program runs (`busy`) is never ended to
  make room: with only such ones left the thread limit waits, and the new terminal is refused
  (`TerminalThread::error`: "40 terminals are open already: close some of them first."; the panel
  shows it as a toast). Threads left without terminals are forgotten unless shown; those whose last
  terminal the user closed are remembered (20 at most) so that no shell opens by itself there.
* **`TerminalThread`** (a list model of its tabs; roles `group`, `title`, `activity`, `bellRung`,
  `busy`, `running`, `hasExited`, `exitCode`, `paneCount`): `key`, `folder` (where new terminals
  start; the thread's folder — its worktree's, else its project's), `count`, `sessionCount`,
  `currentIndex` (rw), `current`, `currentSession`, `busy`, `autoOpen` (false once the user closed
  its last terminal), `error`. `openTab(command, label)` (after the shown tab, shown, started),
  `split(orientation)` (a shell after the focused terminal of the shown tab, in the directory that
  terminal is in now, focused; a tab's terminals are all side by side or all stacked — the last
  split decides), `closeSession(session, byUser)`, `closeTab(index, byUser)`, `focusNext(delta)`
  (through every terminal: the shown tab's panes, then the next tab's, around), `closeIdle()`,
  `closeAll()`. Every new terminal asks the registry for room first.
* **`TerminalGroup`** (a tab; a list model of its sessions, roles `session`, `title`, `running`,
  `hasExited`, `exitCode`, `busy`, `activity`, `bellRung`): at most `MaxSessions` = 4, `orientation`
  (`Qt.Horizontal` side by side, `Qt.Vertical` stacked), `focusIndex` (rw), `focusSession`,
  `canSplit`; its `title`, `hasExited` and `exitCode` are the focused session's, its `activity`,
  `bellRung`, `busy` and `running` any session's.

### TerminalView

QML type `TerminalView` (`QQuickPaintedItem`). Properties: `session`, `font` (default: the
system's fixed font; the panel binds `Theme.fontMono`, `Theme.textBase`), `dark` (the default
palette), `foreground`, `background`, `cursorColor`, `selectionColor` (each with a default per
palette and `RESET`), `ansiColors` (16; fewer: the default palette), `padding`, `appShortcuts`
(portable key chords left to the app's shortcuts); read-only `columns`, `rows`, `cellWidth`,
`cellHeight`, `historySize`, `hasSelection`, `selectedText`, `accessibleText`, `mouseReporting`,
`hoveredLink`, `findText`, `findCount`, `findIndex`; `scrollOffset` (rw). Methods: `copy()`,
`paste()`, `selectAll()`, `clearSelection()`, `scrollToBottom()`, `scrollToTop()`, `scrollLines(n)`,
`visibleText()`, `clear()`, `find(text, caseSensitive)`, `findOlder()`, `findNewer()`, `clearFind()`,
`chatText()`; static `chatBlock(title, folder, text)`. Signals: `linkActivated(url)` (Ctrl+click on an
OSC 8 link or a web address in the text), `fileActivated(path, line, column)` (Ctrl+click on an
existing file's path or a `file:` URL), `contextMenuRequested(x, y)`, `findRequested()`.

* Painting: output marks rows dirty; the view turns them into `update(rect)` calls, the scene graph
  paints once a frame whatever piled up (the painter's clip is the dirty area: only those rows are
  drawn). `yes | head -100000` (200 KB) arrives in ~15 reads and costs ~7 paints. Runs of narrow
  Latin cells in one style are drawn with one `drawText()`; the fonts are letter-spaced so every
  glyph's advance is the cell's width (rounded to device pixels), so runs line up with the grid.
  Wide characters, clusters and other scripts are drawn a cell at a time (font fallback included).
  A synchronized update (?2026) is painted when the program ends it, or after 150 ms.
* Cell size: the font's advance of "M" and its height, rounded to device pixels; `columns` and
  `rows` are what fits inside `padding`, applied to the session (TIOCSWINSZ) once a frame
  (`polish()`), never while the view is too small for one cell (hidden, being laid out).
* Colours: the default foreground / background (swapped by DECSCNM), the 16 ANSI colours of the
  dark or light palette (readable on #16171b and #fbf7ee), xterm's 6×6×6 cube and greys for
  16-255, RGB as given. Inverse swaps, dim mixes the foreground 45 % towards the background,
  invisible draws nothing. Bold uses the bold face (no "bold as bright").
* Cursor: block, underline or bar (DECSCUSR); blinking only while the view has the focus (and the
  program did not ask for a steady one); a hollow block without the focus; none once the program
  ended.
* Keys: everything goes to the program except the terminal's own — Ctrl+Shift+C (copy), Ctrl+Shift+V
  and Shift+Insert (paste), Ctrl+Shift+A (select all the scrollback; Ctrl+A stays the shell's line
  start), Ctrl+Shift+F (`findRequested()`), Ctrl+Shift+Home / End (the start of the scrollback, the
  latest output) — Cmd+C / V / A / F on macOS —, Shift+Page Up / Page Down / Home / End (the
  scrollback, on the main screen) and Ctrl+Tab / Ctrl+Shift+Tab (left to the focus chain). The view
  accepts `ShortcutOverride` for every key but other Ctrl+Shift chords (and Command chords on macOS)
  and the chords in `appShortcuts` (the panel's commands: Ctrl+\`, Ctrl+Alt+] …), so the app's own
  shortcuts (Ctrl+K…) do not take Ctrl+letters from the shell; Ctrl+Shift+<key> stays the app's.
  Typing scrolls to the bottom.
* Mouse: drag selects (positions between cells), double click a word (letters, digits and
  `_-.~/:@%+#&=?$`: paths and addresses), triple click a line (across wrapped rows), Shift+click
  extends. A right click the program does not take (no mouse reporting, or Shift held):
  `contextMenuRequested(x, y)` — the selection stays for the menu's Copy and Add to chat.
* Links without OSC 8: while Ctrl is held (Cmd on macOS) the line under the pointer is looked at
  (`logicalLine` + `findLinks`, a path checked with `resolvePath` against the session's
  `currentDirectory`) and the link found is underlined in the cursor's colour, the pointer a hand;
  without Ctrl a move costs a modifier test. Moving along the same link looks for nothing again; a
  word found to be no file is not looked at again until its line changes; output over the link's
  rows or scrolling ends it. Ctrl+click: an OSC 8 link first, then the text's — `linkActivated(url)`
  for web addresses, `fileActivated(path, line, column)` for files (a `file:` URL of an existing
  file too).
* The find bar's search: `find(text, caseSensitive)` (case-insensitive by default) highlights every
  match (`findText`, at most 10 000) under the selection's layer, the current one stronger, and
  scrolls to the newest; `findOlder()` / `findNewer()` step (around) and scroll a match out of view
  into the middle. New output moves the matches with their lines and finds them again 250 ms later
  (a flood: four searches a second, not one a read), keeping the current match where it was.
* `clear()`: the session's Clear; the selection, a link and the scroll position go, the search is
  run again.
* `chatText()` (the panel's "Add to chat"): the selection, else the rows in view, as
  `chatBlock(title, folder, text)` — "Terminal output (zsh, in ~/projekt):", then the text (trailing
  spaces and blank lines at its ends dropped) in a `console` fence longer than any run of
  backticks in it. Releasing copies to the primary selection where there is one (X11, Wayland); the middle
  button pastes it. The selection follows its text into the scrollback and ends when output changes
  its rows. When the program asked for the mouse, clicks, drags and the wheel go to it (Shift
  bypasses that for selecting). The wheel scrolls the scrollback (3 lines a notch, touchpads by
  pixels); on the alternate screen without mouse reporting it sends arrow keys (less, man).
  Scrolled back, new output keeps the same lines in view.
* Input method: committed text is typed (encodeText); the preedit text is drawn underlined at the
  cursor; `ImCursorRectangle` is the cursor's cell.
* Accessibility: the panel sets `Accessible.role: Accessible.Terminal`, the session's title as the
  name and `accessibleText` (the visible rows, rebuilt at most every 300 ms and only while an
  assistive technology is active) as the description.

### TerminalPanel

`TerminalPanel { threadKey: …; folder: …; dark: Theme.dark }` (a FocusScope). Properties:
`threadKey` (whose terminals: TerminalRegistry's key), `folder` (where new ones start), `dark`,
`openShellOnStart` (default true: a shell opens by itself, without taking the focus, when the panel
is `shown` with a thread that has no terminals and whose last one the user did not close), `shown`,
read-only `thread`, `group`, `count`, `currentIndex`, `available`, `currentSession`, `findOpen`,
`appShortcuts`. Functions: `run(command, label)` (a new tab running `command` through the user's
shell — the project's "scripts" button: dev server, tests; CodeView.runScript), `newTerminal()`,
`openTab(command, label, takeFocus)`, `split(orientation)`, `select(index)`, `focusNext(delta)`,
`focusTerminal()`, `closeTab(index)`, `closeSession(session)`, `closeCurrent()`, `restart()`,
`openFind()`, `closeFind()`, `currentView()`. Signals: `linkActivated(url)` — the owner decides
whether to open a web address a program printed —, `fileActivated(path, line, column)` (CodeView:
`Workspace.projects.openInEditor(path)` for now), `addToChat(text)` (CodeView:
`chat.addToComposer(text)`).

The tab strip (one `TerminalTab` per tab: its focused terminal's title, dimmed once ended with
"exit N" — green for 0, red otherwise —, a dot in the accent colour when output came while it was not
shown, a bell when a program rang one, the number of terminals of a split tab, ×), "+" (new shell),
split right, split down, Find; under it the shown tab's terminals (`TerminalPane` each, equal
shares, 1-pixel gaps; in a split the focused one has an accent line on top; a click focuses one):
the view, a scroll bar over the scrollback (drag the thumb, click beside it), "Jump to latest" while
scrolled back, and once its program ended (or could not start) "Process exited with code N" with
Restart and ×. The find bar (`TerminalFindBar`, top right): the query, "3 of 12", Aa, older /
newer, × — Enter the older match, Shift+Enter the newer, Esc closes it (the keyboard back to the
terminal); another terminal focused: its matches. The context menu (right click; a long press on a
touch screen): Add to chat, Copy, Paste, Select all, Find…, Jump to latest, Clear, Split right,
Split down, Close terminal. Closing a terminal or a tab in which a program runs asks first ("Close
the terminal?"). "No terminals open" with "New terminal" when a thread has none, and "No terminal on
this device" where `available` is false.

objectNames: `terminalPanel`, `terminalTabs`, `terminalTab_<n>` (`terminalTabTitle_<n>`,
`terminalTabActivity_<n>`, `terminalTabBell_<n>`, `terminalTabExit_<n>`, `terminalTabClose_<n>`),
`terminalNew`, `terminalSplitRight`, `terminalSplitDown`, `terminalFindButton`, `terminalPanes`,
`terminalPane_<n>` (`terminalView` — `terminalView_<n>` in a split —, `terminalScrollBar_<n>`,
`terminalJumpLatest_<n>`, `terminalExited_<n>`, `terminalExitedText_<n>`, `terminalRestart_<n>`,
`terminalCloseExited_<n>`), `terminalFindBar` (`terminalFindField`, `terminalFindStatus`,
`terminalFindCase`, `terminalFindOlder`, `terminalFindNewer`, `terminalFindClose`), `terminalMenu`
(`terminalMenuAddToChat`, `terminalMenuCopy`, `terminalMenuPaste`, `terminalMenuSelectAll`,
`terminalMenuFind`, `terminalMenuJumpLatest`, `terminalMenuClear`, `terminalMenuSplitRight`,
`terminalMenuSplitDown`, `terminalMenuClose`), `terminalCloseConfirm`, `terminalEmpty`,
`terminalUnavailable`.

### Keys

The panel's commands are CommandModel's (the palette; Settings → Keyboard shortcuts changes them),
enabled in Code mode (route `code`), uiActions `terminal:<command>` that CodeView carries out — the
panel shown (made) first. Their chords are left to the app inside the terminal too
(`appShortcuts`). The others are the terminal's own (TerminalView: not customisable, like copy and
paste).

| Command | Default | T3 Code (Linux) | Notes |
|---|---|---|---|
| `terminal.toggle` Show or hide the terminal | Ctrl+\` | mod+j | Ctrl+J is today's journal in Brulion (VS Code's key); shown: the keyboard to it, hidden: to the thread's field |
| `terminal.new` New terminal | Ctrl+Shift+T | mod+n | Ctrl+N is the shell's |
| `terminal.splitRight` Split terminal right | Ctrl+Shift+D | mod+d | Ctrl+D is EOF |
| `terminal.splitDown` Split terminal down | Ctrl+Alt+Shift+D | mod+shift+d | |
| `terminal.close` Close terminal | Ctrl+Shift+W | mod+w | asks when a program runs in it |
| `terminal.next` / `terminal.previous` | Ctrl+Alt+] / Ctrl+Alt+[ | the same | through every terminal: panes, then tabs |
| Select all (the view) | Ctrl+Shift+A | Ctrl+Shift+A | |
| Find (the view) | Ctrl+Shift+F | — | outside the terminal Ctrl+Shift+F is Search |
| Jump to start / latest (the view) | Ctrl+Shift+Home / End | the same | reserved keys of editing text: never commands |
| Copy / Paste (the view) | Ctrl+Shift+C / V | the same | |

## Security

* OSC 52 is ignored: a program — or a file shown with `cat`, or output of a remote host over ssh —
  can neither read nor replace the clipboard. The only clipboard writes are the user's copy
  (Ctrl+Shift+C, Cmd+C, `copy()`) and the primary selection after a mouse selection.
* Programs cannot change the view's colours (OSC 4 / 10 / 11 / 12 settings ignored), and colour
  queries reveal only the theme's colours.
* Hyperlinks (OSC 8) and links found in the text are never opened by the view: Ctrl+click emits
  `linkActivated(url)` or `fileActivated(path, …)` and the owner decides (CodeView: a note's link
  rules for URLs; files only once the view found them on disk, opened in the user's editor). A path
  is looked up only for the word under the pointer while Ctrl is held — output cannot make the app
  touch files by itself. Titles are stripped of control characters and cut.
* "Add to chat" puts the text into the composer only (the user sends it); the fence cannot be closed
  by the output (longer than any run of backticks in it).
* Pastes cannot escape bracketed paste (ESC is removed from the text) and carry no control
  characters but TAB and line breaks. Without bracketed paste, a pasted line break is Enter, as in
  every terminal.
* No shell strings: the program is execve()d with an argument vector; a command line reaches only
  the user's own shell (`-lc`), which is what the user asked to run.
* The child inherits no descriptor of the app (the vault's files, sockets, the remote control's
  socket) — tested —, default signal dispositions, an empty signal mask, a session of its own. It
  does inherit the app's environment (minus the AppImage's own variables), as programs started from
  any terminal do.
* Replies to queries are fixed strings or the cursor position and size; nothing the program sends is
  echoed back to it.
* Bounded memory under hostile output: 32 parameters, 4 KB OSC strings, 32-unit clusters, 4096
  links, 1000 × 500 cells, the scrollback's limit, 16 MB of queued input; 10 MB of random bytes and of
  random sequences with resizes in between are a test (`tst_term_fuzz`).

## Performance

Measured on the x86_64 desktop (Debug -O0 build of Brulion's code over Qt's release build, unless
noted):

* Parsing: 10 MB of random bytes 3.2 s (0.49 s with -O2), 10 MB of random sequences 1.2 s (0.46 s
  with -O2); 100 000 lines of `y` 0.47 s (45 ms with -O2).
* `yes | head -n 100000` through a real pty into a shown view: ~0.65 s end to end, 15 reads, 7
  paints.
* A full repaint of a 200 × 60 screen of coloured Polish text: ~9 ms (the dirty-row path repaints a
  typing line in a fraction of that).
* Links cost nothing while Ctrl is up (no hover work but a modifier test) and one line's scan plus
  at most one `stat()` per word under the pointer with it; output never looks for links.
* The find bar: a search of a full 5000-line scrollback builds each line's text once (its cell map
  only for lines with a match); with the bar open, output runs it again at most every 250 ms. The
  matches are painted with the rows being painted (a binary search for the row's first match).
* Threads' terminals: a hidden one costs its scrollback and its program; nothing polls an idle
  session (busy is looked at after output or input, and every 2 s only while a job runs).

The next step, if full-screen redraws at high DPI ever need it, is a scene-graph item with a glyph
atlas (QSGTextNode per row or a custom geometry node) instead of QQuickPaintedItem.

## Not done yet

* Windows: a ConPTY backend (`pty_conpty.cpp`) behind `platform::Pty` — with `hasForegroundJob()`
  and `currentDirectory()` (false / "" are valid answers).
* Reflow of wrapped lines on resize (lines are cut / padded; `Line::wrapped` is already kept).
* A thread's terminal layout (tabs, splits, folders, commands — not the processes) restored after a
  restart; resizable splits (they share the tab equally).
* The sidebar's "a program runs in its terminal" mark (`TerminalRegistry.busyKeys` /
  `busyChanged(key, busy)` are there for it).
* Grapheme clustering of emoji ZWJ sequences, flags and skin tones (widths follow wcwidth()).
* Shell integration (OSC 133 / 633 prompts and command marks), OSC 4 palette queries, DECRQSS,
  XTGETTCAP, the kitty keyboard protocol, sixel / kitty graphics.

## Tests

* `tests/core/term`: `tst_term` (sequences in the style of vttest's basics, wrapping and the
  pending wrap, wide characters at the edge, combining marks, Polish text, SGR in every form,
  replies, OSC, hyperlinks, UTF-8 split at every byte and invalid bytes, long parameter lists,
  scroll regions, scrollback, the alternate screen, modes, resize, dirty rows, character widths),
  `tst_term_keys` (keys, pastes, focus, mouse), `tst_term_fuzz` (10 MB random bytes and sequences
  with resizes, invariants checked; `yes` flood), `tst_term_text` (lines of text mapped back to cells
  with wide characters, marks and emoji; the search — case, Polish letters, wrapped matches, the
  limit; Clear keeping the prompt's row; URLs and paths as compilers, test runners, grep, git and
  Python print them, words that are none; resolving paths; a hostile 1000-column line).
* `tests/platform/tst_pty`: output, `stty size` before and after a resize, input through `cat`,
  the working directory, the environment, no inherited descriptors, exit codes and signals,
  programs that cannot start, terminate with a background job and with SIGHUP ignored, deletion
  while running, a 1 MB paste, a job in the foreground (job control), the foreground program's
  directory following a `cd` — every child reaped.
* `tests/app/terminal/tst_terminal`: TerminalSession end to end (cat, the shell in its folder,
  sizes, titles, OSC 7, stop and restart, exit codes, busy with a job and with its command, marks
  while not shown, the current directory), tabs and splits (four at most, the direction, focus
  through every terminal, closing), the registry (keys, rekeying, renames, archiving leaving the
  busy terminal, deletion, quitting, the limits — never ending a running program), TerminalView in a
  window (keys, Ctrl+C, copy, the terminal's Ctrl+Shift chords, the app's shortcuts left alone, the
  input method, selection by drag / double / triple click, wrapped lines, the scrollback with keys
  and wheel, mouse reporting, links under Ctrl and Ctrl+click, the find bar's matches through new
  output, Clear, the context menu's request, the chat's block, painting at 0×0, 1×1 and other odd
  sizes, colours in both palettes, layout, a `yes` flood's paint count).
* `tst_qml_compiled`: the panel's QML is compiled to C++ in full.
