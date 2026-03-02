#include "code.h"
#include "disassemble.h"
#include "util.h"
#include "val.h"
#include "proc.h"

#include <stdio.h>

static void constant(PCode *code, size_t _, size_t idx)
{
  printf("  [%li] = ", idx);
  print_value(stdout, code->constants.data[idx]);
  printf(ANSI_CYAN);
}

static void size(PCode *code, size_t _, size_t s)
{
  printf(" " ANSI_YELLOW "(%li)" ANSI_CYAN, s);
}

static void jump(PCode *code, size_t offset, size_t jumpable_bytes)
{
  size(code, offset, jumpable_bytes);

  printf(" -> ");
  disassemble_instruction(code,
      offset + 3 + jumpable_bytes); // + 3 for skipping over to the next opcode
}

// Returns the offset where the instruction ends.
size_t disassemble_instruction(PCode *code, size_t offset)
{
  Opcode instruction = code->instructions.data[offset];

#define case_(name, stmt) \
  case OP_##name: { \
      printf("%.2i " #name, (int)get_line(&code->lines, offset)); \
      stmt; \
  }

  // Instructions with 16-bit operands
#define case_fixed_size_op(name, fn) \
  case_(name, \
    { \
        uint8_t *ip = code->instructions.data + offset + 1; \
        fn(code, offset, uint8_to_16(ip)); \
        return offset + 3; \
    })

  // Instructions with 8/16-bit operands
#define case_size_op(name, fn) \
  case_(name, \
    { \
        fn(code, offset, code->instructions.data[offset + 1]); \
        return offset + 2; \
    }) \
  case_fixed_size_op(name##16, fn)

#define case_op(name) case_(name, return offset + 1)

  switch ((Op)instruction) {
  case_op(NOT)
  case_op(NEGATE)
  case_op(FACTORIAL)
  case_op(PERCENTAGE)
  case_op(ADD)
  case_op(SUB)
  case_op(MUL)
  case_op(DIV)
  case_op(POW)
  case_op(MODULO)
  case_op(AND)
  case_op(OR)
  case_op(I9N)
  case_op(EQ)
  case_op(NEQ)
  case_op(LT)
  case_op(GT)
  case_op(LEQ)
  case_op(GEQ)
  case_op(IN)
  case_op(NOTIN)
  case_op(CONCAT)
  }

  switch ((Opcode)instruction) {
  case_size_op(CONST, constant)
  case_(ONE,
    {
      const Value one = value_new(1.0, number);
      printf(" ");
      print_value(stdout, one);
      printf(ANSI_CYAN);
      return offset + 1;
    })
  case_size_op(BUILD_LIST, size)
  case_size_op(BUILD_STR, size)
  case_op(CHAIN_BINOP)
  case_size_op(GET, size)
  case_size_op(SET, size)
  case_op(LIST_GET)
  case_op(LIST_SET)
  case_op(RESERVE_SLOT)
  case_op(DUPLICATE)
  case_op(DISCARD)
  case_size_op(DISCARDN, size)
  case_size_op(RETAIN1_DISCARDN, size)
  case_op(MAKE_SOME)
  case_op(MAKE_NONE)
  case_op(UNWRAP_MAYBE)
  case_fixed_size_op(IF_CLAUSE, jump)
  case_fixed_size_op(ELSE_CLAUSE, jump)
  case_fixed_size_op(ELIF_CLAUSE, jump)
  case_size_op(CALL, size)
  case_op(RETURN)
  case OP_GC:
    break; // Special instruction, shouldn't appear in code
  }

  unreachable();

#undef case_
#undef case_8
#undef case_16
#undef case_op
}

static void disassemble_code(PCode *code)
{
  printf(ANSI_CYAN);

  for (size_t offset = 0; offset < code->instructions.len;) {
    offset = disassemble_instruction(code, offset);
    printf("\n");
  }

  printf(ANSI_RESET);
}

void disassemble(Procedure *program)
{
  PCode *code = &program->code;

  for (size_t i = 0; i < code->constants.len; i++) {
    Value *c = &code->constants.data[i];

    if (c->type == V_procedure) {
      Procedure *fn = c->as.procedure;

      if (fn->name.s == NULL)
        printf("-- anonymous function [%li] --\n",
            fn->arity);
      else
        printf("-- function %.*s [%li] --\n",
            (int)fn->name.len, fn->name.s, fn->arity);
      disassemble_code(&c->as.procedure->code);
      printf("\n");
    }
  }

  printf("-- program --\n");
  disassemble_code(code);
}
