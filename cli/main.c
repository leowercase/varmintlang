#include "../inc/compile.h"
#include "../inc/lex.h"
#include "../inc/varmint.h"

#include <sysexits.h>
#include <stdio.h>
#include <stdlib.h>

#include <readline/readline.h>
#include <readline/history.h>

// IO impls

static void print_out(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stdout, fmt, ap);
  va_end(ap);
}

static void va_print_err(const char *fmt, va_list ap)
{
  fprintf(stderr, ANSI_RED);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, ANSI_RESET);
}

static void print_err(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  va_print_err(fmt, ap);
  va_end(ap);
}

static void print_info(const char *fmt, ...)
{
  fprintf(stderr, ANSI_YELLOW);

  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);

  fprintf(stderr, ANSI_RESET);
}

static String *input(Varmint *vm, const char *prompt)
{
  return String_own(vm, readline(prompt)).as.string;
}

typedef enum {
  // CLI options
  OPT_TOKENS,
  OPT_DIS,
  OPT_EVAL,
  OPT_HELP,

  // Number of command line options.
  OPTS_LEN,

  // Default action for source file
  OPT_EXECUTE,
  // User error
  OPT_ERROR,
} Opt;

typedef struct {
  Str name;
  char *short_forms;
  char *desc;
} OptDesc;

const OptDesc arguments[] = {
  [OPT_TOKENS] = { str_from("tokens"), "t",  "print tokens"         },
  [OPT_DIS]    = { str_from("dis"),    "d",  "disassemble bytecode" },
  [OPT_EVAL]   = { str_from("eval"),   "e",  "evaluate expression"  },
  [OPT_HELP]   = { str_from("help"),   "h?", "print help"           },
};

static void print_usage(const char *program_name)
{
  printf("usage: %s [OPTION] [FILE]\n", program_name);
}

static void print_opts(const char *short_prefix, const char *long_prefix)
{
  for (size_t i = 0; i < OPTS_LEN; i++) {
    OptDesc opt = arguments[i];

    // Print long form.
    printf("  " ANSI_YELLOW
        "%s%.*s", long_prefix, (int)opt.name.len, opt.name.s);

    // Print short forms.
    for (char *short_opt = opt.short_forms; *short_opt != '\0'; short_opt++)
      printf(" %s%c", short_prefix, *short_opt);

    // Print description.
    printf(ANSI_RESET "\n    %s\n", opt.desc);
  }
}

static Opt short_opt(const char *prefix, char opt_c)
{
  for (size_t i = 0; i < OPTS_LEN; i++) {
    for (char *c = arguments[i].short_forms; *c != '\0'; c++)
      if (*c == opt_c)
        return (Opt)i;
  }
  print_err("unknown option %s%c\n", prefix, opt_c);
  return OPT_ERROR;
}

static Opt long_opt(const char *prefix, Str opt_s)
{
  for (size_t i = 0; i < OPTS_LEN; i++) {
    if (strs_eq(arguments[i].name, opt_s))
      return (Opt)i;
  }
  print_err("unknown option %s%.*s\n", prefix, (int)opt_s.len, opt_s.s);
  return OPT_ERROR;
}

static void run(Varmint *vm, Opt opt, String *source,
    const char *program_name, const char *filename, Parse *parse, bool in_repl)
{
  switch (opt) {
  case OPT_TOKENS:
    print_tokens(stdout, source->s);
    break;
  case OPT_DIS:
    varmint_dis(vm, parse, print_out, source, filename);
    break;
  case OPT_EVAL:
    if (varmint_run_with(vm, parse, false, source) == VM_A_OK) {
      print_value(print_out, vm->result);
      printf("\n");
    }
    break;
  case OPT_HELP:
    if (in_repl)
      print_opts(":", ":");
    else {
      print_usage(program_name);
      printf("  " ANSI_YELLOW "(default)" ANSI_RESET "\n    run REPL\n");
      print_opts("-", "--");
    }
    break;
  case OPT_EXECUTE:
    varmint_run(vm, source);
    break;
  case OPT_ERROR:
    if (!in_repl) exit(EX_USAGE);
    printf("\n");
    break;
  case OPTS_LEN:
    unreachable();
  }
}

static size_t file_size(FILE *file)
{
  fseek(file, 0L, SEEK_END);
  size_t size = (size_t)ftell(file);

  rewind(file);

  return size;
}

static char *read_file(const char *filename)
{
  FILE *file;
  file = fopen(filename, "r");

  if (file == NULL) {
    print_err("could not open file %s\n", filename);
    exit(EX_NOINPUT);
  }

  size_t size = file_size(file);
  char *contents = malloc(sizeof(char) * (size + 1));

  if (contents == NULL) {
    print_err("not enough memory to read %s\n", filename);
    exit(EX_OSERR);
  }

  size_t bytes_read =
    fread(contents, sizeof(char), size, file);
  contents[bytes_read] = '\0';

  if (bytes_read < size) {
    print_err("could not read file %s\n", filename);
    exit(EX_NOINPUT);
  }

  fclose(file);
  return contents;
}

// Add input to history.
// Discard consecutive duplicate lines
static void historize(char *input)
{
  HISTORY_STATE *hist = history_get_history_state();
  if (hist->length == 0)
    goto history;

  HIST_ENTRY *prev = hist->entries[hist->length - 1];

  bool dup = prev->line != NULL && strcmp(prev->line, input) == 0;
  if (!dup) goto history;
  return;

history:
  add_history(input);
}

static const Varmio IO = {
  .out = print_out,
  .error = print_err,
  .va_error = va_print_err,
  .info = print_info,
  .input = input,
};

// Run a read-eval-print loop.
static void run_repl(void)
{
  Varmint vm = varmint_init(IO);
  Parse parse = parse_init(&vm);

  // https://en.wikipedia.org/wiki/GNU_Readline#Sample_code
  using_history();

  printf("type :? for help\n");

  for (;;) {
    char *input = readline("vm> ");
    if (!input) break;
    historize(input);

    // By default, evaluate input expr
    Opt opt = OPT_EVAL;
    char *in = input;

    // Parse REPL command.
    if (in[0] == ':') {
      in++;
      Str cmd = {in, 0};
      for (; !isspace(*in) && *in != '\0'; cmd.len++, in++);

      if (cmd.len == 0) {
        print_err("expect REPL command\n");
        free(input);
        continue;
      }
      else if (cmd.len == 1)
        opt = short_opt(":", cmd.s[0]);
      else
        opt = long_opt(":", cmd);
    }

    String *source = String_from(&vm, in).as.string;
    free(input);

    run(&vm, opt, source,
        NULL, NULL, &parse, true);
    printf("\n");

    // Reset status between lines.
    vm.result = NO_VALUE;
    vm.status = VM_A_OK;
  }

  free_parse(&parse);
  varmint_free(&vm);
}

int main(int argc, const char **argv)
{
  if (argc == 1)
    // Run REPL
    run_repl();

  else {
    // Run the CLI
    const char *program_name = argv[0];
    Opt opt = OPT_EXECUTE;

    // Parse command line options.
    size_t i = 1;
    for (; i < (size_t)argc && argv[i][0] == '-'; i++) {
      if (argv[i][1] == '-') {
        const char *s = &argv[i][2];

        Str opt_s = {s, 0};
        for (; *s != '\0'; s++, opt_s.len++);

        if (opt_s.len == 0) {
          print_err("expect long option name\n");
          exit(EX_USAGE);
        }
        opt = long_opt("--", opt_s);
      }
      else
        opt = short_opt("-", argv[1][1]);
    }

    Varmint vm = varmint_init(IO);
    String *source;
    const char *filename = NULL;

    if (opt == OPT_HELP)
      source = NULL;

    else if ((size_t)argc != i + 1) {
      print_usage(program_name);
      printf("run `%s -?` for help\n", program_name);
      exit(EX_USAGE);
    }

    else {
      filename = argv[i];
      source = String_own(&vm, read_file(filename)).as.string;
    }

    // Run script file.
    run(&vm, opt, source,
        program_name, filename, NULL, false);

    varmint_free(&vm);
  }
}
