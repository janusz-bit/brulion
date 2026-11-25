# The Code mode's terminal

The terminal panel of the Code mode (the coding-agent workspace: projects, agent threads, a git
changes panel and this terminal) is a real terminal emulator: an xterm-compatible parser and screen
model in core, a pseudo-terminal per platform, and a Qt Quick view that paints only what changed.

```
src/core/term/        brulion::term — GUI-free (QtCore): Parser, Screen, Terminal, keys, widths
src/platform/pty.h    brulion::platform::Pty — pty_posix.cpp (Linux, macOS, BSDs) / pty_none.cpp
src/app/terminal/     brulion::terminal — TerminalSession, TerminalSessionModel, TerminalView
src/app/qml/terminal/ TerminalPanel.qml (tabs, the view, "Process exited"), TerminalTab.qml
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
* `pty_none.cpp` (Windows, Android, the browser): `available()` is false, `start()` fails with
  "There is no terminal on this device". A Windows backend would replace it with ConPTY
  (`CreatePseudoConsole`, overlapped pipes, `ResizePseudoConsole`, a job object for terminate)
  behind the same class; `src/platform/CMakeLists.txt` chooses the file.

## The session, the view, the panel (`src/app/terminal`, `qml/terminal`)

### TerminalSession

QML type `TerminalSession`. Properties: `folder` (rw), `command` (rw; "" = the user's shell),
`label` (rw), `title` (ro: the program's OSC title, else `label`, else the command, else the
shell's name), `workingDirectory` (ro, OSC 7), `running`, `hasExited`, `exitCode` (128 + signal
when a signal ended it; -1 before), `errorString`, `available` (constant), `columns`, `rows`.
Methods: `start()` (again after the end: on a clean screen), `stop()`, `write(text)` (as is),
`paste(text)` (encodePaste, bracketed when asked), `resize(columns, rows)`. Signals: `bell()`,
`started()`, `exited(code)`, `contentChanged()`, `modesChanged()` and the properties' changes.
C++: `terminal()`, `writeBytes()`, `commandLine()`.

The program: `command` empty → the user's shell (`$SHELL` when it is an executable file, else the
account's login shell, else `/bin/sh`) as an interactive, non-login shell with no arguments;
otherwise that shell runs it as `$SHELL -lc command` (the shell is the program — Brulion never
builds a shell string of its own). The child gets the app's environment with `TERM=xterm-256color`,
`COLORTERM=truecolor`, `PWD` = the folder, a UTF-8 `LC_CTYPE` when it names no locale at all;
`COLUMNS`, `LINES`, `TERMCAP` and the AppImage runtime's own `APPIMAGE`, `APPDIR`, `ARGV0`, `OWD` are
removed. A folder that does not exist: the home folder.

### TerminalSessionModel

QML type `TerminalSessionModel` (the panel's tabs): `count`, `available`; `add(folder, command,
label)` → row (not started), `remove(row)` (stops it, deletes it later), `at(row)`, `indexOf()`.
Roles: `session`, `title`, `running`, `hasExited`, `exitCode`.

### TerminalView

QML type `TerminalView` (`QQuickPaintedItem`). Properties: `session`, `font` (default: the
system's fixed font; the panel binds `Theme.fontMono`, `Theme.textBase`), `dark` (the default
palette), `foreground`, `background`, `cursorColor`, `selectionColor` (each with a default per
palette and `RESET`), `ansiColors` (16; fewer: the default palette), `padding`; read-only
`columns`, `rows`, `cellWidth`, `cellHeight`, `historySize`, `hasSelection`, `selectedText`,
`accessibleText`, `mouseReporting`; `scrollOffset` (rw). Methods: `copy()`, `paste()`,
`selectAll()`, `clearSelection()`, `scrollToBottom()`, `scrollLines(n)`, `visibleText()`. Signal:
`linkActivated(url)` (Ctrl+click on an OSC 8 link).

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
* Keys: everything goes to the program except Ctrl+Shift+C (copy), Ctrl+Shift+V and Shift+Insert
  (paste) — Cmd+C / Cmd+V on macOS —, Shift+Page Up / Page Down / Home / End (the scrollback, on the
  main screen) and Ctrl+Tab / Ctrl+Shift+Tab (left to the focus chain). The view accepts
  `ShortcutOverride` for every key but other Ctrl+Shift chords (and Command chords on macOS), so the
  app's own shortcuts (Ctrl+K…) do not take Ctrl+letters from the shell; Ctrl+Shift+<key> stays the
  app's. Typing scrolls to the bottom.
* Mouse: drag selects (positions between cells), double click a word (letters, digits and
  `_-.~/:@%+#&=?$`: paths and addresses), triple click a line (across wrapped rows), Shift+click
  extends. Releasing copies to the primary selection where there is one (X11, Wayland); the middle
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

`TerminalPanel { folder: …; dark: Theme.dark }` (a FocusScope). Properties: `folder`, `dark`,
`openShellOnStart` (default true: a shell tab opens by itself, without taking the focus),
`currentIndex`, read-only `count`, `available`, `currentSession`. Functions: `run(command, label)`
(a new tab running `command` through the user's shell in `folder` — the project's "scripts"
button: dev server, tests), `newTerminal()`, `openTab(command, label, takeFocus)`, `select(index)`,
`closeTab(index)`, `restart()`, `exitText()`. Signal: `linkActivated(url)` — the owner decides
whether to open a link a program printed.

The tab strip (one `TerminalTab` per session: its title, dimmed once ended, ×), "+" for a new
shell, the current session's `TerminalView`, a bar "Process exited with code N" (or why it could not
start) with Restart, "No terminals open" with "New terminal" when every tab is closed, and "No
terminal on this device" where `available` is false.

objectNames: `terminalPanel`, `terminalTabs`, `terminalTab_<n>`, `terminalTabTitle_<n>`,
`terminalTabClose_<n>`, `terminalNew`, `terminalView`, `terminalEmpty`, `terminalExited`,
`terminalExitedText`, `terminalRestart`, `terminalUnavailable`.

## Security

* OSC 52 is ignored: a program — or a file shown with `cat`, or output of a remote host over ssh —
  can neither read nor replace the clipboard. The only clipboard writes are the user's copy
  (Ctrl+Shift+C, Cmd+C, `copy()`) and the primary selection after a mouse selection.
* Programs cannot change the view's colours (OSC 4 / 10 / 11 / 12 settings ignored), and colour
  queries reveal only the theme's colours.
* Hyperlinks (OSC 8) are never opened by the view: Ctrl+click emits `linkActivated(url)` and the
  owner decides (open http(s) only, say). Titles are stripped of control characters and cut.
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

The next step, if full-screen redraws at high DPI ever need it, is a scene-graph item with a glyph
atlas (QSGTextNode per row or a custom geometry node) instead of QQuickPaintedItem.

## Not done yet

* Windows: a ConPTY backend (`pty_conpty.cpp`) behind `platform::Pty`.
* Reflow of wrapped lines on resize (lines are cut / padded; `Line::wrapped` is already kept).
* Grapheme clustering of emoji ZWJ sequences, flags and skin tones (widths follow wcwidth()).
* Searching the scrollback, a scroll bar, links underlined on hover, a context menu, a bell
  indicator on tabs, persisting sessions across restarts.
* Shell integration (OSC 133 / 633 prompts and command marks), OSC 4 palette queries, DECRQSS,
  XTGETTCAP, the kitty keyboard protocol, sixel / kitty graphics.

## Tests

* `tests/core/term`: `tst_term` (sequences in the style of vttest's basics, wrapping and the
  pending wrap, wide characters at the edge, combining marks, Polish text, SGR in every form,
  replies, OSC, hyperlinks, UTF-8 split at every byte and invalid bytes, long parameter lists,
  scroll regions, scrollback, the alternate screen, modes, resize, dirty rows, character widths),
  `tst_term_keys` (keys, pastes, focus, mouse), `tst_term_fuzz` (10 MB random bytes and sequences
  with resizes, invariants checked; `yes` flood).
* `tests/platform/tst_pty`: output, `stty size` before and after a resize, input through `cat`,
  the working directory, the environment, no inherited descriptors, exit codes and signals,
  programs that cannot start, terminate with a background job and with SIGHUP ignored, deletion
  while running, a 1 MB paste — every child reaped.
* `tests/app/terminal/tst_terminal`: TerminalSession end to end (cat, the shell in its folder,
  sizes, titles, OSC 7, stop and restart, exit codes), the model, TerminalView in a window (keys,
  Ctrl+C, copy, the input method, selection by drag / double / triple click, wrapped lines, the
  scrollback with keys and wheel, mouse reporting, painting at 0×0, 1×1 and other odd sizes, colours
  in both palettes, layout, a `yes` flood's paint count).
* `tst_qml_compiled`: the panel's QML is compiled to C++ in full.
