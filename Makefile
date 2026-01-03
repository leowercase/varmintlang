lang : $(wildcard *.c *.h)
	clang -std=c11 -Wall -O2 -o lang $(wildcard *.c) -lm -lreadline

lang-debug : $(wildcard *.c *.h)
	clang -std=c11 -Wall -O0 -g -o lang $(wildcard *.c) -lm -lreadline
