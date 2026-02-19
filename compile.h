#ifndef LANG_PARSING_H
#define LANG_PARSING_H

#include "generic/dyn_array.h"
#include "lex.h"
#include "proc.h"
#include "varmint.h"

/*
 * Single-pass compilation is parsing&compiling in a single step.
 * - Simplifies some things, complicates expressing more complex grammars.
 * Tokens are converted into stack-based RPN bytecode.
 *
 * https://en.wikipedia.org/wiki/Operator-precedence_parser#Pratt_parsing
 * https://en.wikipedia.org/wiki/Stack_machine#Design
 * https://en.wikipedia.org/wiki/Reverse_Polish_notation
 */

typedef struct Parse Parse;

// Local variable that resides on the operation stack.
// Local going out of scope gets its value pushed off the stack.
typedef struct Local {
  StrSlice name;
  int depth;
  bool initialized;
  size_t stack_slot;
} Local;

// Stack structure implementing a dictionary for local variable lookup.
// Allows variable shadowing; lookup starts from the topmost elem,
// so later entries get preference over the earlier ones.
typedef DYN_ARRAY_STRUCT(Local) Locals;
#define T Local
#define ARR Locals
#include "generic/dyn_array.inc"

// Info about the current expression being parsed.
typedef struct {
  void (*assign_fn)(Parse *p); // Assignment function for left hand side
  Local *assignable_local;

  // Whether the latest left-denoted parse failed.
  bool led_fail;

  // When an error is reached, we ignore any further ones until we
  // hit a synchronization point.
  // https://www.geeksforgeeks.org/compiler-design/error-recovery-strategies-in-compiler-design/
  bool panic;
} SemanticDatum;

typedef DYN_ARRAY_STRUCT(SemanticDatum) SemanticData;
#define T SemanticDatum
#define ARR SemanticData
#include "generic/dyn_array.inc"

struct Parse {
  Lex lex;
  Token current, lookahead;
  bool had_error;
  SemanticData semantic;
  struct Compiler *c;
  Varmint *vm;
};

// Compiler for a procedure
typedef struct Compiler {
  struct Compiler *enclosing;
  Locals locals;
  size_t stack_slot_count;
  int depth; // Current block depth { { ... } }
  Proc *procedure;
} Compiler;

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
  PREC_SIGN,      // -
  PREC_FACTORIAL, // !
  PREC_PERCENT,   // %
  PREC_CALL,      // () []
} Precedence;

typedef enum {
  ASSOC_LEFT = 1,
  ASSOC_NONE,
  ASSOC_RIGHT = -1,
} Associativity;

/*
 * Binding power (BP) symbolizes how an operator grabs its operands.
 * BP(left) = precedence
 * BP(right) = precedence + associativity
 *
 * One Op to rule them all, One Op to find them;
 * One Op to parse them all and in the darkness bind them.
 */

// Null-denoted parse; preceded by nothing (prefix)
typedef void (*NudRule)(Parse *p);

// Left-denoted parse; preceded by something (infix/postfix)
typedef void (*LedRule)(Parse *p, int min_bp);

// Internal lookup table for the parser.
typedef struct {
  NudRule nud;
  LedRule led;
} ParseRule;

Proc *compile(Varmint *vm, char *source);

#endif
