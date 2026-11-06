#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Builds Brulion's own F-Droid repository: the site GitHub Pages serves at
# https://<owner>.github.io/<repo>/ (docs/ANDROID.md, "Brulion's own F-Droid repository"):
#   OUT/index.html  the landing page (packaging/fdroid-repo/index.html): how to add the repository,
#                   its QR code and the fingerprint of its key
#   OUT/repo/       the repository (`fdroid update`): the APKs of the last releases, the signed
#                   index (index-v1.jar, index-v2.json + entry.jar), icon, screenshots and texts
#                   (fastlane/metadata/android, packaging/fdroid-repo/io.github.januszbit.brulion.yml)
# The APKs are the releases' own, signed with the release key: an app installed from a GitHub
# release updates from the repository and the other way round. The "added" dates of versions
# already in the published repository are kept (its index-v2.json is read first).
#
# Usage: scripts/fdroid-repo.sh [-o OUT] [-n N] [-a APK_DIR] [-u URL]
#   -o OUT      output directory, replaced [dist-fdroid]
#   -n N        the APKs of the last N published releases (no drafts, no pre-releases) [3]
#   -a APK_DIR  the APKs from this directory instead of the releases (local runs)
#   -u URL      the site's address, ending in / [https://<owner>.github.io/<repo>/ of GH_REPO]
# Env: the repository's signing key (scripts/fdroid-new-keystore.sh): FDROID_KEYSTORE (a file) or
#      FDROID_KEYSTORE_BASE64, FDROID_KEYSTORE_PASS, FDROID_KEY_ALIAS [brulion-fdroid];
#      GH_REPO [janusz-bit/brulion]; GH_TOKEN for gh (downloads).
# Needs fdroidserver (`fdroid`, 2.4), keytool (JDK 17+), apksigner (on PATH or in the Android SDK's
# build-tools of $ANDROID_HOME), curl, and gh unless -a is given. On NixOS:
#   nix shell nixpkgs#fdroidserver nixpkgs#jdk21_headless nixpkgs#apksigner nixpkgs#gh -c scripts/fdroid-repo.sh
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
APP_ID=io.github.januszbit.brulion

die() {
  printf 'fdroid-repo: error: %s\n' "$*" >&2
  exit 1
}
log() { printf '\033[1m[fdroid-repo]\033[0m %s\n' "$*" >&2; }

out=dist-fdroid releases=3 apk_dir='' url=''
while getopts 'o:n:a:u:h' opt; do
  case $opt in
    o) out=$OPTARG ;;
    n) releases=$OPTARG ;;
    a) apk_dir=$OPTARG ;;
    u) url=$OPTARG ;;
    h)
      awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "${BASH_SOURCE[0]}"
      exit 0
      ;;
    *) exit 2 ;;
  esac
done
shift $((OPTIND - 1))
(($# == 0)) || die "unexpected argument: $1"
[[ $releases =~ ^[1-9][0-9]*$ ]] || die "-n needs a positive number"

GH_REPO=${GH_REPO:-janusz-bit/brulion}
export GH_REPO
if [[ -z $url ]]; then
  owner=${GH_REPO%%/*}
  url="https://${owner,,}.github.io/${GH_REPO#*/}/"
fi
[[ $url =~ ^https://[^/]+/(.*/)?$ ]] || die "the site's address must be https://… and end in /: $url"
repo_url=${url}repo
alias=${FDROID_KEY_ALIAS:-brulion-fdroid}

for tool in fdroid keytool curl; do
  command -v "$tool" >/dev/null || die "$tool not found (see --help)"
done
[[ -n $apk_dir ]] || command -v gh >/dev/null || die "gh not found (or give the APKs with -a)"
[[ -n ${FDROID_KEYSTORE_PASS:-} ]] || die "FDROID_KEYSTORE_PASS is not set (scripts/fdroid-new-keystore.sh)"

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/repo/icons" "$work/metadata"

# The signing key: a file of its own in the working directory, readable by us only.
(
  umask 077
  if [[ -n ${FDROID_KEYSTORE:-} ]]; then
    cp -- "$FDROID_KEYSTORE" "$work/keystore.p12"
  elif [[ -n ${FDROID_KEYSTORE_BASE64:-} ]]; then
    base64 -d <<<"$FDROID_KEYSTORE_BASE64" >"$work/keystore.p12"
  else
    die "neither FDROID_KEYSTORE nor FDROID_KEYSTORE_BASE64 is set (scripts/fdroid-new-keystore.sh)"
  fi
)
fingerprint=$(keytool -exportcert -keystore "$work/keystore.p12" -storetype PKCS12 -alias "$alias" \
  -storepass:env FDROID_KEYSTORE_PASS | sha256sum | cut -d' ' -f1 | tr a-f A-F) ||
  die "cannot read the key $alias from the keystore (wrong password or alias?)"
[[ $fingerprint =~ ^[0-9A-F]{64}$ ]] || die "no certificate for $alias in the keystore"

# fdroid reads the passwords from the environment ({env: …}); config.yml must not be readable by
# others.
(
  umask 077
  cat >"$work/config.yml" <<EOF
repo_url: $repo_url
repo_name: Brulion
repo_description: >-
  Official releases of Brulion (offline Markdown notes) from its author, the same as on GitHub.
  Oficjalne wydania Brulionu (notatki w Markdown offline) prosto od autora, te same co na GitHubie.
repo_icon: icon.png
archive_older: 0
keystore: $work/keystore.p12
keystorepass: {env: FDROID_KEYSTORE_PASS}
keypass: {env: FDROID_KEYSTORE_PASS}
repo_keyalias: $alias
EOF
)

# The APKs.
if [[ -n $apk_dir ]]; then
  shopt -s nullglob
  apks=("$apk_dir"/*.apk)
  shopt -u nullglob
  ((${#apks[@]})) || die "no .apk in $apk_dir"
  cp -- "${apks[@]}" "$work/repo/"
else
  mapfile -t tags < <(gh release list --exclude-drafts --exclude-pre-releases --limit "$releases" \
    --json tagName --jq '.[].tagName' | sed '/^[[:space:]]*$/d')
  ((${#tags[@]})) || die "no published release in $GH_REPO"
  for tag in "${tags[@]}"; do
    log "APK of $tag"
    gh release download "$tag" --pattern 'brulion-*-arm64-v8a.apk' --dir "$work/repo" ||
      die "no APK in the release $tag"
  done
fi

# Texts, icon and screenshots: fdroid reads the fastlane layout from metadata/<app id>/<locale>/.
# A version gets its changelog (changelogs/<versionCode>.txt) only through a build entry of its
# own, so every APK gets one (versionCode = major*1000000 + minor*1000 + patch, from the file name
# brulion-X.Y.Z-arm64-v8a.apk as scripts/android-build.sh writes it); the newest is the current one.
builds=$(cd "$work/repo" && for f in *.apk; do
  [[ $f =~ ^brulion-([0-9]+)\.([0-9]+)\.([0-9]+)-arm64-v8a\.apk$ ]] ||
    die "unexpected APK name $f (brulion-X.Y.Z-arm64-v8a.apk)"
  printf '%d %s\n' $((BASH_REMATCH[1] * 1000000 + BASH_REMATCH[2] * 1000 + BASH_REMATCH[3])) \
    "${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.${BASH_REMATCH[3]}"
done | sort -n)
{
  cat "$ROOT/packaging/fdroid-repo/$APP_ID.yml"
  echo
  echo "Builds:"
  while read -r code name; do
    printf '  - versionName: %s\n    versionCode: %s\n    commit: v%s\n\n' "$name" "$code" "$name"
  done <<<"$builds"
  read -r code name <<<"$(tail -n 1 <<<"$builds")"
  printf 'CurrentVersion: %s\nCurrentVersionCode: %s\n' "$name" "$code"
} >"$work/metadata/$APP_ID.yml"
cp -r -- "$ROOT/fastlane/metadata/android" "$work/metadata/$APP_ID"
# The repository's icon: fdroidserver 2.4.5 (PyPI) reads `repo_icon` as a path from the working
# directory and copies it into repo/icons/ (a missing one becomes a QR code placeholder); newer
# ones look in repo/icons/ first. Both find it this way.
cp -- "$ROOT/fastlane/metadata/android/en-US/images/icon.png" "$work/icon.png"
cp -- "$work/icon.png" "$work/repo/icons/icon.png"

# The published index keeps the "added" dates of the versions already there (none yet: a new
# repository, every version added now).
if curl -fsSL --retry 3 -o "$work/repo/index-v2.json" "$repo_url/index-v2.json"; then
  log "the published index read ($repo_url/index-v2.json)"
else
  rm -f -- "$work/repo/index-v2.json"
  log "no published index at $repo_url (a new repository)"
fi

log "fdroid update ($(fdroid --version 2>/dev/null | head -n 1))"
(cd "$work" && fdroid update --pretty)
for f in index-v1.jar index-v2.json entry.jar index.png; do
  [[ -s $work/repo/$f ]] || die "fdroid update wrote no repo/$f"
done

# The site.
rm -rf -- "$out"
mkdir -p -- "$out"
cp -r -- "$work/repo" "$out/repo"
rm -rf -- "$out/repo/status"
cp -- "$ROOT/assets/logo.svg" "$out/logo.svg"
touch "$out/.nojekyll"
versions=$(cd "$out/repo" && for f in *.apk; do printf '%s ' "$f"; done)
sed -e "s|@REPO_URL@|$repo_url|g" -e "s|@FINGERPRINT@|$fingerprint|g" \
  -e "s|@ADD_URL@|fdroidrepos://${repo_url#https://}?fingerprint=$fingerprint|g" \
  -e "s|@SOURCE_URL@|https://github.com/$GH_REPO|g" \
  "$ROOT/packaging/fdroid-repo/index.html" >"$out/index.html"
grep -q '@[A-Z_]*@' "$out/index.html" && die "unreplaced placeholder in index.html"

log "done: $out ($(du -sh -- "$out" | cut -f1); $versions)"
echo "repository: $repo_url?fingerprint=$fingerprint"
