#include "../inc/compile.h"
#include "../inc/dis.h"
#include "../inc/info.h"
#include "../inc/util.h"
#include "../inc/val.h"

#include <stdio.h>

static inline void print_tag(VmPrint print, Procedure *p,
    size_t offset, const char *name)
{
  print(ANSI_BLUE "%4li" ANSI_CYAN " %s" ANSI_RESET,
        get_line(&p->code.lines, offset), name);
}

static void constant(VmPrint print, Procedure *p, size_t offset, size_t idx)
{
  print("  [%li] = ", idx);
  print_value(print, p->code.constants.data[idx]);
}

static void size(VmPrint print, Procedure *p, size_t offset, size_t s)
{
  print(" " ANSI_YELLOW "(%li)" ANSI_RESET, s);
}

static void jump(VmPrint print, Procedure *p, size_t offset,
    size_t jumpable_code, int sign)
{
  size(print, p, offset, (size_t)jumpable_code);
  print(" -> ");
  size_t dest = (size_t)((long)offset + sign * (long)jumpable_code);
  dis_instruction(print, p, dest);
}

static void jump_fwd(VmPrint print, Procedure *p, size_t offset,
    size_t jumpable_code)
{
  jump(print, p, offset, jumpable_code, +1);
}

static void jump_bkwd(VmPrint print, Procedure *p, size_t offset,
    size_t jumpable_code)
{
  jump(print, p, offset, jumpable_code, -1);
}

static void upval(VmPrint print, Procedure *p, size_t offset,
    size_t upval_idx)
{
  UpvalDesc *desc = &p->closure_desc.data[upval_idx];
  print("  [%li] = %.*s -> %s [%li]",
      upval_idx,
      (int)desc->name.len, desc->name.s,
      desc->captures_local ? "local" : "upvalue",
      desc->idx);
}

// Returns the offset where the instruction ends.
size_t dis_instruction(VmPrint print, Procedure *p, size_t offset)
{
  Opcode instruction = p->code.instructions.data[offset];

  // Print out the name of an instruction.
#define case_(name, stmt) \
  case OP_##name: { \
    print_tag(print, p, offset, #name); \
    stmt; \
    print("\n"); \
  }

  // Instructions with 16-bit operands
#define case_op(name, fn) \
  case_(name, \
    { \
      uint8_t *ip = &p->code.instructions.data[offset + 1]; \
      offset += 3; \
      fn(print, p, offset, uint8_to_16(ip)); \
      return offset; \
    })

  // Instructions with 8/16-bit operands
#define case_var_op(name, fn) \
  case_(name, \
    { \
      uint8_t operand = p->code.instructions.data[offset + 1]; \
      offset += 2; \
      fn(print, p, offset, operand); \
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
  case_i(IMPLIES)
  case_i(EQ)
  case_i(NEQ)
  case_i(LT)
  case_i(GT)
  case_i(LEQ)
  case_i(GEQ)
  case_i(CONCAT)
  case_i(NCAT)

  case_var_op(CONST, constant)

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
  case_i(MAYBE_GET_ELEM)

  case_i(POP)
  case_i(SWAP)
  case_i(SWAP_NEATH)
  case_i(SWAP_MOVE_OVER)
  case_i(DUP_2)
  case_var_op(MIRROR, size)

  case_i(RESERVE_SLOT)
  case_var_op(END_BLOCK, size)
  case_var_op(LEVEL_BLOCK, size)
  case_i(END_SLOTS)

  case_op(JMP, jump_fwd)
  case_op(JMP_WHEN_FALSE, jump_fwd)

  case_op(IF, jump_fwd)
  case_op(ELSE, jump_fwd)
  case_op(ELIF, jump_fwd)

  case_op(LOOP, jump_bkwd)
  case_i(FOR)
  case_op(FOR_JMP, jump_fwd)
  case_i(FOR_DISCARD)

  case_i(CLOSURE)
  case_var_op(HOIST, size)
  case_var_op(PARTIAL, size)
  case_var_op(CALL, size)
  case_i(CALL_UNARY)

  case_i(RETURN)
  case_i(SUSPEND)

  case OP_HALT:
  case OP_RESUME:
  case OP_GC:
    break; // Special instruction, shouldn't appear in code
  }

  unreachable();

#undef case_
#undef case_op
#undef case_var_op
#undef case_i
}

void dis_procedure(VmPrint print, Procedure *p, const char *name)
{
  PCode *code = &p->code;

  if (name != NULL) print("-- %s --\n", name);

  for (size_t offset = 0; offset < code->instructions.len;) {
    offset = dis_instruction(print, p, offset);
    print("\n");
  }

  // Disassemble any functions inside the procedure.
  for (size_t i = 0; i < code->constants.len; i++) {
    Value *c = &code->constants.data[i];

    if (c->type == V_procedure) {
      print("\n");

      Procedure *fn = c->as.procedure;

      if (fn->name.s == NULL)
        print("-- anonymous function [%li] --\n",
            fn->arity);
      else
        print("-- function %.*s [%li] --\n",
            (int)fn->name.len, fn->name.s, fn->arity);

      dis_procedure(print, fn, NULL);
    }
  }
}
