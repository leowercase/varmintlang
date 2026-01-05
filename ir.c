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

static size_t make_constant(PCode *code, Value value)
{
  Constants_push(&code->constants, value);
  return code->constants.len - 1;
}

void emit_constant(PCode *code, size_t line, Value value)
{
  size_t constant_idx = make_constant(code, value);

  if (constant_idx <= UINT8_MAX)
    emit_bytes(code, line, 2, OP_CONST, constant_idx);

  else if (constant_idx <= UINT16_MAX) {
    uint8_t bytes[2] = uint16_to_8((uint16_t)constant_idx);
    emit_bytes(code, line, 3, OP_CONST16, bytes[0], bytes[1]);
  }

  else runtime_error("Too many constants\n");
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
