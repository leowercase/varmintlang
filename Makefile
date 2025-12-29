lang : $(wildcard *.c *.h)
	clang -std=c11 -Wall -o lang $(wildcard *.c) -lreadline
