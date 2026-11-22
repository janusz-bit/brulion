#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Creates the release keystore of the Brulion Android app OUTSIDE the repository and prints the
# `gh secret set` commands that hand it to GitHub Actions (.github/workflows/android.yml) and
# the environment for a local signed build (scripts/android-build.sh).
# Nothing secret is written into the repository or passed on a command line.
#
# Usage: scripts/android-new-keystore.sh [--play] [KEYSTORE_PATH]
#   --play         the upload key of the app for Google Play instead (docs/GOOGLE_PLAY.md "Play App
#                  Signing and the upload key"): default path brulion-play-upload.jks, alias
#                  brulion-upload, the secrets PLAY_UPLOAD_KEYSTORE_* (.github/workflows/release.yml)
#   KEYSTORE_PATH  default: ${XDG_DATA_HOME:-~/.local/share}/brulion/brulion-release.jks
# Env:   BRULION_KEY_ALIAS (default: brulion, with --play brulion-upload), GH_REPO (default: from
#        `git remote origin`), BRULION_KEYSTORE_PASSWORD (default: asked for, or generated when
#        left empty)
#
# Keep a backup of the keystore and its password (e.g. in a password manager): every future
# update of the app must be signed with this key, Android refuses updates signed with another.
# The upload key (--play) is less final: Google Play signs what it delivers with its own app
# signing key, and a lost upload key can be replaced through Play Console (a new one registered).
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
PLAY=0
if [[ ${1:-} == --play ]]; then
  PLAY=1
  shift
fi
if ((PLAY)); then
  DEFAULT_KS=${XDG_DATA_HOME:-$HOME/.local/share}/brulion/brulion-play-upload.jks
  ALIAS=${BRULION_KEY_ALIAS:-brulion-upload}
  SECRET=PLAY_UPLOAD_KEYSTORE
  CN="Brulion upload key"
else
  DEFAULT_KS=${XDG_DATA_HOME:-$HOME/.local/share}/brulion/brulion-release.jks
  ALIAS=${BRULION_KEY_ALIAS:-brulion}
  SECRET=QT_ANDROID_KEYSTORE
  CN=Brulion
fi
KS=${1:-$DEFAULT_KS}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

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
  keytool=(nix shell nixpkgs#jdk21_headless --command keytool)
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
    -dname "CN=$CN, O=Brulion" \
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

if ((PLAY)); then
  cat <<EOF

Back up the keystore file and the password. Google Play takes the key of the first AAB you upload
as the upload key (Play Console: Test and release -> App integrity -> Play app signing); a lost
upload key is replaced through Play Console ("Request upload key reset", docs/GOOGLE_PLAY.md).
The upload certificate, if Play Console asks for one:

  keytool -export -rfc -keystore '$KS' -alias '$ALIAS' -file upload_certificate.pem
EOF
else
  cat <<EOF

Back up the keystore file and the password: every update of the app must be signed with them.
EOF
fi
build_env=
((PLAY)) && build_env='BRULION_ANDROID_PLAY=1 '
cat <<EOF

Hand them to GitHub Actions (secrets of $repo); gh asks for the password interactively:

  base64 -w0 '$KS' | gh secret set ${SECRET}_BASE64 --repo $repo
  gh secret set ${SECRET}_STORE_PASS --repo $repo
  gh secret set ${SECRET}_KEY_PASS --repo $repo    # the same password (PKCS12)
  gh secret set ${SECRET}_ALIAS --repo $repo --body '$ALIAS'

Local signed build (the password stays out of the shell history):

  read -r -s -p 'Keystore password: ' QT_ANDROID_KEYSTORE_STORE_PASS; echo; export QT_ANDROID_KEYSTORE_STORE_PASS
  ${build_env}QT_ANDROID_KEYSTORE_PATH='$KS' QT_ANDROID_KEYSTORE_ALIAS='$ALIAS' scripts/android-build.sh
EOF
