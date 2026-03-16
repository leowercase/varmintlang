#ifndef LANG_VM_H
#define LANG_VM_H

#include "varmint.h"
#include "code.h"

// Execute a procedure
void execute(Varmint *vm, Procedure *program);

#endif
