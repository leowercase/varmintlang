#ifndef VARMINT_DEBUG_H
#define VARMINT_DEBUG_H

#include "code.h"

// Disassemble a program.
void dis(FILE *restrict stream, Procedure *procedure, const char *name);

// Disassemble an instruction.
size_t dis_instruction(FILE *restrict stream, Procedure *p, size_t offset);

#endif
