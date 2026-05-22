CC ?= clang

c_files := $(wildcard src/*c)
header_files := $(wildcard include/*.h include/generic/*.h include/generic/*.inc)

flags := -std=c11 \
	 -Wall -Wextra -Wpedantic -Wconversion -Wno-unused-parameter \
	 -fsanitize=undefined \
	 -g
debug_flags := -O0 -fno-sanitize-merge -fno-omit-frame-pointer -DVARMINT_DEBUG
release_flags := -O3 -fsanitize-trap=all

libs := $$(pkg-config --cflags --libs libxxhash) -lm
cli_libs := $(libs) $$(pkg-config --cflags --libs readline)

all : varmint

varmint : $(c_files) $(header_files) cli/main.c
	$(CC) $(flags) $(release_flags) $(cli_libs) $(c_files) cli/main.c -o varmint

debug : $(c_files) $(header_files) cli/main.c
	$(CC) $(flags) $(debug_flags) $(cli_libs) $(c_files) cli/main.c -o varmint

# The operation stack needs lots of space; 4 MiB ought to cut the mustard
emscripten_flags := -sASSERTIONS=2 -sSTACK_OVERFLOW_CHECK=1 -sSAFE_HEAP=1 \
		    -sSTACK_SIZE=4194304 \
		    -sALLOW_MEMORY_GROWTH=1 \
		    -sMODULARIZE -sEXPORT_ES6 -sEXPORT_NAME=Varmint \
		    -sEXPORTED_FUNCTIONS=_run,_dis -sEXPORTED_RUNTIME_METHODS=cwrap

js : $(c_files) $(header_files) web/main.c web/pre.js
	emcc $(flags) $(debug_flags) $(emscripten_flags) $(libs) $(c_files) web/main.c --pre-js web/pre.js -o varmint.mjs
