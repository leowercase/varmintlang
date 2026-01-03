#include "compiler.h"
#include "disassemble.h"
#include "util.h"
#include "vm.h"

#include <sysexits.h>
#include <stdio.h>
#include <stdlib.h>

#include <readline/readline.h>
#include <readline/history.h>

void repl()
{
  VM vm = vm_new();

  // https://en.wikipedia.org/wiki/GNU_Readline#Sample_code

  // History
  using_history();

  // A read eval print loop.
  for (;;) {
    char *input = readline("> ");
    if (!input) break;

    add_history(input);

    PCode code = compile(input);
    Value result = vm_run(&vm, &code);
    printf("%g\n", result);

    free(input);
  }

  vm_free(&vm);
}

size_t file_size(FILE *file)
{
  fseek(file, 0L, SEEK_END);
  size_t size = ftell(file);

  rewind(file);

  return size;
}

// Run a script file.
void run_file(const char *filename)
{
  FILE *file;
  file = fopen(filename, "r");

  if (file == NULL) {
    error_out("Could not open file %s\n", filename);
    exit(EX_NOINPUT);
  }

  size_t source_size = file_size(file);
  char *source = malloc(sizeof(char) * (source_size + 1));

  if (source == NULL) {
    error_out("Not enough memory to read %s\n", filename);
    exit(EX_OSERR);
  }

  size_t bytes_read =
    fread(source, sizeof(char), source_size, file);
  source[bytes_read] = '\0';

  if (bytes_read < source_size) {
    error_out("Could not read file %s\n", filename);
    exit(EX_NOINPUT);
  }

  {
    Lex l = lex_new(source);

    Token tok;
    do {
      tok = lex_token(&l);
      printf("%.2i ", tok.line);
      print_token(tok);
      printf("\n");
    } while (tok.type != TK_EOF);
  }

  VM vm = vm_new();
  PCode code = compile(source);

  disassemble(&code);

  Value result = vm_run(&vm, &code);
  printf("%g\n", result);

  vm_free(&vm);
  fclose(file);
}

int main(int argc, const char **argv)
{
  if (argc == 1)
    repl();
  else if (argc == 2)
    run_file(argv[1]);
  else {
    error_out("usage: %s [file]\n", argv[0]);
    exit(EX_USAGE);
  }
}
