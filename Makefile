lang : $(wildcard *.c *.h)
	clang -std=c11 -Wall -Wconversion -O2 -fsanitize=undefined -o lang $(wildcard *.c) -lm -lreadline

lang-debug : $(wildcard *.c *.h)
	clang -std=c11 -Wall -Wconversion -O0 -g -fsanitize=undefined -o lang $(wildcard *.c) -lm -lreadline
