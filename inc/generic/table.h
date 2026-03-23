#ifndef VARMINT_TABLE_H
#define VARMINT_TABLE_H

#include <stdbool.h>
#include <stddef.h>

#define TABLE_ENTRY(K, V) \
  K key; V value; bool is_tomb;

#define TABLE_ENTRY_STRUCT(K, V) \
  struct { TABLE_ENTRY(K, V) }

#define TABLE(TBL_ENTRY) \
  size_t entry_count, cap; \
  TBL_ENTRY *entries;

#define TABLE_STRUCT(TBL_ENTRY) \
  struct { TABLE(TBL_ENTRY) }

#endif
