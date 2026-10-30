#!/usr/bin/env bash
# Browser tests of the web build (tests/web: Playwright, Chromium + Firefox) — pictures from the
# internet (CORS, mixed content, no preflight, the export with copies) and the pen's pressure in
# the drawing editor; docs/WEB.md "Testy w przeglądarkach". GitHub Actions runs it (wasm.yml, job
# "browsers"), and so can anyone with a built site:
#
#   scripts/web-test.sh [--dist DIR] [--install] [-- <playwright test arguments>]
#
#   --dist DIR   the built site (default dist-wasm/, scripts/wasm-build.sh)
#   --install    npm ci and Playwright's Chromium and Firefox (on Ubuntu/Debian with sudo also
#                their system libraries)
#   -- …         passed to `playwright test` (e.g. -- --project=firefox stylus.spec.mjs)
#
# Firefox draws WebGL 2 only with an X display: without $DISPLAY the run goes under xvfb-run.
# NixOS: Node, openssl and xvfb-run come from nixpkgs (a `nix shell`); Playwright's own browser
# builds are prebuilt binaries that run through nix-ld (programs.nix-ld) with their libraries from
# nixpkgs (NIX_LD_LIBRARY_PATH). The report: tests/web/playwright-report/
# (`npx playwright show-report`), traces and screenshots of failures: tests/web/test-results/.
# The exit code is Playwright's.
set -euo pipefail
script=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")
root=$(cd "$(dirname "$script")/.." && pwd)

dist="$root/dist-wasm"
install=0
pw_args=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dist) dist="$2"; shift 2 ;;
    --install) install=1; shift ;;
    --) shift; pw_args=("$@"); break ;;
    -h | --help) awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$script"; exit 0 ;;
    *) echo "web-test: unknown option $1" >&2; exit 2 ;;
  esac
done

log() { printf '\033[1m[web-test]\033[0m %s\n' "$*" >&2; }

if [[ -e /etc/NIXOS && -z ${BRULION_WEB_IN_NIX_SHELL:-} ]]; then
  [[ -e /lib64/ld-linux-x86-64.so.2 ]] ||
    { log "NixOS without nix-ld: Playwright's browsers are prebuilt binaries (enable programs.nix-ld)"; exit 2; }
  log "NixOS: entering nix shell (nodejs_22, openssl, xvfb-run) with the browsers' libraries for nix-ld"
  # What Playwright's Chromium and Firefox link (its Ubuntu dependency list).
  libs=$(nix build --no-link --print-out-paths nixpkgs#nss.out nixpkgs#nspr.out nixpkgs#at-spi2-core.out \
    nixpkgs#libxcomposite.out nixpkgs#libxdamage.out nixpkgs#libxrandr.out nixpkgs#libxfixes.out \
    nixpkgs#libxext.out nixpkgs#libx11.out nixpkgs#libxcb.out nixpkgs#libxcursor.out nixpkgs#libxi.out \
    nixpkgs#libxtst.out nixpkgs#libxrender.out nixpkgs#libxkbcommon.out nixpkgs#libgbm.out nixpkgs#alsa-lib.out \
    nixpkgs#cups.lib nixpkgs#dbus.lib nixpkgs#expat.out nixpkgs#glib.out nixpkgs#pango.out nixpkgs#cairo.out \
    nixpkgs#libdrm.out nixpkgs#systemd.out nixpkgs#gtk3.out nixpkgs#gdk-pixbuf.out nixpkgs#harfbuzz.out \
    nixpkgs#freetype.out nixpkgs#fontconfig.lib nixpkgs#zlib.out nixpkgs#libffi.out nixpkgs#dbus-glib.out \
    nixpkgs#stdenv.cc.cc.lib nixpkgs#libGL.out nixpkgs#mesa.out) ||
    { log "nix build of the browsers' libraries failed"; exit 2; }
  ld_path=
  for p in $libs; do ld_path+="$p/lib:"; done
  # The system's OpenGL drivers (Firefox's WebGL 2 under Xvfb: Mesa's llvmpipe).
  [[ -d /run/opengl-driver/lib ]] && ld_path+="/run/opengl-driver/lib:"
  export NIX_LD_LIBRARY_PATH="${ld_path}${NIX_LD_LIBRARY_PATH:-/run/current-system/sw/share/nix-ld/lib}"
  # Playwright checks the libraries with ldd, which does not see nix-ld's.
  export PLAYWRIGHT_SKIP_VALIDATE_HOST_REQUIREMENTS=1
  if [[ -d /run/opengl-driver/share/glvnd/egl_vendor.d ]]; then
    export __EGL_VENDOR_LIBRARY_DIRS=/run/opengl-driver/share/glvnd/egl_vendor.d
  fi
  export BRULION_WEB_IN_NIX_SHELL=1
  again=(--dist "$dist")
  if ((install)); then again+=(--install); fi
  exec nix shell nixpkgs#nodejs_22 nixpkgs#openssl nixpkgs#xvfb-run --command bash "$script" "${again[@]}" \
    -- ${pw_args[@]+"${pw_args[@]}"}
fi

dist=$(cd "$dist" 2>/dev/null && pwd) || { log "no site in $dist (scripts/wasm-build.sh)"; exit 2; }
[[ -f "$dist/index.html" ]] || { log "$dist/index.html missing (scripts/wasm-build.sh)"; exit 2; }
export BRULION_DIST="$dist"
command -v node >/dev/null || { log "Node.js 22 or newer is needed"; exit 2; }
command -v openssl >/dev/null || { log "openssl is needed (the test certificate)"; exit 2; }

cd "$root/tests/web"
run=()

if ((install)); then
  log "npm ci"
  npm ci --no-audit --no-fund
  deps=()
  if [[ ! -e /etc/NIXOS ]] && command -v apt-get >/dev/null && sudo -n true 2>/dev/null; then
    deps=(--with-deps)
  fi
  log "installing Playwright's Chromium and Firefox ${deps[*]:-}"
  npx playwright install ${deps[@]+"${deps[@]}"} chromium firefox
fi
[[ -d node_modules/@playwright/test ]] || { log "tests/web/node_modules missing: run with --install"; exit 2; }

if [[ -z ${DISPLAY:-} ]]; then
  command -v xvfb-run >/dev/null || { log "no \$DISPLAY and no xvfb-run (Firefox needs an X display for WebGL 2)"; exit 2; }
  log "running under xvfb-run (Firefox: WebGL 2 needs an X display)"
  run=(xvfb-run --auto-servernum --server-args='-screen 0 1920x1200x24' ${run[@]+"${run[@]}"})
fi

status=0
${run[@]+"${run[@]}"} npx playwright test ${pw_args[@]+"${pw_args[@]}"} || status=$?
log "report: $root/tests/web/playwright-report/index.html (exit code $status)"
exit "$status"
