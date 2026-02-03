#ifndef LANG_ASTREE
#define LANG_ASTREE

#include "generic/dyn_array_header.h"
#include "ir.h"
#include "util.h"
#include "val.h"

typedef enum {
  AST_NONE,
  AST_UNOP,
  AST_BINOP,
  AST_CONJUNCT_CMP,
  AST_CONSTANT,
  AST_METASTRING,
  AST_IDENT,
  AST_GROUPING,
  AST_LIST,
  AST_BLOCK,
  AST_CALL,
  AST_SUBSCRIPT,
  AST_IF,
  AST_ELSE,
  AST_LOOP,
  AST_FOR,
  AST_WHILE,
  AST_FLOW_CONTROL,
  AST_LET,
  AST_MAPLET,
  AST_ASSIGN,
} AST_T;

typedef DYN_ARRAY_STRUCT(struct TNode *) NodeList;
#define T struct TNode *
#define ARR NodeList
#include "generic/dyn_array.h"

typedef struct TNode {
  AST_T type;
  size_t line;
  Op op_type;
  union {
    Value constant;
    StrSlice ident;

    struct TNode *expr;
    NodeList list;

    // https://en.wikipedia.org/wiki/Flexible_array_member
    struct TNode *operands[];

    struct {
      struct TNode *invokee;
      NodeList list;
    } invocation;

    struct {
      struct TNode *head;
      struct TNode *body;
    } ctrl_construct;

    struct {
      StrSlice ident;
      struct TNode *in;
      struct TNode *body;
    } for_loop;

    struct {
      int breaks;
      bool continues;
    } flow_ctrl;
  };
} TNode;

TNode *treenode_new(AST_T type, size_t line);

TNode *treenode_constant(Value val, size_t line);

TNode *treenode_op(AST_T type, size_t line,
    size_t n, TNode *operands[n]);
TNode *treenode_op_t(AST_T type, Op op_type, size_t line,
    size_t n, TNode *operands[n]);

TNode *treenode_list(AST_T type, size_t line, NodeList list);

// Prints a node like an s-expression.
void treenode_print(TNode *node);

#endif
