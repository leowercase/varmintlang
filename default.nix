let
  pkgs = import <nixpkgs> {};
in
{
  varmint = pkgs.callPackage ./varmint.nix {};
  web = pkgs.callPackage ./web.nix {};
}
