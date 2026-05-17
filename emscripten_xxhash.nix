{
  emscriptenStdenv,
  xxhash,
  nodejs,
}:
  # The build dir is at https://github.com/Cyan4973/xxHash/tree/release/cmake_unofficial
  (xxhash.override { stdenv = emscriptenStdenv; }).overrideAttrs (prev: {
    dontStrip = true;
    outputs = [ "out" ];

    configurePhase = ''
      HOME=$TMPDIR
      mkdir -p .emscriptencache
      export EM_CACHE=$(pwd)/.emscriptencache

      emcmake cmake ./cmake_unofficial $cmakeFlags -DCMAKE_INSTALL_PREFIX=$out -DCMAKE_INSTALL_INCLUDEDIR=$out/include
    '';

    # `emscriptenStdenv` requires a `checkPhase` to validate linkage.
    # https://emscripten.org/docs/compiling/Dynamic-Linking.html#dynamic-checks
    checkPhase = ''
      echo "================= testing xxhash using node ================="

      echo "Compiling a custom test"
      set -x
      emcc -O2 -s EMULATE_FUNCTION_POINTER_CASTS=1 tests/sanity_test.c \
        -I . \
        libxxhash.a \
        -o ./sanity_test.js

      echo "Using node to execute the test"
      ${nodejs}/bin/node ./sanity_test.js

      set +x
      if [ $? -ne 0 ]; then
        echo "Unit test failed"
        exit 1;
      else
        echo "Unit test succeeded"
      fi
      echo "================= /testing xxhash using node ================="
    '';
  })
