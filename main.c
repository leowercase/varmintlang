#include "util.h"

#include <sysexits.h>
#include <stdio.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>

void repl()
{
  // https://en.wikipedia.org/wiki/GNU_Readline#Sample_code

  // History
  using_history();

  // A read eval print loop.
  for (;;) {
    char *input = readline("> ");
    if (!input) break;

    add_history(input);

    // TODO

    free(input);
  }
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
    error_out("Out of memory\n");
    exit(EX_OSERR);
  }

  size_t bytes_read =
    fread(source, source_size, 1, file);

  if (bytes_read < source_size) {
    error_out("Could not read file %s\n", filename);
    exit(EX_NOINPUT);
  }

  // TODO

  fclose(file);
}

int main(int argc, const char **argv)
{
  if (argc == 1)
    repl();
  else if (argc == 2)
    run_file(argv[1]);
  else {
    error_out("usage: %s [file]", argv[0]);
    exit(EX_USAGE);
  }
}
