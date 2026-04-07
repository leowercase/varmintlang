#include "../inc/info.h"
#include "../inc/dis.h"
#include "../inc/util.h"
#include "../inc/val.h"

#include <stdio.h>

static void constant(FILE *restrict stream, Procedure *p, size_t offset, size_t idx)
{
  fprintf(stream, "  [%li] = ", idx);
  print_value(stream, p->code.constants.data[idx]);
  fprintf(stream, ANSI_CYAN);
}

static void size(FILE *restrict stream, Procedure *p, size_t offset, size_t s)
{
  fprintf(stream, " " ANSI_YELLOW "(%li)" ANSI_CYAN, s);
}

static void jump(FILE *restrict stream, Procedure *p, size_t offset,
    size_t jumpable_code, int sign)
{
  size(stream, p, offset, (size_t)jumpable_code);
  fprintf(stream, " -> ");
  size_t dest = (size_t)((long)offset + sign * (long)jumpable_code);
  dis_instruction(stream, p, dest);
}

static void jump_fwd(FILE *restrict stream, Procedure *p, size_t offset,
    size_t jumpable_code)
{
  jump(stream, p, offset, jumpable_code, +1);
}

static void jump_bkwd(FILE *restrict stream, Procedure *p, size_t offset,
    size_t jumpable_code)
{
  jump(stream, p, offset, jumpable_code, -1);
}

static void upval(FILE *restrict stream, Procedure *p, size_t offset,
    size_t upval_idx)
{
  UpvalDesc *desc = &p->closure_desc.data[upval_idx];
  fprintf(stream, "  [%li] = %.*s -> %s [%li]",
      upval_idx,
      (int)desc->name.len, desc->name.s,
      desc->captures_local ? "local" : "upvalue",
      desc->idx);
}

// Returns the offset where the instruction ends.
size_t dis_instruction(FILE *restrict stream, Procedure *p, size_t offset)
{
  Opcode instruction = p->code.instructions.data[offset];

  // Print out the name of an instruction.
#define case_(name, stmt) \
  case OP_##name: { \
      fprintf(stream, "%.2i " #name, (int)get_line(&p->code.lines, offset)); \
      stmt; \
  }

  // Instructions with 16-bit operands
#define case_op(name, fn) \
  case_(name, \
    { \
        uint8_t *ip = &p->code.instructions.data[offset + 1]; \
        offset += 3; \
        fn(stream, p, offset, uint8_to_16(ip)); \
        return offset; \
    })

  // Instructions with 8/16-bit operands
#define case_var_op(name, fn) \
  case_(name, \
    { \
        uint8_t operand = p->code.instructions.data[offset + 1]; \
        offset += 2; \
        fn(stream, p, offset, operand); \
        return offset; \
    }) \
  case_op(name##_16, fn)

  // Simple instructions with no operands.
#define case_i(name) case_(name, return offset + 1)

  switch ((Opcode)instruction) {
  case_i(NOT)
  case_i(NEGATE)
  case_i(FACTORIAL)
  case_i(PERCENTAGE)

  case_i(ADD)
  case_i(SUB)
  case_i(MUL)
  case_i(DIV)
  case_i(POW)
  case_i(MODULO)
  case_i(AND)
  case_i(OR)
  case_i(I9N)
  case_i(EQ)
  case_i(NEQ)
  case_i(LT)
  case_i(GT)
  case_i(LEQ)
  case_i(GEQ)
  case_i(CONCAT)

  case_var_op(CONST, constant)
  case_(ZERO,
    {
      const Value zero = value_new(0.0, number);
      fprintf(stream, " ");
      print_value(stream, zero);
      fprintf(stream, ANSI_CYAN);
      return offset + 1;
    })
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
  case_var_op(BUILD_TABLE, size)
  case_var_op(NESTED_TABLE_ENTRIES, size)
  case_i(MAKE_SOME)
  case_i(MAKE_NONE)

  case_var_op(GET, size)
  case_var_op(SET, size)
  case_var_op(GET_UPVALUE, upval)
  case_var_op(SET_UPVALUE, upval)
  case_i(GET_ELEM)
  case_i(SET_ELEM)

  case_i(POP)
  case_i(SWAP)
  case_i(SWAP_NEATH)
  case_i(SWAP_MOVE_OVER)
  case_i(DUP_2)
  case_var_op(MIRROR, size)

  case_i(RESERVE_SLOT)
  case_var_op(END_BLOCK, size)

  case_op(JMP, jump_fwd)
  case_op(JMP_WHEN_FALSE, jump_fwd)

  case_op(IF, jump_fwd)
  case_op(ELSE, jump_fwd)
  case_op(ELIF, jump_fwd)

  case_i(LIST_COMPREHEND)
  case_op(LOOP, jump_bkwd)
  case_op(LOOP_LIST, jump_bkwd)
  case_op(WHILE, jump_fwd)
  case_op(WHILE_LIST, jump_fwd)
  case_op(FOR, jump_fwd)
  case_op(FOR_LIST, jump_fwd)
  case_var_op(FOR_INCREMENT, size)
  case_op(BREAK, jump_fwd)
  case_op(BREAK_LIST, jump_fwd)
  case_i(DISCARD_FOR)
  case_i(DISCARD_FOR_LIST)

  case_i(CLOSURE)
  case_i(HOIST_UPVALUE)
  case_var_op(PARTIAL, size)
  case_var_op(CALL, size)
  case_i(CALL_UNARY)
  case_i(RETURN)

  case OP_GC:
    break; // Special instruction, shouldn't appear in code
  }

  unreachable();

#undef case_
#undef case_op
#undef case_var_op
#undef case_i
}

void dis(FILE *restrict stream, Procedure *procedure, const char *name)
{
  PCode *code = &procedure->code;

  if (name != NULL) fprintf(stream, "-- %s --\n", name);
  fprintf(stream, ANSI_CYAN);

  for (size_t offset = 0; offset < code->instructions.len;) {
    offset = dis_instruction(stream, procedure, offset);
    fprintf(stream, "\n");
  }

  fprintf(stream, ANSI_RESET "\n");

  // Disassemble any functions inside the procedure.
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

      dis(stream, fn, NULL);
    }
  }
}
