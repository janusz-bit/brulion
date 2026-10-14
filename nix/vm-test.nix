# NixOS VM test for services.brulion-web: SPA fallback, cache headers, base path redirect and
# the Nextcloud WebDAV proxy (against a fake upstream on the same machine).
{ pkgs, module }:

pkgs.testers.runNixOSTest {
  name = "brulion-web";

  nodes.server = {
    imports = [ module ];

    services.brulion-web = {
      enable = true;
      basePath = "/notatki/";
      # Fake "Nextcloud" below, installed under /nc like `https://example.com/nc`.
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
        add_header Set-Cookie "oc_session=secret; Path=/";
        return 207 "method=$request_method uri=$request_uri host=$host cookie=[$http_cookie] auth=$http_authorization\n";
      '';
    };
  };

  testScript = ''
    server.wait_for_unit("nginx.service")
    server.wait_for_open_port(80)

    def curl(args):
        return server.succeed(f"curl -sS --max-time 10 {args}")

    with subtest("app and SPA fallback"):
        index = curl("-f http://localhost/notatki/")
        assert "<title>Brulion</title>" in index, index
        assert 'src="/notatki/assets/' in index, index
        deep = curl("-f http://localhost/notatki/strona/dzi%C5%9B")
        assert deep == index, "deep links must get index.html"

    with subtest("base path redirect"):
        headers = curl("-I http://localhost/")
        assert "302" in headers.splitlines()[0], headers
        assert "/notatki/" in headers, headers

    with subtest("cache headers"):
        headers = curl("-fI http://localhost/notatki/")
        assert "Cache-Control: no-cache" in headers, headers
        assert "Content-Security-Policy:" in headers, headers
        headers = curl("-fI http://localhost/notatki/sw.js")
        assert "Cache-Control: no-cache" in headers, headers
        asset = server.succeed(
            "curl -sSf http://localhost/notatki/ | grep -o '/notatki/assets/[^\"]*\\.js' | head -n1"
        ).strip()
        headers = curl(f"-fI http://localhost{asset}")
        assert "immutable" in headers, headers
        assert "Content-Type: application/javascript" in headers or "text/javascript" in headers, headers

    with subtest("missing assets are 404, not the app"):
        status = curl("-o /dev/null -w '%{http_code}' http://localhost/notatki/assets/missing-chunk.js")
        assert status == "404", status

    with subtest("Nextcloud WebDAV proxy"):
        out = curl(
            "-X PROPFIND -u ala:haslo -H 'Cookie: nc_session=x' "
            "http://localhost/nc/remote.php/dav/files/ala/Notatki/"
        )
        assert "method=PROPFIND" in out, out
        assert "uri=/nc/remote.php/dav/files/ala/Notatki/" in out, out
        assert "host=127.0.0.1" in out, out
        assert "cookie=[]" in out, out
        assert "auth=Basic " in out, out
        headers = curl("-D - -o /dev/null -X PROPFIND http://localhost/nc/remote.php/dav/files/ala/")
        assert "Set-Cookie" not in headers, headers
  '';
}
