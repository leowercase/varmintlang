#ifndef LANG_DEBUG_H
#define LANG_DEBUG_H

#include "proc.h"

// Disassemble a program.
void disassemble(Procedure *program);

// Disassemble an instruction.
size_t disassemble_instruction(PCode *code, size_t offset);

#endif
