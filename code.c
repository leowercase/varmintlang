#include "code.h"
#include "util.h"

void emit_byte(PCode *code, size_t line, uint8_t byte)
{
  Instructions_push(&code->instructions, byte);

  if (code->lines.data != NULL) {
    LineBytes *last = LineInfo_top(&code->lines);

    if (last->line == line) {
      // Increment the number of bytes in that line.
      last->nbytes++;
      return;
    }
  }

  // Else, record new line.
  LineBytes l = {line, 1};
  LineInfo_push(&code->lines, l);
}

size_t defer_op(PCode *code, size_t line, Opcode opcode)
{
  emit_bytes(code, line, 3, (uint8_t)opcode, 0xff, 0xff);
  return code->instructions.len - 2;
}

void patch_op(PCode *code, size_t operand_idx, uint16_t operand)
{
  uint8_t *i = code->instructions.data;

  uint8_t bytes[2] = uint16_to_8(operand);
  i[operand_idx] = bytes[0];
  i[operand_idx + 1] = bytes[1];
}

// We make use of the fact that an 8-bit and a 16-bit op
// reside next to each other in the enum.
bool emit_size_op(PCode *code, size_t line, Opcode opcode, size_t size)
{
  if (size <= UINT8_MAX)
    emit_bytes(code, line, 2, (uint8_t)opcode, (uint8_t)size);

  else if (size <= UINT16_MAX) {
    uint8_t bytes[2] = uint16_to_8((uint16_t)size);
    emit_bytes(code, line, 3, (uint8_t)opcode + 1, bytes[0], bytes[1]);
  }

  else
    return false;

  return true;
}

size_t get_line(LineInfo *lines, size_t offset)
{
  for (size_t i = 0; i < lines->len; i++) {
    LineBytes l = lines->data[i];

    if (offset <= l.nbytes)
      return l.line;

    offset -= l.nbytes;
  };
  unreachable(); // Unreachable, assuming well-formed line info
}
