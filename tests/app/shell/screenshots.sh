#!/usr/bin/env bash
# Screenshots of the app shell (the real app, not the component gallery): onboarding, the main
# window with the sample notebook (journals, the guide page and its editor states: page-editing,
# page-suggest, page-reminder, …; with a simulated on-screen keyboard: page-editing-keyboard,
# journals-editing-keyboard, capture-keyboard), settings, the command palette, the
# quick switcher, quick capture, the phone drawer and folders (tree, drawer-tree, move,
# move-chosen, newpage-folder, newpage-root, newfolder, allpages, allpages-folder) — × {desktop
# 1280×800, phone 412×915 --touch} × {light, dark} — into
# tests/screenshots/shell-<shot>-<size>-<theme>.png.
#
# Every run gets a fresh, temporary XDG config/data home (settings, app data, the sample
# notebook) and the fixed clock --now, so the images are reproducible and nothing of the
# user's own Brulion is touched.
#
#   tests/app/shell/screenshots.sh                    # everything
#   tests/app/shell/screenshots.sh settings palette   # only these shots
#   SIZES=phone THEMES=dark tests/app/shell/screenshots.sh journals
#   SIZES="desktop-tall phone-tall" tests/app/shell/screenshots.sh page   # the whole guide page
#   On the laptop: scripts/remote-build.sh -n shell brulion \
#     --run 'BRULION_BIN=$BRULION_BUILD_DIR/src/app/brulion tests/app/shell/screenshots.sh' \
#     --pull tests/screenshots
#
# Run inside `nix develop` (Qt plugin and QML paths). One app instance at a time (lock).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."

if [[ -z "${BRULION_GUI_LOCKED:-}" ]] && command -v flock >/dev/null 2>&1; then
  export BRULION_GUI_LOCKED=1
  exec flock /tmp/brulion-gui.lock "$0" "$@"
fi

bin="${BRULION_BIN:-build/src/app/brulion}"
out="${OUT_DIR:-tests/screenshots}"
locale="${LOCALE:-pl}"
now="${NOW:-2026-10-14 10:00}"
delay="${DELAY:-1400}"
read -r -a sizes <<< "${SIZES:-desktop phone}"
read -r -a themes <<< "${THEMES:-light dark}"

declare -A shot_args=(
  [onboarding]="--onboarding"
  [journals]="--demo --route journals"
  [page]="--demo"
  [settings]="--demo --route settings"
  [settings-editor]="--demo --route {\"view\":\"settings\",\"section\":\"editor\"}"
  [settings-vault]="--demo --route {\"view\":\"settings\",\"section\":\"vault\"}"
  [settings-reminders]="--demo --route {\"view\":\"settings\",\"section\":\"reminders\"}"
  [settings-app]="--demo --route {\"view\":\"settings\",\"section\":\"app\"}"
  [newpage]="--demo --route journals --popup newPage"
  [about]="--demo --route journals --popup about"
  [palette]="--demo --route journals --popup palette"
  [switcher]="--demo --route journals --popup switcher:rem"
  [capture]="--demo --route journals --popup capture"
  [drawer]="--demo --route journals --popup drawer"
  # Folders: the renovation project open (the tree shows it, the drawer on phones with each row's
  # "⋯"), "Przenieś do…" for it, a new page in its folder (the folder chip), a new folder, and All
  # pages as a folder browser (the root's folders; the project folder with its breadcrumb).
  [tree]="--demo --route {\"view\":\"page\",\"path\":\"Projekty/Remont\u0020mieszkania.md\"}"
  [drawer-tree]="--demo --route {\"view\":\"page\",\"path\":\"Projekty/Remont\u0020mieszkania.md\"} --popup drawer"
  [move]="--demo --route {\"view\":\"page\",\"path\":\"Projekty/Remont\u0020mieszkania.md\"} --popup move"
  [newpage-folder]="--demo --route {\"view\":\"page\",\"path\":\"Projekty/Remont\u0020mieszkania.md\"} --popup newPage"
  # A page at the notebook's root: the new page goes to the root (its chip); the templates folder
  # moved into the project folder, chosen: what changes, both settings that follow.
  [newpage-root]="--demo --route {\"view\":\"page\",\"path\":\"Przepisy.md\"} --popup newPage"
  [move-chosen]="--demo --route journals --popup move:templates>Projekty"
  [newfolder]="--demo --route journals --popup newFolder"
  [allpages]="--demo --route allPages"
  [allpages-folder]="--demo --route {\"view\":\"allPages\",\"path\":\"Projekty\"}"
  # The block editor in the real app on the guide page (qml/dev/PageScript.qml).
  [page-editing]="--demo --editor-script editing"
  # A simulated on-screen keyboard (--keyboard, drawn grey): the touch toolbar docked on it, the
  # bottom navigation out of the way — a page, today in the journal stream, quick capture's sheet.
  [page-editing-keyboard]="--demo --editor-script editing --keyboard 300"
  [journals-editing-keyboard]="--demo --route {\"view\":\"journals\",\"open\":\"write\"} --keyboard 300"
  [capture-keyboard]="--demo --route journals --popup capture --keyboard 300"
  [page-suggest]="--demo --editor-script suggest"
  [page-tags]="--demo --editor-script tags"
  [page-slash]="--demo --editor-script slash"
  [page-date]="--demo --editor-script date"
  [page-reminder]="--demo --editor-script reminder"
  [page-source]="--demo --editor-script source"
  [page-sidepanel]="--demo --editor-script sidepanel"
  [page-empty]="--demo --editor-script empty"
  [page-new]="--demo --route {\"view\":\"page\",\"path\":\"Prezenty.md\"}"
  # Note modes: the guide is a document; editing it; switched to an outline; the page menu with
  # "Przełącz na konspekt"; the renovation project (an outline).
  [page-doc-paragraph]="--demo --editor-script doc-paragraph"
  [page-doc-list]="--demo --editor-script doc-list"
  [page-doc-heading]="--demo --editor-script doc-heading"
  [page-doc-outline]="--demo --editor-script doc-outline"
  [page-menu]="--demo --route {\"view\":\"page\",\"path\":\"Witaj\u0020w\u0020Brulionie.md\",\"open\":\"menu\"}"
  [page-project]="--demo --route {\"view\":\"page\",\"path\":\"Projekty/Remont\u0020mieszkania.md\"}"
  # A picture attachment: the picture with the notes that embed it, its text (read-only), and
  # its references in the side panel (wide windows).
  [page-picture]="--demo --route {\"view\":\"page\",\"path\":\"assets/plan-mieszkania.svg\"}"
  [page-picture-text]="--demo --route {\"view\":\"page\",\"path\":\"assets/plan-mieszkania.svg\",\"open\":\"text\"}"
  [page-picture-links]="--demo --route {\"view\":\"page\",\"path\":\"assets/plan-mieszkania.svg\",\"open\":\"links\"}"
  # The document mode on a long Obsidian-like note (tests/app/shell/notemode/, added to the
  # sample notebook): reading, editing a paragraph, a nested list item, a wrapped nested task, the
  # same note switched to an outline, a heading and a numbered item being edited, a selected task, the
  # header's mode toggle with its tooltip; the journal stream with the notebook's journal mode "document".
  [doc-long]="--route {\"view\":\"page\",\"path\":\"Wyprawa\u0020w\u0020Bieszczady.md\"}"
  [doc-long-paragraph]="--route {\"view\":\"page\",\"path\":\"Wyprawa\u0020w\u0020Bieszczady.md\"} --editor-script edit:Pierwszy@60"
  [doc-long-list]="--route {\"view\":\"page\",\"path\":\"Wyprawa\u0020w\u0020Bieszczady.md\"} --editor-script edit:Bilety"
  [doc-long-task]="--route {\"view\":\"page\",\"path\":\"Wyprawa\u0020w\u0020Bieszczady.md\"} --editor-script edit:Plastry"
  [doc-long-outline]="--route {\"view\":\"page\",\"path\":\"Wyprawa\u0020w\u0020Bieszczady.md\"} --editor-script doc-outline"
  [doc-long-heading]="--route {\"view\":\"page\",\"path\":\"Wyprawa\u0020w\u0020Bieszczady.md\"} --editor-script edit:Plan"
  [doc-long-number]="--route {\"view\":\"page\",\"path\":\"Wyprawa\u0020w\u0020Bieszczady.md\"} --editor-script edit:Piątek"
  [doc-long-select]="--route {\"view\":\"page\",\"path\":\"Wyprawa\u0020w\u0020Bieszczady.md\"} --editor-script select:Kurtki"
  [doc-long-toggle]="--route {\"view\":\"page\",\"path\":\"Wyprawa\u0020w\u0020Bieszczady.md\"} --editor-script mode-tooltip"
  [doc-journal]="--route journals"
  [doc-journal-mixed]="--route journals"
)
# Shots on a notebook of their own: the sample notebook seeded by a first run, then the files of
# tests/app/shell/notemode/ added ("journal": today's journal replaced by a document-like day and
# the notebook's journal mode set to "document"; "journal-mixed": that day switched to a document
# by its own `brulion:` key, the other days outlines).
declare -A shot_notemode=(
  [doc-long]=page [doc-long-paragraph]=page [doc-long-list]=page [doc-long-task]=page [doc-long-outline]=page
  [doc-long-heading]=page [doc-long-number]=page [doc-long-select]=page [doc-long-toggle]=page [doc-journal]=journal
  [doc-journal-mixed]=journal-mixed
)
all_shots=(onboarding journals page page-editing page-editing-keyboard journals-editing-keyboard capture-keyboard
  page-suggest page-tags page-slash page-date page-reminder
  page-source page-sidepanel page-new page-empty page-doc-paragraph page-doc-list page-doc-heading page-doc-outline
  page-menu page-project page-picture page-picture-text page-picture-links doc-long doc-long-paragraph doc-long-list doc-long-task doc-long-outline
  doc-long-heading doc-long-number doc-long-select doc-long-toggle doc-journal doc-journal-mixed settings settings-editor settings-vault settings-reminders settings-app
  palette switcher capture newpage about drawer tree drawer-tree move move-chosen newpage-folder newpage-root newfolder
  allpages allpages-folder)
if [[ $# -gt 0 ]]; then shots=("$@"); else shots=("${all_shots[@]}"); fi

if [[ ! -x "$bin" ]]; then
  echo "screenshots: $bin not found — build first" >&2
  exit 1
fi
mkdir -p "$out"

suffix=""
[[ "$locale" != "pl" ]] && suffix+="-$locale"

failed=0
for shot in "${shots[@]}"; do
  [[ -n "${shot_args[$shot]:-}" ]] || { echo "screenshots: unknown shot $shot" >&2; exit 2; }
  read -r -a args <<< "${shot_args[$shot]}"
  # Editor states are scripted after the page loaded (typing, scrolling): give them time.
  shot_delay="$delay"
  [[ "$shot" == page-* || "$shot" == doc-* || "$shot" == journals-editing-* ]] && shot_delay=$((delay + 800))
  for size in "${sizes[@]}"; do
    case "$size" in
      desktop) geometry=1280x800; extra=() ;;
      phone) geometry=412x915; extra=(--touch) ;;
      tablet) geometry=800x1180; extra=(--touch) ;;
      # Whole long pages (the guide page: SIZES="desktop-tall phone-tall" … page).
      desktop-tall) geometry=1280x5600; extra=() ;;
      phone-tall) geometry=412x11000; extra=(--touch) ;;
      *) echo "screenshots: unknown size $size" >&2; exit 2 ;;
    esac
    # The drawer only exists on narrow windows.
    [[ ( "$shot" == drawer || "$shot" == drawer-tree ) && "$size" == desktop* ]] && continue
    # The side panel only exists on wide windows.
    [[ ( "$shot" == page-sidepanel || "$shot" == page-picture-links ) && "$size" == phone* ]] && continue
    for theme in "${themes[@]}"; do
      file="$out/shell-${shot}-${size}-${theme}${suffix}.png"
      home="$(mktemp -d)"
      vault_args=()
      if [[ -n "${shot_notemode[$shot]:-}" ]]; then
        vault="$home/Przykładowy brulion"
        fixtures=tests/app/shell/notemode
        XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state" \
          QML_DISABLE_DISK_CACHE=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
          "$bin" --demo --vault "$vault" --size "$geometry" --locale "$locale" --now "$now" \
          --delay 600 --screenshot "$home/prep.png" >/dev/null 2>&1 || true
        cp "$fixtures/Wyprawa w Bieszczady.md" "$vault/"
        mkdir -p "$vault/assets"
        cp "$fixtures/bieszczady-trasa.svg" "$vault/assets/"
        if [[ "${shot_notemode[$shot]}" == journal ]]; then
          cp "$fixtures/journal-document.md" "$vault/journals/${now%% *}.md"
          mkdir -p "$vault/.brulion"
          printf '{\n  "journalMode": "document"\n}\n' > "$vault/.brulion/config.json"
        elif [[ "${shot_notemode[$shot]}" == journal-mixed ]]; then
          # Only today is a document (its own key); the other days stay outlines.
          { printf -- '---\nbrulion: document\n---\n'; cat "$fixtures/journal-document.md"; } \
            > "$vault/journals/${now%% *}.md"
        fi
        vault_args=(--vault "$vault")
      fi
      if XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state" \
        QML_DISABLE_DISK_CACHE=1 QT_FORCE_STDERR_LOGGING=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
        "$bin" ${vault_args[@]+"${vault_args[@]}"} "${args[@]}" --size "$geometry" --theme "$theme" --locale "$locale" --now "$now" \
        --delay "$shot_delay" "${extra[@]}" --screenshot "$file" 2> >(grep -v "^brulion: screenshot" >&2); then
        echo "$file"
      else
        echo "screenshots: FAILED $file" >&2
        failed=1
      fi
      rm -rf "$home"
    done
  done
done
exit "$failed"
