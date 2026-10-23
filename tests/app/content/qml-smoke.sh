#!/usr/bin/env bash
# QML smoke test of the content views (ctest tst_content_qml): opens JournalsView, PageView (a
# page, a daily note, a missing page, the rename / export dialogs, the side panel, pages named by
# tags: a tag's page not written yet with its references, one with a sub-tag), AllPagesView,
# TagsView (all tags, a tag's overview), SearchView, TrashView and HistoryView in the real app with the
# sample notebook, desktop and phone sizes, headless, and fails when the app does not start or QML
# reports a warning or error from one of these files (binding loops, type errors, missing
# properties, layout loops).
#
#   tests/app/content/qml-smoke.sh <brulion binary> <scratch dir>
set -uo pipefail
bin="$1"
work="$2"
rm -rf "$work"
mkdir -p "$work"

routes=(
  journals
  '{"view":"journals","open":"jump"}'
  '{"view":"journals","open":"write"}'
  '{"view":"journals","date":"2026-10-13"}'
  '{"view":"journal","date":"2026-10-13"}'
  '{"view":"page","path":"Projekty/Remont mieszkania.md"}'
  '{"view":"page","path":"Projekty/Remont mieszkania.md","open":"rename"}'
  '{"view":"page","path":"Projekty/Remont mieszkania.md","open":"export"}'
  '{"view":"page","path":"Książki 2026.md","open":"unlinked"}'
  '{"view":"page","path":"Nowa strona.md"}'
  '{"view":"page","path":"Witaj w Brulionie.md","open":"outline"}'
  '{"view":"journal","date":"2026-10-13","open":"calendar"}'
  '{"view":"page","path":"Projekty/Remont mieszkania.md","open":"links"}'
  allPages
  '{"view":"allPages","sort":"modified"}'
  tags
  '{"view":"tag","tag":"praca"}'
  '{"view":"tag","tag":"pomysł","overview":true}'
  '{"view":"page","path":"pomysł.md"}'
  '{"view":"page","path":"projekt.md"}'
  '{"view":"search","query":"remont"}'
  search
  trash
  '{"view":"history","path":"Witaj w Brulionie.md"}'
)
pattern='qml/(content/|views/(JournalsView|PageView|AllPagesView|TagsView|SearchView|TrashView|HistoryView))'

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
