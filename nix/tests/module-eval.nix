# checks.<system>.nixos-module: nix/module.nix in a minimal NixOS system.
# * The system with programs.brulion (+ autostart) and services.brulion-web (+ Nextcloud proxy)
#   evaluates: its toplevel drvPath is computed, the system itself is not built.
# * The generated nginx.conf (checked by gixy while it is built) and the autostart entry
#   (desktop-file-validate) build and contain what they should.
# * A release-based configuration under a sub-path evaluates, and a missing web package is
#   reported by an assertion.
# * services.brulion-mcp: its unit (ExecStart with the options, the password as a credential, the
#   hardening) and the `brulion-mcp` admin command (shellcheck) build; a public address without
#   publicUrl, a password file in the store and destructive tools on a read-only server are
#   refused by assertions.
# * services.brulion-acp: its unit (the agents' file, the Nextcloud password as a credential, the
#   notebook of its own, the hardening an agent leaves room for) and the `brulion-acp` admin command
#   build; no agent, a public address without publicUrl and a password file in the store are refused.
{
  pkgs,
  lib,
  nixpkgs,
  module,
}:

let
  inherit (pkgs.stdenv.hostPlatform) system;

  fakeWebBuild = pkgs.callPackage ./fake-web-build.nix { };

  evalSystem =
    extra:
    nixpkgs.lib.nixosSystem {
      modules = [
        module
        {
          nixpkgs.hostPlatform = system;
          boot.loader.grub.enable = false;
          fileSystems."/" = {
            device = "/dev/vda";
            fsType = "ext4";
          };
          system.stateVersion = lib.trivial.release;
          programs.brulion = {
            enable = true;
            autostart = true;
          };
          services.brulion-web = {
            enable = true;
            hostName = "notatki.example.com";
            nextcloudUrl = "https://cloud.example.com/nextcloud/";
            openFirewall = true;
          };
        }
        extra
      ];
    };

  main = evalSystem { services.brulion-web.package = fakeWebBuild; };

  fromRelease = evalSystem {
    services.brulion-web = {
      release = {
        version = "0.1.0";
        hash = lib.fakeHash;
      };
      basePath = "/notatki/";
      brotli = false;
    };
  };

  # Framed by a site of the administrator's own, and by nobody's rules at all.
  framedByNextcloud = evalSystem {
    services.brulion-web = {
      package = fakeWebBuild;
      frameAncestors = "'self' https://cloud.example.com";
      contentSecurityPolicy = "default-src 'self'";
    };
  };
  framedByAnyone = evalSystem {
    services.brulion-web = {
      package = fakeWebBuild;
      frameAncestors = null;
    };
  };

  withoutPackage = evalSystem { };
  failedAssertions = lib.filter (a: !a.assertion) withoutPackage.config.assertions;

  # The remote MCP server, with a stand-in package (evaluation only: nothing is compiled).
  fakeCli = pkgs.writeShellScriptBin "brulion-cli" "exit 0";
  mcpSystem =
    extra:
    evalSystem {
      services.brulion-web.package = fakeWebBuild;
      services.brulion-mcp = lib.mkMerge [
        {
          enable = true;
          package = fakeCli;
          vault = "/home/ala/Notatki";
          publicUrl = "https://notes.example.com";
          passwordFile = "/run/secrets/brulion-mcp";
          readOnly = false;
          allowedOrigins = [ "http://localhost:6274" ];
        }
        extra
      ];
    };
  mcp = mcpSystem { };
  mcpFailed =
    extra: map (a: a.message) (lib.filter (a: !a.assertion) (mcpSystem extra).config.assertions);
  mcpRefused =
    extra: text:
    lib.assertMsg (lib.any (m: lib.hasInfix text m) (
      mcpFailed extra
    )) "services.brulion-mcp: expected '${text}'";

  # The AI agents for other devices, with the same stand-in package.
  acpSystem =
    extra:
    evalSystem {
      services.brulion-web.package = fakeWebBuild;
      services.brulion-acp = lib.mkMerge [
        {
          enable = true;
          package = fakeCli;
          agents.claude = {
            name = "Claude Code";
            command = "/run/current-system/sw/bin/claude-agent-acp";
          };
          publicUrl = "https://agent.example.com";
          nextcloud = {
            url = "https://cloud.example.com";
            user = "jan";
            folder = "Notatki";
            passwordFile = "/run/agenix/brulion-acp-nextcloud";
          };
        }
        extra
      ];
    };
  acp = acpSystem { };
  acpFailed =
    extra: map (a: a.message) (lib.filter (a: !a.assertion) (acpSystem extra).config.assertions);
  acpRefused =
    extra: text:
    lib.assertMsg (lib.any (m: lib.hasInfix text m) (
      acpFailed extra
    )) "services.brulion-acp: expected '${text}'";
in
assert acpRefused { agents = lib.mkForce { }; } "at least one agent";
assert acpRefused {
  listenAddress = "0.0.0.0";
  publicUrl = lib.mkForce null;
} "needs publicUrl";
assert acpRefused {
  nextcloud.passwordFile = lib.mkForce "${pkgs.writeText "password" "x"}";
} "must not be in the Nix store";
assert lib.assertMsg (
  acpFailed { } == [ ]
) "services.brulion-acp: the example must pass its assertions";
assert mcpRefused {
  listenAddress = "0.0.0.0";
  publicUrl = lib.mkForce null;
} "needs publicUrl";
assert mcpRefused {
  passwordFile = lib.mkForce "${pkgs.writeText "password" "x"}";
} "must not be in the Nix store";
assert mcpRefused {
  readOnly = lib.mkForce true;
  allowDestructive = true;
} "allowDestructive needs readOnly = false";
assert lib.assertMsg (
  mcpFailed { } == [ ]
) "services.brulion-mcp: the example must pass its assertions";
assert lib.assertMsg (lib.any (
  a: lib.hasInfix "services.brulion-web" a.message
) failedAssertions) "services.brulion-web without package/release must fail an assertion";
assert lib.assertMsg (
  !lib.elem pkgs.nginxModules.brotli fromRelease.config.services.nginx.additionalModules
) "brotli = false must not add the brotli module";

pkgs.runCommand "brulion-nixos-module"
  {
    toplevel = builtins.unsafeDiscardStringContext main.config.system.build.toplevel.drvPath;
    releaseToplevel = builtins.unsafeDiscardStringContext fromRelease.config.system.build.toplevel.drvPath;
    nginxConf = main.config.environment.etc."nginx/nginx.conf".source;
    nextcloudFrameConf = framedByNextcloud.config.environment.etc."nginx/nginx.conf".source;
    anyFrameConf = framedByAnyone.config.environment.etc."nginx/nginx.conf".source;
    autostart = main.config.environment.etc."xdg/autostart/io.github.januszbit.brulion.desktop".source;
    # The Web Clipper's host for the Chromium browsers (docs/CLIPPER.md).
    # (paths only: the package itself is not built here)
    chromiumHost =
      builtins.unsafeDiscardStringContext
        main.config.environment.etc."chromium/native-messaging-hosts/io.github.januszbit.brulion.json".source;
    chromeHost =
      builtins.unsafeDiscardStringContext
        main.config.environment.etc."opt/chrome/native-messaging-hosts/io.github.januszbit.brulion.json".source;
    firefoxHosts = lib.boolToString (
      lib.elem main.config.programs.brulion.package main.config.programs.firefox.nativeMessagingHosts.packages
    );
    ports = toString main.config.networking.firewall.allowedTCPPorts;
    hasPackage = lib.boolToString (
      lib.elem main.config.programs.brulion.package main.config.environment.systemPackages
    );
    mcpToplevel = builtins.unsafeDiscardStringContext mcp.config.system.build.toplevel.drvPath;
    mcpUnit = mcp.config.systemd.units."brulion-mcp.service".unit;
    mcpAdmin = lib.findFirst (
      p: (p.name or "") == "brulion-mcp"
    ) (throw "no brulion-mcp command") mcp.config.environment.systemPackages;
    mcpUser = lib.boolToString (mcp.config.users.users ? brulion-mcp);
    acpToplevel = builtins.unsafeDiscardStringContext acp.config.system.build.toplevel.drvPath;
    acpUnit = acp.config.systemd.units."brulion-acp.service".unit;
    acpAdmin = lib.findFirst (
      p: (p.name or "") == "brulion-acp"
    ) (throw "no brulion-acp command") acp.config.environment.systemPackages;
    acpUser = lib.boolToString (acp.config.users.users ? brulion-acp);
  }
  ''
    echo "system: $toplevel"
    echo "system with a release under /notatki/: $releaseToplevel"
    fail() { echo "error: $*" >&2; exit 1; }
    expect() { grep -qF -- "$1" "$nginxConf" || fail "nginx.conf has no '$1'"; }
    [ "$hasPackage" = true ] || fail "the package is not in environment.systemPackages"
    [ "$firefoxHosts" = true ] || fail "the package is not in programs.firefox.nativeMessagingHosts.packages"
    case "$chromiumHost$chromeHost" in
      *share/brulion/native-messaging-hosts/io.github.januszbit.brulion.json*share/brulion/native-messaging-hosts/io.github.januszbit.brulion.json) ;;
      *) fail "the Chromium browsers' host manifest is not linked into /etc: $chromiumHost $chromeHost" ;;
    esac
    [ "$ports" = 80 ] || fail "open ports: '$ports', expected 80"
    grep -Eq '^Exec=/nix/store/[^ ]+/bin/brulion --hidden$' "$autostart" ||
      fail "no 'Exec=.../bin/brulion --hidden' in the autostart entry"
    expect 'application/wasm wasm;'
    expect 'brotli_static on;'
    expect 'gzip_static on;'
    expect 'location ^~ / {'
    expect 'add_header Cache-Control $brulion_web_cache_control always;'
    expect 'map $arg_v $brulion_web_cache_control'
    expect 'location ^~ /nextcloud/remote.php/dav/ {'
    expect 'proxy_pass https://cloud.example.com;'
    expect 'proxy_set_header Host $proxy_host;'
    expect 'proxy_ssl_verify on;'
    # No other site frames the app (clickjacking), unless the administrator names one.
    expect "add_header Content-Security-Policy \"frame-ancestors 'self'\" always;"
    expect 'add_header X-Frame-Options "SAMEORIGIN" always;'
    grep -qF -- "add_header Content-Security-Policy \"frame-ancestors 'self' https://cloud.example.com\" always;" \
      "$nextcloudFrameConf" || fail "frameAncestors with another origin is not in the header"
    grep -qF -- "add_header Content-Security-Policy \"default-src 'self'\" always;" "$nextcloudFrameConf" ||
      fail "contentSecurityPolicy is no longer sent next to frame-ancestors"
    ! grep -qF -- 'X-Frame-Options' "$nextcloudFrameConf" ||
      fail "X-Frame-Options: SAMEORIGIN would refuse the origin frameAncestors allows"
    ! grep -qE -- 'frame-ancestors|X-Frame-Options' "$anyFrameConf" ||
      fail "frameAncestors = null must send no frame header"

    echo "system with the remote MCP server: $mcpToplevel"
    unit="$mcpUnit/brulion-mcp.service"
    expectUnit() { grep -qF -- "$1" "$unit" || fail "brulion-mcp.service has no '$1'"; }
    expectUnit '/bin/brulion-cli" "--vault" "/home/ala/Notatki" "--state-dir" "/var/lib/brulion-mcp/device" "mcp" "serve"'
    expectUnit '"--listen" "127.0.0.1" "--port" "8780" "--auth-file" "/var/lib/brulion-mcp/mcp-remote.json"'
    expectUnit '"--public-url" "https://notes.example.com"'
    expectUnit '"--allowed-origin" "http://localhost:6274"'
    expectUnit ' --password-file %d/password'
    expectUnit 'LoadCredential=password:/run/secrets/brulion-mcp'
    expectUnit 'User=brulion-mcp'
    expectUnit 'StateDirectory=brulion-mcp'
    expectUnit 'ProtectSystem=strict'
    expectUnit 'ProtectHome=tmpfs'
    expectUnit 'BindPaths=/home/ala/Notatki'
    expectUnit 'NoNewPrivileges=true'
    expectUnit 'MemoryDenyWriteExecute=true'
    expectUnit 'CapabilityBoundingSet='
    ! grep -qF -- '--read-only' "$unit" || fail "readOnly = false must not pass --read-only"
    [ "$mcpUser" = true ] || fail "the brulion-mcp user is not created"
    test -x "$mcpAdmin/bin/brulion-mcp" || fail "no brulion-mcp admin command"
    grep -qF -- 'runuser -u brulion-mcp' "$mcpAdmin/bin/brulion-mcp" || fail "the admin command does not switch user"

    echo "system with the AI agents for other devices: $acpToplevel"
    unit="$acpUnit/brulion-acp.service"
    expectAcp() { grep -qF -- "$1" "$unit" || fail "brulion-acp.service has no '$1'"; }
    expectAcp '/bin/brulion-cli" "--vault" "/var/lib/brulion-acp/notebook" "--state-dir" "/var/lib/brulion-acp/device" "acp" "serve" "--agents"'
    expectAcp '"--listen" "127.0.0.1" "--port" "8790" "--state-file" "/var/lib/brulion-acp/acp-remote.json"'
    expectAcp '"--public-url" "https://agent.example.com"'
    expectAcp '"--nextcloud" "https://cloud.example.com" "--nextcloud-user" "jan" "--nextcloud-folder" "Notatki" "--sync-interval" "300"'
    expectAcp ' --nextcloud-password-file %d/nextcloud-password'
    expectAcp 'LoadCredential=nextcloud-password:/run/agenix/brulion-acp-nextcloud'
    expectAcp 'User=brulion-acp'
    expectAcp 'StateDirectory=brulion-acp'
    expectAcp 'StateDirectory=brulion-acp/notebook'
    expectAcp 'KillMode=control-group'
    expectAcp 'ProtectSystem=strict'
    expectAcp 'NoNewPrivileges=true'
    ! grep -qF -- 'MemoryDenyWriteExecute' "$unit" || fail "brulion-acp.service: Node.js agents need W+X memory"
    agents=$(grep -oE '/nix/store/[a-z0-9]+-brulion-acp-agents.json' "$unit" | head -1)
    [ -n "$agents" ] || fail "brulion-acp.service names no agents file"
    grep -qF -- '{"command":"/run/current-system/sw/bin/claude-agent-acp","id":"claude","name":"Claude Code"}' "$agents" ||
      fail "the agents file does not list claude: $(cat "$agents")"
    [ "$acpUser" = true ] || fail "the brulion-acp user is not created"
    test -x "$acpAdmin/bin/brulion-acp" || fail "no brulion-acp admin command"
    grep -qF -- 'runuser -u brulion-acp' "$acpAdmin/bin/brulion-acp" || fail "the acp admin command does not switch user"
    touch $out
  ''
