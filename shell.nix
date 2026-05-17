{ pkgs ? import <nixpkgs> {} }:
  pkgs.mkShellNoCC {
    inputsFrom = [
      (pkgs.callPackage ./varmint.nix {})
    ];
    nativeBuildInputs = with pkgs; [
      clang-tools
      clang
      gnumake
      valgrind
      bear
      libllvm # provides llvm-symbolizer for UBSan

      http-server # https://github.com/http-party/http-server
      emscripten
    ];
    # https://nixos.org/manual/nixpkgs/stable/#fortify
    # Debugging is smooth sailing with -O0, but disappeases the _FORTIFY_SOURCE gods
    hardeningDisable = [ "fortify" ];
    # https://github.com/llvm/llvm-project/blob/main/clang/docs/UndefinedBehaviorSanitizer.rst#stack-traces-and-report-symbolization
    UBSAN_OPTIONS = "print_stacktrace=1";
  }
