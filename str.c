#include "str.h"

#include <stdarg.h>
#include <string.h>

// https://github.com/Cyan4973/xxHash
#include <xxhash.h>

// Return the 64-bit hash of a string.
uint64_t str_hash(Str str)
{
  return XXH3_64bits(str.s, str.len);
}

// Compare strings.
bool strs_eq(Str a, Str b)
{
  if (a.len != b.len)
    return false;

  return memcmp(a.s, b.s, a.len) == 0;
}
