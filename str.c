#include "mem.h"
#include "str.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// https://github.com/Cyan4973/xxHash
#include <xxhash.h>

// Return the 64-bit hash of a string.
uint64_t str_hash(Str str)
{
  return XXH3_64bits(str.s, str.len);
}

// Format strings just like sprintf et al., except retaining sanity
Str str_fmt(const char *fmt, ...)
{
  // man 3 vnsprintf
  va_list args;
  int n;

  // Determine required size for format string.
  va_start(args, fmt);
  n = vsnprintf(NULL, 0, fmt, args);
  va_end(args);

  if (n < 0)
    // Error.
    return NULL_STR;

  size_t len = (size_t)n;
  char *s = allocate(NULL, len * sizeof(char) + sizeof('\0'));

  // Actually do the thing™
  va_start(args, fmt);
  n = vsnprintf(s, len + 1, fmt, args);
  va_end(args);

  if (n < 0) {
    // Again, error.
    free(s);
    return NULL_STR;
  }

  return str_new(s, len);
}

// Concatenate two null-terminated strings.
Str str_concat(Str head, Str tail)
{
  size_t len = head.len + tail.len;
  char *s = allocate(NULL, len * sizeof(char) + sizeof('\0'));

  memcpy(s, head.s, head.len);
  memcpy(s + head.len, tail.s, tail.len + 1);

  Str catted = {s, len};
  return catted;
}

Str str_copy(Str str)
{
  char *cstring = allocate(NULL, str.len * sizeof(char) + sizeof('\0'));

  memcpy(cstring, str.s, str.len);
  cstring[str.len] = '\0';

  return str_new(cstring, str.len);
}

// Compare strings.
bool strs_eq(Str a, Str b)
{
  if (a.len != b.len)
    return false;

  return memcmp(a.s, b.s, a.len) == 0;
}
