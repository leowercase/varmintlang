#ifndef VARMINT_COMPILE_H
#define VARMINT_COMPILE_H

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

// Local variable that resides on the operation stack.
// Local going out of scope gets its value pushed off the stack.
typedef struct Local {
  Str name;
  size_t stack_slot;
  size_t depth;
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

// Stack of queued jumps.
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
  bool is_for;
} Loop;

// Similar to the locals array, except for loop labels.
typedef DYN_ARRAY_STRUCT(Loop) LoopStack;
#define T Loop
#define ARR LoopStack
#include "generic/dyn_array.inc"

// To ease creating mutually recursive fns (among other things), we allow
// deferred name resolution in a multiple `var` until the end of the clauses.
typedef struct {
  Token tok;
  ClosureDesc *desc;
  size_t upval_idx;
} DeferredLookup;

typedef DYN_ARRAY_STRUCT(DeferredLookup) DeferredVar;
#define T DeferredLookup
#define ARR DeferredVar
#include "generic/dyn_array.inc"

// Info about the current expression being parsed.
typedef struct SemanticDatum {
  Token assigned_tok;
  union {
    // Locals and upvalues are stored in a dynamic array, so we pass indices
    // around instead of pointers
    size_t local_idx, upval_idx;
  } assignable;

  // Assignment function for left hand side operand
  void (*assign_fn)(Parse *p);
  void (*compound_assign_fn)(Parse *p);

  // Indentation affects whether or not stuff is considered to be
  // a continuation of the previous line.
  struct {
    size_t initial; // First indentation level of an expression chain
    size_t continued; // Indentation level of the continuation
  } indent;

  // When possible, a False `if` condition jumps straight to a corresponding
  // `else` clause without creating a None result value.
  // Similarly, a loop can have an optional `else` clause that it jumps to when
  // terminating without a single cycle.
  size_t else_jmp_idx;
  bool else_chained;

  size_t statement_count;
  // Whether the current surrounding is a statement or inside one
  bool is_stmts, in_stmts;

  // Whether to stop a chain of left-denoted parses.
  bool led_end;

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
  String *source;
};

// Compiler for a procedure
typedef struct Compiler {
  struct Compiler *enclosing;
  Locals locals;
  size_t argc;
  size_t stack_slot_count;
  LoopStack loops;
  size_t depth; // Current block depth { { ... } }
  bool var_declaration; // Whether is a function being declared with `var`.
  DeferredVar deferred_var;
  Procedure *procedure;
} Compiler;

static inline PCode *code(Parse *p)
{
  return &p->c->procedure->code;
}

void parse_error(Parse *p, Token offending_tok, bool pointer,
    char *const msg, ...);

typedef enum {
  PREC_NONE,
  PREC_TOP,       // var as loop for while
  PREC_ASSIGN,    // :=
  PREC_ELSE,      // else elif
  PREC_IF,        // if
  PREC_FLOW,      // break continue return
  PREC_OR,        // or
  PREC_AND,       // and
  PREC_I9N,       // ->
  PREC_CMP,       // = != < > <= >=
  PREC_NOT,       // not
  PREC_TERM,      // + -
  PREC_FACTOR,    // * / %
  PREC_CONCAT,    // ||
  PREC_NCAT,      // |*
  PREC_POWER,     // ^
  PREC_SIGN,      // + -
  PREC_FACTORIAL, // !
  PREC_PERCENT,   // %
  PREC_CALL,      // () [] ?!
  PREC_UFCS,      // :
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

// Initialize a parse
Parse init_parse(Varmint *vm);

// Free parse data
void free_parse(Parse *p);

// Compile some code into an executable procedure.
// If the existing parse `p` is non-NULL, continue parsing with its data
Procedure *compile(Varmint *vm, Parse *p, bool discard_state, String *source);

#endif
