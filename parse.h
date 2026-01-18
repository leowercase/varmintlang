#ifndef LANG_PARSING_H
#define LANG_PARSING_H

#include "varmint.h"
#include "lex.h"
#include "ir.h"

#include <stdio.h>

/*
 * Single pass compilation is parsing & compiling in one step.
 * Expressions are translated into stack-based RPN bytecode.
 *
 * https://en.wikipedia.org/wiki/Stack_machine#Design
 * https://en.wikipedia.org/wiki/Reverse_Polish_notation
 * https://en.wikipedia.org/wiki/Operator-precedence_parser#Pratt_parsing
 */

typedef struct {
  Lex lex;
  Token current, lookahead;
  PCode code;
  FnScope *scope;
} Parse;

static inline
Parse init_parse(Varmint *vm, char *source)
{
  Lex lex = lex_new(source);

  Token current = lex_token(&lex);
  Token lookahead = lex_token(&lex);

  PCode code = new_p_code();

  Parse p = {lex, current, lookahead, code, &vm->current_scope};
  return p;
}

typedef enum {
  PREC_NONE,
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
typedef void (*NudRule)(Parse *p);

typedef enum {
  LED_CONTINUE = 0,
  LED_STOP = 1,
} LedResult;

// Left-denoted parse; preceded by something (infix/postfix)
typedef LedResult (*LedRule)(Parse *p, int min_bp);

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
void expr(Parse *p, int min_bp);

void stmt(Parse *p);

void prefix_op(Parse *p);
void grouping(Parse *p);
void block(Parse *p);
void list(Parse *p);
void boolean(Parse *p);
void number(Parse *p);
void metastring(Parse *p);
void string(Parse *p);
void precrement(Parse *p);
void ident(Parse *p);

// Token is valid, but shouldn't be used as LED
static inline
LedResult no_op(Parse *_, int __)
{
  return LED_STOP;
}

LedResult infix_op(Parse *p, int min_bp);
LedResult postfix_op(Parse *p, int min_bp);
LedResult led_op(Parse *p, int min_bp);
LedResult cmp_op(Parse *p, int min_bp);

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
