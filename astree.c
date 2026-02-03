#include "astree.h"
#include <stdio.h>

// Treenode, plus padding (for flexible array member)
static TNode *treenode_new_pad(AST_T type, size_t line, size_t padding)
{
  TNode *node = malloc(sizeof(TNode) + padding);
  if (node == NULL)
    exit(EX_OSERR);

  node->type = type;
  node->line = line;

  return node;
}

TNode *treenode_new(AST_T type, size_t line)
{
  return treenode_new_pad(type, line, 0L);
}

TNode *treenode_constant(Value val, size_t line)
{
  TNode *val_node = treenode_new(AST_CONSTANT, line);
  val_node->constant = val;
  return val_node;
}

TNode *treenode_op(AST_T type, size_t line,
    size_t n, TNode *operands[n])
{
  TNode *op_node = treenode_new_pad(type, line, sizeof(TNode *) * n);
  memcpy(op_node->operands, operands, sizeof(TNode *) * n);
  return op_node;
}

TNode *treenode_list(AST_T type, size_t line, NodeList list)
{
  TNode *node = treenode_new(type, line);
  node->list = list;
  return node;
}

