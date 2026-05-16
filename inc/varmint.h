#ifndef VARMINT_H
#define VARMINT_H

#include "gc.h"
#include "val.h"
#include "state.h"
#include "io.h"

typedef enum {
  VM_A_OK,
  VM_COMPILE_ERR,
  VM_RUNTIME_ERR,
} VarmintStatus;

// User-defined functions for input and output.
typedef struct {
  VmPrint out, error, info;
  VmVaPrint va_error;
  VmInput input;
} Varmio;

/*
 * Heart of a Varmint, offspring of the C cockroach.
 * Contains all common state.
 */
typedef struct Varmint {
  CallStack call_stack;
  CallFrame *frame;
  OpStack op_stack;
  // Linked list of upvalues that capture a value on the stack.
  // Sorted so it reflects the order of the stack.
  Upval *open_upvalues;

  Varmio io;

  // Builtin variables that are included in every program by default
  NameValues builtins;
  bool builtins_emitted;

  GCData *gc_objects; // GC'd values are stored as a singly linked list.
  GCList grey_worklist;
  size_t bytes_allocd, next_gc; // Tally for the next GC sweep
  GCList compiler_roots;

  // Resume execution at this location after a pause
  const uint8_t *resume_ip;

  Value result;
  VarmintStatus status;
} Varmint;

// Initialize a Varmint instance.
Varmint varmint_init(Varmio io);

// Initialize a Varmint parser.
Parse parse_init(Varmint *vm);

// Run some code!
VarmintStatus varmint_run(Varmint *vm, String *source);

// Run some code with previous parse state
VarmintStatus varmint_run_with(Varmint *vm,
    Parse *parse, bool discard_parse_state, String *source);

// Disassemble code
void varmint_dis(Varmint *vm, Parse *parse, VmPrint print,
    String *source, const char *name);

// Free the poor beast.
void varmint_free(Varmint *vm);

// Controls debug output.
//#define VARMINT_DEBUG

#endif
