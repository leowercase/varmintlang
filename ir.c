#include "ir.h"
#include "util.h"

void emit_byte(PCode *code, size_t line, uint8_t byte)
{
  Instructions_push(&code->instruc, byte);

  if (code->lines.len - 1 == line + 1)
    // Increment line number.
    code->lines.data[line].nbytes++;
  else
    // Start new line.
    LineInfo_push(&code->lines, (LineBytes){1});
}

// For brevity
#define emit_bytes(code, line, n, ...) do { \
  uint8_t bytes[] = {__VA_ARGS__}; \
  for (int i = 0; i < (n); i++) \
    emit_byte((code), (line), bytes[i]); \
} while (false)

static int make_constant(PCode *code, Value value)
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
    uint8_t bytes[2];
    uint16_to_8(constant_idx, bytes);
    emit_bytes(code, line, 3, OP_CONST16, bytes[0], bytes[1]);
  }

  else {
    error_out("Too many constants\n");
    exit(EX_DATAERR);
  }
}

size_t get_line(LineInfo *lines, size_t offset)
{
  for (size_t byte_count = 0, line = 0; line < lines->len; line++) {
    byte_count += lines->data[line].nbytes;
    if (offset <= byte_count)
      return line + 1; // Found it! (Lines start at 1)
  };
  abort(); // Unreachable, assuming well formed line info
}
