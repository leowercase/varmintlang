#include "code.h"
#include "dis.h"
#include "util.h"
#include "val.h"
#include "proc.h"

#include <stdio.h>

static void constant(FILE *restrict stream, PCode *code, size_t _, size_t idx)
{
  fprintf(stream, "  [%li] = ", idx);
  print_value(stream, code->constants.data[idx]);
  fprintf(stream, ANSI_CYAN);
}

static void size(FILE *restrict stream, PCode *code, size_t _, size_t s)
{
  fprintf(stream, " " ANSI_YELLOW "(%li)" ANSI_CYAN, s);
}

static void jump(FILE *restrict stream, PCode *code, size_t offset,
    size_t jumpable_code, int sign)
{
  size(stream, code, offset, (size_t)jumpable_code);
  fprintf(stream, " -> ");
  // +3 accounts for the instruction and its operands
  size_t dest = (size_t)((long)offset + 3 + sign * (long)jumpable_code);
  dis_instruction(stream, code, dest);
}

static void jump_fwd(FILE *restrict stream, PCode *code, size_t offset,
    size_t jumpable_code)
{
  jump(stream, code, offset, jumpable_code, +1);
}

static void jump_bkwd(FILE *restrict stream, PCode *code, size_t offset,
    size_t jumpable_code)
{
  jump(stream, code, offset, jumpable_code, -1);
}

// Returns the offset where the instruction ends.
size_t dis_instruction(FILE *restrict stream, PCode *code, size_t offset)
{
  Opcode instruction = code->instructions.data[offset];

#define case_(name, stmt) \
  case OP_##name: { \
      fprintf(stream, "%.2i " #name, (int)get_line(&code->lines, offset)); \
      stmt; \
  }

  // Instructions with 16-bit operands
#define case_16_op(name, fn) \
  case_(name, \
    { \
        uint8_t *ip = code->instructions.data + offset + 1; \
        fn(stream, code, offset, uint8_to_16(ip)); \
        return offset + 3; \
    })

  // Instructions with 8/16-bit operands
#define case_var_op(name, fn) \
  case_(name, \
    { \
        fn(stream, code, offset, code->instructions.data[offset + 1]); \
        return offset + 2; \
    }) \
  case_16_op(name##16, fn)

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
  case_var_op(CONST, constant)
  case_(ONE,
    {
      const Value one = value_new(1.0, number);
      fprintf(stream, " ");
      print_value(stream, one);
      fprintf(stream, ANSI_CYAN);
      return offset + 1;
    })
  case_var_op(BUILD_LIST, size)
  case_var_op(BUILD_STR, size)
  case_op(CHAIN_BINOP)
  case_var_op(GET, size)
  case_var_op(SET, size)
  case_op(INDEXED_GET)
  case_op(INDEXED_SET)
  case_op(RESERVE_SLOT)
  case_var_op(END_BLOCK, size)
  case_var_op(END_EMPTY_BLOCK, size)
  case_op(MAKE_SOME)
  case_op(MAKE_NONE)
  case_16_op(JMP, jump_fwd)
  case_16_op(JMP_WHEN_FALSE, jump_fwd)
  case_16_op(IF, jump_fwd)
  case_16_op(ELSE, jump_fwd)
  case_16_op(ELIF, jump_fwd)
  case_16_op(LOOP, jump_bkwd)
  case_16_op(LOOP_COMP, jump_bkwd)
  case_op(FOR_INIT)
  case_16_op(FOR, jump_fwd);
  case_var_op(CALL, size)
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

static void dis_code(FILE *restrict stream, PCode *code)
{
  fprintf(stream, ANSI_CYAN);

  for (size_t offset = 0; offset < code->instructions.len;) {
    offset = dis_instruction(stream, code, offset);
    fprintf(stream, "\n");
  }

  fprintf(stream, ANSI_RESET);
}

void dis(FILE *restrict stream, Procedure *program)
{
  PCode *code = &program->code;

  for (size_t i = 0; i < code->constants.len; i++) {
    Value *c = &code->constants.data[i];

    if (c->type == V_procedure) {
      Procedure *fn = c->as.procedure;

      if (fn->name.s == NULL)
        fprintf(stream, "-- anonymous function [%li] --\n",
            fn->arity);
      else
        fprintf(stream, "-- function %.*s [%li] --\n",
            (int)fn->name.len, fn->name.s, fn->arity);
      dis_code(stream, &c->as.procedure->code);
      fprintf(stream, "\n");
    }
  }

  fprintf(stream, "-- program --\n");
  dis_code(stream, code);
}
