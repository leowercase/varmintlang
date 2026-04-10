#ifndef VARMINT_VM_H
#define VARMINT_VM_H

#include "varmint.h"
#include "code.h"

// Ready a program for execution.
void call_program(Varmint *vm, Procedure *program);

// Run the VM.
void run_bytecode(Varmint *vm);

#endif
