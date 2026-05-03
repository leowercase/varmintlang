#include "../inc/code.h"
#include "../inc/info.h"
#include "../inc/gc.h"
#include "../inc/val.h"
#include "../inc/varmint.h"

//#ifdef VARMINT_DEBUG
// #include <stdio.h>
// #define GC_DBG_MSG(msg) info_out("[GC] " msg)
// #define GC_DBG_FMT_MSG(fmt, ...) info_out("[GC] " fmt, __VA_ARGS__)
// #else
#define GC_DBG_MSG(_)
#define GC_DBG_FMT_MSG(_, ...)
// #endif

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
  if (vm->call_stack.len == 0
      || *vm->frame->ip == OP_HALT || *vm->frame->ip == OP_GC)
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

GCData *create_gc_obj(Varmint *vm, Typetag type, size_t size)
{
  GCData *data = gc_alloc(vm, NULL, 0, size);
  data->is_safe = false;

  data->type = type;

  // Insert into objects list
  data->next = vm->gc_objects;
  vm->gc_objects = data;

  return data;
}

static inline void add_grey(Varmint *vm, GCData *obj)
{
  GCList_push(&vm->grey_worklist, obj);
}

static inline void add_grey_val(Varmint *vm, Value val)
{
  if (is_heaped_value(val)) add_grey(vm, val.as.gc_data);
}

static void mark_obj(Varmint *vm, GCData *obj);

static inline void mark_val(Varmint *vm, Value val)
{
  if (is_heaped_value(val)) mark_obj(vm, val.as.gc_data);
}

static inline void mark_procedure(Varmint *vm, Procedure *procedure)
{
  Constants *constants = &procedure->code.constants;

  for (size_t i = 0; i < constants->len; i++)
    mark_val(vm, constants->data[i]);

  mark_obj(vm, (GCData *)procedure->source);
}

static void mark_obj(Varmint *vm, GCData *obj)
{
  Typetag t = obj->type;

  if (obj == NULL || obj->is_safe) return; // Already marked.
  obj->is_safe = true;
  add_grey(vm, obj);

  GC_DBG_FMT_MSG("mark %p of type %s\n", (void *)obj, typetag_cstring(t));

  // Mark child objects
  switch (t) {
  case V_no:
  case V_number:
  case V_boolean:
  case V_native:
    unreachable();
  case V_maybe:
    mark_val(vm, ((Maybe *)obj)->raw);
    break;
  case V_string:
    break;
  case V_list:
    {
      List *list = (List *)obj;
      for (size_t i = 0; i < list->len; i++)
        mark_val(vm, list->data[i]);
      break;
    }
  case V_table:
    {
      Table *table = (Table *)obj;
      for (size_t i = 0; i < table->cap; i++) {
        TableEntry ent = table->entries[i];
        if (!ent.is_tomb && ent.key.type != V_no) mark_val(vm, ent.value);
      }
      break;
    }
  case V_procedure:
    mark_procedure(vm, (Procedure *)obj);
    break;
  case V_upval:
    {
      Upval *upval = (Upval *)obj;
      if (upval->loc == &upval->hoisted) mark_val(vm, upval->hoisted);
      break;
    }
  case V_closure:
    {
      Closure *c = (Closure *)obj;
      mark_procedure(vm, c->procedure);

      for (size_t i = 0; i < c->upvalue_count; i++)
        mark_obj(vm, (GCData *)c->upvalues[i]);
      break;
    }
  case V_cclosure:
    {
      Cclosure *c = (Cclosure *)obj;

      for (size_t i = 0; i < c->upvalue_count; i++)
        mark_val(vm, c->upvalues[i]);
      break;
    }
  case V_partial:
    {
      Partial *p = (Partial *)obj;
      mark_val(vm, p->callee);

      for (size_t i = 0; i < p->application_count; i++)
        mark_val(vm, p->applied[i]);
      break;
    }
  }
}

static void mark(Varmint *vm)
{
  // 1. Move root objects to the "greys" worklist.
  // Among the roots are: compiler roots, the operation stack, builtins

  for (size_t i = 0; i < vm->compiler_roots.len; i++)
    add_grey(vm, vm->compiler_roots.data[i]);

  for (size_t i = 0; i < vm->op_stack.len; i++)
    add_grey_val(vm, vm->op_stack.data[i]);

  for (size_t i = 0; i < vm->builtins.len; i++)
    add_grey_val(vm, vm->builtins.data[i].value);

  GC_DBG_FMT_MSG("mark roots (%li)\n", vm->grey_worklist.len);

  // 2. Trace references. Mark all greys and their children safe.
  while (vm->grey_worklist.len > 0)
    mark_obj(vm, GCList_pop(&vm->grey_worklist));
}

static void free_gc_obj(Varmint *vm, GCData *data)
{
#define FREE(T) gc_free(vm, data, sizeof(T))

  GC_DBG_FMT_MSG("free %p of type %s\n",
      (void *)data, typetag_cstring(data->type));

  switch (data->type) {
  case V_no:
  case V_number:
  case V_boolean:
  case V_native:
    unreachable();
  case V_maybe:
    FREE(Maybe);
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
  case V_cclosure:
    FREE(Cclosure);
    break;
  case V_partial:
    FREE(Partial);
    break;
  }

#undef FREE
}

void sweep(Varmint *vm)
{
  for (GCData **head_ptr = &vm->gc_objects, *obj = vm->gc_objects;
      obj != NULL;) {
    if (obj->is_safe) {
      // "Safe"
      // Reset status for the next GC run.
      obj->is_safe = false;

      // Next.
      head_ptr = &((*head_ptr)->next); // Head points to the current obj ptr
      obj = obj->next;
    }
    else {
      // "DOOMED"
      GCData *next_obj = obj->next;

      // Remove from objects.
      *head_ptr = next_obj;
      // Free memory.
      free_gc_obj(vm, obj);

      // Next.
      obj = next_obj;
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
  for (GCData *obj = vm->gc_objects; obj != NULL;) {
    GCData *next_obj = obj->next;
    free_gc_obj(vm, obj);
    obj = next_obj;
  }
}

