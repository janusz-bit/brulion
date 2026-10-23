#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tests scripts/check-qml-reads.sh on small QML files — the bare reads it has to find, the code it
# has to leave alone — and then runs it over the app's QML, where no binding may depend on a bare
# read (qmlcachegen compiles one away with the binding's dependency; docs/UI.md).
#
# Usage: tests/scripts/tst_check_qml_reads.sh [path/to/check-qml-reads.sh [QML directory]]
#        (default: scripts/check-qml-reads.sh, src/app/qml)
set -euo pipefail

script=$(realpath -- "${1:-$(dirname -- "${BASH_SOURCE[0]}")/../../scripts/check-qml-reads.sh}")
qml=$(realpath -- "${2:-$(dirname -- "$script")/../src/app/qml}")
[[ -f $script && -d $qml ]] || {
  echo "no $script or $qml" >&2
  exit 2
}
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

failed=0
pass() { printf 'PASS   : %s\n' "$1"; }
fail() {
  printf 'FAIL!  : %s\n' "$1"
  failed=$((failed + 1))
}
expect() { # <description> <command...>: the command succeeds
  local what=$1
  shift
  if "$@"; then pass "$what"; else fail "$what"; fi
}
# check <arguments...>: runs the script; sets $status and $out (stdout + stderr).
check() {
  status=0
  out=$(bash "$script" "$@" 2>&1) || status=$?
  printf '%s\n' "$out" | sed 's/^/        | /'
}
has() { grep -qF -- "$1" <<<"$out"; }
reported() { grep -c '\.qml:[0-9]*:' <<<"$out" || true; }

# Each read meant only to make the binding depend on something: lines 7, 10, 12, 14 and 17.
cat >"$tmp/Kalendarz.qml" <<'EOF'
import QtQuick

Item {
    id: root
    property int revision: 0
    readonly property var marks: {
        void Workspace.revision
        return Workspace.journalDates("2026-10-01", "2026-10-31")
    }
    readonly property var dni: { void DatesHelper.locale; return DatesHelper.weekdayNames("short") }
    height: {
        frames.revision
        let h = 0
        root.revision;
        return h
    }
    width: { void(root.revision); return 10 }
}
EOF
check "$tmp/Kalendarz.qml"
expect "bare reads are found" test "$status" = 1
for line in 7 10 12 14 17; do
  expect "... the one on line $line" has "Kalendarz.qml:$line:"
done
expect "... and nothing else" test "$(reported)" = 5
expect "... saying why" has "a binding has to use what it follows for real"

# Reads used for real, comments, strings, continued expressions and statements that are no reads.
cat >"$tmp/Zażółć.qml" <<'EOF'
import QtQuick
import QtQuick.Layouts

// `void Workspace.revision` in a comment says what not to write.
Item {
    id: root
    property int revision: 0
    readonly property var marks: Workspace.revision >= 0 ? Workspace.journalDates("2026-10-01", "2026-10-31") : []
    readonly property string label: qsTr("Avoid the void: %1").arg(root.revision) // void root.revision
    readonly property url site: "https://example.com/void.html"
    Layout.fillWidth: true
    function przelicz(): void {
        const suma = root.revision +
            root.width
        const lista = [
            root.height
        ]
        if (suma < 0)
            return
        else
            policz(lista)
    }
    height: {
        if (frames.revision < 0) // (read for real)
            return 0
        let total = 0
        for (let f = parent; f; f = f.parent)
            total += 1
        return total
            ? total
            : root.revision
    }
}
EOF
check "$tmp/Zażółć.qml"
expect "reads used for real, comments, strings and continued lines pass" test "$status/$(reported)" = 0/0

check "$tmp"
expect "a directory: its QML files are checked" test "$status/$(reported)" = 1/5

check "$tmp/Brak.qml"
expect "a missing file is an error of its own" test "$status" = 2

check "$qml"
expect "the app's QML has no binding that depends on a bare read" test "$status/$(reported)" = 0/0

echo
if ((failed)); then
  echo "Totals: $failed failed"
  exit 1
fi
echo "Totals: all passed"
