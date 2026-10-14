# NixOS module: the Brulion desktop app (programs.brulion) and a self-hosted browser version
# (services.brulion-web, served by nginx). docs/NIXOS.md describes both.
#
# The flake exports it as nixosModules.default = import ./nix/module.nix { inherit self; }, so
# the package defaults to the flake's own package. Without a flake (`import ./module.nix { }`)
# the package is built from nix/package.nix with the system's nixpkgs.
{
  self ? null,
}:
{
  config,
  lib,
  pkgs,
  ...
}:

let
  inherit (lib)
    literalExpression
    mkEnableOption
    mkIf
    mkMerge
    mkOption
    optionalString
    types
    ;

  appId = "io.github.januszbit.brulion";

  appCfg = config.programs.brulion;
  webCfg = config.services.brulion-web;

  system = pkgs.stdenv.hostPlatform.system;
  defaultPackage =
    if self != null && self.packages ? ${system} then
      self.packages.${system}.default
    else
      pkgs.callPackage ./package.nix { };

  # XDG autostart entry: the app starts hidden in the system tray (`--hidden`).
  autostartEntry =
    pkgs.runCommand "${appId}-autostart.desktop"
      {
        nativeBuildInputs = [ pkgs.desktop-file-utils ];
        text = ''
          [Desktop Entry]
          Type=Application
          Name=Brulion
          Comment=Brulion in the system tray: reminders and alarms
          Comment[pl]=Brulion w zasobniku systemowym: przypomnienia i alarmy
          Exec=${lib.getExe appCfg.package} --hidden
          Icon=${appId}
          Terminal=false
          X-GNOME-Autostart-enabled=true
          X-KDE-autostart-phase=2
        '';
        passAsFile = [ "text" ];
      }
      ''
        cp "$textPath" "$out"
        desktop-file-validate "$out"
      '';

  # --- services.brulion-web -------------------------------------------------------------------

  webPackage =
    if webCfg.package != null then
      webCfg.package
    else if webCfg.release != null then
      pkgs.callPackage ./web-release.nix { inherit (webCfg.release) version tag hash; }
    else
      null;

  # A directory outside the Nix store (e.g. "/srv/brulion-web", deployed by other means) is
  # served as it is: it cannot be precompressed at build time.
  webInStore =
    webPackage != null
    && (
      lib.isDerivation webPackage
      || builtins.isPath webPackage
      || lib.hasPrefix "${builtins.storeDir}/" (toString webPackage)
    );

  # Copy with .gz (and .br) files next to the originals for gzip_static/brotli_static.
  webRoot =
    if webCfg.precompress && webInStore then
      pkgs.callPackage ./web-site.nix {
        src = webPackage;
        withBrotli = webCfg.brotli;
      }
    else
      webPackage;

  basePath = webCfg.basePath;

  # nextcloudUrl split into origin and path: https://example.com/nextcloud/ ->
  # { origin = "https://example.com"; path = "/nextcloud"; }.
  nextcloud =
    let
      parts = builtins.match "(https?)://([^/]+)(/.*)?" (lib.removeSuffix "/" webCfg.nextcloudUrl);
      path = builtins.elemAt parts 2;
    in
    {
      https = builtins.elemAt parts 0 == "https";
      origin = "${builtins.elemAt parts 0}://${builtins.elemAt parts 1}";
      path = if path == null then "" else path;
    };

  # The proxy keeps Nextcloud's own URL path, so the hrefs in PROPFIND answers
  # (/nextcloud/remote.php/dav/files/...) match the URLs the browser asked for.
  davLocation = "${nextcloud.path}/remote.php/dav/";

  cspValue = lib.escape [ "\"" ] (toString webCfg.contentSecurityPolicy);

  # Explicit types: the site's files only (independent of services.nginx.defaultMimeTypes).
  # application/wasm is required for streaming compilation.
  commonConfig = ''
    types {
      text/html html;
      text/javascript js mjs;
      application/wasm wasm;
      application/json json;
      application/manifest+json webmanifest;
      text/css css;
      image/svg+xml svg;
      image/png png;
      image/x-icon ico;
      text/plain txt;
      font/woff2 woff2;
    }
    default_type application/octet-stream;
    charset utf-8;
    charset_types text/plain text/css text/javascript application/json application/manifest+json image/svg+xml;
    gzip_static on;
    gzip_vary on;
    ${optionalString webCfg.brotli "brotli_static on;"}
    ${optionalString (!webInStore || !webCfg.precompress) ''
      gzip on;
      gzip_types text/javascript application/wasm application/json application/manifest+json text/css image/svg+xml text/plain;
    ''}
    add_header X-Content-Type-Options "nosniff" always;
    add_header Referrer-Policy "no-referrer" always;
    ${optionalString (webCfg.contentSecurityPolicy != null) ''
      add_header Content-Security-Policy "${cspValue}" always;
    ''}
  '';

  # `^~`: regular expression locations of the same host (e.g. Nextcloud's for *.js, *.wasm,
  # *.php) must not take over these paths.
  #
  # Caching: the app files are requested as `<file>?v=<content hash>` (scripts/wasm-build.sh),
  # so such a URL never changes and may be cached for good; everything else (index.html, sw.js,
  # the manifest, icons) is revalidated on every use ($brulion_web_cache_control, see
  # commonHttpConfig below). Revalidation works for files in the Nix store too, although they
  # all have the same mtime: nixpkgs' nginx derives their ETag from the store path hash.
  webLocations = {
    "^~ ${basePath}" = {
      alias = "${webRoot}/";
      index = "index.html";
      extraConfig = commonConfig + ''
        add_header Cache-Control $brulion_web_cache_control always;
      '';
    };
  }
  // lib.optionalAttrs (basePath != "/") {
    "= ${lib.removeSuffix "/" basePath}".return = "301 ${basePath}";
  }
  // lib.optionalAttrs (webCfg.nextcloudUrl != null) {
    "^~ ${davLocation}" = {
      # No URI part: nginx passes the request URI unchanged (as the client encoded it), also
      # with services.nginx.proxyResolveWhileRunning.
      proxyPass = nextcloud.origin;
      # Host must be Nextcloud's ($proxy_host), not this virtual host's; X-Forwarded-Host would
      # make a Nextcloud that trusts this proxy reject the request (untrusted domain).
      recommendedProxySettings = false;
      extraConfig = ''
        proxy_set_header Host $proxy_host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header Connection "";
        # Brulion authenticates every request (Basic auth with an app password); Nextcloud's
        # session cookies would trigger its CSRF check ("CSRF check failed").
        proxy_set_header Cookie "";
        proxy_hide_header Set-Cookie;
        proxy_http_version 1.1;
        proxy_request_buffering off;
        proxy_buffering off;
        proxy_connect_timeout 30s;
        proxy_read_timeout 300s;
        proxy_send_timeout 300s;
        client_max_body_size ${webCfg.maxUploadSize};
        ${optionalString nextcloud.https ''
          proxy_ssl_server_name on;
          proxy_ssl_verify on;
          proxy_ssl_verify_depth 3;
          proxy_ssl_trusted_certificate ${config.security.pki.caBundle};
        ''}
      '';
    };
  };

  nginxCfg = config.services.nginx;
  vhost = nginxCfg.virtualHosts.${webCfg.hostName};
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
  _file = ./module.nix;

  options.programs.brulion = {
    enable = mkEnableOption "Brulion, an offline-first Markdown notes app (desktop version)";

    package = mkOption {
      type = types.package;
      default = defaultPackage;
      defaultText = literalExpression "brulion.packages.\${pkgs.stdenv.hostPlatform.system}.default";
      description = ''
        The Brulion package. The default is this flake's package, built with the nixpkgs the
        flake input uses (`inputs.brulion.inputs.nixpkgs.follows = "nixpkgs"` builds it with
        the system's nixpkgs, sharing Qt with the rest of the system).
      '';
    };

    autostart = mkOption {
      type = types.bool;
      default = false;
      description = ''
        Start Brulion hidden in the system tray when a graphical session starts, so that
        reminders and alarms fire while its window is closed: an XDG autostart entry
        {file}`/etc/xdg/autostart/${appId}.desktop` running `brulion --hidden`. Users can
        turn it off for themselves in their desktop's autostart settings. Needs a desktop with
        a system tray (StatusNotifierItem); on GNOME install the AppIndicator extension.
      '';
    };
  };

  options.services.brulion-web = {
    enable = mkEnableOption "the Brulion browser version (Qt for WebAssembly, offline PWA), served by nginx";

    package = mkOption {
      type = types.nullOr types.path;
      default = null;
      example = literalExpression ''
        pkgs.fetchzip {
          url = "https://github.com/janusz-bit/brulion/releases/download/v0.1.0/brulion-web-0.1.0.zip";
          hash = "sha256-…";
          stripRoot = false;
        }
      '';
      description = ''
        The static browser build: a directory with `index.html`, `brulion.wasm`, `brulion.js`,
        `qtloader.js`, `sw.js`… — `dist-wasm/` made by `scripts/wasm-build.sh`, or the
        unpacked `brulion-web-<version>.zip` of a GitHub release (nixpkgs has no Qt for
        WebAssembly, so Nix cannot build it). A store path (a derivation or a path literal
        such as `./dist-wasm`) is copied with precompressed files
        ({option}`services.brulion-web.precompress`); an absolute path outside the store as a
        string (e.g. `"/srv/brulion-web"`) is served as it is and may be replaced at any time
        (nginx must be able to read it). Takes precedence over
        {option}`services.brulion-web.release`.
      '';
    };

    release = mkOption {
      type = types.nullOr (
        types.submodule {
          options = {
            version = mkOption {
              type = types.strMatching "[0-9]+\\.[0-9]+\\.[0-9]+";
              example = "0.1.0";
              description = "Version of the GitHub release (`brulion-web-<version>.zip`).";
            };
            tag = mkOption {
              type = types.nullOr types.str;
              default = null;
              example = "v0.2.0-rc.1";
              description = "Release tag, if not `v<version>` (pre-releases).";
            };
            hash = mkOption {
              type = types.str;
              default = "";
              example = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
              description = ''
                Hash of the unpacked zip (`fetchzip`). Leave it empty once: the build fails
                and prints the right value (`got: sha256-…`).
              '';
            };
          };
        }
      );
      default = null;
      example = {
        version = "0.1.0";
        hash = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
      };
      description = ''
        Serve the browser build of a GitHub release of Brulion
        (`https://github.com/janusz-bit/brulion/releases`), fetched at build time.
      '';
    };

    hostName = mkOption {
      type = types.str;
      default = "localhost";
      example = "notatki.example.com";
      description = ''
        Name of the nginx virtual host that serves Brulion. TLS, ACME, listen addresses and
        other settings belong to that host:
        {option}`services.nginx.virtualHosts.<hostName>` (e.g. `enableACME` and `forceSSL`).
        Browsers allow service workers, and so offline use and installing the app, only over
        HTTPS or on `localhost`. It can be an existing host, e.g. Nextcloud's
        ({option}`services.nextcloud.hostName`) with a {option}`services.brulion-web.basePath`
        such as `/brulion/`: the web app and Nextcloud are then the same origin and no proxy
        ({option}`services.brulion-web.nextcloudUrl`) is needed.
      '';
    };

    basePath = mkOption {
      type = types.strMatching "/([A-Za-z0-9._~-]+/)*";
      default = "/";
      example = "/notatki/";
      description = ''
        URL path under which Brulion is served; starts and ends with `/`. The build uses
        relative URLs only, so any path works without rebuilding it.
      '';
    };

    nextcloudUrl = mkOption {
      type = types.nullOr (types.strMatching "https?://[A-Za-z0-9.:_-]+(/[A-Za-z0-9._~%/-]*)?");
      default = null;
      example = "https://cloud.example.com";
      description = ''
        Address of a Nextcloud server to reach through this host. Browsers block WebDAV
        requests from the web app to another server unless that server allows CORS, which
        Nextcloud does not. When set, nginx forwards `<path>/remote.php/dav/` of this host to
        the same path on Nextcloud (`<path>` is the path part of this URL, usually empty), so
        in the web app you enter this host's address (plus `<path>`) as the Nextcloud server,
        e.g. `https://notatki.example.com` instead of `https://cloud.example.com`. Cookies are
        not passed through: Brulion authenticates every request (use an app password). HTTPS
        certificates of Nextcloud are verified against the system's CA bundle.
      '';
    };

    maxUploadSize = mkOption {
      type = types.str;
      default = "512M";
      description = "Largest file the WebDAV proxy accepts in one request (nginx `client_max_body_size`).";
    };

    precompress = mkOption {
      type = types.bool;
      default = true;
      description = ''
        Store gzip (and with {option}`services.brulion-web.brotli` brotli) compressed copies of
        the text and WebAssembly files next to them at build time, served by nginx's
        `gzip_static`/`brotli_static` (`brulion.wasm` shrinks about 3x, compressing once at
        the highest level instead of on every request). Only for packages in the Nix store;
        otherwise nginx compresses on the fly (gzip).
      '';
    };

    brotli = mkOption {
      type = types.bool;
      default = true;
      description = ''
        Also serve brotli (about 30 % smaller than gzip for `brulion.wasm`). Adds
        `pkgs.nginxModules.brotli` to {option}`services.nginx.additionalModules`, as
        {option}`services.nginx.recommendedBrotliSettings` does (the default nginx with that
        module is in the NixOS binary cache).
      '';
    };

    contentSecurityPolicy = mkOption {
      type = types.nullOr types.str;
      default = null;
      example = "default-src 'self'; script-src 'self' 'unsafe-inline' 'wasm-unsafe-eval'; connect-src 'self' https:; img-src 'self' data: blob: https:; style-src 'self' 'unsafe-inline'; object-src 'none'; base-uri 'self'; frame-ancestors 'self'";
      description = ''
        `Content-Security-Policy` header for the app, or `null` for none. Test a policy in the
        browser's console before using it: the HTML shell has an inline script and
        Emscripten's glue code may need `'unsafe-eval'`.
      '';
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
      environment.etc."xdg/autostart/${appId}.desktop" = mkIf appCfg.autostart {
        source = autostartEntry;
      };
    })

    (mkIf webCfg.enable {
      assertions = [
        {
          assertion = webPackage != null;
          message = ''
            services.brulion-web: set `package` (a directory with the browser build, e.g. from
            scripts/wasm-build.sh) or `release.version` (+ `release.hash`) of a GitHub release.
          '';
        }
      ];

      services.nginx = {
        enable = true;
        additionalModules = lib.optional webCfg.brotli pkgs.nginxModules.brotli;
        # Versioned URLs (?v=<hash>) never change; without a version, revalidate.
        commonHttpConfig = ''
          map $arg_v $brulion_web_cache_control {
            "" "no-cache";
            default "public, max-age=31536000, immutable";
          }
        '';
        virtualHosts.${webCfg.hostName}.locations = mkIf (webPackage != null) webLocations;
      };

      networking.firewall.allowedTCPPorts = mkIf webCfg.openFirewall (lib.unique vhostPorts);
    })
  ];
}
