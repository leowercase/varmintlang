#ifndef LANG_VM_H
#define LANG_VM_H

#include "varmint.h"
#include "proc.h"

// Execute a procedure
void execute(Varmint *vm, Proc *program);

#endif
