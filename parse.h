#ifndef LANG_PARSING_H
#define LANG_PARSING_H

#include "astree.h"
#include "varmint.h"
#include "lex.h"
#include "ir.h"

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
  PREC_ASSIGN,    // :=
  PREC_OR,        // or
  PREC_AND,       // and
  PREC_I9N,       // ->
  PREC_CMP,       // = != < > <= >=
  PREC_NOT,       // not
  PREC_TERM,      // + -
  PREC_FACTOR,    // * / %
  PREC_CONCAT,    // ||
  PREC_POWER,     // ^
  PREC_SIGN,      // + -
  PREC_FACTORIAL, // !
  PREC_PERCENT,   // %
  PREC_SUBSCRIPT, // []
} Precedence;

typedef enum {
  ASSOC_LEFT = 1,
  ASSOC_NONE,
  ASSOC_RIGHT = -1,
} Associativity;

// Null-denoted parse; preceded by nothing (prefix)
typedef TNode *(*NudRule)(Parse *p);

// Left-denoted parse; preceded by something (infix/postfix)
typedef TNode *(*LedRule)(Parse *p, TNode *lhs, int min_bp);

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
TNode *expr(Parse *p, int min_bp);

TNode *stmt(Parse *p);

TNode *prefix_op(Parse *p);
TNode *grouping(Parse *p);
TNode *block(Parse *p);
TNode *list(Parse *p);
TNode *boolean(Parse *p);
TNode *number(Parse *p);
TNode *metastring(Parse *p);
TNode *string(Parse *p);
TNode *ident(Parse *p);
TNode *let(Parse *p);

// No parse result from function.
static inline TNode *no_op(Parse *_, TNode *__, int ___)
{
  return NULL;
}

TNode *infix_op(Parse *p, TNode *lhs, int min_bp);
TNode *postfix_op(Parse *p, TNode *lhs, int min_bp);
TNode *led_op(Parse *p, TNode *lhs, int min_bp);
TNode *cmp_op(Parse *p, TNode *lhs, int min_bp);
TNode *assignage(Parse *p, TNode *lhs, int min_bp);
TNode *subscript(Parse *p, TNode *lhs, int min_bp);

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
