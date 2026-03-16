#ifndef LANG_DEBUG_H
#define LANG_DEBUG_H

#include "code.h"

// Disassemble a program.
void dis(FILE *restrict stream, Procedure *program);

// Disassemble an instruction.
size_t dis_instruction(FILE *restrict stream, PCode *code, size_t offset);

#endif
