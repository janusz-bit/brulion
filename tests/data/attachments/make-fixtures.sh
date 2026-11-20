#!/usr/bin/env bash
# The pictures of the attachments' search tests (tst_ocr, tst_attachmentindexer, the shell's
# self-test), written again by `BRULION_OCR_FIXTURES_OUT=<dir> tst_ocr writeFixtures` with the bundled
# Literata (assets/fonts/ui), so they are the same on every machine:
#
#   tablica.png    a whiteboard's photo, cropped: "Spotkanie zespołu we wtorek" / "Żółw idzie powoli
#                  przez łąkę" (8-bit grey)
#   obrocone.jpg   a phone's photo of notes stored lying on its side with the EXIF orientation 6
#                  (rotate 90°) that turns it upright: "Notatki z wykładu" / "Twierdzenie Pitagorasa"
#   skan.pdf       a scanned A4 page — a 150 dpi picture, no text layer: "Całka oznaczona" /
#                  "Źródło: notatki z ćwiczeń" / "Pole pod wykresem funkcji"
#
# The text PDF with Polish text is tests/data/pdf/wyklad.pdf. Run from the repository's root in the
# dev shell after a build (BRULION_BUILD_DIR: the build dir, default ./build).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
build="${BRULION_BUILD_DIR:-../../../build}"
test_binary="$(find "${build}" -name tst_ocr -type f -perm -u+x | head -1)"
[[ -x "${test_binary}" ]] || { echo "make-fixtures: build tst_ocr first (${build})" >&2; exit 1; }
QT_QPA_PLATFORM=offscreen BRULION_OCR_FIXTURES_OUT="$PWD" "${test_binary}" writeFixtures >/dev/null
# The EXIF orientation 6 ("rotate 90° clockwise to show"): Qt's JPEG writer cannot write it.
exiftool_cmd=(exiftool)
command -v exiftool >/dev/null || exiftool_cmd=(nix shell nixpkgs#exiftool -c exiftool)
"${exiftool_cmd[@]}" -q -overwrite_original -n -Orientation=6 obrocone.jpg
ls -l tablica.png obrocone.jpg skan.pdf
