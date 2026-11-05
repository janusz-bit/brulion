#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Creates the signing key of Brulion's own F-Droid repository (its index, not the APKs: those keep
# the release key of scripts/android-new-keystore.sh) OUTSIDE the repository, and prints the
# `gh secret set` commands that hand it to GitHub Actions (.github/workflows/fdroid.yml,
# scripts/fdroid-repo.sh) and the fingerprint users add the repository with.
# Nothing secret is written into the repository or passed on a command line.
#
# Usage: scripts/fdroid-new-keystore.sh [KEYSTORE_PATH]
#   KEYSTORE_PATH  default: ${XDG_DATA_HOME:-~/.local/share}/brulion/brulion-fdroid-repo.p12
# Env:   FDROID_KEY_ALIAS (default: brulion-fdroid), GH_REPO (default: from `git remote origin`),
#        FDROID_KEYSTORE_PASS (default: asked for, or generated when left empty)
#
# Keep a backup of the keystore and its password (e.g. in a password manager): F-Droid accepts the
# repository's index only when it is signed with this key; a new key means every user has to
# remove the repository and add it again.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
DEFAULT_KS=${XDG_DATA_HOME:-$HOME/.local/share}/brulion/brulion-fdroid-repo.p12
KS=${1:-$DEFAULT_KS}
ALIAS=${FDROID_KEY_ALIAS:-brulion-fdroid}

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

if [[ -z ${FDROID_KEYSTORE_PASS:-} ]]; then
  if [[ -t 0 ]]; then
    read -r -s -p "Keystore password (Enter = generate a random one): " FDROID_KEYSTORE_PASS
    echo
    if [[ -n $FDROID_KEYSTORE_PASS ]]; then
      read -r -s -p "Repeat the password: " again
      echo
      [[ $again == "$FDROID_KEYSTORE_PASS" ]] || die "the passwords differ"
    fi
  fi
  if [[ -z ${FDROID_KEYSTORE_PASS:-} ]]; then
    FDROID_KEYSTORE_PASS=$(od -An -tx1 -N18 /dev/urandom | tr -d ' \n')
    generated=1
  fi
fi
((${#FDROID_KEYSTORE_PASS} >= 8)) || die "use a password of at least 8 characters"
export FDROID_KEYSTORE_PASS

mkdir -p -- "$(dirname -- "$KS")"
(
  umask 077
  # PKCS12: the key password is the store password (keytool ignores a separate one).
  "${keytool[@]}" -genkeypair -noprompt \
    -keystore "$KS" -storetype PKCS12 -alias "$ALIAS" \
    -keyalg RSA -keysize 4096 -sigalg SHA256withRSA -validity 10000 \
    -dname "CN=Brulion F-Droid repository, O=Brulion" \
    -storepass:env FDROID_KEYSTORE_PASS -keypass:env FDROID_KEYSTORE_PASS
)
chmod 600 "$KS"

repo=${GH_REPO:-}
if [[ -z $repo ]]; then
  remote=$(git -C "$ROOT" remote get-url origin 2>/dev/null || true)
  repo=$(sed -nE 's#^(https://|ssh://git@|git@)github\.com[:/]([^/]+/[^/]+)$#\2#p' <<<"$remote")
  repo=${repo%.git}
fi
repo=${repo:-janusz-bit/brulion}

# The fingerprint F-Droid shows and checks: SHA-256 of the certificate (DER).
fingerprint=$("${keytool[@]}" -exportcert -keystore "$KS" -storetype PKCS12 -alias "$ALIAS" \
  -storepass:env FDROID_KEYSTORE_PASS | sha256sum | cut -d' ' -f1 | tr a-f A-F)

echo
echo "Keystore: $KS (alias: $ALIAS)"
echo "Repository fingerprint: $fingerprint"
if [[ ${generated:-0} == 1 ]]; then
  echo
  echo "Generated password (store it in your password manager NOW, it is not saved anywhere):"
  echo "  $FDROID_KEYSTORE_PASS"
fi

cat <<EOF

Back up the keystore file and the password: F-Droid accepts the repository only signed with them.

Hand them to GitHub Actions (secrets of $repo); gh asks for the password interactively:

  base64 -w0 '$KS' | gh secret set FDROID_KEYSTORE_BASE64 --repo $repo
  gh secret set FDROID_KEYSTORE_PASS --repo $repo
  gh secret set FDROID_KEY_ALIAS --repo $repo --body '$ALIAS'

Then publish the repository (it is also rebuilt after every release):

  gh workflow run fdroid.yml --repo $repo
EOF
