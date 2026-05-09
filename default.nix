let
  pkgs = import <nixpkgs> {};
in
pkgs.callPackage ./varmint.nix {}
