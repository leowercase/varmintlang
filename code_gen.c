#include "code.h"
#include "code_gen.h"
#include "compile.h"
#include "util.h"

static inline PCode *code(Parse *p)
{
  return &p->c->procedure->code;
}

void emit_byte(Parse *p, uint8_t byte)
{
  Instructions_push(p->vm, &code(p)->instructions, byte);

  size_t line = p->current.line;

  if (code(p)->lines.data != NULL) {
    LineBytes *last = LineInfo_top(&code(p)->lines);

    if (last->line == line) {
      // Increment the number of bytes in that line.
      last->nbytes++;
      return;
    }
  }

  // Else, record new line.
  LineBytes l = {line, 1};
  LineInfo_push(p->vm, &code(p)->lines, l);
}

size_t defer_op(Parse *p, Opcode opcode)
{
  emit_bytes(p, 3, (uint8_t)opcode, 0xff, 0xff);
  return code(p)->instructions.len - 2;
}

void patch_op(Parse *p, size_t operand_idx, uint16_t operand)
{
  uint8_t *i = code(p)->instructions.data;

  uint8_t bytes[2] = uint16_to_8(operand);
  i[operand_idx] = bytes[0];
  i[operand_idx + 1] = bytes[1];
}

// We make use of the fact that an 8-bit and a 16-bit op
// reside next to each other in the enum.
bool emit_var_op(Parse *p, Opcode opcode, size_t operand)
{
  if (operand <= UINT8_MAX)
    emit_bytes(p, 2, (uint8_t)opcode, (uint8_t)operand);

  else if (operand <= UINT16_MAX) {
    uint8_t bytes[2] = uint16_to_8((uint16_t)operand);
    emit_bytes(p, 3, (uint8_t)opcode + 1, bytes[0], bytes[1]);
  }

  else
    return false;

  return true;
}

Value *emit_constant(Parse *p, Value value)
{
  Value *constant = Constants_push(p->vm, &code(p)->constants, value);
  size_t idx = code(p)->constants.len - 1;

  if (!emit_var_op(p, OP_CONST, idx))
    parse_error(p, p->current, true, "too many constants");

  return constant;
}

void patch_jump_to(Parse *p, Token loop_tok,
    size_t jmp_operand_idx, size_t jumpable_code)
{
  if (jumpable_code > UINT16_MAX)
    parse_error(p, loop_tok, true, "Too much code to jump over");

  patch_op(p, jmp_operand_idx, (uint16_t)jumpable_code);
}

void patch_jump(Parse *p, Token jmp_tok, size_t jmp_operand_idx)
{
  // 2 slots account for the 16-bit operand.
  size_t jumpable_code = code(p)->instructions.len - jmp_operand_idx - 2;
  patch_jump_to(p, jmp_tok, jmp_operand_idx, jumpable_code);
}

size_t code_top(Parse *p)
{
  return code(p)->instructions.len;
}

void emit_loop(Parse *p, Token loop_tok, Opcode loopcode,
    size_t loop_start)
{
  size_t op_idx = defer_op(p, loopcode);
  size_t jumpable_code = code(p)->instructions.len - loop_start;

  if (jumpable_code > UINT16_MAX)
    parse_error(p, loop_tok, true, "too much code to loop over");

  patch_op(p, op_idx, (uint16_t)jumpable_code);
}

void change_opcode(Parse *p, size_t operand_idx, Opcode new_opcode)
{
  code(p)->instructions.data[operand_idx - 1] = (uint8_t)new_opcode;
}
