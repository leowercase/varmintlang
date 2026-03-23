c_files := $(wildcard src/*c)
header_files := $(wildcard include/*.h include/generic/*.h include/generic/*.inc)

flags := -std=c11 \
	 -Wall -Wextra -Wpedantic -Wconversion -Wno-unused-parameter \
	 -fsanitize=undefined \
	 -g
debug_flags := -O0 -fno-sanitize-merge -fno-omit-frame-pointer -DVARMINT_DEBUG
release_flags := -O3 -fsanitize-trap=all

libs := $$(pkg-config --cflags --libs readline libxxhash) -lm

release : $(c_files) $(header_files) cli/main.c
	clang $(flags) $(release_flags) $(libs) $(c_files) cli/main.c -o varmint

debug : $(c_files) $(header_files) cli/main.c
	clang $(flags) $(debug_flags) $(libs) $(c_files) cli/main.c -o varmint
