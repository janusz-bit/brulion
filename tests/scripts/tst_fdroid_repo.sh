#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tests scripts/fdroid-repo.sh (Brulion's own F-Droid repository, docs/ANDROID.md "Własne
# repozytorium F-Droid") with stand-ins for fdroid, keytool, gh and curl on PATH — what the script
# itself decides: the APKs of the last releases, fdroid's config (the site's address, the
# passwords only as {env: …}, readable by its owner only), a build entry per APK so each version
# gets its changelog (sorted, the newest current), the texts and icon from fastlane, the landing
# page (address, fingerprint = SHA-256 of the certificate, the "Add to F-Droid" link) and what it
# refuses. The real `fdroid update` runs in CI (.github/workflows/fdroid.yml).
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
    tag=$3 dir=${@: -1}
    [[ $tag == "$STUB_NO_APK" ]] && exit 1
    echo "apk $tag" >"$dir/brulion-${tag#v}-arm64-v8a.apk" ;;
  *) exit 9 ;;
esac
EOF
} >"$bin/gh"
printf '#!%s\necho "curl: (22) 404" >&2\nexit 22\n' "$BASH" >"$bin/curl"
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
expect "downloads each release's APK" test "$(grep -c '^gh release download v' "$log/gh.txt")" -eq 3
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

mkdir -p "$tmp/apks"
echo apk >"$tmp/apks/brulion-1.2.3-arm64-v8a.apk"
run -- -a "$tmp/apks" -u https://example.org/brulion/
expect "-a and -u: APKs from a folder, another address (exit 0)" test "$status" -eq 0
expect "-a downloads nothing" test ! -e "$log/gh.txt"
expect "-u sets the repository's address" grep -qx 'repo_url: https://example.org/brulion/repo' "$log/config.yml"
expect "1.2.3 is versionCode 1002003" grep -qx 'CurrentVersionCode: 1002003' "$log/io.github.januszbit.brulion.yml"

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
