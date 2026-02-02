common_flags := -std=c11 -Wall -Wconversion -fsanitize=undefined
lib_flags := $$(pkg-config --cflags --libs readline libxxhash) -lm

lang : $(wildcard *.c *.h)
	clang -O2 $(common_flags) $(lib_flags) -o lang $(wildcard *.c)

lang-debug : $(wildcard *.c *.h)
	clang -O0 -g $(common_flags) $(lib_flags) -DVARMINT_DEBUG -o lang $(wildcard *.c)
