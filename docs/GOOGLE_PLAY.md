# Brulion on Google Play

Everything the repository needs for Google Play is ready; what is left is the owner's work in Play
Console, listed here as a checklist. **Brulion is not in the store yet.** Facts about Play Console
were checked against Google's help pages in November 2026; Play Console changes often, so a step
marked **(check in Play Console)** is one where the exact wording, place or rule may differ — trust
the Console over this page there.

## The app for Google Play

Google Play keeps some permissions for other kinds of apps: "All files access"
(`MANAGE_EXTERNAL_STORAGE`, file managers and backup apps) and `USE_EXACT_ALARM` (alarm clocks and
calendars). The Android app from GitHub and F-Droid needs both, so Google Play gets **an app of
its own**, built with the CMake option `BRULION_PLAY` (docs/ANDROID.md "A note on distribution"):

| | GitHub, F-Droid | Google Play |
|---|---|---|
| Package | `io.github.januszbit.brulion` | `io.github.januszbit.brulion.play` |
| Notebook folder | any folder on the shared storage ("All files access") or the app's storage | a folder picked in Android's folder dialog (Storage Access Framework, a persisted permission, no storage permission), kept in two-way sync with the notebook in the app's own storage, on the device; or the app's storage |
| Exact alarms | `USE_EXACT_ALARM` (Android 13+), `SCHEDULE_EXACT_ALARM` (Android 12) | `SCHEDULE_EXACT_ALARM` on every version: the user turns on "Alarms & reminders" (Brulion opens that screen) |
| Check for a new version on GitHub | yes (can be turned off) | no (Play updates the app) |
| Signed by | the release key (`scripts/android-new-keystore.sh`) | Google (Play App Signing); uploads with the upload key |

The two are separate apps: they install side by side, have their own data, and one never updates
the other. `android/play/AndroidManifest.xml` removes the permissions; the build checks that they
are gone (`scripts/android-build.sh`, `nix/android.nix`). Everything else — reminders, widgets,
"Share → Brulion", the AI chat and its "Report response" action, sync with Nextcloud, Syncthing
through the picked folder, GitHub — is the same app.

## Checklist

1. [Developer account](#1-developer-account): register, verify your identity; a new personal
   account must run a closed test with at least 12 testers for 14 days before production.
2. [Create the app](#2-create-the-app) in Play Console.
3. [Upload key](#3-play-app-signing-and-the-upload-key): `scripts/android-new-keystore.sh --play`;
   Play App Signing keeps the app signing key.
4. [Build and sign the AAB](#4-building-and-signing-the-aab): `BRULION_ANDROID_PLAY=1
   scripts/android-build.sh`, `nix build .#brulion-android-play` or the Android workflow with
   `play`.
5. [Store listing](#5-store-listing) from `fastlane/metadata/android` (with the Play variant of the
   description's last paragraph) and the feature graphic.
6. [Privacy policy](#6-privacy-policy): <https://janusz-bit.github.io/brulion/privacy.html>.
7. [App content](#7-app-content): Data safety, permissions (foreground service with a video,
   full-screen intent), ads, app access, content rating, target audience, AI-generated content,
   other declarations.
8. [Release tracks](#8-release-tracks): the first AAB by hand to internal testing, then closed
   testing (12 testers, 14 days), apply for production access.
9. [Releases from CI](#9-releases-from-ci): secrets for the upload key and a service account; every
   release tag then goes to internal testing by itself.
10. [Apps outside Google Play](#10-apps-outside-google-play-developer-verification): register the
    GitHub/F-Droid app (`io.github.januszbit.brulion` and the release key) for Android's developer
    verification.

## 1. Developer account

* **Personal or organisation.** <https://play.google.com/console/signup>; a one-time fee (US$25)
  either way. An **organisation** account needs a D-U-N-S number (free from Dun & Bradstreet,
  takes days to weeks) and the organisation's details; a **personal** account needs your legal
  name, address and an identity document. Both verify the contact e-mail and phone number. For a
  project of one person a personal account is enough.
* **Identity verification** is required for every account; until it is done you cannot publish
  (check in Play Console for the documents asked in your country).
* **Testing before production (personal accounts created after 13 November 2023):** production
  (and pre-registration) stays locked until you have run a **closed test with at least 12 testers
  who stayed opted in for 14 days in a row**, then applied for production access on the Console's
  Dashboard (answers about the test, the app and its readiness; Google's review "usually takes
  seven days or less"). A tester who leaves and comes back starts the 14 days again; too few
  testers or too little engagement may mean more testing. Organisation accounts are exempt. Plan
  the testers early (a Google Group or an e-mail list of 15–20 people, so dropouts do not reset the
  clock).
* **What the store shows about you:** the developer name, the contact e-mail (and website); for a
  personal account without paid apps or in-app purchases the address is not shown publicly (check
  in Play Console).
* The verified Play account also counts for Android's developer verification of apps outside Play
  (section 10).

## 2. Create the app

Play Console → **Create app**:

* App name **Brulion** (at most 30 characters), default language **English (United States) –
  en-US**, **App**, **Free** (a free app can never become paid), accept the declarations.
* The package name is set by the **first upload** and can never change or be used again:
  `io.github.januszbit.brulion.play`. Upload an AAB of the Play build only (section 4) — an AAB of
  the GitHub app has the wrong package.
* Store settings: category **Productivity**, contact e-mail (required, public), website
  `https://janusz-bit.github.io/brulion/`.
* Translations of the listing: add **Polish – pl-PL** (section 5).

## 3. Play App Signing and the upload key

New apps on Google Play use **Play App Signing**: Google keeps the **app signing key** that signs
what users install, and you sign every upload with an **upload key** that Google checks. Keep the
default, a key generated by Google: the Play app is a package of its own, so there is no reason to
give Google the release key of the GitHub/F-Droid app.

* **Make a separate upload key** (outside the repository, like the release key):

  ```sh
  scripts/android-new-keystore.sh --play   # ~/.local/share/brulion/brulion-play-upload.jks, alias brulion-upload
  ```

  It prints the certificate's SHA-256, the `gh secret set` commands for the
  `PLAY_UPLOAD_KEYSTORE_*` secrets (section 9) and the local build command. By hand it is
  `keytool -genkeypair -keystore brulion-play-upload.jks -storetype PKCS12 -alias brulion-upload
  -keyalg RSA -keysize 4096 -validity 10000 -dname "CN=Brulion upload key, O=Brulion"`.
  Back it up with its password (a password manager).
* **The first upload decides the upload key**: whatever key signs the first AAB becomes the upload
  key. Never upload an AAB signed with the throwaway key (`scripts/android-build.sh` without a key;
  the CI upload refuses one, section 9). The upload key can also be the release key — CI falls back
  to it when there are no `PLAY_UPLOAD_KEYSTORE_*` secrets — but a separate key keeps the release
  key out of Play's reach and is replaceable.
* **A lost or leaked upload key** is replaceable: Play Console → Test and release → App integrity →
  Play app signing → **Request upload key reset** (check in Play Console), with the certificate of
  a new key:

  ```sh
  keytool -export -rfc -keystore NEW.jks -alias brulion-upload -file upload_certificate.pem
  ```

  Google resets it after a short wait (days); until then nothing can be uploaded. The app signing
  key cannot be lost: Google keeps it.
* The app's certificates (SHA-1/SHA-256 of the app signing key and of the upload key) are on the
  same Console page.

## 4. Building and signing the AAB

The app for Google Play is an AAB (Play takes bundles only) of the package
`io.github.januszbit.brulion.play`, with the same `versionCode` as the GitHub app
(`major·1000000 + minor·1000 + patch`). Play refuses a `versionCode` it was ever given, even for a
release that was deleted, so a rebuilt version goes out as a new patch version.

* **Script** (an x86_64 Linux host, docs/ANDROID.md "Building the APK"):

  ```sh
  read -r -s -p 'Upload key password: ' QT_ANDROID_KEYSTORE_STORE_PASS; echo; export QT_ANDROID_KEYSTORE_STORE_PASS
  BRULION_ANDROID_PLAY=1 QT_ANDROID_KEYSTORE_PATH=~/.local/share/brulion/brulion-play-upload.jks \
    QT_ANDROID_KEYSTORE_ALIAS=brulion-upload scripts/android-build.sh
  # dist-android/brulion-<version>-arm64-v8a-play.aab (Play Console) and -play.apk (a phone, beside the release)
  ```

  The script checks the package, that "All files access", the older storage permissions and
  `USE_EXACT_ALARM` are gone and that `SCHEDULE_EXACT_ALARM` has no `maxSdkVersion`.
* **Nix** (reproducible, unsigned; then signed outside the store):

  ```sh
  nix build .#brulion-android-play          # result/brulion-<version>-arm64-v8a-play-unsigned.{apk,aab}
  nix run .#brulion-android-sign -- result/brulion-*-play-unsigned.aab \
    --keystore ~/.local/share/brulion/brulion-play-upload.jks --alias brulion-upload
  # ./brulion-<version>-arm64-v8a-play.aab, signed with jarsigner
  ```

* **CI**: Actions → Android → Run workflow with **play** (artifact `brulion-android-play`), and the
  release workflow for every release tag (section 9).
* **ABIs**: arm64-v8a only, as the GitHub APK; Play then offers the app to 64-bit ARM phones only.
  `BRULION_ANDROID_ABIS="arm64-v8a x86_64"` puts more into one bundle (Play delivers each device
  its own); the model on the device exists only for 64-bit ABIs.
* **Play's technical requirements**, already met: target API 36 (Play raises the minimum every
  year — check in Play Console), 16 KB memory pages (Android 15+; both build paths check the
  alignment), an AAB. Play limits the compressed download size of a bundle's base module (check in
  Play Console for the current figure); the APK is about 90 MB.

## 5. Store listing

The texts and pictures are in `fastlane/metadata/android/<locale>/` (F-Droid reads the same
files); `scripts/check-fastlane-metadata.sh` checks them against both stores' limits — the
stricter where they differ (ctest `tst_scripts_check_fastlane_metadata`).

| Play Console field | File (en-US; pl is the same under `pl/`) | Limit |
|---|---|---|
| App name | `title.txt` | 30 characters |
| Short description | `short_description.txt` | 80 characters |
| Full description | `full_description.txt` (see below) | 4000 characters |
| App icon | `images/icon.png` | 512 × 512 PNG, 32-bit with alpha, at most 1 MB |
| Feature graphic | `images/featureGraphic.png` | 1024 × 500, PNG or JPEG, no alpha |
| Phone screenshots | `images/phoneScreenshots/*.png` | 2–8, sides 320–3840 px, the long side at most 2.3 × the short one, no alpha |
| Release notes ("What's new") | `changelogs/<versionCode>.txt` | 500 characters |

* **Languages**: Play's codes are `en-US` and **`pl-PL`** (fastlane's folder is `pl`).
* **The full description's last paragraph is about the GitHub/F-Droid app** ("All files access").
  In Play Console replace it with:
  * en-US: "Pick the notebook's folder in Android's own folder dialog — for example a folder that
    Syncthing syncs — and Brulion keeps it in sync with its own copy, with no storage permission.
    Or keep the notebook in the app's storage and sync it with Nextcloud."
  * pl-PL: "Folder notatnika wybierasz w systemowym oknie wyboru folderu Androida — np. folder
    synchronizowany przez Syncthing — a Brulion synchronizuje go z własną kopią, bez uprawnień do
    pamięci. Możesz też trzymać notatnik w pamięci aplikacji i synchronizować go z Nextcloudem."
* **Feature graphic**: made by `scripts/play-feature-graphic.sh` from
  `packaging/google-play/feature-graphic.svg` (the logo, "Brulion" and a tagline per language, in
  the bundled Inter font only, librsvg and optipng of the flake's nixpkgs): re-run it after a
  change of the logo or a tagline; the result is the same on every machine.
* **Screenshots**: `docs/images/phone-*.png` copied as `phoneScreenshots` (docs/ANDROID.md "F-Droid
  and IzzyOnDroid"). They show the GitHub app; make sure none shows "All files access" or the
  new-version message. Play recommends larger screenshots (at least 1080 px) for its promoted
  places (check in Play Console).
* **Category** Productivity; **tags** (up to five) e.g. Notes, Productivity, To-do list, Calendar.
* CI uploads only the AAB and the release notes; the listing is edited in Play Console.

## 6. Privacy policy

**<https://janusz-bit.github.io/brulion/privacy.html>** — `docs/PRIVACY.md` (English, then the
Polish "Polityka prywatności" at `#polityka-prywatnosci`) rendered by `scripts/fdroid-repo.sh` with
`scripts/markdown-page.py` into the project's GitHub Pages every time the site is built: with every
published release and every push to `main` that changes the policy (`.github/workflows/fdroid.yml`;
by hand: `gh workflow run fdroid.yml`). The page exists from the first rebuild of the site after
this change.

* Play Console → App content → **Privacy policy**: that address.
* In the app: **About Brulion → Privacy policy** and **Settings → Privacy → Read the privacy
  policy** (Play wants it reachable inside the app too).
* Keep it true: a new connection, permission or anything that sends data goes into
  `docs/PRIVACY.md` (both languages, a new date) and into the Data safety form below.

## 7. App content

Play Console → Policy → **App content** (check in Play Console for the current list). For Brulion:

### Data safety

What the code does (audited for this page, November 2026; the policy above says the same):

* **Nothing goes to the developer.** No analytics, crash reporting, ads, Firebase or Google Play
  services; no account; the developer runs no server. The Gradle project adds no library besides
  androidx; the native libraries are Qt, OpenSSL, llama.cpp and whisper.cpp. The event log stays
  on the device unless the user shares it. Crash reports are written to standard error on Linux
  only.
* **What leaves the device, only for features the user turns on, to servers the user chooses**:
  the notebook's files to their Nextcloud/WebDAV server (also in the background), CalDAV
  calendars, a GitHub repository (with their token), publishing (GitHub Pages, WebDAV, a public
  Nextcloud link); AI requests — messages, parts of the notebook, attached pictures — to the AI
  provider they chose with their own key (Anthropic, OpenAI or any compatible server); the AI chat
  of an agent on their own computer (`brulion-cli acp serve`, the device name at pairing);
  one-off imports (Microsoft To Do/OneNote, Todoist, Airtable).
* **Downloads on request**: model files from Hugging Face (the on-device AI model, search by
  meaning, Whisper); pictures linked in notes after a tap on "Show" (or automatically when the user
  turns that on).
* **On-device only**: voice notes (recorded to the notebook, transcribed by whisper.cpp on the
  device), the on-device models, Syncthing's local API (`127.0.0.1`), "Share → Brulion",
  automation intents. Dictation in the AI chat goes through the phone's speech recognizer app
  (Brulion gets the text); photos through the camera app.
* **Encryption in transit**: built-in hosts are HTTPS; a user may enter a plain `http://` address
  for their own server (Nextcloud, CalDAV, an AI server, the agent's computer) after a warning, and
  pictures may come over http.
* **Secrets** (passwords, keys, tokens) are files in the app's private storage, never synced, left
  out of Android's backup and device transfer.

Google's definition decides the answers: "'Collect' means transmitting data from your app off a
user's device", "irrespective of whether data is transmitted to you or a third-party server";
transfers "based on a specific user-initiated action" are not "sharing". Data that never leaves the
device is not declared. Recommended answers (conservative — they describe what the app can send
at the user's request):

* Does your app collect or share any of the required user data types? **Yes.**
* Is all of the user data collected by your app encrypted in transit? **No** — plain `http://`
  addresses the user enters are allowed. (Answering Yes would need the Play build to refuse
  `http://` for servers other than the device itself.)
* Do you provide a way for users to request that their data is deleted? The developer holds no
  data and the app has no accounts; say so where the form allows (deletion: uninstalling, and on
  the user's own servers). Check in Play Console which answer fits an app without accounts.
* Data types, each **collected: yes, shared: no, processed ephemerally: no, optional** (users can
  choose), purpose **App functionality**:
  * Files and docs — the notebook's files synced, committed, published or sent to an AI provider;
  * Photos — pictures in the notebook, a photo attached to an AI message;
  * Audio → Voice or sound recordings — voice notes are files of the notebook, synced with it;
  * App activity → Other user-generated content — the AI chat's messages to the AI provider.
* Not collected: location, personal info (name, e-mail address, …), financial info, health and
  fitness, messages (e-mail, SMS), contacts, calendar (CalDAV events go between the device and the
  user's own server — if you count them, add Calendar → Calendar events the same way), web
  browsing, app info and performance (crash logs, diagnostics), device or other IDs (the device's
  name, which an agent's pairing and the names of conflict copies carry, goes only to the user's
  own computer and server).

A lighter reading — "No data collected", as many local-first apps answer — treats every transfer
as going to the user's own accounts. It does not fit the AI provider, a third party that receives
the user's notes, so this page recommends the declaration above.

### Permissions

The Play build's merged manifest (`aapt2 dump badging dist-android/*-play.apk`) has:

| Permission | Declaration in Play Console | Used by |
|---|---|---|
| `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_DATA_SYNC` | **Foreground service: yes** — description, impact, **video** | `AgentService`: an AI agent on the user's computer answering in the AI chat while the screen is off; a model on the device downloaded, or a model file of the user's own copied |
| `FOREGROUND_SERVICE_SPECIAL_USE` | **Foreground service: yes** — the special use's description (the manifest's `PROPERTY_SPECIAL_USE_FGS_SUBTYPE`), impact, **video** | `AgentService` while the model on the device computes an answer |
| `USE_FULL_SCREEN_INTENT` | **Full-screen intent** declaration | the alarm screen over the lock screen (`AlarmActivity`) |
| `SCHEDULE_EXACT_ALARM` | none (only `USE_EXACT_ALARM` needs one; not in the Play build) | reminders and alarms at the exact minute |
| `RECORD_AUDIO` | none (a runtime permission; Data safety covers audio) | voice notes |
| `POST_NOTIFICATIONS`, `RECEIVE_BOOT_COMPLETED`, `VIBRATE`, `WAKE_LOCK`, `USE_BIOMETRIC`, `INTERNET`, `ACCESS_NETWORK_STATE`, `MODIFY_AUDIO_SETTINGS` | none | reminders, re-arming after a restart, the agent's wake lock, the app lock, the network, audio routing |

Not used: `QUERY_ALL_PACKAGES` (the manifest's `<queries>` names only the camera and speech
recognition intents), `READ_MEDIA_*` (no photo and video permissions declaration), `CAMERA`,
location, SMS, call log, accessibility services, `MANAGE_EXTERNAL_STORAGE`, `USE_EXACT_ALARM`.

* **Foreground service (dataSync).** For each type Play asks a description, what happens to the
  user if the task is deferred and if it is interrupted, a **link to a video** that shows the
  steps a user takes to start it, and a use case. Suggested:
  * use case **Network transfer: Upload or download** (the app fetches what the agent on the
    user's computer writes);
  * description: "While an AI agent on the user's own computer answers a question the user asked
    in Brulion's AI chat, the app keeps the connection to that computer and fetches the answer, so
    the answer arrives with the screen off. The service runs only during that answer and shows a
    notification with Stop.";
  * deferred: "The answer the user is waiting for does not arrive until the app is opened again";
    interrupted: "The answer stops in the middle; the user has to ask again";
  * video (unlisted on YouTube, about 30 s, on a phone): Settings → Extensions → AI agents → connect
    an agent on a computer; AI chat → ask; the notification appears; lock the screen; unlock — the
    answer has arrived.

  The same service is dataSync while a model of the device is downloaded from Hugging Face or a
  model file the user picked is copied into the app (a second use case under the same type:
  "Download of an AI model of 1–3 GB the user asked for; it shows its progress and Stop").

  Google recommends a user-initiated data transfer job instead of a dataSync service for network
  transfers a user starts. If the declaration is refused, the fallback is a Play build without the
  service (the agent answers only while the app is open).
* **Foreground service (specialUse).** While the model on the device computes an answer
  (docs/ANDROID.md "A model on the phone") the service runs as specialUse: no other type covers
  work on the device's own processor that the user waits for (it is no media, sync, location or
  data transfer, and a WorkManager job may be deferred and is ended after minutes). Play reviews
  the subtype the manifest gives (`PROPERTY_SPECIAL_USE_FGS_SUBTYPE`: "An AI model that runs on
  the device answers the user's question about their notes: it computes on the processor for up
  to some minutes and must go on while the screen is off or another app is in front. Nothing
  leaves the device."). Suggested:
  * deferred: "The answer the user asked for is not written until the app is opened again";
    interrupted: "The answer stops in the middle; the user has to ask again";
  * video (about 30 s, on a phone): Settings → Extensions → AI → On this device → Download; AI
    chat → ask; the notification "The model on this device is answering…" appears; lock the
    screen; unlock — the answer is there.

  If specialUse is refused, the Play build keeps dataSync for the downloads and the model answers
  only while the app is on screen (`AgentService.typeFor`: no "model" kind).
* **Full-screen intent.** On Android 14+ Play grants `USE_FULL_SCREEN_INTENT` by default only to
  apps whose core functionality is **calling or alarms**. Brulion's alarms (⏰!) ring like an
  alarm clock; declare the alarm functionality and see whether Play accepts it. If not, users turn
  "Full screen notifications" on themselves (Brulion opens the screen) and without it the alarm
  still rings, as an ordinary notification (docs/ANDROID.md "Permissions").
* **Exact alarms.** On Android 14+ `SCHEDULE_EXACT_ALARM` is no longer granted to new installs:
  the user turns "Alarms & reminders" on (Brulion asks and opens the screen); without it
  reminders come a few minutes late.

### Other declarations

* **Ads**: No. **Advertising ID**: the app does not use it (no Google Play services, no
  `com.google.android.gms.permission.AD_ID` in the manifest).
* **App access**: no login of Brulion's own — "All functionality is available without any special
  access". Sync, GitHub and the AI providers use the user's own accounts and keys; say so in the
  instructions field, and that the on-device features (notes, journal, reminders, the on-device
  model after its download) need nothing (check in Play Console whether the reviewers ask for a
  test account anyway).
* **Content rating** (IARC questionnaire): category "Utility, Productivity, Communication, or
  Other" (check in Play Console); no violence, sexual content, bad language, drugs, gambling;
  users cannot talk to each other in the app; no location sharing; no purchases. If asked about
  generative AI or unmoderated content: the AI chat answers with the user's chosen provider. The
  rating is assigned by IARC from the answers (likely the lowest age groups).
* **Target audience and content**: **18 and over** (or 13+ at the lowest) and "not designed to
  appeal to children" — the AI chat talks to providers the user picks, without Brulion's own
  moderation, and choosing ages under 13 brings in the Families policy.
* **News app**: No. **COVID-19 contact tracing or status**: No. **Government app**: No.
  **Financial features**: None. **Health apps**: none — though Brulion has a gym log and habit
  tracking (built-in Luau extensions, off by default, which record what the user types); if the
  form asks about fitness tracking, answer for those (check in Play Console).
* **AI-generated content** (Play's policy for generative AI apps): apps must let users **report or
  flag offensive AI-generated content without leaving the app**. Brulion's AI chat has "Report
  response" on its answers: an in-app dialog from which the report goes by a prefilled e-mail (to
  the address of the CMake variable `BRULION_AI_REPORT_EMAIL`) or as a prefilled public GitHub
  issue, and which flags or hides the answer on the device. Mention it in the declaration; sending
  opens the e-mail app or the browser — if the review objects to that, a report would need a
  server of the developer's.
* **Luau extensions and downloaded code.** Play's Device and Network Abuse policy forbids
  downloading executable code (dex, JAR, `.so`) from outside Play, and allows code that runs in an
  interpreter or virtual machine without direct access to Android APIs, as long as it does not
  break Play's policies. Brulion's extensions are Luau scripts in the user's own notebook, run by
  the embedded Luau interpreter in a sandbox that reaches only the `b` API (docs/EXTENSIONS.md;
  network and AI only with the user's approval); the app never downloads native code — model files
  are data (GGUF weights), checked by SHA-256.

## 8. Release tracks

* **Internal testing**: up to 100 testers by e-mail; a release usually reaches them within minutes
  (check in Play Console how much review it gets). Start here: upload the first AAB **by hand** (Test and release → Testing →
  Internal testing → Create new release; the API cannot create an app's first release), fill in the
  release notes from `changelogs/<versionCode>.txt`.
* **Closed testing**: the 12 testers × 14 days of section 1 (a list of e-mails or a Google Group;
  testers opt in through the link Play gives). Promote a release from internal testing.
* **Production**: after production access is granted; a staged rollout (e.g. 20 %) first. Every
  production release goes through review.
* Until the app has been published to any track, Play keeps it a **draft app**, and the API takes
  only draft releases (`PLAY_RELEASE_STATUS=draft`, section 9).

## 9. Releases from CI

`.github/workflows/release.yml`, for a release tag `vX.Y.Z` (not a pre-release with `-`):

* the job **Google Play** runs `android.yml` with `play: true` in parallel with the other builds:
  the Play APK and AAB, signed with the upload key, in the workflow artifact
  **`brulion-android-play`** (30 days). It is not a file of the GitHub release and nothing of the
  release waits for it;
* the job **Upload to Google Play (internal testing)** runs once the GitHub release is published
  and only for an AAB signed with a key of the repository (never the throwaway key): with the
  secret `PLAY_SERVICE_ACCOUNT_JSON` it uploads the AAB to the **internal** track with the
  version's release notes (`changelogs/<versionCode>.txt` of `en-US` and `pl` as `pl-PL`), through
  [r0adkll/upload-google-play](https://github.com/r0adkll/upload-google-play) pinned to the commit
  of v1.1.5; without the secret it only notes that the AAB waits in the artifact.

Secrets and variables (Settings → Secrets and variables → Actions):

| Name | What | Without it |
|---|---|---|
| `PLAY_UPLOAD_KEYSTORE_BASE64`, `_STORE_PASS`, `_KEY_PASS`, `_ALIAS` | the upload key (`scripts/android-new-keystore.sh --play` prints the commands) | the release key (`QT_ANDROID_KEYSTORE_*`), else a throwaway key and no upload |
| `PLAY_SERVICE_ACCOUNT_JSON` | the JSON key of a Google Cloud service account with release rights for the app | the AAB stays in the artifact for a manual upload |
| variable `PLAY_RELEASE_STATUS` | `draft` while the app is a draft app; `completed` (default) afterwards | `completed` |

The service account (check in Play Console; the Console's steps change): in Google Cloud, a
project with the **Google Play Android Developer API** enabled and a service account with a JSON
key; in Play Console → **Users and permissions** → invite the service account's e-mail with the
app's permissions to release to testing tracks (not production); then

```sh
gh secret set PLAY_SERVICE_ACCOUNT_JSON < play-service-account.json
gh variable set PLAY_RELEASE_STATUS --body draft     # until the app is published on a track
```

Promoting a release from internal testing to closed testing and production stays a step in Play
Console. By hand without a release: Actions → Android → Run workflow → **play**.

## 10. Apps outside Google Play: developer verification

Google now requires apps installed on certified Android devices to come from a **verified
developer** who registered the app's package name — **also apps installed from outside Google
Play**, like the GitHub/F-Droid app (Android Developers Blog, March and June 2026):

* enforcement started on **30 September 2026** for users in Brazil, Indonesia, Singapore and
  Thailand; Google will extend it **worldwide in 2027** (no date yet);
* a developer whose Play Console account is verified is a verified developer; register the apps
  you distribute outside Play in **Play Console** (or in the Android Developer Console, for
  developers without Play) — here `io.github.januszbit.brulion` with the release key's
  certificate (check in Play Console for what the registration asks);
* unregistered apps can still be installed with `adb` or Android's "advanced flow" for apps of
  unverified developers;
* a "limited distribution" account (no ID, no fee) covers up to 20 devices only — not enough for a
  public app;
* an APK that F-Droid itself builds and signs (f-droid.org, docs/ANDROID.md "Building from source")
  is F-Droid's signature; how f-droid.org deals with verification is F-Droid's question. Brulion's
  own F-Droid repository serves the release key's APKs, covered by the registration above.
