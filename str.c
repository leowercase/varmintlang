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
  char *s = malloc(len * sizeof(char) + sizeof('\0'));
  if (s == NULL)
    return NULL_STR;

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
  char *s = malloc(len * sizeof(char) + sizeof('\0'));

  if (s == NULL) {
    error_out("Out of memory\n");
    exit(EX_OSERR);
  }

  memcpy(s, head.s, head.len);
  memcpy(s + head.len, tail.s, tail.len + 1);

  Str catted = {s, len};
  return catted;
}

Str str_from_slice(StrSlice slice)
{
  char *cstring = malloc(slice.len * sizeof(char) + sizeof('\0'));

  memcpy(cstring, slice.s, slice.len);
  cstring[slice.len] = '\0';

  return str_new(cstring, slice.len);
}

// Compare strings.
bool strs_eq(Str a, Str b)
{
  if (a.len != b.len)
    return false;

  return memcmp(a.s, b.s, a.len) == 0;
}
