#include "../mem.h"
#include "../str.h"
#include "../util.h"

/*
 * This header file declares a hash table of keys, values type K, V.
 *
 * Impl uses open addressing; entries reside directly in the buckets array.
 * Cache locality favors this approach as opposed to separate chaining.
 *
 * https://en.wikipedia.org/wiki/Hash_table
 * https://en.wikipedia.org/wiki/Lazy_deletion
 *
 * K             key type (required)
 * V             value type (required)
 * TBL_ENTRY     entry struct of K, V (required)
 * TBL           hash table struct (required)
 * HASH(key)     hasher (required)
 * KEYS_EQ(a, b) test equality for two keys, defaults to primitive equality ==
 * USE_GC        whether to use GC for allocation
 */

#ifndef K
#error Oops! Must define a key type K.
#endif

#ifndef V
#error Oops! Must define a value type V.
#endif

#ifndef TBL_ENTRY
#error Oops! Must define the table entry structure TBL_ENTRY.
#endif

#ifndef TBL
#error Oops! Must define the table structure TBL.
#endif

#ifndef HASH
#error Oops! Must define the hashing function HASH.
#endif

#ifndef KEYS_EQ
#define KEYS_EQ(a, b) (a == b)
#endif

#ifndef USE_GC
#define ALLOCS(...) (__VA_ARGS__)
#define CALLS_ALLOC(...) (__VA_ARGS__)
#else
struct Varmint;
#define ALLOCS(...) (struct Varmint *vm, __VA_ARGS__)
#define CALLS_ALLOC(...) (vm, __VA_ARGS__)
// Forward declaration.
void *gc_alloc(struct Varmint *vm, void *ptr, size_t old_size, size_t new_size);
#endif

#define METHOD(name) JOIN(TBL, name)

/*
 * https://en.wikipedia.org/wiki/Hash_table#Load_factor
 * Load factor α = entry count / capacity.
 */
#define MAX_LOAD_FACTOR 0.75

// Lazily initialize a new hash table
static inline
TBL METHOD(_init)(void)
{
  TBL table;
  table.entry_count = table.cap = 0;
  table.entries = NULL;

  return table;
}

// Find the entry matching key, or the first usable empty entry.
static inline
TBL_ENTRY *METHOD(_probe_entry)(TBL *table, K key)
{
  uint64_t hash = HASH(key);

  TBL_ENTRY *tomb = NULL;
  // Crucial to keep α < α_max and α_max < 1, otherwise we loop forever!
  for (size_t i = mod_2(hash, table->cap);; i = mod_2(i + 1, table->cap)) {
    TBL_ENTRY *entry = &table->entries[i];

    if (entry->is_tomb && tomb == NULL)
      // Mark the first tomb crossed
      tomb = entry;

    else if (entry->key.s == NULL)
      // Found an empty entry!
      // Couldn't find the maching entry, so we return an empty one.
      // Tomb recyclage takes top priority, though.
      return tomb == NULL ? entry : tomb;

    else if (KEYS_EQ(key, entry->key))
      // We found the matching entry!
      return entry;
  }
}

// Resize table to a new capacity.
static inline
void METHOD(_resize)ALLOCS(TBL *table)
{
  size_t new_cap = grow_cap(table->cap);

  TBL_ENTRY *new_entries =
#ifndef USE_GC
    allocate(NULL, new_cap * sizeof(TBL_ENTRY));
#else
    gc_alloc(vm, NULL,
      table->cap * sizeof(TBL_ENTRY), new_cap * sizeof(TBL_ENTRY));
#endif

  // Zero out entries
  for (size_t i = 0; i < new_cap; i++) {
    new_entries[i].key.s = NULL;
    new_entries[i].is_tomb = false;
  }

  // Copy entries over.
  for (size_t i = 0; i < table->cap; i++) {
    TBL_ENTRY *entry = &table->entries[i];

    if (entry->is_tomb) {
      // Don't include tombstones in the new table.
      // They can easily inflate entry count
      table->entry_count--;
      continue;
    }
    else if (entry->key.s == NULL)
      continue;

    new_entries[i] = *entry;
  }

  table->cap = new_cap;
  table->entries = new_entries;
}

// Get table value.
static inline
V *METHOD(_get)(TBL *table, K key)
{
  if (table->entry_count == 0)
    return NULL;

  TBL_ENTRY *entry = METHOD(_probe_entry)(table, key);

  if (entry->key.s == NULL || entry->is_tomb)
    return NULL;
  else
    return &entry->value;
}

// Set table value.
// Returns whether a new entry was recorded.
static inline
bool METHOD(_set)ALLOCS(TBL *table, K key, V value)
{
  if ((double)table->entry_count >= (double)table->cap * MAX_LOAD_FACTOR)
    // α >= α_max or table is empty, should resize.
    METHOD(_resize)CALLS_ALLOC(table);

  TBL_ENTRY *entry = METHOD(_probe_entry)(table, key);
  bool is_new_entry = entry->key.s == NULL;

  if (is_new_entry && !entry->is_tomb)
    table->entry_count++;

  entry->key = key;
  entry->value = value;
  entry->is_tomb = false;

  return is_new_entry;
}

// Delete an entry.
static inline
V METHOD(_delete)(TBL *table, K key)
{
  // A "tombstone" is placed on the dead entry, we are so lazy.
  TBL_ENTRY *entry = METHOD(_probe_entry)(table, key);
  entry->is_tomb = true;
  return entry->value;
}

#undef MAX_LOAD_FACTOR
#undef METHOD
#undef ALLOC_PARAMS
#undef ALLOC_ARGS
#undef ALLOC
#undef TBL
#undef KEYS_EQ
#undef HASH
#undef TBL_ENTRY
#undef V
#undef K
