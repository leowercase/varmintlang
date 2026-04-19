#ifndef VARMINT_DIS_H
#define VARMINT_DIS_H

#include "varmint.h"

// Disassemble an instruction.
size_t dis_instruction(FILE *restrict stream, Procedure *p, size_t offset);

// Disassemble a program.
void dis(FILE *restrict stream, Procedure *procedure, const char *name);

// Disassemble source code
void dis_source(FILE *restrict stream,
    Varmint *vm, Parse *parse, String *source, const char *name);

#endif
