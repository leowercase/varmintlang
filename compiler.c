#include "compiler.h"
#include "parsing.h"

PCode compile(char *source)
{
  Lex lex = lex_new(source);
  Token current = lex_token(&lex);
  Token lookahead = lex_token(&lex);

  PCode code = new_p_code();

  Compiler c = {lex, current, lookahead, code};

  expr(&c, PREC_NONE);
  emit_byte(&c.code, c.code.lines.len - 1, OP_RETURN);

  return c.code;
}
