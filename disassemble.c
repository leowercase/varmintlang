#include "ir.h"
#include "util.h"
#include "val.h"

#include <stdio.h>

static size_t constant(PCode *code, size_t offset)
{
  uint8_t idx = code->instruc.data[offset + 1];

  printf("    [%i] = " ANSI_RED "'", idx);

  print_value(code->constants.data[idx]);

  printf("'" ANSI_CYAN);

  return offset + 2;
}

static size_t constant16(PCode *code, size_t offset)
{
  uint16_t idx = uint8_to_16(code->instruc.data + offset + 1);

  printf("  [%i] = " ANSI_RED "'", idx);

  print_value(code->constants.data[idx]);

  printf("'" ANSI_CYAN);

  return offset + 3;
}

// Returns the offset where the instruction ends.
static size_t disassemble_instruction(PCode *code, size_t offset)
{
  Opcode instruction = code->instruc.data[offset];

#define CASE_E(name, return_expr) \
  case OP_##name: { \
      printf("%.2i " #name, (int)get_line(&code->lines, offset)); \
      return return_expr; \
  }

#define CASE(name) CASE_E(name, offset + 1)

  switch (instruction) {
  CASE(NONE)
  CASE_E(CONST, constant(code, offset))
  CASE_E(CONST16, constant16(code, offset))
  CASE(NOT)
  CASE(NEGATE)
  CASE(FACTORIAL)
  CASE(PERCENTAGE)
  CASE(ADD)
  CASE(SUB)
  CASE(MUL)
  CASE(DIV)
  CASE(POW)
  CASE(MODULO)
  CASE(AND)
  CASE(OR)
  CASE(I9N)
  CASE(EQ)
  CASE(NEQ)
  CASE(LT)
  CASE(GT)
  CASE(LEQ)
  CASE(GEQ)
  CASE(CHAIN_BINOP)
  CASE(RETURN)
  }

#undef CASE_E
#undef CASE
}

void disassemble(PCode *code)
{
  printf(ANSI_CYAN);

  for (size_t offset = 0; offset < code->instruc.len;) {
    offset = disassemble_instruction(code, offset);
    printf("\n");
  }

  printf(ANSI_RESET);
}
