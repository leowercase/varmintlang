#ifndef LANG_UTIL_H
#define LANG_UTIL_H

// ANSI escape codes control styling in terminals
// https://en.wikipedia.org/wiki/ANSI_escape_code#Colors
#define ANSI_BLACK   "\x1b[30m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_BLUE    "\x1b[34m"
#define ANSI_MAGENTA "\x1b[35m"
#define ANSI_CYAN    "\x1b[36m"
#define ANSI_WHITE   "\x1b[37m"
#define ANSI_RESET   "\x1b[0m"

// Helper function for errors.
void error_out(const char *msg_template, ...);

#endif
