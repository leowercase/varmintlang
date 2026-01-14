#ifndef LANG_COMPILER_H
#define LANG_COMPILER_H

#include "ir.h"
/*
 * Single pass compilation is parsing & compiling in one step.
 * Expressions are translated into stack-based RPN bytecode.
 *
 * https://en.wikipedia.org/wiki/Stack_machine#Design
 * https://en.wikipedia.org/wiki/Reverse_Polish_notation
 * https://en.wikipedia.org/wiki/Operator-precedence_parser#Pratt_parsing
 */

PCode compile(char *source);

#endif
