#!/usr/bin/env bash
# Android's own backup and its transfer to a new phone leave out the passwords and tokens
# (docs/ANDROID.md, "Backups"): the manifest names both rule files, each of their sections leaves
# out the folder the app keeps its secrets in, and that folder is the one the code writes to.
# The same for a model downloaded to run on the device (over a gigabyte: with it the app's data
# would be over the backup's quota, and nothing of the app would be backed up).
# Usage: tst_android_backup_rules.sh <repository root>
set -euo pipefail

root=${1:?usage: tst_android_backup_rules.sh <repository root>}
manifest=$root/android/AndroidManifest.xml
old_rules=$root/android/res/xml/backup_rules.xml
new_rules=$root/android/res/xml/data_extraction_rules.xml
failures=0

fail() {
  echo "FAIL: $*" >&2
  failures=$((failures + 1))
}

# The number of lines of a file that match a pattern (0 for a missing file).
count() {
  grep -c -E -- "$1" "$2" 2>/dev/null || true
}

for file in "$manifest" "$old_rules" "$new_rules"; do
  [[ -f $file ]] || fail "missing $file"
done

[[ $(count 'android:allowBackup="true"' "$manifest") == 1 ]] ||
  fail "the manifest no longer allows backups: these rules and this test want another look"
[[ $(count 'android:fullBackupContent="@xml/backup_rules"' "$manifest") == 1 ]] ||
  fail "the manifest does not name backup_rules.xml (Android 9-11)"
[[ $(count 'android:dataExtractionRules="@xml/data_extraction_rules"' "$manifest") == 1 ]] ||
  fail "the manifest does not name data_extraction_rules.xml (Android 12+)"

secrets='<exclude domain="file" path="secrets" */>'
[[ $(count "$secrets" "$old_rules") == 1 ]] || fail "backup_rules.xml does not leave out files/secrets"
# One for the cloud backup, one for the transfer to another device.
[[ $(count "$secrets" "$new_rules") == 2 ]] ||
  fail "data_extraction_rules.xml leaves out files/secrets in $(count "$secrets" "$new_rules") of its 2 sections"
[[ $(count '<cloud-backup disableIfNoEncryptionCapabilities="true">' "$new_rules") == 1 ]] ||
  fail "the cloud backup may be readable by the backup service"
[[ $(count '<device-transfer>' "$new_rules") == 1 ]] || fail "no rules for the transfer to another device"
# The models' folder, in the same places.
models='<exclude domain="file" path="models" */>'
[[ $(count "$models" "$old_rules") == 1 ]] || fail "backup_rules.xml does not leave out files/models"
[[ $(count "$models" "$new_rules") == 2 ]] ||
  fail "data_extraction_rules.xml leaves out files/models in $(count "$models" "$new_rules") of its 2 sections"
# Nothing is let back in: an <include> would make every other rule an exception to it.
for file in "$old_rules" "$new_rules"; do
  [[ $(count '<include ' "$file") == 0 ]] || fail "$file includes paths: what it leaves out is no longer the whole rule"
done

# The folder of the rules is the folder of the code (every secret file is beside the password).
settings=$root/src/app/shell/appsettings.cpp
[[ $(count 'u"/secrets/nextcloud-password"' "$settings") == 1 ]] ||
  fail "the Nextcloud password is no longer written to <app data>/secrets: the backup rules name another folder"
[[ $(count 'u"/secrets/' "$settings") == $(count 'u"/secrets/[a-z-]+"_s' "$settings") ]] ||
  fail "a secret is written outside <app data>/secrets"

# The models are downloaded into the folder the rules name (<app data>/models).
controller=$root/src/app/shell/appcontroller.cpp
[[ $(count 'localAi\.folder = m_dataDir \+ u"/models"_s' "$controller") == 1 ]] ||
  fail "the models are no longer downloaded to <app data>/models: the backup rules name another folder"

if ((failures)); then
  echo "$failures failure(s)" >&2
  exit 1
fi
echo "PASS: Android's backup rules leave out the secrets and the downloaded models"
