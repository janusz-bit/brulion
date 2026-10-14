# checks.<system>.nixos-module: nix/module.nix in a minimal NixOS system.
# * The system with programs.brulion (+ autostart) and services.brulion-web (+ Nextcloud proxy)
#   evaluates: its toplevel drvPath is computed, the system itself is not built.
# * The generated nginx.conf (checked by gixy while it is built) and the autostart entry
#   (desktop-file-validate) build and contain what they should.
# * A release-based configuration under a sub-path evaluates, and a missing web package is
#   reported by an assertion.
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

  withoutPackage = evalSystem { };
  failedAssertions = lib.filter (a: !a.assertion) withoutPackage.config.assertions;
in
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
    autostart = main.config.environment.etc."xdg/autostart/io.github.januszbit.brulion.desktop".source;
    ports = toString main.config.networking.firewall.allowedTCPPorts;
    hasPackage = lib.boolToString (
      lib.elem main.config.programs.brulion.package main.config.environment.systemPackages
    );
  }
  ''
    echo "system: $toplevel"
    echo "system with a release under /notatki/: $releaseToplevel"
    fail() { echo "error: $*" >&2; exit 1; }
    expect() { grep -qF -- "$1" "$nginxConf" || fail "nginx.conf has no '$1'"; }
    [ "$hasPackage" = true ] || fail "the package is not in environment.systemPackages"
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
    touch $out
  ''
