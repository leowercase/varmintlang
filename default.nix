let
  nixpkgs = fetchTarball "https://github.com/NixOS/nixpkgs/archive/3d8f0f3f72a6cd4d93d0ad13203f2ea1cb7e1456.tar.gz";
  pkgs = import nixpkgs { config = {}; overlays = []; };
in
{
  varmint = pkgs.callPackage ./varmint.nix {};
  web = pkgs.callPackage ./web.nix {};
}
