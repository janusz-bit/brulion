# NixOS module: the Brulion desktop app (programs.brulion) and a self-hosted web version
# (services.brulion-web, served by nginx). The flake's nixosModules.default imports this file
# and points the `package` options at the flake's own packages; on its own the module expects
# `pkgs.brulion` / `pkgs.brulion-web` (overlays.default).
{
  config,
  lib,
  pkgs,
  ...
}:

let
  inherit (lib)
    mkEnableOption
    mkIf
    mkMerge
    mkOption
    mkPackageOption
    types
    ;

  appCfg = config.programs.brulion;
  webCfg = config.services.brulion-web;

  # The web build bakes its base path in, so rebuild the package for the configured one.
  webPackage =
    if webCfg.package ? override && (lib.functionArgs webCfg.package.override) ? basePath then
      webCfg.package.override { inherit (webCfg) basePath; }
    else
      webCfg.package;

  # nginx document root: the app itself for "/", otherwise a tree with the app at basePath, so
  # that plain `root` + `try_files` work (`alias` + `try_files` is unreliable in nginx).
  webRoot =
    if webCfg.basePath == "/" then
      webPackage
    else
      pkgs.linkFarm "brulion-web-root" [
        {
          name = lib.removePrefix "/" (lib.removeSuffix "/" webCfg.basePath);
          path = webPackage;
        }
      ];

  # nextcloudUrl without a trailing slash, and its path part:
  # https://example.com/nextcloud -> "/nextcloud", https://cloud.example.com -> "".
  nextcloud =
    let
      url = lib.removeSuffix "/" webCfg.nextcloudUrl;
      parts = builtins.match "(https?://[^/]+)(/.*)?" url;
      path = builtins.elemAt parts 1;
    in
    {
      inherit url;
      path = if path == null then "" else path;
    };

  # The proxy keeps Nextcloud's own URL path, so the `<d:href>`s in PROPFIND answers
  # (/remote.php/dav/files/...) match the URLs the browser requested.
  davLocation = "${nextcloud.path}/remote.php/dav/";

  securityHeaders = ''
    add_header X-Content-Type-Options "nosniff" always;
    add_header Referrer-Policy "no-referrer" always;
  ''
  + lib.optionalString (webCfg.contentSecurityPolicy != null) ''
    add_header Content-Security-Policy "${webCfg.contentSecurityPolicy}" always;
  '';

  vhost = config.services.nginx.virtualHosts.${webCfg.hostName};
  nginxCfg = config.services.nginx;
  vhostHasTls = vhost.onlySSL || vhost.addSSL || vhost.forceSSL;
  vhostPorts =
    if vhost.listen != [ ] then
      map (
        l:
        if l.port != null then
          l.port
        else if l.ssl then
          nginxCfg.defaultSSLListenPort
        else
          nginxCfg.defaultHTTPListenPort
      ) vhost.listen
    else
      lib.optional (!vhost.onlySSL) nginxCfg.defaultHTTPListenPort
      ++ lib.optional vhostHasTls nginxCfg.defaultSSLListenPort;
in
{
  options.programs.brulion = {
    enable = mkEnableOption "Brulion, an offline-first Markdown notes app (desktop version)";

    package = mkPackageOption pkgs "brulion" { };

    autostart = mkOption {
      type = types.bool;
      default = false;
      description = ''
        Start Brulion hidden in the system tray when a graphical session starts (an XDG
        autostart entry, {file}`/etc/xdg/autostart/brulion.desktop`, running
        `brulion --hidden`), so that reminders and alarms fire while its window is closed.
        Needs a desktop with a system tray (StatusNotifierItem/AppIndicator); on GNOME install
        the AppIndicator extension.
      '';
    };
  };

  options.services.brulion-web = {
    enable = mkEnableOption "the Brulion web version (PWA), served by nginx";

    package = mkPackageOption pkgs "brulion-web" {
      extraDescription = ''
        It is rebuilt for {option}`services.brulion-web.basePath` with
        `.override { basePath = ...; }` when it accepts that argument.
      '';
    };

    hostName = mkOption {
      type = types.str;
      default = "localhost";
      example = "notatki.example.com";
      description = ''
        Name of the nginx virtual host that serves Brulion. TLS, ACME and listen addresses are
        set on that host directly: {option}`services.nginx.virtualHosts.<hostName>`
        (e.g. `enableACME` and `forceSSL`). Browsers allow service workers, and so offline use
        and installing the app, only over HTTPS or on `localhost`.
      '';
    };

    basePath = mkOption {
      type = types.strMatching "/([A-Za-z0-9._~-]+/)*";
      default = "/";
      example = "/notatki/";
      description = "URL path under which Brulion is served. Starts and ends with `/`.";
    };

    nextcloudUrl = mkOption {
      type = types.nullOr (types.strMatching "https?://[A-Za-z0-9.:_-]+(/[A-Za-z0-9._~%/-]*)?");
      default = null;
      example = "https://cloud.example.com";
      description = ''
        Address of a Nextcloud server to reach through this host. Browsers block WebDAV
        requests from the web app to another server unless that server allows CORS, which
        Nextcloud does not. When set, nginx forwards `<path>/remote.php/dav/` of this host to
        the same path on Nextcloud (`<path>` = the path part of this URL, usually empty), so
        in the web app you enter this host's address (plus `<path>`) as the Nextcloud server,
        e.g. `https://notatki.example.com` instead of `https://cloud.example.com`. Cookies are
        not passed through: Brulion authenticates every request (use an app password).
      '';
    };

    maxUploadSize = mkOption {
      type = types.str;
      default = "512M";
      description = "Largest file the WebDAV proxy accepts in one request (nginx `client_max_body_size`).";
    };

    contentSecurityPolicy = mkOption {
      type = types.nullOr types.str;
      # Mirrors the CSP of the desktop app (src-tauri/tauri.conf.json): notes may come from
      # anywhere, so scripts are limited to the app's own files. Images over http(s) are allowed
      # (Markdown images), as are requests to any sync server.
      default = lib.concatStringsSep "; " [
        "default-src 'self'"
        "script-src 'self' 'wasm-unsafe-eval'"
        "style-src 'self' 'unsafe-inline'"
        "img-src 'self' data: blob: https: http:"
        "media-src 'self' data: blob:"
        "font-src 'self' data:"
        "connect-src 'self' data: blob: https: http:"
        "worker-src 'self' blob:"
        "frame-src 'self' blob:"
        "object-src 'none'"
        "base-uri 'self'"
        "form-action 'none'"
        "frame-ancestors 'self'"
      ];
      description = "`Content-Security-Policy` header for the app, or `null` to send none.";
    };

    openFirewall = mkOption {
      type = types.bool;
      default = false;
      description = "Open the ports the virtual host listens on (80, and 443 with TLS) in the firewall.";
    };
  };

  config = mkMerge [
    (mkIf appCfg.enable {
      environment.systemPackages = [ appCfg.package ];

      environment.etc."xdg/autostart/brulion.desktop" = mkIf appCfg.autostart {
        text = ''
          [Desktop Entry]
          Type=Application
          Name=Brulion
          Comment=Brulion in the system tray (reminders and alarms)
          Comment[pl]=Brulion w zasobniku systemowym (przypomnienia i alarmy)
          Exec=${lib.getExe appCfg.package} --hidden
          Icon=brulion
          Terminal=false
          X-GNOME-Autostart-enabled=true
          X-GNOME-Autostart-Delay=5
          X-KDE-autostart-after=panel
        '';
      };
    })

    (mkIf webCfg.enable {
      services.nginx = {
        enable = true;
        virtualHosts.${webCfg.hostName} = {
          root = webRoot;
          extraConfig = ''
            gzip on;
            gzip_vary on;
            gzip_min_length 1024;
            gzip_types text/css text/plain application/javascript application/json application/manifest+json application/wasm image/svg+xml;
          '';
          locations = {
            # index.html, sw.js, the web manifest, icons: always revalidate, so a new version
            # (and its service worker) is picked up right away. Unknown paths (deep links) get
            # the app.
            ${webCfg.basePath} = {
              tryFiles = "$uri $uri/ ${webCfg.basePath}index.html";
              extraConfig = securityHeaders + ''
                add_header Cache-Control "no-cache" always;
              '';
            };
            # Content-hashed build output: cache forever. A missing chunk is a 404, not the app.
            "${webCfg.basePath}assets/" = {
              tryFiles = "$uri =404";
              extraConfig = securityHeaders + ''
                add_header Cache-Control "public, max-age=31536000, immutable" always;
              '';
            };
          }
          // lib.optionalAttrs (webCfg.basePath != "/") {
            "= /".return = "302 ${webCfg.basePath}";
          }
          // lib.optionalAttrs (webCfg.nextcloudUrl != null) {
            ${davLocation} = {
              proxyPass = "${nextcloud.url}/remote.php/dav/";
              # Host must be Nextcloud's ($proxy_host), not this virtual host's.
              recommendedProxySettings = false;
              extraConfig = ''
                proxy_set_header Host $proxy_host;
                proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
                proxy_set_header X-Forwarded-Proto $scheme;
                proxy_set_header X-Forwarded-Host $host;
                # Basic auth on every request; Nextcloud session cookies would trigger its
                # CSRF check ("CSRF check failed") for requests without a request token.
                proxy_set_header Cookie "";
                proxy_hide_header Set-Cookie;
                proxy_ssl_server_name on;
                proxy_http_version 1.1;
                proxy_request_buffering off;
                proxy_buffering off;
                proxy_read_timeout 300s;
                proxy_send_timeout 300s;
                client_max_body_size ${webCfg.maxUploadSize};
              '';
            };
          };
        };
      };

      networking.firewall.allowedTCPPorts = mkIf webCfg.openFirewall (lib.unique vhostPorts);
    })
  ];

}
