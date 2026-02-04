#ifndef LANG_ASTREE
#define LANG_ASTREE

#include "generic/dyn_array.h"
#include "op.h"
#include "val.h"

/*
 * An abstract syntax tree is a representation of code's structure.
 * https://en.wikipedia.org/wiki/Abstract_syntax_tree
 */

typedef enum {
  // OP_NOT, ...
  // OP_ADD, ...
  AST_ASSIGN = NATIVE_OPERATOR_COUNT,
  AST_COMPOUND_ASSIGN,
  AST_MAPLET,
  AST_CONSTANT,
  AST_METASTRING,
  AST_GROUPING,
  AST_CALL,
  AST_LIST,
  AST_SUBSCRIPT,
  AST_IDENT,
  AST_BLOCK, AST_CLOSED_BLOCK,
  AST_IF, AST_ELSE,
  AST_LOOP, AST_FOR, AST_WHILE,
  AST_FLOW_CONTROL,
  AST_LET,
} AST_T;

typedef DYN_ARRAY_STRUCT(struct Tnode *) NodeList;
#define T struct Tnode *
#define ARR NodeList
#include "generic/dyn_array.inc"

typedef struct Tnode {
  GCData gc_data;
  AST_T type;
  size_t line;
  bool assignable; // Whether the expr can be assigned to.
  union {
    Value constant;
    StrSlice ident;

    struct Tnode *expr;
    NodeList list;

    Op compound_assign_op;

    struct {
      struct Tnode *head;
      struct Tnode *body;
    } construct;

    struct {
      StrSlice ident;
      struct Tnode *in;
      struct Tnode *body;
    } for_loop;

    struct {
      int breaks;
      bool continues;
    } flow;
  };
  // https://en.wikipedia.org/wiki/Flexible_array_member
  struct Tnode *operands[];
} Tnode;

Tnode *treenode_new(AST_T type, size_t line);
Tnode *treenode_constant(Value val, size_t line);
Tnode *treenode_op(AST_T type, size_t line,
    size_t n, Tnode *operands[n]);
Tnode *treenode_list(AST_T type, size_t line, NodeList list);

#endif
