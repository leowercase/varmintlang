#include "ir.h"
#include "util.h"

void emit_byte(PCode *code, size_t line, uint8_t byte)
{
  Instructions_push(&code->instruc, byte);

  if (code->lines.data != NULL) {
    size_t last_line = LineInfo_top(&code->lines).line;

    if (last_line == line) {
      // Increment the number of bytes in that line.
      code->lines.data[code->lines.len - 1].nbytes++;
      return;
    }
  }

  // Else, record new line.
  LineBytes l = {line, 1};
  LineInfo_push(&code->lines, l);
}

uint8_t *defer_operand(PCode *code, size_t line)
{
  emit_bytes(code, line, 2, 0xff, 0xff);
  return code->instruc.data + code->instruc.len - 1;
}

void patch_operand(PCode *code, uint8_t *ip, uint16_t operand)
{
  uint8_t bytes[2] = uint16_to_8(operand);
  ip[0] = bytes[0];
  ip[1] = bytes[1];
}

// We make use of the fact that an 8-bit and a 16-bit op
// reside next to each other in the enum.
bool emit_size_op(PCode *code, size_t line, Opcode opcode, size_t size)
{
  if (size <= UINT8_MAX)
    emit_bytes(code, line, 2, opcode, (uint8_t)size);

  else if (size <= UINT16_MAX) {
    uint8_t bytes[2] = uint16_to_8((uint16_t)size);
    emit_bytes(code, line, 3, opcode + 1, bytes[0], bytes[1]);
  }

  else
    return false;

  return true;
}

static size_t make_constant(PCode *code, Value value)
{
  Constants_push(&code->constants, value);
  return code->constants.len - 1;
}

void emit_constant(PCode *code, size_t line, Value value)
{
  size_t idx = make_constant(code, value);

  if (emit_size_op(code, line, OP_CONST, idx))
    return;

  runtime_error("Too many constants\n");
}

size_t get_line(LineInfo *lines, size_t offset)
{
  for (size_t i = 0; i < lines->len; i++) {
    LineBytes l = lines->data[i];

    if (offset <= l.nbytes)
      return l.line;

    offset -= l.nbytes;
  };
  abort(); // Unreachable, assuming well-formed line info
}
