#include "../inc/code.h"
#include "../inc/info.h"
#include "../inc/gc.h"
#include "../inc/val.h"
#include "../inc/varmint.h"

#ifdef VARMINT_DEBUG
#include <stdio.h>
#define GC_DBG_MSG(msg) info_out("[GC] " msg)
#define GC_DBG_FMT_MSG(fmt, ...) info_out("[GC] " fmt, __VA_ARGS__)
#else
#define GC_DBG_MSG(_)
#define GC_DBG_FMT_MSG(_, ...)
#endif

const size_t INITIAL_NEXT_GC = 16384;
const float64_t GC_GROWTH_FACTOR = 2.0;

void gc_init(Varmint *vm)
{
  vm->bytes_allocd = 0;
  vm->next_gc = INITIAL_NEXT_GC;
  vm->gc_objects = NULL;
  vm->grey_worklist = GCList_init();
  vm->compiler_roots = GCList_init();
}

static const uint8_t GC_INSTRUCTION = OP_GC;
// Credit: https://news.ycombinator.com/item?id=40958899
//
// Set ip temporarily to a special instruction, OP_GC, triggering GC.
// Simplifies things.
// We no longer have to worry about semi-initialized data suddenly getting
// swept away mid-instruction just because it wasn't traceable to a root yet
//
// ...A potential downside is when the OS is short on memory and need to alloc
// (but that is left as an exercise for the reader. :)
static inline void set_gc_ip(Varmint *vm)
{
  if (vm->call_stack.len == 0 || vm->frame->ip == &GC_INSTRUCTION)
    return;
  vm->gc_resume_ip = vm->frame->ip;
  vm->frame->ip = &GC_INSTRUCTION;
}

static inline void record_bytes_delta(Varmint *vm, size_t old, size_t new)
{
  size_t delta = new - old;
  vm->bytes_allocd += delta;

  if (vm->bytes_allocd > vm->next_gc)
    // Need to collect garbage.
    set_gc_ip(vm);
}

void *gc_alloc(Varmint *vm, void *ptr, size_t old_size, size_t new_size)
{
  record_bytes_delta(vm, old_size, new_size);
  return allocate(ptr, new_size);
}

// Free bytes from GC.
void gc_free(Varmint *vm, void *ptr, size_t size)
{
  record_bytes_delta(vm, size, 0);
  free(ptr);
}

void gc_own_bytes(Varmint *vm, size_t nbytes)
{
  record_bytes_delta(vm, 0, nbytes);
}

Value *create_gc_obj(Varmint *vm, Typetag type, size_t size)
{
  GCData *data = gc_alloc(vm, NULL, 0, size);
  data->is_safe = false;

  Value *obj = gc_alloc(vm, NULL, 0, sizeof(Value));
  obj->type = type;
  obj->as.gc_data = data;

  // Insert into objects list
  data->next = vm->gc_objects;
  vm->gc_objects = obj;

  return obj;
}

static inline void add_grey(Varmint *vm, Value obj)
{
  GCList_push(&vm->grey_worklist, obj);
}

static void mark_obj(Varmint *vm, Value obj);

static inline void mark_procedure(Varmint *vm, Procedure *procedure)
{
  Constants *constants = &procedure->code.constants;

  for (size_t i = 0; i < constants->len; i++) {
    Value c = constants->data[i];
    if (is_heaped_value(c)) mark_obj(vm, c);
  }
}

static void mark_obj(Varmint *vm, Value obj)
{
  Typetag t = obj.type;
  GCData *data = obj.as.gc_data;

  if (data == NULL || data->is_safe) return; // Already marked.
  data->is_safe = true;
  add_grey(vm, obj);

  GC_DBG_FMT_MSG("mark %p of type %s\n", (void *)data, value_type_cstring(t));

  switch (t) {
  case V_no:
  case V_number:
  case V_boolean:
  case V_native:
    unreachable();
  case V_maybe:
    {
      if (is_heaped_value(obj.as.maybe->raw))
        mark_obj(vm, obj.as.maybe->raw);
      break;
    }
  case V_range:
  case V_string:
    break;
  case V_list:
    for (size_t i = 0; i < obj.as.list->len; i++) {
      Value elem = obj.as.list->data[i];
      if (is_heaped_value(elem)) mark_obj(vm, elem);
    }
    break;
  case V_table:
    for (size_t i = 0; i < obj.as.table->cap; i++) {
      TableEntry ent = obj.as.table->entries[i];
      if (!ent.is_tomb && ent.key.type != V_no && is_heaped_value(ent.value))
        mark_obj(vm, ent.value);
    }
    break;
  case V_procedure:
    mark_procedure(vm, obj.as.procedure);
    break;
  case V_upval:
    {
      Upval *upval = obj.as.upval;
      if (upval->loc == &upval->hoisted && is_heaped_value(upval->hoisted))
        mark_obj(vm, upval->hoisted);
      break;
    }
  case V_closure:
    mark_procedure(vm, obj.as.closure->procedure);
    break;
  }
}

static void mark(Varmint *vm)
{
  // 1. Move root objects to the "greys" worklist.
  // Among the roots are: compiler roots, the operation stack

  for (size_t i = 0; i < vm->compiler_roots.len; i++)
    add_grey(vm, vm->compiler_roots.data[i]);

  for (size_t i = 0; i < vm->op_stack.len; i++) {
    Value val = vm->op_stack.data[i];
    if (is_heaped_value(val)) add_grey(vm, val);
  }

  GC_DBG_FMT_MSG("mark roots (%li)\n", vm->grey_worklist.len);

  // 2. Trace references. Mark all greys and their children safe.
  while (vm->grey_worklist.len > 0)
    mark_obj(vm, GCList_pop(&vm->grey_worklist));
}

static void free_obj_data(Varmint *vm, Typetag t, GCData *data)
{
#define FREE(T) gc_free(vm, data, sizeof(T))

  GC_DBG_FMT_MSG("free %p of type %s\n", (void *)data, value_type_cstring(t));

  switch (t) {
  case V_no:
  case V_number:
  case V_boolean:
  case V_native:
    unreachable();
  case V_maybe:
    FREE(Maybe);
    break;
  case V_range:
    FREE(Range);
    break;
  case V_string:
    {
      String *string = (String *)data;
      gc_free(vm, string->s, string->len * sizeof(char));
      FREE(String);
      break;
    }
  case V_list:
    {
      List *list = (List *)data;
      gc_free(vm, list->data, list->cap * sizeof(Value));
      FREE(List);
      break;
    }
  case V_table:
    {
      Table *table = (Table *)data;
      gc_free(vm, table->entries, table->cap * sizeof(TableEntry));
      FREE(Table);
      break;
    }
  case V_procedure:
    {
      PCode *code = &((Procedure *)data)->code;

      // Free p-code
      Constants_free(vm, &code->constants);
      Instructions_free(vm, &code->instructions);
      LineInfo_free(vm, &code->lines);

      // Free closure description
      ClosureDesc_free(vm, &((Procedure *)data)->closure_desc);

      FREE(Procedure);
      break;
    }
  case V_upval:
    FREE(Upval);
    break;
  case V_closure:
    FREE(Closure);
    break;
  }

#undef FREE
}

void sweep(Varmint *vm)
{
  if (vm->gc_objects == NULL) return;

  for (Value **head = &vm->gc_objects, *obj = vm->gc_objects; obj != NULL;) {
    GCData *data = obj->as.gc_data;

    if (data->is_safe) {
      // Reset "safe" status for the next GC run.
      data->is_safe = false;
      // Next.
      head = &obj;
      obj = data->next;
    }
    else {
      Value *next = data->next;
      // Remove from objects.
      (*head)->as.gc_data->next = next;

      // Free.
      free_obj_data(vm, obj->type, data);
      gc_free(vm, obj, sizeof(Value));

      // Next.
      obj = next;
    }
  }
}

// A tri-color stop-the-world GC.
void gcollect(Varmint *vm)
{
  GC_DBG_MSG("start\n");

  // Mark reachable objects
  mark(vm);

  // Sweep through the objects.
  sweep(vm);

  // Set threshold for next GC
  vm->next_gc = (size_t)((float64_t)vm->next_gc * GC_GROWTH_FACTOR);

  GC_DBG_MSG("end\n");
  GC_DBG_FMT_MSG("next GC in %li heaped bytes\n", vm->next_gc);
}

void gc_end(Varmint *vm)
{
  // Free GC lists.
  free(vm->grey_worklist.data);
  free(vm->compiler_roots.data);

  // Free objects registered.
  for (Value *obj = vm->gc_objects; obj != NULL;) {
    Typetag t = obj->type;
    GCData *data = obj->as.gc_data;

    Value *next_obj = data->next;

    free_obj_data(vm, t, data);
    free(obj);

    obj = next_obj;
  }
}

