#include "parsing.h"

PCode compile(char *source)
{
  Lex lex = lex_new(source);

  Token current = lex_token(&lex);
  Token lookahead = lex_token(&lex);

  PCode code = new_p_code();

  Scope scope;
  scope.locals = Locals_new();
  scope.depth = 0;
  scope.enclosing_scope = NULL;

  Parser p = {lex, current, lookahead, code, scope};

  expr(&p, PREC_NONE);
  emit_byte(&p.code, p.code.lines.len - 1, OP_RETURN);

  return p.code;
}
