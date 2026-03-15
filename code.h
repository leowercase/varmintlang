#ifndef LANG_CODE_H
#define LANG_CODE_H

#include "generic/dyn_array.h"
#include "lex.h"
#include "util.h"
#include "val.h"

/*
 * Intermediate representation.
 * https://en.wikipedia.org/wiki/Bytecode
 */

typedef enum {
  OP_NOT = 0,
  OP_NEGATE,
  OP_FACTORIAL,
  OP_PERCENTAGE, // 100% a useful op
  OP_UNWRAP,

  OP_ADD,
  OP_SUB,
  OP_MUL,
  OP_DIV,
  OP_POW,
  OP_MODULO,
  OP_AND,
  OP_OR,
  OP_I9N,
  OP_EQ,
  OP_NEQ,
  OP_LT,
  OP_GT,
  OP_LEQ,
  OP_GEQ,
  OP_IN,
  OP_NOTIN,
  OP_RANGE,
  OP_RANGE_IN,
  OP_CONCAT,

  OP_CONST,
  OP_CONST16,
  OP_ZERO,
  OP_ONE,
  OP_CHAIN_BINOP,
  OP_DUP_2,
  OP_BUILD_LIST,
  OP_BUILD_LIST16,
  OP_BUILD_STR,
  OP_BUILD_STR16,
  OP_MAKE_SOME,
  OP_MAKE_NONE,
  OP_GET,
  OP_GET16,
  OP_SET,
  OP_SET16,
  OP_INDEXED_GET,
  OP_INDEXED_SET,
  OP_POP,
  OP_RESERVE_SLOT,
  OP_END_BLOCK,
  OP_END_BLOCK16,
  OP_END_EMPTY_BLOCK,
  OP_END_EMPTY_BLOCK16,
  OP_JMP,
  OP_JMP_WHEN_FALSE,
  OP_IF,
  OP_ELSE,
  OP_ELIF,
  OP_LIST_COMPREHEND,
  OP_LOOP,
  OP_LOOP_LIST,
  OP_WHILE,
  OP_WHILE_LIST,
  OP_FOR,
  OP_FOR_LIST,
  OP_FOR_INCREMENT,
  OP_FOR_INCREMENT16,
  OP_BREAK,
  OP_BREAK_LIST,
  OP_DISCARD_FOR,
  OP_DISCARD_FOR_LIST,
  OP_CALL,
  OP_CALL16,
  OP_RETURN,
  // Special instruction for GC
  OP_GC,
} Opcode;

static_assert(OP_GC <= UINT8_MAX, "Oops! Too many opcodes.");

static
const Opcode OP_NONE = (Opcode)UINT8_MAX;

typedef DYN_ARRAY_STRUCT(uint8_t) Instructions;
#define T uint8_t
#define ARR Instructions
#define USE_GC
#include "generic/dyn_array.inc"

// The line of text a group of bytes come from.
typedef struct { size_t line, nbytes; } LineBytes;

typedef DYN_ARRAY_STRUCT(LineBytes) LineInfo;
#define T LineBytes
#define ARR LineInfo
#define USE_GC
#include "generic/dyn_array.inc"

/*
 * Lines are run-length encoded to save memory.
 * This makes line info a bit slow to emit, but it only happens on errors.
 * https://en.wikipedia.org/wiki/Run-length_encoding
 */
size_t get_line(LineInfo *l, size_t instruction_idx);

typedef DYN_ARRAY_STRUCT(Value) Constants;
#define T Value
#define ARR Constants
#define USE_GC
#include "generic/dyn_array.inc"

typedef struct {
  Constants constants;
  Instructions instructions;
  LineInfo lines;
} PCode;

struct Parse;

// Record a byte into code.
void emit_byte(struct Parse *p, size_t line, uint8_t byte);

// For brevity.
#define emit_bytes(p, line, n, ...) do { \
  uint8_t b[] = {__VA_ARGS__}; \
  for (int i = 0; i < (n); i++) emit_byte(p, (line), b[i]); \
} while (false)

// Returns the index of the (16-bit) operand in the code chunk.
size_t defer_op(struct Parse *p, size_t line, Opcode op);
// Inserts operand of defer_op into code
void patch_op(struct Parse *p, size_t operand_idx, uint16_t operand);

// Emit an operation that has a variable sized operand
bool emit_var_op(struct Parse *p, size_t line, Opcode opcode, size_t operand);

// Emit a code constant.
Value *emit_constant(struct Parse *p, size_t line, Value value);

// Patch a jumping instruction to a specific instruction index.
void patch_jump_to(struct Parse *p, Token loop_tok,
    size_t jmp_operand_idx, size_t jumpable_code);

// Patch a jumping instruction.
void patch_jump(struct Parse *p, Token jmp_tok, size_t jmp_operand_idx);

// Get the topmost instruction index.
size_t code_top(struct Parse *p);

// Emit a looping instruction.
void emit_loop(struct Parse *p, Token loop_tok, Opcode loopcode,
    size_t loop_start);

// Change the opcode of the preceding 16-bit operand.
void change_opcode(struct Parse *p, size_t operand_idx, Opcode new_opcode);

#endif
