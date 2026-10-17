# `nix run .#brulion-android-sign -- <apk|aab> [options]`: signs an APK built by
# `nix build .#brulion-android` (or an AAB) with the release key, at run time — keystores and
# passwords never enter the Nix store. Runs on any Linux (apksigner from nixpkgs is Java), so it
# also signs on the machine that keeps the key.
{
  lib,
  writeShellApplication,
  apksigner,
  jdk21_headless,
  python3,
  coreutils,
  gnugrep,
  gnused,
}:

let
  # Checks the zip alignment Android needs (what `zipalign -c -P 16 4` checks): uncompressed
  # entries start at 4-byte boundaries, uncompressed native libraries at 16 KB (Android 15+
  # devices with 16 KB memory pages).
  checkAlignment = builtins.toFile "check-apk-alignment.py" ''
    import struct, sys, zipfile
    bad = []
    with open(sys.argv[1], "rb") as raw, zipfile.ZipFile(raw) as apk:
        for info in apk.infolist():
            if info.compress_type != zipfile.ZIP_STORED or info.is_dir():
                continue
            raw.seek(info.header_offset)
            header = raw.read(30)
            name_length, extra_length = struct.unpack("<HH", header[26:30])
            offset = info.header_offset + 30 + name_length + extra_length
            need = 16384 if info.filename.endswith(".so") else 4
            if offset % need:
                bad.append(f"{info.filename} at {offset} (needs {need})")
    if bad:
        sys.exit("not aligned: " + ", ".join(bad))
  '';
in
writeShellApplication {
  name = "brulion-android-sign";
  runtimeInputs = [
    apksigner
    jdk21_headless
    python3
    coreutils
    gnugrep
    gnused
  ];
  text = ''
    usage() {
      cat <<'EOF'
    Usage: brulion-android-sign INPUT.apk|INPUT.aab [options]

    Signs Brulion's unsigned APK (result/brulion-<version>-<abi>-unsigned.apk of
    `nix build .#brulion-android`) or AAB and writes the signed copy to the current directory
    (brulion-<version>-<abi>.apk). An APK is verified (apksigner verify, zip alignment for 16 KB
    pages) after signing.

    Options (and the environment variables they default to):
      --keystore PATH       keystore ($BRULION_ANDROID_KEYSTORE, default
                            ''${XDG_DATA_HOME:-~/.local/share}/brulion/brulion-release.jks, the key
                            of scripts/android-new-keystore.sh)
      --password-file PATH  file holding the keystore password ($BRULION_ANDROID_KEYSTORE_PASSWORD_FILE).
                            Without one: $QT_ANDROID_KEYSTORE_STORE_PASS if set (the variable
                            of scripts/android-build.sh and CI), else the keystore's path with
                            .password instead of .jks if that file exists, else apksigner asks.
      --alias ALIAS         key alias ($BRULION_ANDROID_KEY_ALIAS or $QT_ANDROID_KEYSTORE_ALIAS;
                            default: the keystore's only key)
      --out PATH            the signed file (default: ./<input name without -unsigned>)
      -h, --help            this help
    The key password is the keystore password (PKCS12 keystores have only one).
    EOF
    }
    die() {
      printf 'brulion-android-sign: %s\n' "$*" >&2
      exit 1
    }

    input=
    keystore=''${BRULION_ANDROID_KEYSTORE:-''${XDG_DATA_HOME:-$HOME/.local/share}/brulion/brulion-release.jks}
    password_file=''${BRULION_ANDROID_KEYSTORE_PASSWORD_FILE:-}
    alias=''${BRULION_ANDROID_KEY_ALIAS:-''${QT_ANDROID_KEYSTORE_ALIAS:-}}
    out=
    while (($#)); do
      case $1 in
        -h | --help) usage; exit 0 ;;
        --keystore) keystore=''${2:?--keystore needs a path}; shift 2 ;;
        --password-file) password_file=''${2:?--password-file needs a path}; shift 2 ;;
        --alias) alias=''${2:?--alias needs a name}; shift 2 ;;
        --out) out=''${2:?--out needs a path}; shift 2 ;;
        -*) die "unknown option: $1 (see --help)" ;;
        *)
          [[ -z $input ]] || die "only one input file, got '$input' and '$1'"
          input=$1
          shift
          ;;
      esac
    done
    [[ -n $input ]] || { usage >&2; exit 1; }
    [[ -f $input ]] || die "no such file: $input"
    [[ -f $keystore ]] || die "no keystore at $keystore (--keystore, or create one with scripts/android-new-keystore.sh)"

    name=$(basename -- "$input")
    case $name in
      *.apk) kind=apk ;;
      *.aab) kind=aab ;;
      *) die "$input is neither an .apk nor an .aab" ;;
    esac
    out=''${out:-./''${name/-unsigned/}}
    [[ $(realpath -m -- "$out") != "$(realpath -- "$input")" ]] || die "--out must differ from the input"

    # The password, never on a command line: a given password file, else
    # $QT_ANDROID_KEYSTORE_STORE_PASS, else the file next to the keystore, else apksigner asks.
    if [[ -z $password_file && -z ''${QT_ANDROID_KEYSTORE_STORE_PASS:-} && -f ''${keystore%.jks}.password ]]; then
      password_file=''${keystore%.jks}.password
    fi
    ks_pass=()
    jar_pass=()
    if [[ -n $password_file ]]; then
      [[ -r $password_file ]] || die "cannot read the password file $password_file"
      ks_pass=(--ks-pass "file:$password_file")
      jar_pass=(-storepass:file "$password_file")
    elif [[ -n ''${QT_ANDROID_KEYSTORE_STORE_PASS:-} ]]; then
      ks_pass=(--ks-pass env:QT_ANDROID_KEYSTORE_STORE_PASS)
      jar_pass=(-storepass:env QT_ANDROID_KEYSTORE_STORE_PASS)
    fi

    tmp=$(mktemp -d)
    trap 'rm -rf -- "$tmp"' EXIT
    mkdir -p -- "$(dirname -- "$out")"

    if [[ $kind == apk ]]; then
      alias_args=()
      [[ -z $alias ]] || alias_args=(--ks-key-alias "$alias")
      # apksigner aligns uncompressed entries itself (native libraries to 16 KB pages).
      # Its Java stack trace on a wrong password is shortened (but a password prompt must show).
      log=/dev/stderr
      ((''${#ks_pass[@]} == 0)) || log=$tmp/apksigner.log
      if ! apksigner sign --ks "$keystore" "''${alias_args[@]}" "''${ks_pass[@]}" \
        --v4-signing-enabled false --out "$tmp/signed.apk" "$input" 2>"$log"; then
        [[ $log == /dev/stderr ]] || grep -v -E '^\s+(at |\.\.\. [0-9]+ more)' "$log" | head -n 3 >&2 || true
        die "apksigner could not sign $input (wrong password or alias?)"
      fi
      apksigner verify --print-certs "$tmp/signed.apk" | grep -E '^Signer #1 certificate (DN|SHA-256 digest):' ||
        die "apksigner cannot verify the signed APK"
      python3 ${checkAlignment} "$tmp/signed.apk" || die "the signed APK is not aligned for 16 KB pages"
    else
      if [[ -z $alias && ''${#jar_pass[@]} -gt 0 ]]; then
        alias=$(keytool -list -keystore "$keystore" "''${jar_pass[@]}" 2>/dev/null |
          sed -nE 's/^([^,]+), .*PrivateKeyEntry.*/\1/p' | head -n 1)
      fi
      [[ -n $alias ]] || die "jarsigner needs the key alias: pass --alias"
      cp -- "$input" "$tmp/signed.aab"
      chmod u+w -- "$tmp/signed.aab"
      # jarsigner prints its errors ("jarsigner error: …") on stdout: show only those (not the
      # "jar signed." chatter); its password prompt goes to stderr and stays visible.
      if ! jarsigner -keystore "$keystore" "''${jar_pass[@]}" "$tmp/signed.aab" "$alias" |
        { grep -E '^jarsigner' >&2 || true; }; then
        die "jarsigner could not sign $input (wrong password or alias?)"
      fi
      jarsigner -verify "$tmp/signed.aab" >/dev/null || die "jarsigner cannot verify the signed AAB"
    fi
    mv -f -- "$tmp/signed.$kind" "$out"
    echo "signed: $out ($(du -h -- "$out" | cut -f1))"
  '';

  meta = {
    description = "Signs Brulion's Android APK/AAB with the release key (at run time)";
    license = lib.licenses.agpl3Plus;
    mainProgram = "brulion-android-sign";
  };
}
