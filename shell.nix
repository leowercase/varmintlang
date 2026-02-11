{ pkgs ? import <nixpkgs> {} }:
  pkgs.mkShellNoCC {
    nativeBuildInputs = with pkgs; [
      clang-tools
      clang
      gnumake
      pkg-config
      valgrind
      bear
      libllvm # provides llvm-symbolizer for UBSan
    ];
    buildInputs = with pkgs; [
      readline.dev
      xxHash # https://github.com/Cyan4973/xxHash
    ];
    # https://nixos.org/manual/nixpkgs/stable/#fortify
    # Debugging is smooth sailing with -O0, but disappeases the _FORTIFY_SOURCE gods
    hardeningDisable = [ "fortify" ];
    # https://github.com/llvm/llvm-project/blob/main/clang/docs/UndefinedBehaviorSanitizer.rst#stack-traces-and-report-symbolization
    UBSAN_OPTIONS = "print_stacktrace=1";
  }
