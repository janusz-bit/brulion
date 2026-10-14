#!/usr/bin/env bash
# Playwright e2e / screenshot suite (see e2e/README.md).
#
# Playwright's downloaded browsers don't run on NixOS, so Chromium comes from nixpkgs (binary
# cache) and is handed to playwright.config.ts via CHROMIUM_PATH. All arguments are passed to
# `playwright test`, e.g.:
#   scripts/e2e.sh                          # everything, both projects
#   scripts/e2e.sh --grep @shots            # only the screenshot tests (npm run e2e:shots)
#   scripts/e2e.sh --project=desktop e2e/gallery.spec.ts
#   E2E_SKIP_BUILD=1 scripts/e2e.sh         # reuse the last build in e2e/.dist
# Set CHROMIUM_PATH yourself to skip the nix lookup; E2E_ALLOW_CHROMIUM_BUILD=1 allows nix to
# compile Chromium if it isn't in the binary cache (hours — normally you don't want that).
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

# Only one Chromium run at a time on this machine (low RAM; several agents share it).
# Re-exec under an exclusive lock unless we already hold it.
if [[ -z "${BRULION_E2E_LOCKED:-}" ]] && command -v flock >/dev/null 2>&1; then
  export BRULION_E2E_LOCKED=1
  exec flock "${BRULION_E2E_LOCK:-${TMPDIR:-/tmp}/brulion-e2e.lock}" "$0" "$@"
fi

if [[ -z "${CHROMIUM_PATH:-}" ]]; then
  if command -v nix >/dev/null 2>&1; then
    dry_run="$(nix build --dry-run nixpkgs#chromium 2>&1 || true)"
    if [[ -z "${E2E_ALLOW_CHROMIUM_BUILD:-}" ]] && grep -q 'will be built' <<<"${dry_run}"; then
      echo "e2e: nixpkgs#chromium is not in the binary cache and would be compiled locally." >&2
      echo "e2e: set CHROMIUM_PATH to an existing Chromium, or E2E_ALLOW_CHROMIUM_BUILD=1." >&2
      exit 1
    fi
    chromium_out="$(nix build --no-link --print-out-paths nixpkgs#chromium)"
    CHROMIUM_PATH="${chromium_out}/bin/chromium"
  elif command -v chromium >/dev/null 2>&1; then
    CHROMIUM_PATH="$(command -v chromium)"
  fi
fi

if [[ -n "${CHROMIUM_PATH:-}" ]]; then
  if [[ ! -x "${CHROMIUM_PATH}" ]]; then
    echo "e2e: CHROMIUM_PATH=${CHROMIUM_PATH} is not executable" >&2
    exit 1
  fi
  export CHROMIUM_PATH
  echo "e2e: chromium ${CHROMIUM_PATH}" >&2
else
  echo "e2e: no nix and no chromium on PATH — falling back to Playwright's bundled browser" >&2
fi

exec npx playwright test "$@"
