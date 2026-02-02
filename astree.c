#include "astree.h"
#include <stdio.h>

// My job? Plates 'n' boilers, boilers 'n' plates.
// Plus that one boiler plate.

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

TNode *treenode_op(AST_T type, Op op_type, size_t line,
    size_t n, TNode *operands[n])
{
  TNode *node = treenode_new_pad(type, line, sizeof(TNode *) * n);
  node->op.op_type = op_type;

  memcpy(node->op.operands, operands, sizeof(TNode *) * n);
  return node;
}

TNode *treenode_list(AST_T type, size_t line, NodeList list)
{
  TNode *node = treenode_new(type, line);
  node->list = list;
  return node;
}

// Alas,
// this file here is mostly just boring switch statements from now on.

static const char *unop_cstring(UnOp operator)
{
  switch (operator) {
  case OP_NOT: return "not";
  case OP_POSITE: return "+";
  case OP_NEGATE: return "-";
  case OP_FACTORIAL: return "!";
  case OP_PERCENTAGE: return "%";
  }
}

static const char *binop_cstring(BinOp operator)
{
  switch (operator) {
  case OP_ADD: return "+";
  case OP_SUB: return "-";
  case OP_MUL: return "*";
  case OP_DIV: return "/";
  case OP_POW: return "^";
  case OP_MODULO: return "mod";
  case OP_AND: return "and";
  case OP_OR: return "or";
  case OP_I9N: return "->";
  case OP_EQ: return "=";
  case OP_NEQ: return "≠";
  case OP_LT: return "<";
  case OP_GT: return ">";
  case OP_LEQ: return "⩽";
  case OP_GEQ: return "⩾";
  case OP_CONCAT: return "||";
  }
}

static void print_list(
    const char *begin, const char *delim, const char *end,
    TNode *list[], size_t list_len)
{
  printf("%s", begin);

  treenode_print(list[0]);

  for (size_t i = 1; i < list_len; i++) {
    printf("%s", delim);
    treenode_print(list[i]);
  }

  printf("%s", end);
}

static void print_op(const char *op, TNode *operands[], size_t operand_count)
{
  printf("(" ANSI_BLUE "%s" ANSI_RESET, op);
  print_list(" ", " ", ")", operands, operand_count);
}

void treenode_print(TNode *node)
{
  switch (node->type) {
  case AST_NONE:
    printf("()");
    break;
  case AST_CONSTANT:
    print_value(node->constant);
    break;
  case AST_IDENT:
    printf("%.*s", (int)node->ident.len, node->ident.s);
    break;
  case AST_GROUPING:
    printf("(");
    treenode_print(node->expr);
    printf(")");
    break;
  case AST_UNOP:
    print_op(unop_cstring((UnOp)node->op.op_type), node->op.operands, 1);
    break;
  case AST_BINOP:
    print_op(binop_cstring((BinOp)node->op.op_type), node->op.operands, 2);
    break;
  case AST_CONJUNCT_CMP:
    printf("(" ANSI_RED "%s" ANSI_YELLOW "∧" ANSI_RESET,
        binop_cstring((BinOp)node->op.op_type));
    print_list(" ", " ", ")", node->op.operands, 2);
    break;
  case AST_LIST:
    print_list("[", ", ", "]", node->list.data, node->list.len);
    break;
  case AST_SUBSCRIPT:
    print_op("[]", node->op.operands, 2);
    break;
  case AST_BLOCK:
    print_list("{", "; ", "}", node->list.data, node->list.len);
    break;
  case AST_LET:
    printf("(" ANSI_YELLOW "let" ANSI_RESET " %.*s)",
        (int)node->ident.len, node->ident.s);
    break;
  case AST_ASSIGN:
    print_op(":=", node->op.operands, 2);
    break;
  case AST_METASTRING:
    print_op("\"\"", node->list.data, node->list.len);
    break;
  }
}

void treenode_free(TNode *node)
{
  switch (node->type) {
  case AST_NONE:
  case AST_CONSTANT:
  case AST_IDENT:
  case AST_LET:
    break;
  case AST_GROUPING:
    treenode_free(node->expr);
    break;
  case AST_UNOP:
    treenode_free(node->op.operands[0]);
    break;
  case AST_BINOP:
  case AST_CONJUNCT_CMP:
  case AST_SUBSCRIPT:
  case AST_ASSIGN:
    treenode_free(node->op.operands[0]);
    treenode_free(node->op.operands[1]);
    break;
  case AST_LIST:
  case AST_BLOCK:
  case AST_METASTRING:
    for (size_t i = 0; i < node->list.len; i++)
      treenode_free(node->list.data[i]);
    free(node->list.data);
    break;
  }

  free(node);
}

