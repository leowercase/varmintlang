{ pkgs ? import <nixpkgs> {} }:
  pkgs.mkShellNoCC {
    nativeBuildInputs = with pkgs; [
      clang-tools
      clang
      gnumake
      pkg-config
    ];
    buildInputs = with pkgs; [
      readline.dev
    ];
    shellHook = ''
      NIX_CFLAGS_COMPILE="$(pkg-config --cflags --libs readline) $NIX_CFLAGS_COMPILE"
    '';
    # https://nixos.org/manual/nixpkgs/stable/#fortify
    # Debugging is smooth sailing with -O0, but disappeases the _FORTIFY_SOURCE gods
    hardeningDisable = [ "fortify" ];
  }
