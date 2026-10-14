#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Create the release keystore for the Brulion Android app OUTSIDE the repository and print the
# `gh secret set` commands that hand it to GitHub Actions (android.yml / release.yml).
# Nothing secret is written into the repository or passed on a command line.
#
# Usage: scripts/new-android-keystore.sh [KEYSTORE_PATH]
#   KEYSTORE_PATH  default: ${XDG_DATA_HOME:-~/.local/share}/brulion/brulion-release.jks
# Env:   BRULION_KEY_ALIAS (default: brulion), GH_REPO (default: from `git remote origin`),
#        BRULION_KEYSTORE_PASSWORD (default: asked for, or generated when left empty)
#
# Keep a backup of the keystore and its password (e.g. in a password manager): every future
# update of the app must be signed with this key, Android refuses updates signed with another.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
DEFAULT_KS=${XDG_DATA_HOME:-$HOME/.local/share}/brulion/brulion-release.jks
KS=${1:-$DEFAULT_KS}
ALIAS=${BRULION_KEY_ALIAS:-brulion}

die() { printf 'error: %s\n' "$*" >&2; exit 1; }

case ${1:-} in -h | --help)
  awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "${BASH_SOURCE[0]}"
  exit 0
  ;;
esac

KS=$(realpath -m -- "$KS")
REPO_REAL=$(realpath -- "$ROOT")
case $KS in "$REPO_REAL"/*) die "the keystore must live outside the repository ($REPO_REAL)" ;; esac
[[ -e $KS ]] && die "$KS already exists; refusing to overwrite a signing key"

if command -v keytool >/dev/null; then
  keytool=(keytool)
elif [[ -n ${JAVA_HOME:-} && -x $JAVA_HOME/bin/keytool ]]; then
  keytool=("$JAVA_HOME/bin/keytool")
elif command -v nix >/dev/null; then
  keytool=(nix shell nixpkgs#jdk17_headless -c keytool)
else
  die "keytool not found: install a JDK (17+)"
fi

if [[ -z ${BRULION_KEYSTORE_PASSWORD:-} ]]; then
  if [[ -t 0 ]]; then
    read -r -s -p "Keystore password (Enter = generate a random one): " BRULION_KEYSTORE_PASSWORD
    echo
    if [[ -n $BRULION_KEYSTORE_PASSWORD ]]; then
      read -r -s -p "Repeat the password: " again
      echo
      [[ $again == "$BRULION_KEYSTORE_PASSWORD" ]] || die "the passwords differ"
    fi
  fi
  if [[ -z ${BRULION_KEYSTORE_PASSWORD:-} ]]; then
    BRULION_KEYSTORE_PASSWORD=$(od -An -tx1 -N18 /dev/urandom | tr -d ' \n')
    generated=1
  fi
fi
((${#BRULION_KEYSTORE_PASSWORD} >= 8)) || die "use a password of at least 8 characters"
export BRULION_KEYSTORE_PASSWORD

mkdir -p -- "$(dirname -- "$KS")"
(
  umask 077
  # PKCS12: the key password is the store password (keytool ignores a separate one).
  "${keytool[@]}" -genkeypair -v -noprompt \
    -keystore "$KS" -storetype PKCS12 -alias "$ALIAS" \
    -keyalg RSA -keysize 4096 -validity 10000 \
    -dname "CN=Brulion, O=Brulion" \
    -storepass:env BRULION_KEYSTORE_PASSWORD -keypass:env BRULION_KEYSTORE_PASSWORD
)
chmod 600 "$KS"

repo=${GH_REPO:-}
if [[ -z $repo ]]; then
  remote=$(git -C "$ROOT" remote get-url origin 2>/dev/null || true)
  repo=$(sed -nE 's#^(https://|ssh://git@|git@)github\.com[:/]([^/]+/[^/]+)$#\2#p' <<<"$remote")
  repo=${repo%.git}
fi
repo=${repo:-janusz-bit/brulion}

echo
echo "Keystore: $KS (alias: $ALIAS)"
"${keytool[@]}" -list -v -keystore "$KS" -alias "$ALIAS" -storepass:env BRULION_KEYSTORE_PASSWORD |
  grep -E '^\s*SHA256:' | sed 's/^\s*/Certificate /' || true
if [[ ${generated:-0} == 1 ]]; then
  echo
  echo "Generated password (store it in your password manager NOW, it is not saved anywhere):"
  echo "  $BRULION_KEYSTORE_PASSWORD"
fi

cat <<EOF

Back up the keystore file and the password: every update of the app must be signed with them.

Hand them to GitHub Actions (secrets of $repo); gh asks for the password interactively:

  base64 -w0 '$KS' | gh secret set ANDROID_KEYSTORE_BASE64 --repo $repo
  gh secret set ANDROID_KEYSTORE_PASSWORD --repo $repo
  gh secret set ANDROID_KEY_PASSWORD --repo $repo    # the same password (PKCS12)
  gh secret set ANDROID_KEY_ALIAS --repo $repo --body '$ALIAS'

Local signed build (the password stays out of the shell history):

  read -r -s -p 'Keystore password: ' ANDROID_KEYSTORE_PASSWORD; echo; export ANDROID_KEYSTORE_PASSWORD
  ANDROID_KEYSTORE_FILE='$KS' ANDROID_KEY_ALIAS='$ALIAS' scripts/android-build.sh
EOF
