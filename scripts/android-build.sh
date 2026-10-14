#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Build a signed, universal Brulion APK (optionally also an AAB) with the Tauri CLI.
# Idempotent: safe to run again; the generated Android project (src-tauri/gen/android, not
# committed) is created on first use and patched in place.
#
# Usage: scripts/android-build.sh [extra `tauri android build` arguments, e.g. --config FILE]
#
# Requirements: Node + `npm ci`, Rust with the Android targets, JDK 17+, Android SDK and NDK.
#
# Environment:
#   ANDROID_HOME (or ANDROID_SDK_ROOT)      Android SDK (required)
#   NDK_HOME (or ANDROID_NDK_HOME)          Android NDK (default: newest $ANDROID_HOME/ndk/*)
#   ANDROID_KEYSTORE_BASE64                 release keystore, base64 (CI secret), or
#   ANDROID_KEYSTORE_FILE                   path to the release keystore (local builds)
#   ANDROID_KEYSTORE_PASSWORD               store password (required with a keystore)
#   ANDROID_KEY_ALIAS                       key alias (required with a keystore)
#   ANDROID_KEY_PASSWORD                    key password (default: the store password)
#     Without a keystore an existing src-tauri/gen/android/keystore.properties is reused, else a
#     throwaway key is generated (the APK installs, but cannot update a differently signed one).
#   BRULION_ANDROID_TARGETS                 subset of "aarch64 armv7 i686 x86_64" (default: all)
#   BRULION_ANDROID_AAB=1                   also build an Android App Bundle (.aab)
#
# Output: dist-android/brulion-<version>.apk (+ .aab). On GitHub Actions the paths are also
# written to $GITHUB_OUTPUT as `apk`, `aab` and `version`.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

GEN=src-tauri/gen/android
OUT_DIR=dist-android
KS_PROPS=$GEN/keystore.properties
THROWAWAY_KS=$GEN/brulion-throwaway.jks

info() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
warn() {
  # $1 = title, $2 = details (one line each, shown as a GitHub annotation in CI)
  if [[ -n ${GITHUB_ACTIONS:-} ]]; then echo "::warning title=$1::$2"; fi
  printf '\n\033[1;33m!!! WARNING: %s\033[0m\n\033[33m!!! %s\033[0m\n\n' "$1" "$2" >&2
}
die() {
  if [[ -n ${GITHUB_ACTIONS:-} ]]; then echo "::error::$*"; fi
  printf '\033[1;31merror:\033[0m %s\n' "$*" >&2
  exit 1
}
tauri() { npx --no tauri "$@"; }

case ${1:-} in -h | --help)
  awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "${BASH_SOURCE[0]}"
  exit 0
  ;;
esac

# --- prerequisites ---------------------------------------------------------------------------

command -v node >/dev/null || die "node is not installed"
command -v python3 >/dev/null || die "python3 is not installed"
if [[ ! -x node_modules/.bin/tauri ]]; then
  info "Installing npm dependencies"
  npm ci
fi

export ANDROID_HOME=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
[[ -n $ANDROID_HOME && -d $ANDROID_HOME ]] || die "set ANDROID_HOME to the Android SDK directory"

if [[ -z ${NDK_HOME:-} ]]; then
  if [[ -n ${ANDROID_NDK_HOME:-} ]]; then
    NDK_HOME=$ANDROID_NDK_HOME
  else
    NDK_HOME=$(find "$ANDROID_HOME/ndk" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sort -V | tail -n 1 || true)
  fi
fi
[[ -n ${NDK_HOME:-} && -f $NDK_HOME/source.properties ]] ||
  die "Android NDK not found: install one (sdkmanager \"ndk;27.2.12479018\") and set NDK_HOME"
export NDK_HOME

KEYTOOL=$(command -v keytool || true)
if [[ -z $KEYTOOL && -n ${JAVA_HOME:-} && -x $JAVA_HOME/bin/keytool ]]; then
  KEYTOOL=$JAVA_HOME/bin/keytool
fi

VERSION=$(node -p "require('./package.json').version")
info "Brulion $VERSION for Android (SDK $ANDROID_HOME, NDK $NDK_HOME)"

# --- 1. generated Android project ------------------------------------------------------------

if [[ ! -f $GEN/app/build.gradle.kts ]]; then
  info "Generating the Android project ($GEN)"
  init_args=(android init --ci)
  # Without rustup (e.g. a Nix toolchain) the Android targets must already be installed.
  command -v rustup >/dev/null || init_args+=(--skip-targets-install)
  tauri "${init_args[@]}"
else
  info "Reusing the existing Android project ($GEN)"
fi
[[ -f $GEN/app/build.gradle.kts ]] || die "tauri android init did not create $GEN/app/build.gradle.kts"

# The build scripts of Tauri plugins (tauri-plugin 2.7) copy the whole tauri-android library,
# including the build/ outputs Gradle writes into it, into each plugin. With the configuration
# cache Gradle runs tasks in parallel, so the Rust build (and those copies) can overlap the
# Kotlin compilation of that library and fail with "failed to copy tauri-api to the plugin
# project ... No such file or directory". One Gradle worker keeps the two apart; the Rust
# build itself still uses every core.
gradle_props=$GEN/gradle.properties
if ! grep -q '^org\.gradle\.workers\.max=' "$gradle_props" 2>/dev/null; then
  printf '\n# brulion (scripts/android-build.sh): no parallel tasks, see the tauri-api copy race there\norg.gradle.workers.max=1\n' >>"$gradle_props"
fi

# Missing SDK platforms and build-tools are downloaded by Gradle (Android Gradle Plugin) itself;
# that needs a writable SDK with accepted licences (`sdkmanager --licenses`).

# --- 2. launcher icons -----------------------------------------------------------------------

info "Generating launcher icons from assets-src/logo-1024.png"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# Same adaptive-icon background as the committed src-tauri/icons/android set.
icon_bg=$(sed -n 's:.*<color name="ic_launcher_background">\(#[0-9A-Fa-f]*\)</color>.*:\1:p' \
  src-tauri/icons/android/values/ic_launcher_background.xml 2>/dev/null || true)
# `-o` into a temp dir: `tauri icon` would otherwise also rewrite the committed desktop icons.
tauri icon assets-src/logo-1024.png --ios-color "${icon_bg:-#283045}" -o "$work/icons" >/dev/null
[[ -d $work/icons/android ]] || die "tauri icon did not produce Android icons"
cp -R "$work/icons/android/." "$GEN/app/src/main/res/"

# --- 3. release signing ----------------------------------------------------------------------

write_properties() { # <keystore> <alias>; passwords via ANDROID_KEYSTORE_PASSWORD/ANDROID_KEY_PASSWORD
  python3 scripts/android-signing.py properties --out "$KS_PROPS" --store-file "$1" --key-alias "$2"
}

info "Configuring release signing"
signing=release
if [[ -n ${ANDROID_KEYSTORE_BASE64:-} || -n ${ANDROID_KEYSTORE_FILE:-} ]]; then
  [[ -n ${ANDROID_KEYSTORE_PASSWORD:-} ]] || die "ANDROID_KEYSTORE_PASSWORD is required with a release keystore"
  [[ -n ${ANDROID_KEY_ALIAS:-} ]] || die "ANDROID_KEY_ALIAS is required with a release keystore"
  if [[ -n ${ANDROID_KEYSTORE_BASE64:-} ]]; then
    keystore=${RUNNER_TEMP:-$ROOT/$GEN}/brulion-release.jks
    (
      umask 077
      printf '%s' "$ANDROID_KEYSTORE_BASE64" | tr -d ' \t\r\n' | base64 --decode >"$keystore"
    ) || die "ANDROID_KEYSTORE_BASE64 is not valid base64"
  else
    [[ -f $ANDROID_KEYSTORE_FILE ]] || die "ANDROID_KEYSTORE_FILE=$ANDROID_KEYSTORE_FILE does not exist"
    keystore=$(cd -- "$(dirname -- "$ANDROID_KEYSTORE_FILE")" && pwd)/$(basename -- "$ANDROID_KEYSTORE_FILE")
  fi
  if [[ -n $KEYTOOL ]]; then # fail early (and readably) on a wrong password or alias
    if ! out=$("$KEYTOOL" -list -keystore "$keystore" -storepass:env ANDROID_KEYSTORE_PASSWORD \
      -alias "$ANDROID_KEY_ALIAS" 2>&1); then
      printf '%s\n' "$out" >&2
      die "cannot open key '$ANDROID_KEY_ALIAS' in the release keystore (wrong password or alias?)"
    fi
  fi
  write_properties "$keystore" "$ANDROID_KEY_ALIAS"
elif [[ -f $KS_PROPS ]]; then
  echo "Using the existing $KS_PROPS"
  if grep -q 'brulion-throwaway' "$KS_PROPS"; then signing=throwaway; fi
else
  [[ -n $KEYTOOL ]] || die "keytool (from a JDK) is needed to create a signing key"
  info "No release keystore given: generating a throwaway signing key"
  rm -f "$THROWAWAY_KS"
  ANDROID_KEYSTORE_PASSWORD=$(od -An -tx1 -N24 /dev/urandom | tr -d ' \n')
  export ANDROID_KEYSTORE_PASSWORD
  unset ANDROID_KEY_PASSWORD
  "$KEYTOOL" -genkeypair -noprompt -keystore "$THROWAWAY_KS" -storetype PKCS12 \
    -storepass:env ANDROID_KEYSTORE_PASSWORD -keypass:env ANDROID_KEYSTORE_PASSWORD \
    -alias brulion-throwaway -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=Brulion unofficial build, O=Throwaway key" >/dev/null 2>&1 ||
    die "keytool could not create $THROWAWAY_KS"
  write_properties "$ROOT/$THROWAWAY_KS" brulion-throwaway
  unset ANDROID_KEYSTORE_PASSWORD
  signing=throwaway
fi
if [[ $signing == throwaway ]]; then
  warn "APK signed with a THROWAWAY key" \
    "No release keystore (ANDROID_KEYSTORE_BASE64 etc.) was provided. The APK installs, but it cannot update a Brulion APK signed with another key (uninstall first, which deletes app-private data) and later builds cannot update it. See scripts/new-android-keystore.sh."
fi

python3 scripts/android-signing.py patch --gradle "$GEN/app/build.gradle.kts"

# --- 4. build --------------------------------------------------------------------------------

build_args=(android build --ci --apk)
[[ ${BRULION_ANDROID_AAB:-0} == 1 ]] && build_args+=(--aab)
read -r -a targets <<<"${BRULION_ANDROID_TARGETS:-}"
for target in "${targets[@]}"; do build_args+=(--target "$target"); done
build_args+=("$@")

rm -rf "$GEN/app/build/outputs" # never pick up an APK from an earlier run
info "tauri ${build_args[*]}"
tauri "${build_args[@]}"

# --- 5. collect the outputs ------------------------------------------------------------------

info "Build outputs"
find "$GEN/app/build/outputs" -type f \( -name '*.apk' -o -name '*.aab' \) -print | sort

apk=$GEN/app/build/outputs/apk/universal/release/app-universal-release.apk
if [[ ! -f $apk ]]; then
  if find "$GEN/app/build/outputs/apk" -name '*-unsigned.apk' 2>/dev/null | grep -q .; then
    die "Gradle produced an unsigned APK: the release signing config was not applied"
  fi
  die "universal release APK not found at $apk"
fi

apksigner=$(find "$ANDROID_HOME/build-tools" -mindepth 2 -maxdepth 2 -name apksigner 2>/dev/null |
  sort -V | tail -n 1 || true)
if [[ -n $apksigner ]]; then
  info "Verifying the APK signature"
  # Via bash: the wrapper script's #!/bin/bash does not exist everywhere (NixOS).
  certs=$(bash "$apksigner" verify --print-certs "$apk") || die "apksigner could not verify $apk"
  grep -E 'Signer #1 certificate (DN|SHA-256)' <<<"$certs" || printf '%s\n' "$certs"
else
  warn "apksigner not found" "Skipping the APK signature check (no build-tools in $ANDROID_HOME)"
fi

mkdir -p "$OUT_DIR"
final_apk=$OUT_DIR/brulion-$VERSION.apk
cp "$apk" "$final_apk"
final_aab=
if [[ ${BRULION_ANDROID_AAB:-0} == 1 ]]; then
  aab=$(find "$GEN/app/build/outputs/bundle" -name '*.aab' -path '*elease*' 2>/dev/null | sort | head -n 1 || true)
  [[ -n $aab ]] || die "AAB not found under $GEN/app/build/outputs/bundle"
  final_aab=$OUT_DIR/brulion-$VERSION.aab
  cp "$aab" "$final_aab"
fi

if [[ -n ${GITHUB_OUTPUT:-} ]]; then
  {
    echo "version=$VERSION"
    echo "apk=$final_apk"
    echo "aab=$final_aab"
    echo "signing=$signing"
  } >>"$GITHUB_OUTPUT"
fi

info "Done"
(cd "$OUT_DIR" && sha256sum -- "$(basename -- "$final_apk")" ${final_aab:+"$(basename -- "$final_aab")"})
echo "APK: $ROOT/$final_apk"
[[ -z $final_aab ]] || echo "AAB: $ROOT/$final_aab"
