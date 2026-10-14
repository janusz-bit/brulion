# The browser build prepared for nginx (services.brulion-web): a copy with gzip (and brotli)
# compressed files next to the compressible originals, for gzip_static/brotli_static. Checks
# first that `src` looks like a Brulion browser build.
{
  lib,
  runCommand,
  brotli,
  src,
  withBrotli ? true,
}:

runCommand "brulion-web-site"
  {
    inherit src;
    nativeBuildInputs = lib.optional withBrotli brotli;
  }
  ''
    for f in index.html sw.js brulion.js brulion.wasm qtloader.js; do
      if [ ! -f "$src/$f" ]; then
        echo "error: $src is not a Brulion browser build (no $f): use dist-wasm/ from" >&2
        echo "scripts/wasm-build.sh or the brulion-web-<version>.zip of a GitHub release." >&2
        exit 1
      fi
    done

    cp -R --no-preserve=mode,ownership "$src" "$out"
    find "$out" -type f \( -name '*.html' -o -name '*.js' -o -name '*.mjs' -o -name '*.wasm' \
      -o -name '*.json' -o -name '*.webmanifest' -o -name '*.svg' -o -name '*.css' \
      -o -name '*.txt' \) -print0 |
      while IFS= read -r -d "" file; do
        gzip -9 -n -k "$file"
        ${lib.optionalString withBrotli ''brotli -q 11 -k "$file"''}
      done
  ''
