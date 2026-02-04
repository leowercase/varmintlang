#ifndef LANG_ASTREE
#define LANG_ASTREE

#include "generic/dyn_array.h"
#include "pcode.h"
#include "op.h"
#include "util.h"
#include "val.h"

/*
 * An abstract syntax tree is a representation of code's structure.
 * https://en.wikipedia.org/wiki/Abstract_syntax_tree
 */

typedef enum {
  // OP_NOT, ...
  // OP_ADD, ...
  AST_NONE = NATIVE_OPERATOR_COUNT,
  AST_ASSIGN,
  AST_COMPOUND_ASSIGN,
  AST_MAPLET,
  AST_CONSTANT,
  AST_METASTRING,
  AST_GROUPING,
  AST_CALL,
  AST_SUBSCRIPT,
  AST_LIST,
  AST_IDENT,
  AST_BLOCK,
  AST_IF, AST_ELSE,
  AST_LOOP, AST_FOR, AST_WHILE,
  AST_FLOW_CONTROL,
  AST_LET,
} AST_T;

typedef DYN_ARRAY_STRUCT(struct TNode *) NodeList;
#define T struct TNode *
#define ARR NodeList
#include "generic/dyn_array.inc"

typedef struct TNode {
  AST_T type;
  size_t line;
  union {
    Value constant;
    StrSlice ident;

    struct TNode *expr;
    NodeList list;

    Op compound_assign_op;

    struct {
      struct TNode *head;
      struct TNode *body;
    } construct;

    struct {
      StrSlice ident;
      struct TNode *in;
      struct TNode *body;
    } for_loop;

    struct {
      int breaks;
      bool continues;
    } flow;
  };
  // https://en.wikipedia.org/wiki/Flexible_array_member
  struct TNode *operands[];
} TNode;

TNode *treenode_new(AST_T type, size_t line);

TNode *treenode_constant(Value val, size_t line);

TNode *treenode_op(AST_T type, size_t line,
    size_t n, TNode *operands[n]);

TNode *treenode_list(AST_T type, size_t line, NodeList list);

#endif
