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
    # For debugging with -O0
    hardeningDisable = [ "fortify" ];
  }
