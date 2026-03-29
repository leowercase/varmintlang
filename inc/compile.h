#ifndef VARMINT_PARSING_H
#define VARMINT_PARSING_H

#include "generic/dyn_array.h"
#include "lex.h"
#include "code.h"
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
  Str name;
  size_t stack_slot;
  int depth;
  bool initialized;
  // Whether the local is captured by an upvalue
  bool is_captured;
} Local;

// Stack structure implementing a dictionary for local variable lookup.
// Allows variable shadowing; lookup starts from the topmost elem,
// so later entries get preference over the earlier ones.
typedef DYN_ARRAY_STRUCT(Local) Locals;
#define T Local
#define ARR Locals
#include "generic/dyn_array.inc"

// Stack of jumps queued to a loop.
typedef DYN_ARRAY_STRUCT(size_t) JumpIndices;
#define T size_t
#define ARR JumpIndices
#include "generic/dyn_array.inc"

// Loops can be continued or broken out of.
typedef struct {
  Str label; // Label identifier for multi-level breaks/continues
  JumpIndices breaks;
  JumpIndices continues;
  size_t stack_slot;
  size_t start, // Index of the first instruction in the loop.
         iter; // Index of the looping instruction
  bool is_for, is_list_compre;
} Loop;

// Similar to the locals array, except for loop labels.
typedef DYN_ARRAY_STRUCT(Loop) LoopStack;
#define T Loop
#define ARR LoopStack
#include "generic/dyn_array.inc"

// To ease creating mutually recursive fns (among other things), we allow
// deferred name resolution in a multiple `let` until the end of the clauses.
typedef struct {
  UpvalDesc *upval;
  Token tok;
} DeferredLookup;

typedef DYN_ARRAY_STRUCT(DeferredLookup) DeferredLet;
#define T DeferredLookup
#define ARR DeferredLet
#include "generic/dyn_array.inc"

// Info about the current expression being parsed.
typedef struct SemanticDatum {
  Token assigned_tok;
  union {
    Local *local;
    size_t upval_idx;
  } assignable;
  // Assignment function for left hand side operand
  void (*assign_fn)(Parse *p);

  // Left-denoted parsing can be ambiguous.
  struct {
    Token token;
    bool deferred;
  } led_op;

  // Indentation affects whether or not the next line is considered
  // a continuation of an expression.
  struct {
    size_t initial; // First indentation level of an expression chain
    size_t continued; // Indentation level of the continuation
  } indent;

  size_t if_jmp_op_idx;
  // if..else..elif chains are optimized a bit to avoid useless shuffling
  bool if_else_chained;

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
  LoopStack loops;
  int depth; // Current block depth { { ... } }
  bool let_declaration; // Whether is a function being declared with `let`.
  DeferredLet deferred_let;
  Procedure *procedure;
} Compiler;

void parse_error(Parse *p, Token offending_tok, bool pointer,
    char *const msg, ...);

typedef enum {
  PREC_NONE,
  PREC_ASSIGN,    // :=
  PREC_TOP,       // let loop for while using
  PREC_ELSE,      // else elif unwrapped
  PREC_IF,        // if
  PREC_FLOW,      // break continue return
  PREC_MAPLET,    // =>
  PREC_OR,        // or
  PREC_AND,       // and
  PREC_I9N,       // ->
  PREC_CMP,       // = != < > <= >=
  PREC_NOT,       // not
  PREC_IS_IN,     // is in  is not in
  PREC_RANGE,     // .. ..=
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

Procedure *compile(Varmint *vm, char *source);

#endif
