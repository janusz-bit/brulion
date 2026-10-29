# lz-string test vectors

Copied unchanged from lz-string's own test data (https://github.com/pieroxy/lz-string,
`test/data/<name>/data.bin` and `base64.bin`, commit 9a3d312, MIT licence: LICENSE.md here),
renamed `<name>.data.bin` / `<name>.base64.bin`:

* `hello_world` — "Hello world"
* `repeated` — "abcd", 128 of each character
* `all_ascii` — every printable ASCII character
* `tattoo` — a paragraph of English text

Both files hold a JavaScript string as UTF-16 code units, big-endian (lz-string's
`loadBinaryFile`): two bytes a unit; a file of an odd length ends with the high byte of a last unit
whose low byte is 0. `data.bin` is the input, `base64.bin` what `LZString.compressToBase64` gives
for it (and what `decompressFromBase64` turns back into the input).

Used by `tests/core/drawing/tst_drawing_exmd.cpp` (Brulion's C++ LZ-String) and
`tests/data/drawing/make-fixtures.py` (its independent Python port, checked before it writes the
`obsidian_*.excalidraw.md` fixtures).
