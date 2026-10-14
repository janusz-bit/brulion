# checks.x86_64-linux.web-vm: services.brulion-web and programs.brulion in a NixOS VM (needs
# KVM): MIME types, precompressed gzip/brotli files, cache headers, the base-path redirect, the
# Nextcloud WebDAV proxy (against a fake Nextcloud on the same machine), the autostart entry and
# a headless start of the installed app.
{ pkgs, module }:

let
  fakeWebBuild = pkgs.callPackage ./fake-web-build.nix { };
in
pkgs.testers.runNixOSTest {
  name = "brulion-web";

  nodes.server = {
    imports = [ module ];

    virtualisation.memorySize = 1536;
    environment.systemPackages = [
      pkgs.curl
      pkgs.brotli
    ];
    # The original files, to compare with what nginx sends.
    environment.etc."brulion-web-check".source = fakeWebBuild;

    programs.brulion = {
      enable = true;
      autostart = true;
    };

    services.brulion-web = {
      enable = true;
      package = fakeWebBuild;
      basePath = "/notatki/";
      # The fake "Nextcloud" below, installed under /nc like `https://example.com/nc`.
      nextcloudUrl = "http://127.0.0.1:8081/nc";
      openFirewall = true;
    };

    # Echoes what the proxy forwarded, and sets a cookie that must not reach the browser.
    services.nginx.virtualHosts.fake-nextcloud = {
      listen = [
        {
          addr = "127.0.0.1";
          port = 8081;
        }
      ];
      locations."/nc/remote.php/dav/".extraConfig = ''
        client_max_body_size 100m;
        add_header Set-Cookie "oc_session=secret; Path=/";
        return 207 "method=$request_method uri=$request_uri host=$http_host cookie=[$http_cookie] auth=$http_authorization\n";
      '';
    };
  };

  testScript = ''
    server.wait_for_unit("nginx.service")
    server.wait_for_open_port(80)

    base = "http://localhost/notatki/"

    def curl(args):
        return server.succeed(f"curl -sS --max-time 20 {args}")

    def headers(args):
        out = curl(f"-D - -o /dev/null {args}")
        return {
            line.split(":", 1)[0].strip().lower(): line.split(":", 1)[1].strip()
            for line in out.splitlines()[1:]
            if ":" in line
        }, out.splitlines()[0]

    with subtest("index.html and sw.js: revalidated, by ETag (store path hash), not by mtime"):
        h, status = headers(base)
        assert " 200" in status, status
        assert h["content-type"] == "text/html; charset=utf-8", h
        assert h["cache-control"] == "no-cache", h
        assert h["x-content-type-options"] == "nosniff", h
        assert "last-modified" not in h, h
        etag = h["etag"]
        _, status = headers(f"-H 'If-None-Match: {etag}' {base}")
        assert " 304" in status, status
        h, status = headers(f"-H 'If-Modified-Since: Thu, 01 Jan 1970 00:00:01 GMT' {base}sw.js")
        assert " 200" in status, status
        assert h["content-type"].startswith("text/javascript"), h
        assert h["cache-control"] == "no-cache", h

    with subtest("base path without the slash redirects"):
        h, status = headers("http://localhost/notatki")
        assert " 301" in status, status
        assert h["location"].endswith("/notatki/"), h

    with subtest("brulion.wasm: application/wasm, precompressed, versioned URLs cached for good"):
        h, status = headers(f"{base}brulion.wasm?v=abc123")
        assert h["content-type"] == "application/wasm", h
        assert h["cache-control"] == "public, max-age=31536000, immutable", h
        assert "content-encoding" not in h, h
        h, _ = headers(f"-H 'Accept-Encoding: gzip' {base}brulion.wasm?v=abc123")
        assert h["content-encoding"] == "gzip", h
        assert h["vary"] == "Accept-Encoding", h
        assert int(h["content-length"]) < 1000000 // 3, h
        h, _ = headers(f"-H 'Accept-Encoding: gzip, deflate, br' {base}brulion.wasm?v=abc123")
        assert h["content-encoding"] == "br", h
        assert h["content-type"] == "application/wasm", h
        server.succeed(
            f"curl -sSf -H 'Accept-Encoding: gzip' '{base}brulion.wasm?v=abc123' | gunzip"
            " | cmp - /etc/brulion-web-check/brulion.wasm"
        )
        server.succeed(
            f"curl -sSf -H 'Accept-Encoding: br' '{base}brulion.wasm?v=abc123' | brotli -d"
            " | cmp - /etc/brulion-web-check/brulion.wasm"
        )
        h, _ = headers(f"{base}brulion.wasm")
        assert h["cache-control"] == "no-cache", h

    with subtest("other files"):
        h, _ = headers(f"{base}qtloader.js?v=abc123")
        assert h["content-type"].startswith("text/javascript"), h
        assert "immutable" in h["cache-control"], h
        h, _ = headers(f"{base}manifest.webmanifest")
        assert h["content-type"].startswith("application/manifest+json"), h
        h, _ = headers(f"{base}icons/logo.svg")
        assert h["content-type"].startswith("image/svg+xml"), h
        _, status = headers(f"{base}missing.js")
        assert " 404" in status, status

    with subtest("Nextcloud WebDAV proxy"):
        out = curl(
            "-X PROPFIND -u ala:haslo -H 'Cookie: nc_session=x' -H 'Depth: 1' "
            "'http://localhost/nc/remote.php/dav/files/ala/Notatki/Za%C5%BC%C3%B3%C5%82%C4%87%20g%C4%99%C5%9Bl%C4%85.md'"
        )
        assert "method=PROPFIND" in out, out
        assert "uri=/nc/remote.php/dav/files/ala/Notatki/Za%C5%BC%C3%B3%C5%82%C4%87%20g%C4%99%C5%9Bl%C4%85.md" in out, out
        assert "host=127.0.0.1:8081" in out, out
        assert "cookie=[]" in out, out
        assert "auth=Basic " in out, out
        h, status = headers("-X PROPFIND http://localhost/nc/remote.php/dav/files/ala/")
        assert " 207" in status, status
        assert "set-cookie" not in h, h
        # Larger than nginx's (1m) and NixOS's (10m) default client_max_body_size.
        server.succeed("head -c 12582912 /dev/zero > /tmp/big")
        out = curl("-X PUT --data-binary @/tmp/big http://localhost/nc/remote.php/dav/files/ala/big.bin")
        assert "method=PUT" in out, out

    with subtest("desktop app: menu entry, icon, autostart in the tray"):
        server.succeed("test -f /run/current-system/sw/share/applications/io.github.januszbit.brulion.desktop")
        server.succeed("test -f /run/current-system/sw/share/icons/hicolor/scalable/apps/io.github.januszbit.brulion.svg")
        server.succeed("test -f /run/current-system/sw/share/metainfo/io.github.januszbit.brulion.metainfo.xml")
        server.succeed(
            "grep -Ex 'Exec=/nix/store/[^ ]+/bin/brulion --hidden' /etc/xdg/autostart/io.github.januszbit.brulion.desktop"
        )
        # Headless start: still running after 10 s (timeout's exit code 124), no QML errors.
        # execute(), not succeed(): the driver runs commands with `set -e`, so the expected
        # status 124 must not end the command.
        status, _ = server.execute(
            "cd /tmp && HOME=/tmp QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software"
            " timeout 10 brulion > /tmp/brulion.log 2>&1"
        )
        out = server.succeed("cat /tmp/brulion.log")
        assert status == 124, f"brulion exited with status {status} within 10 s:\n{out}"
        assert "QQmlApplicationEngine failed" not in out and "is not installed" not in out, out
  '';
}
