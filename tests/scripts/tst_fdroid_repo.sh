#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tests scripts/fdroid-repo.sh (Brulion's own F-Droid repository, docs/ANDROID.md "Brulion's
# own F-Droid repository") with stand-ins for fdroid, keytool, gh, curl and unzip on PATH — what the
# script itself decides: the APKs of the last releases, fdroid's config (the site's address, the
# passwords only as {env: …}, readable by its owner only), a build entry per APK so each version
# gets its changelog (sorted, the newest current), the texts and icon from fastlane, the landing
# page (the release it offers for download: direct links to files the release has, their sizes and
# date; address, fingerprint = SHA-256 of the certificate, the "Add to F-Droid" link), the same
# release's browser version in app/ (its brulion-web zip unpacked, "Open in the browser") and what
# it refuses. The real `fdroid update` and unzip run in CI (.github/workflows/fdroid.yml).
#
# Usage: tests/scripts/tst_fdroid_repo.sh [path/to/fdroid-repo.sh]
set -euo pipefail

script=$(realpath -- "${1:-$(dirname -- "${BASH_SOURCE[0]}")/../../scripts/fdroid-repo.sh}")
[[ -f $script ]] || {
  echo "no $script" >&2
  exit 2
}
root=$(dirname -- "$(dirname -- "$script")")
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

failed=0
pass() { printf 'PASS   : %s\n' "$1"; }
fail() {
  printf 'FAIL!  : %s\n' "$1"
  failed=$((failed + 1))
}
expect() { # <description> <command...>: the command succeeds
  local what=$1
  shift
  if "$@"; then pass "$what"; else fail "$what"; fi
}

# --- stand-ins ------------------------------------------------------------------------------------
bin=$tmp/bin log=$tmp/log
mkdir -p "$bin" "$log"
{
  printf '#!%s\n' "$BASH"
  cat <<'EOF'
[[ $1 == --version ]] && { echo 2.4.5; exit 0; }
[[ $1 == update ]] || exit 9
cp config.yml metadata/io.github.januszbit.brulion.yml "$STUB_LOG/"
stat -c %a config.yml >"$STUB_LOG/config.mode"
ls metadata/io.github.januszbit.brulion >"$STUB_LOG/locales.txt"
ls repo >"$STUB_LOG/repo.txt"
cmp -s repo/icons/icon.png "$STUB_ICON" && cmp -s icon.png "$STUB_ICON" && touch "$STUB_LOG/icon"
[[ $FDROID_KEYSTORE_PASS == 'hasło-ząb 123' && -s keystore.p12 ]] && touch "$STUB_LOG/key"
for f in index-v1.jar index-v2.json entry.jar index.png; do echo signed >"repo/$f"; done
mkdir -p repo/status && echo '{}' >repo/status/update.json
EOF
} >"$bin/fdroid"
{
  printf '#!%s\n' "$BASH"
  cat <<'EOF'
# -exportcert … -alias <alias> -storepass:env FDROID_KEYSTORE_PASS: the certificate of that alias.
args=" $* "
[[ $args == *" -exportcert "* && $args == *" -storepass:env FDROID_KEYSTORE_PASS "* ]] || exit 3
[[ $args == *" -alias ${STUB_ALIAS:-brulion-fdroid} "* ]] || { echo "keytool error: alias" >&2; exit 1; }
printf 'certificate of Brulion'
EOF
} >"$bin/keytool"
{
  printf '#!%s\n' "$BASH"
  cat <<'EOF'
echo "gh $*" >>"$STUB_LOG/gh.txt"
case "$1 $2" in
  "release list") printf '%s\n' ${STUB_TAGS:-v0.10.0 v0.9.1 v0.9.0} ;;
  "release download")
    # <tag> --pattern <pattern> --dir <dir>
    tag=$3 pattern=$5 dir=${@: -1}
    if [[ $pattern == brulion-web-*.zip ]]; then
      # The browser version: a stand-in zip for the unzip below, one entry per line.
      [[ $tag == "$STUB_NO_WEB" ]] && exit 1
      [[ $pattern == "brulion-web-${tag#v}.zip" ]] || exit 8
      files=${STUB_WEB_FILES:-index.html sw.js brulion.js brulion.wasm qtloader.js manifest.webmanifest}
      {
        printf '%s\n' $files icons/logo.svg .nojekyll
        [[ -z ${STUB_WEB_LINK:-} ]] || echo "$STUB_WEB_LINK -> /etc/hostname"
      } >"$dir/$pattern"
      exit 0
    fi
    [[ $tag == "$STUB_NO_APK" ]] && exit 1
    echo "apk $tag" >"$dir/brulion-${tag#v}-arm64-v8a.apk" ;;
  "release view")
    # [tag] --json … --jq …: the first line "tag publishedAt isDraft isPrerelease", then "size name".
    tag=$3
    [[ $tag == --* ]] && tag=${STUB_LATEST:-v0.10.0}
    [[ $tag == "$STUB_NO_RELEASE" ]] && { echo "release not found" >&2; exit 1; }
    v=${tag#v}
    printf '%s %s %s %s\n' "$tag" "${STUB_PUBLISHED:-2026-11-07T19:13:26Z}" "${STUB_DRAFT:-false}" "${STUB_PRE:-false}"
    while read -r size name; do
      [[ $name == "$STUB_MISSING" ]] || echo "$size $name"
    done <<LIST
92839736 brulion-$v-arm64-v8a.apk
73214456 Brulion-$v-x86_64.AppImage
250481 Brulion-$v-x86_64.AppImage.zsync
75655688 Brulion-$v-aarch64.AppImage
41094914 Brulion-$v-windows-x86_64-setup.exe
60295774 brulion-$v-windows-x86_64.zip
23282235 brulion-web-$v.html
17467109 brulion-web-$v.zip
8287847 brulion-$v-source.tar.gz
977 SHA256SUMS.txt
LIST
    ;;
  *) exit 9 ;;
esac
EOF
} >"$bin/gh"
printf '#!%s\necho "curl: (22) 404" >&2\nexit 22\n' "$BASH" >"$bin/curl"
{
  printf '#!%s\n' "$BASH"
  cat <<'EOF'
# [-q] [-o] <zip> -d <dir>: unpacks gh's stand-in zip — "<name>" is a file holding "<name> of <zip>",
# "<name> -> <target>" a symbolic link.
zip='' dir=''
while (($#)); do
  case $1 in
    -d) dir=$2; shift 2 ;;
    -*) shift ;;
    *) zip=$1; shift ;;
  esac
done
[[ -f $zip && -n $dir ]] || exit 10
echo "unzip $zip -d $dir" >>"$STUB_LOG/unzip.txt"
[[ -n ${STUB_WEB_BROKEN:-} ]] && { echo "End-of-central-directory signature not found." >&2; exit 9; }
while read -r name arrow target; do
  mkdir -p "$dir/$(dirname -- "$name")"
  if [[ $arrow == '->' ]]; then ln -s -- "$target" "$dir/$name"; else echo "$name of ${zip##*/}" >"$dir/$name"; fi
done <"$zip"
EOF
} >"$bin/unzip"
chmod +x "$bin"/*

export STUB_LOG=$log STUB_ICON=$root/fastlane/metadata/android/en-US/images/icon.png
export PATH="$bin:$PATH"
printf 'p12' >"$tmp/key.p12"
fingerprint=$(printf 'certificate of Brulion' | sha256sum | cut -d' ' -f1 | tr a-f A-F)

# run <env assignments…> -- <arguments…>: sets $status and $out (stdout + stderr).
run() {
  local envs=()
  while [[ $1 != -- ]]; do envs+=("$1"); shift; done
  shift
  rm -rf "${log:?}"/* "$tmp/site"
  status=0
  out=$(env -u GH_REPO -u FDROID_KEYSTORE_BASE64 -u FDROID_KEY_ALIAS FDROID_KEYSTORE="$tmp/key.p12" \
    FDROID_KEYSTORE_PASS='hasło-ząb 123' ${envs[@]+"${envs[@]}"} bash "$script" -o "$tmp/site" "$@" 2>&1) || status=$?
  printf '%s\n' "$out" | sed 's/^/        | /'
}
has() { grep -qF -- "$1" <<<"$out"; }

# --- the last releases ------------------------------------------------------------------------------
run --
expect "builds the site (exit 0)" test "$status" -eq 0
expect "asks for the last three published releases" \
  grep -qF 'gh release list --exclude-drafts --exclude-pre-releases --limit 3 --json tagName --jq .[].tagName' "$log/gh.txt"
expect "downloads each release's APK" \
  test "$(grep -c -- '--pattern brulion-\*-arm64-v8a\.apk ' "$log/gh.txt")" -eq 3
expect "the APKs reach fdroid" grep -qx 'brulion-0.10.0-arm64-v8a.apk' "$log/repo.txt"
config=$log/config.yml
expect "the repository's address from the default GitHub repository" \
  grep -qx 'repo_url: https://janusz-bit.github.io/brulion/repo' "$config"
expect "the passwords are read from the environment" \
  bash -c "grep -qx 'keystorepass: {env: FDROID_KEYSTORE_PASS}' '$config' && grep -qx 'keypass: {env: FDROID_KEYSTORE_PASS}' '$config'"
expect "no password in config.yml" bash -c "! grep -q 'hasło' '$config'"
expect "config.yml is readable by its owner only" test "$(cat "$log/config.mode")" = 600
expect "the default key alias" grep -qx 'repo_keyalias: brulion-fdroid' "$config"
expect "no archive (only the repository's versions)" grep -qx 'archive_older: 0' "$config"
expect "fdroid gets the key and its password" test -e "$log/key"
expect "the repository's icon is the app's (in repo/icons/ and as repo_icon's path)" test -e "$log/icon"
expect "the texts of every locale" bash -c "grep -qx en-US '$log/locales.txt' && grep -qx pl '$log/locales.txt'"

meta=$log/io.github.januszbit.brulion.yml
expect "the metadata start with packaging/fdroid-repo's" \
  bash -c "head -n \"\$(wc -l <'$root/packaging/fdroid-repo/io.github.januszbit.brulion.yml')\" '$meta' | cmp -s - '$root/packaging/fdroid-repo/io.github.januszbit.brulion.yml'"
codes=$(grep -oE 'versionCode: [0-9]+' "$meta" | cut -d' ' -f2 | tr '\n' ' ')
expect "a build entry per APK, oldest first, versionCode from the name ($codes)" test "$codes" = "9000 9001 10000 "
expect "a build entry's name and tag" bash -c "grep -qx '  - versionName: 0.10.0' '$meta' && grep -qx '    commit: v0.10.0' '$meta'"
expect "the newest is the current version" \
  bash -c "grep -qx 'CurrentVersion: 0.10.0' '$meta' && grep -qx 'CurrentVersionCode: 10000' '$meta'"
if python3 -c 'import yaml' 2>/dev/null; then
  expect "the metadata are valid YAML" python3 -c 'import sys, yaml; d = yaml.safe_load(open(sys.argv[1])); assert len(d["Builds"]) == 3' "$meta"
fi

page=$tmp/site/index.html
expect "the landing page" test -s "$page"
expect "no placeholder left" bash -c "! grep -q '@[A-Z_]*@' '$page'"
expect "the fingerprint is the certificate's SHA-256" grep -qF "<code class=\"value\">$fingerprint</code>" "$page"
expect "the link that adds the repository to F-Droid" \
  grep -qF "href=\"fdroidrepos://janusz-bit.github.io/brulion/repo?fingerprint=$fingerprint\"" "$page"
expect "the source links" grep -qF 'href="https://github.com/janusz-bit/brulion/releases/latest"' "$page"

# The downloads: the newest release's files, linked directly.
expect "the page's release is read with its files" \
  grep -qF 'gh release view v0.10.0 --json tagName,publishedAt,isDraft,isPrerelease,assets --jq' "$log/gh.txt"
expect "the version and the publication date" \
  grep -qF 'Download Brulion 0.10.0 <time class="released" datetime="2026-11-07">7 November 2026</time>' "$page"
links=$(grep -oE 'https://github\.com/[^"]*/releases/download/[^"]*' "$page" | sort -u)
expect "seven files linked directly ($(wc -l <<<"$links"))" test "$(wc -l <<<"$links")" -eq 7
expect "every link is a file of v0.10.0" \
  test -z "$(grep -vE '^https://github\.com/janusz-bit/brulion/releases/download/v0\.10\.0/[^/]+$' <<<"$links")"
for name in brulion-0.10.0-arm64-v8a.apk Brulion-0.10.0-x86_64.AppImage Brulion-0.10.0-aarch64.AppImage \
  Brulion-0.10.0-windows-x86_64-setup.exe brulion-0.10.0-windows-x86_64.zip brulion-web-0.10.0.html \
  brulion-web-0.10.0.zip; do
  expect "a link to $name" grep -qxF "https://github.com/janusz-bit/brulion/releases/download/v0.10.0/$name" <<<"$links"
done
expect "each file's size in MiB, one decimal" bash -c "grep -qF 'data-size=\"88.5 MB\"' '$page' &&
  grep -qF 'data-size=\"69.8 MB\"' '$page' && grep -qF 'data-size=\"72.2 MB\"' '$page' &&
  grep -qF 'data-size=\"39.2 MB\"' '$page' && grep -qF '57.5 MB' '$page' &&
  grep -qF '22.2 MB' '$page' && grep -qF '16.7 MB' '$page'"
expect "the release page" grep -qF 'href="https://github.com/janusz-bit/brulion/releases/tag/v0.10.0"' "$page"
expect "the NixOS command" grep -qF 'nix run github:janusz-bit/brulion/v0.10.0' "$page"
mapfile -t keys < <(grep -oE "offer\('[a-z0-9_-]+'" "$page" | cut -d"'" -f2 | sort -u)
expect "the download button offers four systems (${keys[*]})" test "${#keys[@]}" -eq 4
for key in "${keys[@]}"; do
  expect "the button's system $key has a link" grep -qF "data-for=\"$key\"" "$page"
done

# The browser version: the same release's zip unpacked into app/, next to the page.
expect "downloads the browser version of the page's release" \
  grep -qE '^gh release download v0\.10\.0 --pattern brulion-web-0\.10\.0\.zip --dir ' "$log/gh.txt"
expect "unpacks it once" test "$(wc -l <"$log/unzip.txt")" -eq 1
app=$tmp/site/app
expect "its files in app/, as in the zip" bash -c "grep -qx 'index.html of brulion-web-0.10.0.zip' '$app/index.html' &&
  grep -qx 'brulion.wasm of brulion-web-0.10.0.zip' '$app/brulion.wasm' &&
  grep -qx 'icons/logo.svg of brulion-web-0.10.0.zip' '$app/icons/logo.svg' && test -s '$app/.nojekyll'"
expect "the page's own button opens app/" grep -qE '<a class="button open" id="open" href="app/">Open in the browser' "$page"
expect "the Polish section's button opens app/" grep -qF '<a class="button" href="app/">Otwórz w przeglądarce</a>' "$page"
expect "the browser's row opens app/" grep -qF '<div><a href="app/">Open in the browser</a>' "$page"
expect "the repository's address printed" has "repository: https://janusz-bit.github.io/brulion/repo?fingerprint=$fingerprint"
expect "the signed index in the site" test -s "$tmp/site/repo/index-v1.jar"
expect "fdroid's status files are left out" test ! -e "$tmp/site/repo/status"
expect "the logo and .nojekyll" bash -c "test -s '$tmp/site/logo.svg' && test -e '$tmp/site/.nojekyll'"

# --- options ----------------------------------------------------------------------------------------
run GH_REPO=Someone/notes FDROID_KEY_ALIAS=inny STUB_ALIAS=inny -- -n 1
expect "-n 1, another GitHub repository and key alias (exit 0)" test "$status" -eq 0
expect "-n 1 asks for one release" grep -qF -- '--limit 1 ' "$log/gh.txt"
expect "the address of another owner, in lower case" grep -qx 'repo_url: https://someone.github.io/notes/repo' "$log/config.yml"
expect "the other alias" grep -qx 'repo_keyalias: inny' "$log/config.yml"
expect "the downloads from the other GitHub repository" \
  grep -qF 'href="https://github.com/Someone/notes/releases/download/v0.10.0/brulion-0.10.0-arm64-v8a.apk"' "$tmp/site/index.html"

mkdir -p "$tmp/apks"
echo apk >"$tmp/apks/brulion-1.2.3-arm64-v8a.apk"
run -- -a "$tmp/apks" -u https://example.org/brulion/
expect "-a and -u: APKs from a folder, another address (exit 0)" test "$status" -eq 0
expect "-a downloads no APK" bash -c "! grep -q -- '--pattern brulion-\*-arm64-v8a\.apk' '$log/gh.txt'"
expect "-a: the page offers the latest published release" grep -qF 'gh release view --json ' "$log/gh.txt"
expect "-a: app/ holds that release's browser version" \
  bash -c "grep -qF 'gh release download v0.10.0 --pattern brulion-web-0.10.0.zip ' '$log/gh.txt' &&
  grep -qx 'index.html of brulion-web-0.10.0.zip' '$tmp/site/app/index.html'"
expect "-u sets the repository's address" grep -qx 'repo_url: https://example.org/brulion/repo' "$log/config.yml"
expect "1.2.3 is versionCode 1002003" grep -qx 'CurrentVersionCode: 1002003' "$log/io.github.januszbit.brulion.yml"

run STUB_PUBLISHED=2026-01-31T23:59:59Z -- -a "$tmp/apks" -r v0.9.1
expect "-r: another release on the page (exit 0)" test "$status" -eq 0
expect "-r reads that release" grep -qF 'gh release view v0.9.1 --json ' "$log/gh.txt"
expect "-r: its version, files and date" bash -c "grep -qF 'Download Brulion 0.9.1 <time class=\"released\" datetime=\"2026-01-31\">31 January 2026</time>' '$tmp/site/index.html' &&
  grep -qF '/releases/download/v0.9.1/brulion-web-0.9.1.html\"' '$tmp/site/index.html'"
expect "-r: its browser version in app/" grep -qx 'index.html of brulion-web-0.9.1.zip' "$tmp/site/app/index.html"

run FDROID_KEYSTORE= FDROID_KEYSTORE_BASE64="$(base64 -w0 "$tmp/key.p12")" -- -a "$tmp/apks"
expect "the key from FDROID_KEYSTORE_BASE64 (exit 0)" test "$status" -eq 0
expect "fdroid gets the decoded key" test -e "$log/key"

# --- refused -------------------------------------------------------------------------------------------
refused() { # <description> <text in the output> <env…> -- <arguments…>
  local what=$1 text=$2
  shift 2
  run "$@"
  if [[ $status -ne 0 ]] && has "$text" && [[ ! -e $tmp/site/index.html ]]; then pass "$what"; else fail "$what"; fi
}
refused "no password" 'FDROID_KEYSTORE_PASS is not set' FDROID_KEYSTORE_PASS= --
refused "no key" 'neither FDROID_KEYSTORE nor FDROID_KEYSTORE_BASE64' FDROID_KEYSTORE= --
refused "a key without the alias" 'cannot read the key' STUB_ALIAS=other --
refused "an http:// address" 'must be https://' -- -u http://example.org/
refused "an address without the final /" 'must be https://' -- -u https://example.org/brulion
refused "-n 0" '-n needs a positive number' -- -n 0
refused "a release without an APK" 'no APK in the release v0.9.1' STUB_NO_APK=v0.9.1 --
refused "no published release" 'no published release' STUB_TAGS=' ' --
refused "a release without one of the linked files" 'the release v0.10.0 has no brulion-web-0.10.0.html' \
  STUB_MISSING=brulion-web-0.10.0.html --
refused "the page's release cannot be read" 'cannot read the release v0.10.0' STUB_NO_RELEASE=v0.10.0 --
refused "a pre-release's tag on the page" "the page offers a release vX.Y.Z, not 'v1.0.0-rc.1'" -- -r v1.0.0-rc.1
refused "a pre-release on the page" 'the release v1.1.0 is a draft or a pre-release' STUB_PRE=true -- -r v1.1.0
refused "a draft on the page" 'the release v0.10.0 is a draft or a pre-release' STUB_DRAFT=true --
refused "the browser version cannot be downloaded" 'cannot download brulion-web-0.10.0.zip of the release v0.10.0' \
  STUB_NO_WEB=v0.10.0 --
refused "a browser version that does not unpack" 'cannot unpack brulion-web-0.10.0.zip' STUB_WEB_BROKEN=1 --
refused "a zip that is not the browser version" 'brulion-web-0.10.0.zip is not a Brulion browser build (no brulion.wasm)' \
  STUB_WEB_FILES='index.html sw.js brulion.js qtloader.js' --
refused "a symbolic link in the browser version" \
  'brulion-web-0.10.0.zip holds something other than files and folders: icons/hostname' STUB_WEB_LINK=icons/hostname --
echo apk >"$tmp/apks/brulion-debug.apk"
refused "an APK name without the version" 'unexpected APK name brulion-debug.apk' -- -a "$tmp/apks"
rm "$tmp/apks"/*
refused "an empty APK folder" 'no .apk in' -- -a "$tmp/apks"

echo
if ((failed)); then
  echo "Totals: $failed failed"
  exit 1
fi
echo "Totals: all passed"
