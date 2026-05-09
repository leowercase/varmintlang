{ lib, clangStdenv, pkg-config, readline, xxhash }:
  let fs = lib.fileset;
  in
  clangStdenv.mkDerivation {
    pname = "varmint";
    version = "1.0";

    src =
      fs.toSource {
        root = ./.;
        fileset = fs.unions [ ./src ./inc ./cli ./Makefile ];
      };

    nativeBuildInputs = [
      pkg-config
    ];
    buildInputs = [
      readline.dev
      xxhash # https://github.com/Cyan4973/xxHash
    ];

    buildPhase = ''
      make release
    '';

    installPhase = ''
      mkdir -p $out/bin
      cp varmint $out/bin
    '';
  }
