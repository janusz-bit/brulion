#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Finds statements in QML that only read a value: `void Workspace.revision`, or a line that is just
# `frames.revision`. Such a read is meant to make a binding follow the value (re-evaluate when it
# changes), but qmlcachegen compiles it away, together with the binding's dependency: the view
# goes stale (docs/UI.md: a binding that has to follow a value reads it for real —
# `if (frames.revision < 0) return 0`, `weekdayNames("short", DatesHelper.locale)`).
#
# Usage: scripts/check-qml-reads.sh [file or directory...]   (default: src/app/qml)
# Prints file:line: text for each such statement; exit status 1 when there is one, 2 on bad usage.
set -euo pipefail

if [[ $# -eq 0 ]]; then
  set -- "$(dirname -- "${BASH_SOURCE[0]}")/../src/app/qml"
fi
files=()
for arg in "$@"; do
  if [[ -d $arg ]]; then
    while IFS= read -r -d '' f; do files+=("$f"); done < <(find "$arg" -name '*.qml' -type f -print0 | sort -z)
  elif [[ -f $arg ]]; then
    files+=("$arg")
  else
    echo "check-qml-reads: no such file or directory: $arg" >&2
    exit 2
  fi
done
[[ ${#files[@]} -gt 0 ]] || exit 0

# The code of a line: without a trailing `// comment` (a URL's "://" in a string stays) and
# without a whole-line comment. Reported:
#   * the void operator on a name or a member (`void x.y`, `{ void x; return … }`);
#   * a line that is only a name or a member read (`x.y`, `x.y;`), unless it continues the line
#     before (that one ends with an operator, a comma or an opening bracket).
awk '
function code(s) {
    sub(/^[ \t]*\/\/.*$/, "", s)
    sub(/[ \t]+\/\/.*$/, "", s)
    return s
}
FNR == 1 { last = "" }
{
    line = code($0)
    if (line ~ /(^|[^A-Za-z0-9_$.])void([ \t]+[A-Za-z_$(]|\()/) {
        printf "%s:%d: %s\n", FILENAME, FNR, $0
        found = 1
    } else if (line ~ /^[ \t]*[A-Za-z_$][A-Za-z0-9_$]*(\.[A-Za-z_$][A-Za-z0-9_$]*)*[ \t]*;?[ \t]*$/ \
               && line !~ /^[ \t]*(else|break|continue|return|debugger|do|try|finally)[ \t]*;?[ \t]*$/ \
               && index("+-*/%&|^!=<>?:,([.", last) == 0) {
        printf "%s:%d: %s\n", FILENAME, FNR, $0
        found = 1
    }
    sub(/[ \t]+$/, "", line)
    if (line != "")
        last = substr(line, length(line), 1)
}
END {
    if (found) {
        print "check-qml-reads: a binding has to use what it follows for real (docs/UI.md)" > "/dev/stderr"
        exit 1
    }
}
' "${files[@]}"
