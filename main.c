#include "varmint.h"

#include <sysexits.h>
#include <stdio.h>
#include <stdlib.h>

#include <readline/readline.h>
#include <readline/history.h>

static void run(Varmint *vm, char *source)
{
  Value result = varmint_run(vm, source);
  print_value(result);
  printf("\n");
}

static void repl(void)
{
  Varmint vm = varmint_start();

  // https://en.wikipedia.org/wiki/GNU_Readline#Sample_code

  // History
  using_history();

  // A read eval print loop.
  for (;;) {
    char *input = readline("> ");
    if (!input) break;
    add_history(input);

    run(&vm, input);

    free(input);
  }

  varmint_free(&vm);
}

static size_t file_size(FILE *file)
{
  fseek(file, 0L, SEEK_END);
  size_t size = (size_t)ftell(file);

  rewind(file);

  return size;
}

// Run a script file.
static void run_file(const char *filename)
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

  Varmint vm = varmint_start();

  run(&vm, source);

  varmint_free(&vm);
  free(source);
  fclose(file);
}

int main(int argc, const char **argv)
{
  if (argc == 1)
    repl();
  else if (argc == 2)
    run_file(argv[1]);
  else {
    error_out("usage: %s [script]\n", argv[0]);
    exit(EX_USAGE);
  }
}
