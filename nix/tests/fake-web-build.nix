# Stand-in for the browser build in the module tests (the real one needs Qt for WebAssembly,
# see docs/WEB.md): the same file names, brulion.wasm = about 1 MiB of compressible bytes starting
# with the WebAssembly magic.
{ runCommand }:

runCommand "brulion-web-fake" { } ''
  mkdir -p $out/icons
  cat > $out/index.html <<'HTML'
  <!doctype html><html lang="pl"><head><meta charset="utf-8"><title>Brulion</title></head>
  <body><script src="qtloader.js?v=abc123"></script></body></html>
  HTML
  echo "self.addEventListener('fetch', () => {}); // zażółć gęślą jaźń" > $out/sw.js
  for f in brulion.js qtloader.js; do
    for i in $(seq 2000); do echo "function f$i() { return $i; } // $f"; done > $out/$f
  done
  { printf '\0asm\1\0\0\0'; for i in $(seq 61680); do printf 'brulion-%08d\n' $i; done; } \
    > $out/brulion.wasm
  echo '{"name": "Brulion", "start_url": "./"}' > $out/manifest.webmanifest
  cp ${../../assets/logo.svg} $out/icons/logo.svg
  touch $out/.nojekyll
''
