#ifndef LANG_PARSING_H
#define LANG_PARSING_H

#include "astree.h"
#include "varmint.h"
#include "lex.h"
#include "pcode.h"

#include <stdio.h>

/*
 * Expressions are translated into an Abstract Syntax Tree.
 * The AST is then converted into stack-based RPN bytecode.
 *
 * https://en.wikipedia.org/wiki/Stack_machine#Design
 * https://en.wikipedia.org/wiki/Reverse_Polish_notation
 * https://en.wikipedia.org/wiki/Operator-precedence_parser#Pratt_parsing
 */

typedef struct {
  Lex lex;
  Token current, lookahead;
} Parse;

static inline
Parse init_parse(Varmint *vm, char *source)
{
  Lex lex = lex_new(source);

  Token current = lex_token(&lex);
  Token lookahead = lex_token(&lex);

  Parse p = {lex, current, lookahead};
  return p;
}

typedef enum {
  PREC_NONE,
  PREC_BASE,      // else: elif:
  PREC_ASSIGN,    // :=
  PREC_MAPLET,    // =>
  PREC_OR,        // or
  PREC_AND,       // and
  PREC_I9N,       // ->
  PREC_CMP,       // = != < > <= >=
  PREC_NOT,       // not
  PREC_IN,        // in notin
  PREC_TERM,      // + -
  PREC_FACTOR,    // * / %
  PREC_CONCAT,    // ||
  PREC_POWER,     // ^
  PREC_SIGN,      // + -
  PREC_FACTORIAL, // !
  PREC_PERCENT,   // %
  PREC_CALL,      // () []
} Precedence;

typedef enum {
  ASSOC_LEFT = 1,
  ASSOC_NONE,
  ASSOC_RIGHT = -1,
} Associativity;

// Null-denoted parse; preceded by nothing (prefix)
typedef Tnode *(*NudRule)(Parse *p);

// Left-denoted parse; preceded by something (infix/postfix)
typedef Tnode *(*LedRule)(Parse *p, Tnode *lhs, int min_bp);

// Internal lookup table for the parser.
typedef struct {
  NudRule nud;
  LedRule led;
}
ParseRule;

/*
 * Binding power (BP) symbolizes how an operator grabs its operands.
 *
 * One Op to rule them all, One Op to find them;
 * One Op to parse them all and in the darkness "bind" them.
 */
Tnode *expr(Parse *p, int min_bp);

Tnode *stmt(Parse *p);

Tnode *prefix_op(Parse *p);
Tnode *grouping(Parse *p);
Tnode *block(Parse *p);
Tnode *list(Parse *p);
Tnode *cond(Parse *p);
Tnode *loop(Parse *p);
Tnode *for_loop(Parse *p);
Tnode *loop_break(Parse *p);
Tnode *loop_cont(Parse *p);
Tnode *boolean(Parse *p);
Tnode *number(Parse *p);
Tnode *metastring(Parse *p);
Tnode *string(Parse *p);
Tnode *ident(Parse *p);
Tnode *let(Parse *p);

// No parse result from function.
static inline Tnode *no_op(Parse *_, Tnode *__, int ___)
{
  return NULL;
}

Tnode *infix_op(Parse *p, Tnode *lhs, int min_bp);
Tnode *postfix_op(Parse *p, Tnode *lhs, int min_bp);
Tnode *led_op(Parse *p, Tnode *lhs, int min_bp);
Tnode *cmp_op(Parse *p, Tnode *lhs, int min_bp);
Tnode *else_elif(Parse *p, Tnode *lhs, int min_bp);
Tnode *assign(Parse *p, Tnode *lhs, int min_bp);
Tnode *invocation(Parse *p, Tnode *lhs, int min_bp);
Tnode *subscript(Parse *p, Tnode *lhs, int min_bp);
Tnode *maplet(Parse *p, Tnode *lhs, int min_bp);

// Helper function
static inline
void invalid_token(Token tok)
{
  error_out("Line %i: ", tok.line);

  if (tok.type == TK_ERR)
    runtime_error("lexing error: \"%.*s\"\n",
        tok.slice.len, tok.slice.s);
  else
    runtime_error("invalid token %s `%.*s`\n",
        tok_cstring(tok.type), tok.slice.len, tok.slice.s);
}

#endif
