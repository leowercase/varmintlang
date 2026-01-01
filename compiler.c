#include "compiler.h"
#include "parsing.h"

PCode compile(char *source)
{
  Lex lex = lex_new(source);
  PCode code = new_p_code();
  Compiler c = {lex, lex_token(&lex), lex_token(&lex), code};

  expr(&c, PREC_NONE);

  return c.code;
}
