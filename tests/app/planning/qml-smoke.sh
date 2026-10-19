#!/usr/bin/env bash
# QML smoke test of the planning views (ctest tst_planning_qml): opens CalendarView (month, week,
# agenda, the add dialog), TasksView (with the query dialog and a quick-add preview) and GraphView
# (whole, local, a selected node) in the real app with the sample notebook, desktop and phone
# sizes, headless, and fails when the app does not start or QML reports a warning or error from
# one of these files (binding loops, type errors, missing properties, layout loops).
#
#   tests/app/planning/qml-smoke.sh <brulion binary> <scratch dir>
set -uo pipefail
bin="$1"
work="$2"
rm -rf "$work"
mkdir -p "$work"

routes=(
  calendar
  '{"view":"calendar","mode":"week","open":"day","date":"2026-10-16"}'
  '{"view":"calendar","mode":"agenda"}'
  '{"view":"calendar","open":"reminder","date":"2026-10-16"}'
  tasks
  '{"view":"tasks","query":"path:Projekty/ status:open","open":"query"}'
  '{"view":"tasks","open":"add:Zadzwonić do Ani jutro o 9 #praca"}'
  graph
  '{"view":"graph","path":"Projekty/Remont mieszkania.md","depth":2}'
  '{"view":"graph","select":"Projekty/Remont mieszkania.md"}'
)
pattern='qml/(planning/|views/(CalendarView|TasksView|GraphView))'

failed=0
for size in desktop phone; do
  if [[ "$size" == desktop ]]; then geometry=(--size 1280x800); else geometry=(--size 412x915 --touch); fi
  for route in "${routes[@]}"; do
    home="$work/home"
    rm -rf "$home"
    log="$work/log.txt"
    if ! XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state" \
      "$bin" --demo --route "$route" --no-single-instance "${geometry[@]}" --now "2026-10-14 10:00" \
      --locale pl --delay 1500 --screenshot "$work/shot.png" 2> "$log"; then
      echo "FAIL: the app did not run for $route ($size)"
      cat "$log"
      failed=$((failed + 1))
      continue
    fi
    if grep -E "$pattern" "$log"; then
      echo "FAIL: QML warnings for $route ($size)"
      failed=$((failed + 1))
    else
      echo "ok: $route ($size)"
    fi
  done
done
exit "$failed"
