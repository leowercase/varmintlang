#include "../mem.h"
#include "../str.h"
#include "../util.h"

/*
 * This header file declares a hash table of keys, values of type K, V.
 * 
 * Impl uses open addressing; entries reside directly in the buckets array.
 * This dynamic array approach is friends with CPU cache locality.
 *
 * https://en.wikipedia.org/wiki/Hash_table
 * https://en.wikipedia.org/wiki/Lazy_deletion
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

#ifndef HASH
#error Oops! Must define the hashing function HASH.
#endif

#ifndef KEYS_EQ
#define KEYS_EQ(a, b) (a == b)
#endif

#ifndef TBL
#error Oops! Must define the table structure TBL.
#endif

#define METHOD(name) JOIN(TBL, name)

/*
 * https://en.wikipedia.org/wiki/Hash_table#Load_factor
 * Load factor α = entry count / capacity.
 */
#define MAX_LOAD_FACTOR 0.75

// Lazily initialize a new hash table
static inline
TBL METHOD(_new)()
{
  TBL table;
  table.entry_count = table.cap = 0;
  table.entries = NULL;

  return table;
}

// Find the entry matching key, or the first usable empty entry.
static inline
TBL_ENTRY *METHOD(_probe_entry)(TBL *table, Str key)
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
void METHOD(_resize)(TBL *table)
{
  size_t new_cap = grow_cap(table->cap);
  TBL_ENTRY *new_entries = allocate(NULL, new_cap * sizeof(TBL_ENTRY));

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
V *METHOD(_get)(TBL *table, Str key)
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
bool METHOD(_set)(TBL *table, Str key, V value)
{
  if (table->entry_count >= table->cap * MAX_LOAD_FACTOR)
    // α >= α_max or table is empty, should resize.
    METHOD(_resize)(table);

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
V METHOD(_delete)(TBL *table, Str key)
{
  // A "tombstone" is placed on the dead entry, we are so lazy.
  TBL_ENTRY *entry = METHOD(_probe_entry)(table, key);
  entry->is_tomb = true;
  return entry->value;
}

#undef MAX_LOAD_FACTOR
#undef METHOD
#undef TBL
#undef KEYS_EQ
#undef HASH
#undef TBL_ENTRY
#undef V
#undef K
