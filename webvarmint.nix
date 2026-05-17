{ lib, emscriptenStdenv, callPackage }:
  let
    fs = lib.fileset;

    varmint' = callPackage ./varmint.nix {
      xxhash = callPackage ./emscripten_xxhash.nix {};
      clangStdenv = emscriptenStdenv;
    };
  in
  varmint'.overrideAttrs (prev: {
    src = fs.toSource {
      root = ./.;
      fileset = fs.union prev.meta.sourceFileset ./web/main.c;
    };

    dontStrip = true;

    configurePhase = "";

    buildPhase = ''
      make js
    '';

    installPhase = ''
      mkdir -p $out/bin
      cp varmint.js varmint.wasm -t $out/bin
    '';

    # It works on my machine™
    checkPhase = "";
  })
