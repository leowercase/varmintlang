#include "../inc/code.h"
#include "../inc/code_gen.h"
#include "../inc/compile.h"
#include "../inc/util.h"

void emit_byte(Parse *p, Token tok, uint8_t byte)
{
  Instructions_push(p->vm, &code(p)->instructions, byte);

  size_t line = tok.line;

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

void emit_16(Parse *p, Token tok, size_t data)
{
  if (data > UINT16_MAX)
    parse_error(p, tok, true, "too large operand to bytecode instruction");

  uint8_t bytes[2] = uint16_to_8(data);
  emit_bytes(p, tok, 2, bytes[0], bytes[1]);
}

void emit_op(Parse *p, Token tok, Opcode opcode, size_t operand)
{
  emit_byte(p, tok, (uint8_t)opcode);
  emit_16(p, tok, operand);
}

void emit_var_op(Parse *p, Token tok, Opcode opcode, size_t operand)
{
  if (operand <= UINT8_MAX)
    emit_bytes(p, tok, 2, (uint8_t)opcode, (uint8_t)operand);

  else
    // An 8-bit and a 16-bit op reside next to each other in the enum.
    emit_op(p, tok, opcode + 1, operand);
}

Value *emit_constant(Parse *p, Token tok, Value value)
{
  Value *constant = Constants_push(p->vm, &code(p)->constants, value);
  size_t idx = code(p)->constants.len - 1;

  emit_var_op(p, tok, OP_CONST, idx);
  return constant;
}

size_t defer_op(Parse *p, Token tok, Opcode opcode)
{
  emit_op(p, tok, opcode, 0L);
  return code(p)->instructions.len - 2;
}

void patch_op(Parse *p, Token tok, size_t operand_idx, size_t operand)
{
  if (operand > UINT16_MAX)
    parse_error(p, tok, true, "too large operand to bytecode instruction");

  uint8_t *i = code(p)->instructions.data;

  uint8_t bytes[2] = uint16_to_8((uint16_t)operand);
  i[operand_idx] = bytes[0];
  i[operand_idx + 1] = bytes[1];
}

void patch_jump_to(Parse *p, Token loop_tok,
    size_t jmp_operand_idx, size_t jumpable_code)
{
  patch_op(p, loop_tok, jmp_operand_idx, jumpable_code);
}

void patch_jump(Parse *p, Token jmp_tok, size_t jmp_operand_idx)
{
  // 2 slots account for the 16-bit operand.
  size_t jumpable_code = code(p)->instructions.len - jmp_operand_idx - 2;
  patch_jump_to(p, jmp_tok, jmp_operand_idx, jumpable_code);
}

void emit_loop(Parse *p, Token loop_tok, Opcode loopcode,
    size_t loop_start)
{
  size_t op_idx = defer_op(p, loop_tok, loopcode);
  size_t jumpable_code = code(p)->instructions.len - loop_start;

  patch_op(p, loop_tok, op_idx, jumpable_code);
}

void change_opcode(Parse *p, size_t operand_idx, Opcode new_opcode)
{
  code(p)->instructions.data[operand_idx - 1] = (uint8_t)new_opcode;
}
