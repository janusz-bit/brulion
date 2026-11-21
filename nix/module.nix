# NixOS module: the Brulion desktop app (programs.brulion), a self-hosted browser version
# (services.brulion-web, served by nginx), the remote MCP server for claude.ai's and ChatGPT's
# connectors (services.brulion-mcp, `brulion-cli mcp serve`) and AI agents of this machine for the
# app on a phone (services.brulion-acp, `brulion-cli acp serve`). docs/NIXOS.md describes them.
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
  utils,
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
  mcpCfg = config.services.brulion-mcp;
  acpCfg = config.services.brulion-acp;

  system = pkgs.stdenv.hostPlatform.system;
  defaultPackage =
    if self != null && self.packages ? ${system} then
      self.packages.${system}.default
    else
      pkgs.callPackage ./package.nix { };
  defaultCliPackage =
    if self != null && self.packages ? ${system} then
      self.packages.${system}.brulion-cli
    else
      pkgs.callPackage ./cli.nix { brulion = defaultPackage; };

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
  frameAncestorsValue = lib.escape [ "\"" ] (toString webCfg.frameAncestors);

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
    ${optionalString (webCfg.frameAncestors != null) ''
      add_header Content-Security-Policy "frame-ancestors ${frameAncestorsValue}" always;
    ''}
    ${optionalString (webCfg.frameAncestors == "'self'") ''
      add_header X-Frame-Options "SAMEORIGIN" always;
    ''}
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

  # --- services.brulion-mcp -------------------------------------------------------------------

  mcpStateDir = "/var/lib/brulion-mcp";
  mcpAuthFile = "${mcpStateDir}/mcp-remote.json";
  mcpLoopback = lib.elem mcpCfg.listenAddress [
    "127.0.0.1"
    "::1"
    "localhost"
  ];
  # The notebook's device state (history, lock, index cache) and the server's state file live in
  # the state directory, whoever's notebook it serves.
  mcpCommonArgs = [
    (lib.getExe' mcpCfg.package "brulion-cli")
    "--vault"
    mcpCfg.vault
    "--state-dir"
    "${mcpStateDir}/device"
  ];
  mcpServeArgs =
    mcpCommonArgs
    ++ [
      "mcp"
      "serve"
      "--listen"
      mcpCfg.listenAddress
      "--port"
      (toString mcpCfg.port)
      "--auth-file"
      mcpAuthFile
    ]
    ++ lib.optionals (mcpCfg.publicUrl != null) [
      "--public-url"
      mcpCfg.publicUrl
    ]
    ++ lib.optionals (mcpCfg.tls != null) [
      "--tls-cert"
      mcpCfg.tls.certFile
      "--tls-key"
      mcpCfg.tls.keyFile
    ]
    ++ lib.optionals (mcpCfg.allowedOrigins != [ ]) [
      "--allowed-origin"
      (lib.concatStringsSep "," mcpCfg.allowedOrigins)
    ]
    ++ lib.optionals (mcpCfg.allowedHosts != [ ]) [
      "--allowed-host"
      (lib.concatStringsSep "," mcpCfg.allowedHosts)
    ]
    ++ lib.optional mcpCfg.readOnly "--read-only"
    ++ lib.optional mcpCfg.allowDestructive "--allow-destructive"
    ++ mcpCfg.extraArgs;

  # `sudo brulion-mcp password | clients | revoke …`: the server's commands as its user, on its
  # notebook and state file.
  mcpAdmin = pkgs.writeShellApplication {
    name = "brulion-mcp";
    runtimeInputs = [ pkgs.util-linux ];
    text = ''
      case "''${1:-}" in
        password | clients | revoke) ;;
        *)
          echo "usage: brulion-mcp password [--print-hash] | clients | revoke <connection> | revoke --all" >&2
          echo "(the remote MCP server's commands, run as its user; docs/NIXOS.md \"Remote MCP server\")" >&2
          exit 2
          ;;
      esac
      if [ "$(id -u)" != 0 ]; then
        echo "brulion-mcp: run it as root: sudo brulion-mcp $*" >&2
        exit 1
      fi
      command=$1
      shift
      cd ${mcpStateDir}
      exec runuser -u ${lib.escapeShellArg mcpCfg.user} -- env HOME=${mcpStateDir} \
        ${lib.escapeShellArgs mcpCommonArgs} mcp "$command" --auth-file ${mcpAuthFile} "$@"
    '';
  };

  # --- services.brulion-acp -------------------------------------------------------------------

  acpStateDir = "/var/lib/brulion-acp";
  acpStateFile = "${acpStateDir}/acp-remote.json";
  acpVault = if acpCfg.vault != null then acpCfg.vault else "${acpStateDir}/notebook";
  acpOwnVault = acpCfg.vault == null;
  acpLoopback = lib.elem acpCfg.listenAddress [
    "127.0.0.1"
    "::1"
    "localhost"
  ];
  acpAgentsFile = pkgs.writeText "brulion-acp-agents.json" (
    builtins.toJSON (
      lib.mapAttrsToList (id: a: {
        inherit id;
        inherit (a) name command;
      }) acpCfg.agents
    )
  );
  acpCommonArgs = [
    (lib.getExe' acpCfg.package "brulion-cli")
    "--vault"
    acpVault
    "--state-dir"
    "${acpStateDir}/device"
  ];
  acpServeArgs =
    acpCommonArgs
    ++ [
      "acp"
      "serve"
      "--agents"
      acpAgentsFile
      "--listen"
      acpCfg.listenAddress
      "--port"
      (toString acpCfg.port)
      "--state-file"
      acpStateFile
    ]
    ++ lib.optionals (acpCfg.publicUrl != null) [
      "--public-url"
      acpCfg.publicUrl
    ]
    ++ lib.optionals (acpCfg.tls != null) [
      "--tls-cert"
      acpCfg.tls.certFile
      "--tls-key"
      acpCfg.tls.keyFile
    ]
    ++ lib.optionals (acpCfg.allowedHosts != [ ]) [
      "--allowed-host"
      (lib.concatStringsSep "," acpCfg.allowedHosts)
    ]
    ++ lib.optional (!acpCfg.tools) "--no-tools"
    ++ lib.optionals (acpCfg.nextcloud != null) [
      "--nextcloud"
      acpCfg.nextcloud.url
      "--nextcloud-user"
      acpCfg.nextcloud.user
      "--nextcloud-folder"
      acpCfg.nextcloud.folder
      "--sync-interval"
      (toString acpCfg.nextcloud.interval)
    ]
    ++ lib.optionals (acpCfg.nextcloud != null && acpCfg.nextcloud.deviceName != null) [
      "--device-name"
      acpCfg.nextcloud.deviceName
    ]
    ++ acpCfg.extraArgs;

  # `sudo brulion-acp pair | clients | revoke …` (the server's commands as its user, on its state
  # file) and `sudo brulion-acp login <program> …` (an agent's own sign-in, e.g. `claude`, as that
  # user with its home: the subscription the agent then uses).
  acpAdmin = pkgs.writeShellApplication {
    name = "brulion-acp";
    runtimeInputs = [ pkgs.util-linux ];
    text = ''
      case "''${1:-}" in
        pair | clients | revoke | login) ;;
        *)
          echo "usage: brulion-acp pair | clients | revoke <device> | revoke --all | login <program> [args…]" >&2
          echo "(the AI agents server's commands, run as its user; docs/NIXOS.md \"AI agents for the phone\")" >&2
          exit 2
          ;;
      esac
      if [ "$(id -u)" != 0 ]; then
        echo "brulion-acp: run it as root: sudo brulion-acp $*" >&2
        exit 1
      fi
      command=$1
      shift
      cd ${acpStateDir}
      if [ "$command" = login ]; then
        if [ $# -eq 0 ]; then
          echo "usage: brulion-acp login <program> [args…] (e.g. brulion-acp login claude)" >&2
          exit 2
        fi
        # (the service's own environment: an agent keeps its sign-in under HOME or XDG_*)
        exec runuser -u ${lib.escapeShellArg acpCfg.user} -- env HOME=${acpStateDir} \
          XDG_CONFIG_HOME=${acpStateDir}/config XDG_DATA_HOME=${acpStateDir}/data \
          XDG_CACHE_HOME=/var/cache/brulion-acp PATH=${lib.makeBinPath acpCfg.path}:"$PATH" "$@"
      fi
      exec runuser -u ${lib.escapeShellArg acpCfg.user} -- env HOME=${acpStateDir} \
        ${lib.escapeShellArgs acpCommonArgs} acp "$command" --state-file ${acpStateFile} "$@"
    '';
  };
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

    webClipper = mkOption {
      type = types.bool;
      default = true;
      description = ''
        Connect the browsers to the Brulion Web Clipper extension (docs/CLIPPER.md): the native
        messaging host manifest naming the package's brulion-cli in
        {file}`/etc/chromium/native-messaging-hosts`, {file}`/etc/opt/chrome/native-messaging-hosts`,
        {file}`/etc/opt/edge/native-messaging-hosts` and {file}`/etc/opt/brave/native-messaging-hosts`,
        and the package in `programs.firefox.nativeMessagingHosts.packages` (Firefox of
        `programs.firefox`). A Firefox installed otherwise is connected per user with
        `brulion-cli clip install --browser firefox`.
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

    frameAncestors = mkOption {
      type = types.nullOr types.str;
      default = "'self'";
      example = "'self' https://cloud.example.com";
      description = ''
        Who may show the app inside a frame: the sources of the `frame-ancestors` directive, sent
        as a `Content-Security-Policy` header of its own next to
        {option}`services.brulion-web.contentSecurityPolicy` (a browser applies both), and as
        `X-Frame-Options: SAMEORIGIN` when it is the default `'self'`. A page of another site
        that frames the app can lead the user's clicks into it (clickjacking): name another
        origin only for a site of your own that embeds Brulion (Nextcloud's "External sites"
        on another host). `null` sends neither header.
      '';
    };

    openFirewall = mkOption {
      type = types.bool;
      default = false;
      description = "Open the ports the virtual host listens on (80, and 443 with TLS) in the firewall.";
    };
  };

  options.services.brulion-mcp = {
    enable = mkEnableOption ''
      the remote MCP server of a Brulion notebook (`brulion-cli mcp serve`): the notebook's tools
      for claude.ai, the Claude apps and ChatGPT, added there as a custom connector, with OAuth
      sign-in by the owner's password'';

    package = mkOption {
      type = types.package;
      default = defaultCliPackage;
      defaultText = literalExpression "brulion.packages.\${pkgs.stdenv.hostPlatform.system}.brulion-cli";
      description = ''
        The package with `bin/brulion-cli`: the small command-line package by default (Qt's base
        only); the app's package has it too.
      '';
    };

    vault = mkOption {
      type = types.str;
      example = "/var/lib/syncthing/Notatki";
      description = ''
        The notebook folder, e.g. the copy Syncthing keeps on this machine. The service user
        ({option}`services.brulion-mcp.user`) must be able to read it, and to write it unless
        {option}`services.brulion-mcp.readOnly`.
      '';
    };

    listenAddress = mkOption {
      type = types.str;
      default = "127.0.0.1";
      example = "0.0.0.0";
      description = ''
        The address to listen on. Keep the default behind a reverse proxy or a tunnel (Caddy,
        nginx, Tailscale Funnel, Cloudflare Tunnel) that adds HTTPS; another address needs
        {option}`services.brulion-mcp.publicUrl`, and HTTPS of its own
        ({option}`services.brulion-mcp.tls`) or a proxy in a trusted network.
      '';
    };

    port = mkOption {
      type = types.port;
      default = 8780;
      description = "The port to listen on.";
    };

    publicUrl = mkOption {
      type = types.nullOr (types.strMatching "https?://[^ ]+");
      default = null;
      example = "https://notes.example.com";
      description = ''
        The address the apps reach the server at (the proxy's or tunnel's): the OAuth issuer and,
        with `/mcp` added, the URL to paste into claude.ai or ChatGPT. It must be `https://`
        (`http://` only for this machine). `null`: `http://<listenAddress>:<port>`, which only
        works on this machine.
      '';
    };

    passwordFile = mkOption {
      type = types.nullOr types.str;
      default = null;
      example = "/run/secrets/brulion-mcp-password";
      description = ''
        A file with the owner's password, or its hash from `brulion-cli mcp password
        --print-hash` (a secret manager's file such as agenix's or sops-nix's; not a path in the
        Nix store, which everybody can read). systemd hands it to the service as a credential.
        `null`: the password set with `sudo brulion-mcp password`, kept as a salted hash in the
        state file.
      '';
    };

    readOnly = mkOption {
      type = types.bool;
      default = true;
      description = ''
        No app may change notes: the consent page offers reading only. Turn it off to let the
        owner allow writing (journal, tasks, reminders, edits) app by app.
      '';
    };

    allowDestructive = mkOption {
      type = types.bool;
      default = false;
      description = ''
        Also offer the destructive tools (replace, move and delete pages; deleted pages go to the
        trash) to apps allowed to write. Needs {option}`services.brulion-mcp.readOnly` off.
      '';
    };

    tls = mkOption {
      type = types.nullOr (
        types.submodule {
          options = {
            certFile = mkOption {
              type = types.str;
              example = "/var/lib/acme/notes.example.com/fullchain.pem";
              description = "The certificate chain (PEM), reloaded when it changes.";
            };
            keyFile = mkOption {
              type = types.str;
              example = "/var/lib/acme/notes.example.com/key.pem";
              description = "Its private key (PEM, RSA or EC, without a passphrase).";
            };
          };
        }
      );
      default = null;
      description = ''
        Serve HTTPS itself (no proxy). The service user must be able to read both files: with
        ACME, e.g. `security.acme.certs."notes.example.com".group = "brulion-mcp";`. Renewed
        certificates are taken without a restart.
      '';
    };

    allowedOrigins = mkOption {
      type = types.listOf types.str;
      default = [ ];
      example = [ "http://localhost:6274" ];
      description = "Origins of browser-based MCP clients (e.g. the MCP Inspector) allowed to call it (CORS).";
    };

    allowedHosts = mkOption {
      type = types.listOf types.str;
      default = [ ];
      description = ''
        More `Host` values it answers to, for a proxy that rewrites `Host` to something else than
        the public name or `127.0.0.1:<port>` (requests for other names are refused: DNS rebinding).
      '';
    };

    user = mkOption {
      type = types.str;
      default = "brulion-mcp";
      example = "syncthing";
      description = ''
        The user the server runs as. The default system user is created; to write notes that
        Syncthing keeps, name Syncthing's user ({option}`services.syncthing.user`).
      '';
    };

    group = mkOption {
      type = types.str;
      default = "brulion-mcp";
      description = "The service's group (created when it is the default).";
    };

    openFirewall = mkOption {
      type = types.bool;
      default = false;
      description = "Open the port in the firewall (only when it listens on another address than loopback).";
    };

    extraArgs = mkOption {
      type = types.listOf types.str;
      default = [ ];
      description = "More options of `brulion-cli mcp serve`.";
    };
  };

  options.services.brulion-acp = {
    enable = mkEnableOption ''
      AI agents of this machine for Brulion on other devices (`brulion-cli acp serve`): the app on
      a phone, which starts no programs, pairs with this server and talks to an agent here (Claude
      Code, Codex …) over the Agent Client Protocol; the agent works in this machine's copy of the
      notebook and runs its commands here'';

    package = mkOption {
      type = types.package;
      default = defaultCliPackage;
      defaultText = literalExpression "brulion.packages.\${pkgs.stdenv.hostPlatform.system}.brulion-cli";
      description = "The package with `bin/brulion-cli`.";
    };

    agents = mkOption {
      type = types.attrsOf (
        types.submodule (
          { name, ... }:
          {
            options = {
              name = mkOption {
                type = types.str;
                default = name;
                example = "Claude Code";
                description = "The agent's name, as the chat shows it.";
              };
              command = mkOption {
                type = types.str;
                example = literalExpression "lib.getExe pkgs.claude-agent-acp";
                description = ''
                  The program and its arguments (split as a shell would). Prefer a program from
                  nixpkgs to `npx -y …`, which fetches unpinned code when it starts.
                '';
              };
            };
          }
        )
      );
      default = { };
      example = literalExpression ''
        {
          claude = { name = "Claude Code"; command = lib.getExe pkgs.claude-agent-acp; };
          codex = { name = "Codex"; command = lib.getExe pkgs.codex-acp; };
        }
      '';
      description = ''
        The agents a device may start, by id (letters, digits, `-`, `_`): a device asks for one by
        its id, never for a program. Each signs in by itself as the service's user
        (`sudo brulion-acp login claude`, then `/login`).
      '';
    };

    path = mkOption {
      type = types.listOf types.package;
      default = with pkgs; [
        bashInteractive
        coreutils
        findutils
        gnugrep
        gnused
        git
      ];
      defaultText = literalExpression "with pkgs; [ bashInteractive coreutils findutils gnugrep gnused git ]";
      example = literalExpression "with pkgs; [ bashInteractive coreutils git claude-code ripgrep ]";
      description = ''
        Programs on the PATH of the agents and of the commands they run (and of
        `brulion-acp login`: add the agent's own program to sign in with, e.g. `pkgs.claude-code`).
      '';
    };

    vault = mkOption {
      type = types.nullOr types.str;
      default = null;
      example = "/var/lib/syncthing/Notatki";
      description = ''
        The notebook folder the agents work in. `null`: {file}`/var/lib/brulion-acp/notebook`, the
        server's own copy — kept with {option}`services.brulion-acp.nextcloud`. Another folder (a
        copy Syncthing keeps) must be readable and writable by the service user.
      '';
    };

    nextcloud = mkOption {
      type = types.nullOr (
        types.submodule {
          options = {
            url = mkOption {
              type = types.str;
              example = "https://cloud.example.com";
              description = "Nextcloud's address (as in the app's sync settings), or the notebook folder's WebDAV URL.";
            };
            user = mkOption {
              type = types.str;
              description = "The Nextcloud user.";
            };
            folder = mkOption {
              type = types.str;
              default = "Brulion";
              description = "The notebook's folder in that user's files (as in the app's sync settings).";
            };
            passwordFile = mkOption {
              type = types.str;
              example = "/run/agenix/brulion-acp-nextcloud";
              description = ''
                A file with a Nextcloud app password (Settings → Security → Devices & sessions), from a
                secret manager — not a path in the Nix store. Handed to the service as a credential.
              '';
            };
            interval = mkOption {
              type = types.ints.positive;
              default = 300;
              description = "Seconds between sync runs (a run also comes before and after every turn).";
            };
            deviceName = mkOption {
              type = types.nullOr types.str;
              default = null;
              description = "This machine's name in conflict copies (`null`: its host name).";
            };
          };
        }
      );
      default = null;
      description = ''
        Keep the notebook in sync with Nextcloud with Brulion's own sync engine, as one more
        device of the notebook: the agents see the notes just written on the phone, and the phone
        sees what they changed.
      '';
    };

    tools = mkOption {
      type = types.bool;
      default = true;
      description = "Give the agents' sessions Brulion's MCP server (`brulion-cli mcp`: search, queries, tasks, journals).";
    };

    listenAddress = mkOption {
      type = types.str;
      default = "127.0.0.1";
      description = ''
        The address to listen on: keep the default behind a reverse proxy that adds HTTPS and is
        reachable only in your own network (a tailnet, a VPN) — the agents run commands here.
      '';
    };

    port = mkOption {
      type = types.port;
      default = 8790;
      description = "The port to listen on.";
    };

    publicUrl = mkOption {
      type = types.nullOr (types.strMatching "https?://[^ ]+");
      default = null;
      example = "https://agent.example.com";
      description = ''
        The address the devices reach the server at (the proxy's): what `brulion-acp pair` shows
        and what the app is given. `https://` (`http://` only for this machine).
      '';
    };

    tls = mkOption {
      type = types.nullOr (
        types.submodule {
          options = {
            certFile = mkOption {
              type = types.str;
              description = "The certificate chain (PEM), reloaded when it changes.";
            };
            keyFile = mkOption {
              type = types.str;
              description = "Its private key (PEM, RSA or EC, without a passphrase).";
            };
          };
        }
      );
      default = null;
      description = "Serve HTTPS itself (no proxy); the service user must be able to read both files.";
    };

    allowedHosts = mkOption {
      type = types.listOf types.str;
      default = [ ];
      description = "More `Host` values it answers to (a proxy that rewrites `Host`).";
    };

    user = mkOption {
      type = types.str;
      default = "brulion-acp";
      description = ''
        The user the server and its agents run as: a system user of its own by default, with no
        groups and nothing of anyone else's to read or change.
      '';
    };

    group = mkOption {
      type = types.str;
      default = "brulion-acp";
      description = "The service's group (created when it is the default).";
    };

    openFirewall = mkOption {
      type = types.bool;
      default = false;
      description = "Open the port in the firewall (only when it listens on another address than loopback).";
    };

    extraArgs = mkOption {
      type = types.listOf types.str;
      default = [ ];
      description = "More options of `brulion-cli acp serve`.";
    };
  };

  config = mkMerge [
    (mkIf appCfg.enable {
      environment.systemPackages = [ appCfg.package ];
      environment.etc."xdg/autostart/${appId}.desktop" = mkIf appCfg.autostart {
        source = autostartEntry;
      };
    })

    (mkIf (appCfg.enable && appCfg.webClipper) {
      environment.etc =
        lib.genAttrs
          (map (dir: "${dir}/native-messaging-hosts/${appId}.json") [
            "chromium"
            "opt/chrome"
            "opt/edge"
            "opt/brave"
          ])
          (_: {
            source = "${appCfg.package}/share/brulion/native-messaging-hosts/${appId}.json";
          });
      programs.firefox.nativeMessagingHosts.packages = [ appCfg.package ];
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

    (mkIf mcpCfg.enable {
      assertions = [
        {
          assertion = mcpLoopback || mcpCfg.publicUrl != null;
          message = "services.brulion-mcp: listening on ${mcpCfg.listenAddress} needs publicUrl (the address the apps reach).";
        }
        {
          assertion = !(mcpCfg.allowDestructive && mcpCfg.readOnly);
          message = "services.brulion-mcp: allowDestructive needs readOnly = false.";
        }
        {
          assertion = mcpCfg.passwordFile == null || !lib.hasPrefix builtins.storeDir mcpCfg.passwordFile;
          message = "services.brulion-mcp: passwordFile must not be in the Nix store (everybody can read it).";
        }
      ];

      environment.systemPackages = [ mcpAdmin ];

      users.users = mkIf (mcpCfg.user == "brulion-mcp") {
        brulion-mcp = {
          isSystemUser = true;
          group = mcpCfg.group;
          home = mcpStateDir;
          description = "Brulion remote MCP server";
        };
      };
      users.groups = mkIf (mcpCfg.group == "brulion-mcp") { brulion-mcp = { }; };

      systemd.services.brulion-mcp = {
        description = "Brulion remote MCP server (claude.ai and ChatGPT connectors)";
        documentation = [ "https://github.com/janusz-bit/brulion/blob/main/docs/NIXOS.md" ];
        wantedBy = [ "multi-user.target" ];
        # Client ID metadata documents are fetched over the network after a sign-in.
        wants = [ "network-online.target" ];
        after = [ "network-online.target" ];
        environment = {
          HOME = mcpStateDir;
          XDG_CONFIG_HOME = "${mcpStateDir}/config";
          XDG_DATA_HOME = "${mcpStateDir}/data";
          XDG_CACHE_HOME = "/var/cache/brulion-mcp";
          LANG = "C.UTF-8";
        };
        serviceConfig = {
          ExecStart =
            utils.escapeSystemdExecArgs mcpServeArgs
            + optionalString (mcpCfg.passwordFile != null) " --password-file %d/password";
          LoadCredential = lib.optional (mcpCfg.passwordFile != null) "password:${mcpCfg.passwordFile}";
          User = mcpCfg.user;
          Group = mcpCfg.group;
          StateDirectory = "brulion-mcp";
          StateDirectoryMode = "0700";
          CacheDirectory = "brulion-mcp";
          WorkingDirectory = mcpStateDir;
          Restart = "on-failure";
          RestartSec = 5;
          UMask = "0027";

          # Hardening: the notebook is the only path it sees outside its own directories (read
          # only unless readOnly is off), no privileges, network sockets only.
          ProtectSystem = "strict";
          ProtectHome = "tmpfs";
          BindPaths = lib.optional (!mcpCfg.readOnly) mcpCfg.vault;
          BindReadOnlyPaths =
            lib.optional mcpCfg.readOnly mcpCfg.vault
            ++ lib.optionals (mcpCfg.tls != null) [
              (dirOf mcpCfg.tls.certFile)
              (dirOf mcpCfg.tls.keyFile)
            ];
          ReadWritePaths = lib.optional (!mcpCfg.readOnly) mcpCfg.vault;
          PrivateTmp = true;
          PrivateDevices = true;
          NoNewPrivileges = true;
          ProtectKernelTunables = true;
          ProtectKernelModules = true;
          ProtectKernelLogs = true;
          ProtectControlGroups = true;
          ProtectClock = true;
          ProtectHostname = true;
          ProtectProc = "invisible";
          ProcSubset = "pid";
          RestrictAddressFamilies = [
            "AF_INET"
            "AF_INET6"
            "AF_UNIX"
            "AF_NETLINK"
          ];
          RestrictNamespaces = true;
          RestrictRealtime = true;
          RestrictSUIDSGID = true;
          LockPersonality = true;
          MemoryDenyWriteExecute = true;
          RemoveIPC = true;
          KeyringMode = "private";
          SystemCallArchitectures = "native";
          SystemCallFilter = [
            "@system-service"
            "~@privileged"
          ];
          SystemCallErrorNumber = "EPERM";
          CapabilityBoundingSet = if mcpCfg.port < 1024 then [ "CAP_NET_BIND_SERVICE" ] else [ "" ];
          AmbientCapabilities = lib.optional (mcpCfg.port < 1024) "CAP_NET_BIND_SERVICE";
        };
      };

      networking.firewall.allowedTCPPorts = mkIf mcpCfg.openFirewall [ mcpCfg.port ];
    })

    (mkIf acpCfg.enable {
      assertions = [
        {
          assertion = acpCfg.agents != { };
          message = "services.brulion-acp: name at least one agent in `agents`.";
        }
        {
          assertion = acpLoopback || acpCfg.publicUrl != null;
          message = "services.brulion-acp: listening on ${acpCfg.listenAddress} needs publicUrl (the address the devices reach).";
        }
        {
          assertion =
            acpCfg.nextcloud == null || !lib.hasPrefix builtins.storeDir acpCfg.nextcloud.passwordFile;
          message = "services.brulion-acp: nextcloud.passwordFile must not be in the Nix store (everybody can read it).";
        }
      ];

      environment.systemPackages = [ acpAdmin ];

      users.users = mkIf (acpCfg.user == "brulion-acp") {
        brulion-acp = {
          isSystemUser = true;
          group = acpCfg.group;
          home = acpStateDir;
          # (`brulion-acp login` runs the agent's own program as this user)
          shell = pkgs.bashInteractive;
          description = "Brulion AI agents for other devices";
        };
      };
      users.groups = mkIf (acpCfg.group == "brulion-acp") { brulion-acp = { }; };

      systemd.services.brulion-acp = {
        description = "Brulion AI agents for other devices (brulion-cli acp serve)";
        documentation = [ "https://github.com/janusz-bit/brulion/blob/main/docs/NIXOS.md" ];
        wantedBy = [ "multi-user.target" ];
        # The agents talk to their providers, the sync to Nextcloud.
        wants = [ "network-online.target" ];
        after = [ "network-online.target" ];
        path = acpCfg.path;
        environment = {
          HOME = acpStateDir;
          XDG_CONFIG_HOME = "${acpStateDir}/config";
          XDG_DATA_HOME = "${acpStateDir}/data";
          XDG_CACHE_HOME = "/var/cache/brulion-acp";
          LANG = "C.UTF-8";
        };
        serviceConfig = {
          ExecStart =
            utils.escapeSystemdExecArgs acpServeArgs
            + optionalString (acpCfg.nextcloud != null) " --nextcloud-password-file %d/nextcloud-password";
          LoadCredential = lib.optional (
            acpCfg.nextcloud != null
          ) "nextcloud-password:${acpCfg.nextcloud.passwordFile}";
          User = acpCfg.user;
          Group = acpCfg.group;
          StateDirectory = [ "brulion-acp" ] ++ lib.optional acpOwnVault "brulion-acp/notebook";
          StateDirectoryMode = "0700";
          CacheDirectory = "brulion-acp";
          WorkingDirectory = acpStateDir;
          Restart = "on-failure";
          RestartSec = 5;
          UMask = "0077";
          # The agents and their commands end with the server.
          KillMode = "control-group";

          # Hardening that leaves the agents able to work: they run Node.js (a JIT: no
          # MemoryDenyWriteExecute), any program of `path`, and may sandbox their own commands
          # (namespaces). Nothing outside their own directories and the notebook is writable.
          ProtectSystem = "strict";
          ProtectHome = "tmpfs";
          BindPaths = lib.optional (!acpOwnVault) acpVault;
          BindReadOnlyPaths = lib.optionals (acpCfg.tls != null) [
            (dirOf acpCfg.tls.certFile)
            (dirOf acpCfg.tls.keyFile)
          ];
          ReadWritePaths = lib.optional (!acpOwnVault) acpVault;
          PrivateTmp = true;
          PrivateDevices = true;
          NoNewPrivileges = true;
          ProtectKernelTunables = true;
          ProtectKernelModules = true;
          ProtectKernelLogs = true;
          ProtectControlGroups = true;
          ProtectClock = true;
          ProtectHostname = true;
          ProtectProc = "invisible";
          RestrictRealtime = true;
          RestrictSUIDSGID = true;
          LockPersonality = true;
          RemoveIPC = true;
          KeyringMode = "private";
          SystemCallArchitectures = "native";
          CapabilityBoundingSet = if acpCfg.port < 1024 then [ "CAP_NET_BIND_SERVICE" ] else [ "" ];
          AmbientCapabilities = lib.optional (acpCfg.port < 1024) "CAP_NET_BIND_SERVICE";
        };
      };

      networking.firewall.allowedTCPPorts = mkIf acpCfg.openFirewall [ acpCfg.port ];
    })
  ];
}
