{ lib, clangStdenv, pkg-config, readline, xxhash }:
  let
    fs = lib.fileset;
    sourceFileset = fs.unions [ ./src ./inc ./cli ./Makefile ];
  in
  clangStdenv.mkDerivation {
    pname = "varmint";
    version = "1.0";

    src = fs.toSource { root = ./.; fileset = sourceFileset; };

    nativeBuildInputs = [
      pkg-config
    ];
    buildInputs = [
      readline.dev
      xxhash # https://github.com/Cyan4973/xxHash
    ];

    buildPhase = ''
      make
    '';

    installPhase = ''
      mkdir -p $out/bin
      cp varmint $out/bin
    '';

    meta = { inherit sourceFileset; };
  }
