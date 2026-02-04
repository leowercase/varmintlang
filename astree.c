#include "astree.h"
#include <stdio.h>

// Treenode, plus padding (for flexible array member)
static Tnode *treenode_new_pad(AST_T type, size_t line, size_t padding)
{
  Tnode *node = malloc(sizeof(Tnode) + padding);
  if (node == NULL)
    exit(EX_OSERR);

  node->type = type;
  node->line = line;
  node->assignable = false; // By default.

  return node;
}

Tnode *treenode_new(AST_T type, size_t line)
{
  return treenode_new_pad(type, line, 0L);
}

Tnode *treenode_op(AST_T type, size_t line,
    size_t n, Tnode *operands[n])
{
  Tnode *op_node = treenode_new_pad(type, line, sizeof(Tnode *) * n);
  memcpy(op_node->operands, operands, sizeof(Tnode *) * n);
  return op_node;
}

Tnode *treenode_constant(Value val, size_t line)
{
  Tnode *val_node = treenode_new(AST_CONSTANT, line);
  val_node->constant = val;
  return val_node;
}

Tnode *treenode_list(AST_T type, size_t line, NodeList list)
{
  Tnode *node = treenode_new(type, line);
  node->list = list;
  return node;
}

