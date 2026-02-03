{ pkgs ? import <nixpkgs> {} }:
  pkgs.mkShellNoCC {
    nativeBuildInputs = with pkgs; [
      clang-tools
      clang
      gnumake
      pkg-config
      valgrind
      bear
    ];
    buildInputs = with pkgs; [
      readline.dev
      xxHash # https://github.com/Cyan4973/xxHash
    ];
    # https://nixos.org/manual/nixpkgs/stable/#fortify
    # Debugging is smooth sailing with -O0, but disappeases the _FORTIFY_SOURCE gods
    hardeningDisable = [ "fortify" ];
  }
