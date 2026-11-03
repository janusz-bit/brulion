#!/usr/bin/env bash
# The checked-in PDFs of the PDF viewer's tests (tst_pdfpages, the Android emulator's --pdf mode,
# tests/web/pdf.spec.mjs), written again:
#
#   wyklad.pdf             3 pages: "Wykład 3 — Zażółć gęślą jaźń" (A4), a slide (A4 landscape) with a
#                          red square, "Strona trzecia: żółw" (A4) — QPdfWriter, through
#                          `BRULION_PDF_FIXTURES_OUT=<dir> tst_pdfpages writeFixtures`
#   spis.pdf               3 pages written by hand (tst_pdfpages: linkedPdf()) with a table of
#                          contents, links to its pages, to a web page and ones that must never open
#                          (JavaScript, a launch), and a red square annotation — the same way
#   zaszyfrowany.pdf       wyklad.pdf with the user password "tajne" (AES-256): locked
#   zaszyfrowany-rc4.pdf   the same with 40-bit RC4 (an old Acrobat's): locked
#   haslo-polskie.pdf      the same with the user password "zażółć" (AES-256, UTF-8): locked
#   tylko-wlasciciel.pdf   an owner password only (permissions): opens
#
# The encrypted ones come from qpdf (from PATH, else `nix shell nixpkgs#qpdf`) with --static-id and
# --static-aes-iv, so the files are the same each time. Run from the repository's root in the dev
# shell after a build (BRULION_BUILD_DIR: the build dir, default ./build):
#
#   scripts/remote-build.sh -n <name> tst_pdfpages --run 'tests/data/pdf/make-fixtures.sh' --pull tests/data/pdf
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
build="${BRULION_BUILD_DIR:-../../../build}"
test_binary="$(find "${build}" -name tst_pdfpages -type f -perm -u+x | head -1)"
[[ -x "${test_binary}" ]] || { echo "make-fixtures: build tst_pdfpages first (${build})" >&2; exit 1; }

out="$(mktemp -d)"
trap 'rm -rf "${out}"' EXIT
QT_QPA_PLATFORM=offscreen BRULION_PDF_FIXTURES_OUT="${out}" "${test_binary}" writeFixtures >/dev/null
cp "${out}/wyklad.pdf" wyklad.pdf
cp "${out}/spis.pdf" spis.pdf

qpdf_cmd=(qpdf)
command -v qpdf >/dev/null || qpdf_cmd=(nix shell nixpkgs#qpdf -c qpdf)
"${qpdf_cmd[@]}" --static-id --static-aes-iv --encrypt tajne tajne 256 -- wyklad.pdf zaszyfrowany.pdf
"${qpdf_cmd[@]}" --static-id --allow-weak-crypto --encrypt tajne tajne 40 -- wyklad.pdf zaszyfrowany-rc4.pdf
"${qpdf_cmd[@]}" --static-id --static-aes-iv --encrypt zażółć zażółć 256 -- wyklad.pdf haslo-polskie.pdf
"${qpdf_cmd[@]}" --static-id --static-aes-iv --encrypt "" wlasciciel 256 --modify=none --extract=n -- \
    wyklad.pdf tylko-wlasciciel.pdf
ls -l ./*.pdf
