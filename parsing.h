#ifndef LANG_PARSING_H
#define LANG_PARSING_H

#include "compile.h"
#include "lex.h"

#include <stdio.h>

// Local variable
typedef struct {
  StrSlice name;
  int depth;
  bool initialized;
  size_t stack_slot;
} Local;

#define T Local
#define TYPE_NAME Locals
#include "dyn_array.h"

typedef struct Scope {
  Locals locals;
  int depth;
  struct Scope *enclosing_scope;
} Scope;

typedef struct {
  Lex lex;
  Token current, lookahead;
  PCode code;
  Scope scope;
} Parser;

typedef enum {
  PREC_NONE,
  PREC_STATEMENT, // ...; ... let
  PREC_LIST,      // ..., ...
  PREC_ASSIGN,    // :=
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
typedef void (*NudRule)(Parser *p);

// Left-denoted parse; preceded by something (infix/postfix)
typedef bool (*LedRule)(Parser *p, int min_bp);
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
void expr(Parser *p, int min_bp);

void prefix_op(Parser *p);
void grouping(Parser *p);
void block(Parser *p);
void boolean(Parser *p);
void number(Parser *p);
void metastring(Parser *p);
void string(Parser *p);
void ident(Parser *p);

// Token is valid, but shouldn't be used as LED
static inline bool no_op(Parser *_, int __)
{
  return true;
}

bool list(Parser *p, int min_bp);
bool infix_op(Parser *p, int min_bp);
bool postfix_op(Parser *p, int min_bp);
bool led_op(Parser *p, int min_bp);
bool cmp_op(Parser *p, int min_bp);

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
