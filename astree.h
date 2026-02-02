#ifndef LANG_ASTREE
#define LANG_ASTREE

#include "generic/dyn_array_header.h"
#include "ir.h"
#include "util.h"
#include "val.h"

typedef enum {
  AST_NONE,
  AST_UNOP  = 1,
  AST_BINOP = 2,
  AST_CONSTANT,
  AST_METASTRING,
  AST_IDENT,
  AST_GROUPING,
  AST_CONJUNCT_CMP,
  AST_LIST,
  AST_SUBSCRIPT,
  AST_BLOCK,
  AST_LET,
  AST_ASSIGN,
} AST_T;

typedef struct {
  Op op_type;
  // https://en.wikipedia.org/wiki/Flexible_array_member
  struct TNode *operands[];
} NodeOp;

typedef struct {
  DYN_ARRAY(struct TNode *)
} NodeList;

#define T struct TNode *
#define ARR NodeList
#include "generic/dyn_array.h"

typedef struct TNode {
  AST_T type;
  size_t line;
  union {
    Value constant;
    StrSlice ident;
    struct TNode *expr;
    NodeOp op;
    NodeList list;
  };
} TNode;

TNode *treenode_new(AST_T type, size_t line);

TNode *treenode_constant(Value val, size_t line);

TNode *treenode_op(AST_T type, Op op_type, size_t line,
    size_t n, TNode *operands[n]);

TNode *treenode_list(AST_T type, size_t line, NodeList list);

// Prints a node like an s-expression.
void treenode_print(TNode *node);

void treenode_free(TNode *node);

#endif
