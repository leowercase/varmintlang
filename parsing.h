#ifndef LANG_PARSING_H
#define LANG_PARSING_H

#include "compiler.h"

#include <stdio.h>

typedef enum {
  PREC_NONE,
  PREC_STATEMENT, // ...; ...
  PREC_LIST,      // ..., ...
  PREC_OR,        // or
  PREC_AND,       // and
  PREC_I9N,       // -> (implication)
  PREC_CMP,       // = != < > <= >=
  PREC_NOT,       // not
  PREC_TERM,      // + -
  PREC_FACTOR,    // * / %
  PREC_CONCAT,    // ||
  PREC_POWER,     // ^
  PREC_SIGN,      // + -
  PREC_FACTORIAL, // !
  PREC_PERCENT,   // %
} Precedence;

typedef enum {
  ASSOC_LEFT = 1,
  ASSOC_NONE,
  ASSOC_RIGHT = -1,
} Associativity;

// Null-denoted parse; preceded by nothing (prefix)
typedef void (*NudRule)(Compiler *c);

// Left-denoted parse; preceded by something (infix/postfix)
typedef bool (*LedRule)(Compiler *c, int min_bp);
// Rule returns true if the parse wasn't continued.

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
 * One Op to bring them all and in the darkness "bind" them.
 */
void expr(Compiler *c, int min_bp);

void prefix_op(Compiler *c);
void grouping(Compiler *c);
void boolean(Compiler *c);
void number(Compiler *c);
void metastring(Compiler *c);
void string(Compiler *c);

// Token is valid, but shouldn't be used as LED
static inline bool no_op(Compiler *_, int __)
{
  return true;
}

bool statement(Compiler *c, int min_bp);
bool list(Compiler *c, int min_bp);
bool infix_op(Compiler *c, int min_bp);
bool postfix_op(Compiler *c, int min_bp);
bool led_op(Compiler *c, int min_bp);
bool cmp_op(Compiler *c, int min_bp);

// Helper function
static inline
void invalid_token(Token tok)
{
  error_out("Line %i: ", tok.line);

  if (tok.type == TK_ERR)
    runtime_error("lexing error: \"%.*s\"\n",
        tok.raw_str.len, tok.raw_str.s);
  else
    runtime_error("invalid token %s `%.*s`\n",
        tok_cstring(tok.type), tok.raw_str.len, tok.raw_str.s);
}

#endif
