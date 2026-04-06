#ifndef VARMINT_CODE_GEN_H
#define VARMINT_CODE_GEN_H

#include "code.h"
#include "compile.h"
#include "lex.h"
#include "util.h"
#include "val.h"

// Record a byte into code.
void emit_byte(Parse *p, Token tok, uint8_t byte);

// For brevity.
#define emit_bytes(p, tok, n, ...) do { \
  uint8_t b[] = {__VA_ARGS__}; \
  for (size_t i = 0; i < (n); i++) emit_byte(p, tok, b[i]); \
} while (false)

// Record 16 bits into code.
void emit_16(Parse *p, Token tok, size_t data);


// Emit an opcode with a 16-bit operand.
void emit_op(Parse *p, Token tok, Opcode opcode, size_t operand);
// Emit an opcode with a variable sized operand
void emit_var_op(Parse *p, Token tok, Opcode opcode, size_t operand);

// Returns the index of the (16-bit) operand in the code chunk.
size_t defer_op(Parse *p, Token tok, Opcode opcode);
// Inserts operand of defer_op into code
void patch_op(Parse *p, Token tok, size_t operand_idx, size_t operand);

// Emit a code constant.
Value *emit_constant(Parse *p, Token tok, Value value);

// Patch a jumping instruction to a specific instruction index.
void patch_jump_to(Parse *p, Token loop_tok,
    size_t jmp_operand_idx, size_t jumpable_code);

// Patch a jumping instruction.
void patch_jump(Parse *p, Token jmp_tok, size_t jmp_operand_idx);

// Get the instruction index that will be written to next
static inline
size_t code_idx(Parse *p)
{
  return code(p)->instructions.len;
}

// Emit a looping instruction.
void emit_loop(Parse *p, Token loop_tok, Opcode loopcode, size_t loop_start);

// Change the preceding 16-bit opcode.
void change_opcode(Parse *p, size_t operand_idx, Opcode new_opcode);

#endif
