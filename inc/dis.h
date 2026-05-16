#ifndef VARMINT_DIS_H
#define VARMINT_DIS_H

#include "code.h"
#include "io.h"

// Disassemble an instruction.
size_t dis_instruction(VmPrint print, Procedure *p, size_t offset);

// Disassemble a procedure.
void dis(VmPrint print, Procedure *p, const char *name);

#endif
