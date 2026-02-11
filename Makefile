files := $(wildcard *.c *.h generic/*.h generic/*.inc)
common_flags := -std=c11 -Wall -Wpedantic -Wconversion -fsanitize=undefined
libs := $$(pkg-config --cflags --libs readline libxxhash) -lm

lang : $(files)
	clang -O2 $(common_flags) -fsanitize-trap=all $(libs) -o lang $(wildcard *.c)

lang-debug : $(files)
	clang -O0 -g $(common_flags) -fno-sanitize-merge -fno-omit-frame-pointer $(libs) -DVARMINT_DEBUG -o lang $(wildcard *.c)
