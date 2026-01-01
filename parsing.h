#ifndef LANG_PARSING_H
#define LANG_PARSING_H

#include "compiler.h"

typedef enum {
  PREC_NONE,
  PREC_OR,        // or
  PREC_AND,       // and
  PREC_I9N,       // -> (implication)
  PREC_CMP,       // = != < > <= >=
  PREC_NOT,       // not
  PREC_TERM,      // + -
  PREC_FACTOR,    // * / %
  PREC_POWER,     // ^
  PREC_SIGN,      // + -
  PREC_FACTORIAL, // !
  PREC_PERCENT,   // %
  PREC_PRIMARY,   // literal (...)
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

void expr(Compiler *c, int min_bp);

void prefix_op(Compiler *c);
void grouping(Compiler *c);
void number(Compiler *c);

// Token is valid, but shouldn't be used as LED
static inline bool no_op(Compiler *_, int __)
{
  return true;
}

bool infix_op(Compiler *c, int min_bp);
bool postfix_op(Compiler *c, int min_bp);
bool led_op(Compiler *c, int min_bp);
bool cmp_op(Compiler *c, int min_bp);

static const ParseRule parse_rules[] =
  {
/*  token type       NUD        LED        */
    [TK_EOF]     = { NULL,      no_op      },
    [TK_ERR]     = { NULL,      NULL       },

    [TK_PLUS]    = { prefix_op, infix_op   },
    [TK_MINUS]   = { prefix_op, infix_op   },
    [TK_STAR]    = { NULL,      infix_op   },
    [TK_SLASH]   = { NULL,      infix_op   },
    [TK_CARET]   = { NULL,      infix_op   },
    [TK_PERCENT] = { NULL,      led_op     },
    [TK_BANG]    = { NULL,      postfix_op },

    [TK_EQ]      = { NULL,      cmp_op     },
    [TK_NEQ]     = { NULL,      cmp_op     },
    [TK_LT]      = { NULL,      cmp_op     },
    [TK_GT]      = { NULL,      cmp_op     },
    [TK_LEQ]     = { NULL,      cmp_op     },
    [TK_GEQ]     = { NULL,      cmp_op     },

    [TK_LPAREN]  = { grouping,  NULL       },
    [TK_RPAREN]  = { NULL,      no_op      },
    [TK_NUMERAL] = { number,    NULL       },
  };

static inline
const ParseRule *parse_rule(TokenType type)
{
  return &parse_rules[type];
}

// Helper function
static inline
void invalid_token(Token t)
{
  error_out("L%i: invalid token `%.*s`\n",
      t.line, t.string.len, t.string.s);
  exit(EX_DATAERR);
}

#endif
