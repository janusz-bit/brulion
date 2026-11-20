# Vendored code

* `Readability.js` — Mozilla's Readability 0.6.0 (`@mozilla/readability` on npm, 2025-03-03),
  unchanged; SHA-256 `34dcab3d0832d0019f02990eed6b6124e029e8c32b9f0c6f2550544ff8dff174`.
  Licence: Apache License 2.0 (`Readability-LICENSE.md`; Copyright (c) 2010 Arc90 Inc).
  The clipper (`content/extract.js`) runs it on a copy of the page to find the article's text.

To update: `npm pack @mozilla/readability@<version>` in an empty folder, copy `package/Readability.js`
and `package/LICENSE.md` here, and change the version and hash above (and in docs/CLIPPER.md).
