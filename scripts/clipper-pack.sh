#!/usr/bin/env bash
# The Web Clipper's packages (docs/CLIPPER.md "Packages"): from clipper/, with the version of
# CMakeLists.txt, into dist-clipper/:
#
#   brulion-clipper-<version>-chrome.zip     Chrome, Chromium, Brave, Edge — unzip and "Load unpacked"
#                                            (the manifest's key keeps the id the host allows)
#   brulion-clipper-<version>-firefox.xpi    Firefox, unsigned (about:debugging, or Firefox Developer
#                                            Edition / Nightly with xpinstall.signatures.required off)
#   brulion-clipper-<version>-chrome-store.zip  with --store: without the key (the Chrome Web Store
#                                            gives the extension an id of its own: `brulion-cli clip
#                                            install --extension-id <id>` allows it)
#
#   scripts/clipper-pack.sh [--out DIR] [--store] [--lint]
#
#   --lint   also web-ext lint the Firefox package (nixpkgs#web-ext through nix shell, else web-ext on
#            PATH); errors fail the script, warnings are printed
#
# One code base: each package gets its browser's manifest — Chromium's without Firefox's settings
# and background scripts, Firefox's without the key and the service worker. The archives are
# reproducible (sorted entries, fixed times). A release attaches the Chrome and Firefox packages
# (release.yml, the source job).
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/dist-clipper"
store=0
lint=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) out="$2"; shift 2 ;;
    --store) store=1; shift ;;
    --lint) lint=1; shift ;;
    -h | --help) awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; exit 0 ;;
    *) echo "clipper-pack: unknown option $1" >&2; exit 2 ;;
  esac
done

log() { printf '\033[1m[clipper-pack]\033[0m %s\n' "$*" >&2; }

if command -v python3 >/dev/null; then
  python=(python3)
else
  command -v nix >/dev/null || { log "python3 is needed"; exit 2; }
  python=(nix shell nixpkgs#python3 --command python3)
fi

version=$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+)[[:space:]]*$/\1/p' "$root/CMakeLists.txt" | head -n 1)
[[ -n $version ]] || { log "no VERSION in CMakeLists.txt"; exit 2; }
mkdir -p "$out"

"${python[@]}" - "$root/clipper" "$out" "$version" "$store" <<'PY'
import io, json, os, sys, zipfile

src, out, version, store = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4] == "1"
SKIP_DIRS = {"test"}
SKIP_FILES = {"README.md"}

def files():
    for folder, dirs, names in os.walk(src):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS and not d.startswith("."))
        rel = os.path.relpath(folder, src)
        for name in sorted(names):
            if (rel == "." and name in SKIP_FILES) or name.startswith(".") or name == "manifest.json" and rel != ".":
                continue
            path = name if rel == "." else os.path.join(rel, name).replace(os.sep, "/")
            if path == "manifest.json":
                continue
            yield path

with open(os.path.join(src, "manifest.json"), encoding="utf-8") as f:
    base = json.load(f)
base["version"] = version

def chromium(with_key):
    m = json.loads(json.dumps(base))
    m.pop("browser_specific_settings", None)
    m["background"] = {"service_worker": m["background"]["service_worker"], "type": "module"}
    if not with_key:
        m.pop("key", None)
    return m

def firefox():
    m = json.loads(json.dumps(base))
    m.pop("key", None)
    m.pop("minimum_chrome_version", None)
    m["background"] = {"scripts": m["background"]["scripts"], "type": "module"}
    return m

def write(name, manifest, folder):
    path = os.path.join(out, name)
    staged = os.path.join(out, folder)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        def add(arc, data):
            info = zipfile.ZipInfo(arc, date_time=(2026, 1, 1, 0, 0, 0))
            info.external_attr = 0o644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
            target = os.path.join(staged, arc)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with open(target, "wb") as t:
                t.write(data)
        add("manifest.json", (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))
        for rel in files():
            with open(os.path.join(src, rel), "rb") as f:
                add(rel, f.read())
    print(path)

write(f"brulion-clipper-{version}-chrome.zip", chromium(True), "chrome")
write(f"brulion-clipper-{version}-firefox.xpi", firefox(), "firefox")
if store:
    write(f"brulion-clipper-{version}-chrome-store.zip", chromium(False), "chrome-store")
PY

if ((lint)); then
  log "web-ext lint (Firefox)"
  if command -v web-ext >/dev/null; then
    web-ext lint --source-dir "$out/firefox" --self-hosted
  else
    nix shell nixpkgs#web-ext --command web-ext lint --source-dir "$out/firefox" --self-hosted
  fi
fi
log "done: $out"
