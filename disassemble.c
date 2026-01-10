#include "ir.h"
#include "util.h"
#include "val.h"

#include <stdio.h>

static void constant(PCode *code, int idx)
{
  printf("  [%i] = ", idx);
  print_value(code->constants.data[idx]);
  printf(ANSI_CYAN);
}

static void size(PCode *code, int s)
{
  printf(" " ANSI_YELLOW "(%i)" ANSI_CYAN, s);
}

// Returns the offset where the instruction ends.
static size_t disassemble_instruction(PCode *code, size_t offset)
{
  Opcode instruction = code->instruc.data[offset];

#define CASE_(name, statement) \
  case OP_##name: { \
      printf("%.2i " #name, (int)get_line(&code->lines, offset)); \
      statement; \
  }

// Instructions with 8-bit operands
#define CASE_8(name, fn) \
  CASE_(name, \
        fn(code, code->instruc.data[offset + 1]); \
        return offset + 2)

// Instructions with 16-bit operands
#define CASE_16(name, fn) \
  CASE_(name, \
        uint8_t *ip = code->instruc.data + offset + 1; \
        fn(code, uint8_to_16(ip)); \
        return offset + 3)

#define CASE(name) CASE_(name, return offset + 1)

  switch (instruction) {
  CASE(NONE)
  CASE_8(CONST, constant)
  CASE_16(CONST16, constant)
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
  CASE_8(BUILD_LIST, size)
  CASE_16(BUILD_LIST16, size)
  CASE(TO_STR)
  CASE(CONCAT)
  CASE_8(BUILD_STR, size)
  CASE_16(BUILD_STR16, size)
  CASE(CHAIN_BINOP)
  CASE(DISCARD)
  CASE(RETURN)
  }

#undef CASE_
#undef CASE_8
#undef CASE_16
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
