# Brulion na Androidzie

Brulion na Androidzie to ta sama aplikacja co na komputerze (Tauri 2), z jednym dodatkiem:
wtyczką `brulion-android` (`src-tauri/plugins/brulion-android`), która daje dostęp do folderu
z notatkami na pamięci współdzielonej, planuje przypomnienia i alarmy w systemie oraz otwiera
właściwą notatkę po stuknięciu w powiadomienie.

Notatki to zwykłe pliki `.md` w folderze. Na telefonie folder może:

* być **folderem Syncthing** na pamięci współdzielonej (np. `/storage/emulated/0/Brulion`) —
  wymaga uprawnienia „Dostęp do wszystkich plików”, opis niżej;
* leżeć w **prywatnym katalogu aplikacji** (bez dodatkowych uprawnień) i być synchronizowany
  z **Nextcloud** (WebDAV) z poziomu Brulionu — Ustawienia → Synchronizacja.

Wszystko działa offline. Synchronizacja (Syncthing czy Nextcloud) nigdy nie blokuje pisania.

## Folder Syncthing na telefonie

1. Zainstaluj **Syncthing-Fork** (np. z F-Droid). Pierwotna aplikacja „Syncthing” na Androida nie
   jest już rozwijana — fork jest jej kontynuacją.
2. W Syncthing-Fork dodaj folder udostępniany z komputera (albo utwórz nowy) i ustaw jego ścieżkę
   na pamięci telefonu, np. `/storage/emulated/0/Brulion` lub `/storage/emulated/0/Syncthing/Notatki`.
   Typ folderu: „Wysyłanie i odbieranie”.
3. Otwórz Brulion → wybór notatnika → wskaż ten sam folder.
4. Gdy Brulion poprosi o **„Dostęp do wszystkich plików”**, otworzy się ekran systemowy —
   włącz przełącznik dla Brulionu i wróć do aplikacji (sprawdzi uprawnienie sama).
   Ręcznie: *Ustawienia → Aplikacje → Specjalny dostęp aplikacji → Dostęp do wszystkich plików →
   Brulion* (nazwy menu różnią się między producentami).

Dlaczego to uprawnienie? Folder Syncthing leży na pamięci współdzielonej, a Brulion czyta i zapisuje
notatki zwykłym systemem plików (szybka lista plików, zapis atomowy, wykrywanie zmian zrobionych
przez Syncthing). Bez tego uprawnienia Android 11+ widzi tam tylko pliki multimedialne.
Brulion nie zagląda poza wybrany folder.

Starsze wersje Androida:

* **Android 10** — zamiast „Dostępu do wszystkich plików” wystarcza zwykłe uprawnienie do plików
  (Brulion korzysta z `requestLegacyExternalStorage`).
* **Android 7–9** — Brulion poprosi o uprawnienie „Pamięć” (pliki i multimedia). Jeśli wcześniej
  wybrano „Nie pytaj ponownie”, otworzą się ustawienia aplikacji.

Bez folderu na pamięci współdzielonej domyślny notatnik powstaje w prywatnym katalogu Brulionu
(`…/files/vault`). Do niego Syncthing nie ma dostępu — wtedy synchronizuj przez Nextcloud.

Wskazówki do Syncthing:

* W folderze warto mieć plik `.stignore` pomijający pliki tymczasowe (Brulion ma gotowy
  szablon). Dane urządzenia (historia zmian, indeksy) Brulion trzyma poza folderem, więc nie
  powodują konfliktów.
* Konflikty Syncthing (`*.sync-conflict-*.md`) Brulion wykrywa i proponuje scalenie.
* Wersjonowanie plików w Syncthing („Proste” lub „Przedziałowe”) jest dobrym, dodatkowym
  zabezpieczeniem.

## Przypomnienia i alarmy

Przypomnienie (`⏰ 2026-10-14 09:00` w notatce) staje się powiadomieniem systemowym, które pojawi
się **także przy zamkniętej aplikacji i po restarcie telefonu**. **Alarm** (`⏰! 2026-10-14 06:30`)
dzwoni jak budzik. Składnia: `docs/SYNTAX.md`, rozdział 6.

### Jak to działa

* Gdy Brulion działa (przy uruchomieniu i po zmianach w notatkach), przekazuje systemowi listę
  nadchodzących przypomnień, zastępując poprzednią. Wtyczka zapisuje ją w pamięci aplikacji
  i nastawia budziki systemowe (`AlarmManager`): alarmy przez `setAlarmClock` (budzi telefon,
  w pasku stanu widać ikonę budzika), zwykłe przypomnienia przez `setExactAndAllowWhileIdle`.
  Naraz planowanych jest najwyżej 400 najbliższych przypomnień.
* **Kanały powiadomień**: „Przypomnienia” (wysoki priorytet, dźwięk powiadomienia), „Alarmy”
  (dźwięk budzika na strumieniu alarmu, wibracje, dzwoni aż do reakcji, widoczne na ekranie
  blokady) i „Przegapione alarmy” (ciche). Dźwięk i wibracje każdego kanału zmienisz
  w *Ustawienia → Aplikacje → Brulion → Powiadomienia*.
* **Alarm bez reakcji** przestaje dzwonić sam po **10 minutach** (aplikacja może ustawić inny
  czas dla danego alarmu, od 1 do 60 minut) — także przy zamkniętej aplikacji. Zamienia się
  wtedy w ciche powiadomienie **„Przegapiony alarm”**, a ekran alarmu się zamyka; stuknięcie
  w powiadomienie nadal otwiera notatkę.
* **Alarm przy wygaszonym ekranie** otwiera pełnoekranowy ekran alarmu nad ekranem blokady
  z przyciskami **Drzemka 10 min**, **OK** i **Otwórz notatkę**.
* Przyciski w powiadomieniu: **Drzemka 10 min** (przypomnienie wróci za 10 minut — działa także
  przy zamkniętej aplikacji) i **OK**. Stuknięcie w powiadomienie otwiera notatkę w miejscu
  przypomnienia. Przy alarmie każda z tych reakcji wycisza go i zamyka ekran alarmu.
* **Po restarcie** telefonu (po pierwszym odblokowaniu), po aktualizacji aplikacji i po zmianie
  godziny przypomnienia są nastawiane ponownie. Te, które przypadły, gdy telefon był wyłączony
  (do 24 godzin wstecz), pojawią się od razu jako „Przegapione”.
* **Zmiana strefy czasowej**: przypomnienia trzymają się czasu lokalnego — „09:00” zostaje
  o 09:00 w nowej strefie (drzemki nie są przesuwane).

Ograniczenie: Brulion planuje przypomnienia na podstawie notatek, które przeczytał. Przypomnienie
dodane albo zmienione na innym urządzeniu (i zsynchronizowane, gdy Brulion był zamknięty) zostanie
zaplanowane przy najbliższym otwarciu Brulionu. Warto otworzyć aplikację po większych zmianach
na komputerze.

### Uprawnienia

* **Powiadomienia** (Android 13+): Brulion poprosi o zgodę systemowym okienkiem. Gdy system
  już nie pyta (odmowa „na stałe” albo powiadomienia wyłączone ręcznie), ponowna prośba otwiera
  ustawienia powiadomień Brulionu.
* **Alarmy i przypomnienia** (dokładne budziki):
  * Android 13+ — przyznawane przy instalacji (`USE_EXACT_ALARM`), nic nie trzeba robić;
  * Android 12 — *Ustawienia → Aplikacje → Specjalny dostęp aplikacji → Alarmy i przypomnienia*
    (Brulion otworzy ten ekran). Po włączeniu przypomnienia są od razu przestawiane na dokładne;
  * bez tego uprawnienia przypomnienia przychodzą z opóźnieniem (minuty, w trybie uśpienia dłużej).
* **Powiadomienia pełnoekranowe** (Android 14+): potrzebne do ekranu alarmu nad ekranem blokady —
  *Ustawienia → Aplikacje → Specjalny dostęp aplikacji → Powiadomienia pełnoekranowe* (Brulion
  może otworzyć ten ekran; gdy telefon go nie ma — ustawienia powiadomień Brulionu). Bez tego
  uprawnienia alarm i tak dzwoni, ale pokazuje się jako zwykłe powiadomienie.
* **Nie przeszkadzać**: alarmy mają kategorię „alarm”. Jeśli w trybie „Nie przeszkadzać” alarmy
  są wyciszone, zezwól na nie w ustawieniach tego trybu.

### Oszczędzanie baterii

Budziki systemowe działają także w trybie uśpienia (Doze), ale część producentów agresywnie
„ubija” aplikacje w tle, a **wymuszone zatrzymanie kasuje wszystkie budziki aplikacji** aż do jej
ponownego uruchomienia. Dlatego:

* *Ustawienia → Aplikacje → Brulion → Bateria* → **Bez ograniczeń**;
* **Xiaomi / Redmi / POCO** (MIUI, HyperOS): włącz **Autostart** dla Brulionu i ustaw
  „Oszczędzanie baterii” na „Bez ograniczeń”;
* **Samsung**: usuń Brulion z „Aplikacji uśpionych” i „Aplikacji w głębokim uśpieniu”
  (*Ustawienia → Bateria → Limity użycia w tle*);
* **Huawei / Honor**: *Uruchamianie aplikacji* → Brulion → zarządzaj ręcznie (wszystkie trzy
  przełączniki włączone);
* **OnePlus / OPPO / realme**: zezwól na działanie w tle i autostart.

To samo zrób dla **Syncthing-Fork**, żeby synchronizował w tle. Szczegóły dla konkretnych
telefonów: <https://dontkillmyapp.com>.

## Budowanie APK lokalnie

Potrzebne:

* Node.js (z npm) — frontend i Tauri CLI (`npm ci` instaluje `@tauri-apps/cli`);
* Rust (`rustup`) z celami Androida:
  `rustup target add aarch64-linux-android armv7-linux-androideabi i686-linux-android x86_64-linux-android`;
* JDK 17 lub nowszy;
* Android SDK (platformy 36 i 37 — projekt Gradle z Tauri CLI 2.12 ma `compileSdk = 37` — oraz
  build-tools) i NDK, zmienne `ANDROID_HOME` i `NDK_HOME` (np. przez Android Studio → SDK
  Manager). Brakujące pakiety Gradle dociąga sam, jeśli katalog SDK jest zapisywalny.

Na **NixOS** SDK i NDK daje `androidenv` z nixpkgs (licencja SDK wymaga akceptacji:
`android_sdk.accept_license = true` i `allowUnfree = true`). Przykładowy `shell.nix`:

```nix
{ pkgs ? import <nixpkgs> {
    config = { allowUnfree = true; android_sdk.accept_license = true; };
  } }:
let
  android = pkgs.androidenv.composeAndroidPackages {
    platformVersions = [ "36" "37.0" ];  # API 37 nazywa się w repozytorium SDK „37.0”
    buildToolsVersions = [ "36.0.0" ];
    includeNDK = true;
  };
  sdk = "${android.androidsdk}/libexec/android-sdk";
in pkgs.mkShell {
  packages = [ android.androidsdk pkgs.jdk17 pkgs.nodejs pkgs.rustup ];
  ANDROID_HOME = sdk;
  shellHook = ''
    export NDK_HOME="$(echo ${sdk}/ndk/*)"
    # Gradle pobiera aapt2 z Mavena, a ten binarny plik nie uruchomi się na NixOS:
    export GRADLE_OPTS="-Dorg.gradle.project.android.aapt2FromMavenOverride=${sdk}/build-tools/36.0.0/aapt2"
  '';
}
```

Budowanie podpisanego APK — skrypt projektu (to samo robi GitHub Actions, `android.yml`):

```sh
npm ci
scripts/new-android-keystore.sh      # raz: klucz wydań POZA repozytorium (zrób kopię zapasową!)
ANDROID_KEYSTORE_FILE=~/.local/share/brulion/brulion-release.jks \
ANDROID_KEYSTORE_PASSWORD=... ANDROID_KEY_ALIAS=brulion \
BRULION_ANDROID_TARGETS=aarch64 \
  scripts/android-build.sh
# wynik: dist-android/brulion-<wersja>.apk
adb install -r dist-android/brulion-*.apk
```

Bez klucza skrypt podpisze APK kluczem jednorazowym — taki APK da się zainstalować, ale nie
zaktualizuje wersji podpisanej innym kluczem. `BRULION_ANDROID_TARGETS=aarch64` buduje tylko
dla 64-bitowych telefonów ARM (prawie wszystkie współczesne); bez tej zmiennej APK obejmuje
wszystkie architektury. Opcje: `scripts/android-build.sh --help`.

Ręcznie, samym Tauri CLI (APK niepodpisany) — zalecany jest jednak `scripts/android-build.sh`,
który robi to samo i omija znane pułapki:

```sh
npx tauri android init          # tylko raz: tworzy projekt Gradle w src-tauri/gen/android
# tylko raz: jeden wątek Gradle (patrz niżej)
printf '\norg.gradle.workers.max=1\n' >> src-tauri/gen/android/gradle.properties
npx tauri android build --apk --target aarch64
# src-tauri/gen/android/app/build/outputs/apk/universal/release/app-universal-release-unsigned.apk
```

Bez `org.gradle.workers.max=1` budowanie potrafi losowo przerwać się błędem
„failed to copy tauri-api to the plugin project … No such file or directory”: skrypty `build.rs`
wtyczek Tauri kopiują bibliotekę `tauri-android` (razem z jej katalogiem `build/`) do każdej
wtyczki, a Gradle może w tym samym czasie kompilować tę bibliotekę w innym wątku. Jeden wątek
Gradle rozdziela te kroki (Rust i tak kompiluje się na wszystkich rdzeniach).
`scripts/android-build.sh` dopisuje to ustawienie sam.

Do pracy nad aplikacją na podłączonym telefonie (USB, debugowanie włączone):
`npx tauri android dev`.

### Wtyczka `brulion-android` — dla programistów

* Kontrakt IPC (komendy, argumenty, zdarzenie `notification-activated`) jest w
  `docs/ARCHITECTURE.md`, sekcja „Tauri IPC contract”.
* Rust: `src-tauri/plugins/brulion-android/src` — na Androidzie komendy przekazują wywołania do
  Kotlina, na komputerze zwracają wartości neutralne. Testy (również zgodności Rust ↔ Kotlin ↔
  uprawnienia ↔ dokumentacja):
  `cargo test --workspace` w `src-tauri` (wtyczka jest członkiem workspace’u aplikacji; to samo
  uruchamia CI i `nix build`).
* Kotlin: `src-tauri/plugins/brulion-android/android` (pakiet `io.github.januszbit.brulion.android`).
  Testy jednostkowe i lint: w `src-tauri/gen/android` uruchom
  `./gradlew :tauri-plugin-brulion-android:testDebugUnitTest :tauri-plugin-brulion-android:lintDebug`
  (to samo robi `android.yml` w CI).
* Logi: `adb logcat -s BrulionAndroid:* Tauri:*`.
* Nastawione budziki: `adb shell dumpsys alarm | grep -B2 -A6 io.github.januszbit.brulion`.
* Test ponownego nastawiania bez restartu (odbiornik nie jest eksportowany, więc potrzebny jest
  `adb root`, np. emulator bez Google Play):
  `adb shell am broadcast -a android.intent.action.BOOT_COMPLETED -p io.github.januszbit.brulion`.

### Uwaga o dystrybucji

Google Play ogranicza uprawnienia `MANAGE_EXTERNAL_STORAGE` (dostęp do wszystkich plików)
i `USE_EXACT_ALARM` do wybranych kategorii aplikacji. Brulion jest przeznaczony do instalacji
z pliku APK (np. z wydań na GitHubie) lub z F-Droid.
