#include "lex.h"
#include "parse.h"
#include "val.h"

static inline Token peek(Parse *p)
{
  return p->lookahead;
}

static inline Token next(Parse *p)
{
  Token next_tok = p->lookahead;
  p->current = next_tok;
  p->lookahead = lex_token(&p->lex);
  return next_tok;
}

static inline Token eat(Parse *p)
{
  Token tok = p->current;
  next(p);
  return tok;
}

static bool match(Parse *p, TokenType expected)
{
  Token tok = p->current;
  if (tok.type != TK_EOF && tok.type == expected) {
    next(p);
    return true;
  }
  else return false;
}

static Token consume(Parse *p, TokenType expected)
{
  Token tok = p->current;
  if (tok.type != TK_EOF && tok.type == expected) {
    next(p);
    return tok;
  }
  else invalid_token(tok);

  unreachable();
}

static const ParseRule parse_rules[] =
  {
/*  token type         NUD         LED        */
    [TK_EOF]       = { NULL,       no_op      },
    [TK_ERR]       = { NULL,       NULL       },

    [TK_PLUS]      = { prefix_op,  infix_op   },
    [TK_MINUS]     = { prefix_op,  infix_op   },
    [TK_STAR]      = { NULL,       infix_op   },
    [TK_SLASH]     = { NULL,       infix_op   },
    [TK_CARET]     = { NULL,       infix_op   },
    [TK_PERCENT]   = { NULL,       led_op     },
    [TK_BANG]      = { NULL,       postfix_op },
    [TK_2PIPE]     = { NULL,       infix_op   },

    [TK_EQ]        = { NULL,       cmp_op     },
    [TK_NEQ]       = { NULL,       cmp_op     },
    [TK_LT]        = { NULL,       cmp_op     },
    [TK_GT]        = { NULL,       cmp_op     },
    [TK_LEQ]       = { NULL,       cmp_op     },
    [TK_GEQ]       = { NULL,       cmp_op     },

    [TK_ASSIGN]    = { NULL,       assign     },
    [TK_2PLUS]     = { NULL,       NULL       },
    [TK_2MINUS]    = { NULL,       NULL       },

    [TK_LET]       = { let,        NULL       },

    [TK_NOT]       = { prefix_op,  NULL       },
    [TK_AND]       = { NULL,       infix_op   },
    [TK_OR]        = { NULL,       infix_op   },
    [TK_IN]        = { NULL,       infix_op   },
    [TK_NOTIN]     = { NULL,       infix_op   },

    [TK_MOD]       = { NULL,       infix_op   },

    [TK_IF]        = { cond,       NULL       },
    [TK_ELSE]      = { NULL,       else_elif  },
    [TK_ELIF]      = { NULL,       else_elif  },

    [TK_LOOP]      = { loop,       NULL       },
    [TK_FOR]       = { for_loop,   NULL       },
    [TK_WHILE]     = { cond,       NULL       },

    [TK_BREAK]     = { loop_break, NULL       },
    [TK_CONTINUE]  = { loop_cont,  NULL       },

    [TK_TRUE]      = { boolean,    NULL       },
    [TK_FALSE]     = { boolean,    NULL       },

    [TK_ARROW]     = { NULL,       infix_op   },
    [TK_MAPS_TO]   = { NULL,       maplet     },

    [TK_LPAREN]    = { grouping,   invocation },
    [TK_RPAREN]    = { NULL,       no_op      },

    [TK_LCURLY]    = { block,      NULL       },
    [TK_RCURLY]    = { NULL,       no_op      },

    [TK_LBRACK]    = { list,       invocation },
    [TK_RBRACK]    = { NULL,       no_op      },

    [TK_COLON]     = { NULL,       no_op      },
    [TK_SEMICOLON] = { NULL,       no_op      },
    [TK_COMMA]     = { NULL,       no_op      },

    [TK_NUMERAL]   = { number,     NULL       },

    [TK_STRCONT]   = { metastring, no_op      },
    [TK_STREND]    = { string,     no_op      },

    [TK_WORD]      = { ident,      NULL       },
  };

static inline
const ParseRule *parse_rule(TokenType type)
{
  return &parse_rules[type];
}

typedef struct {
  UnOp type;
  Precedence precedence;
} UnaryOp;

typedef struct {
  BinOp type;
  Precedence precedence;
  Associativity associativity;
} BinaryOp;

typedef struct {
  AST_T ast_type;
  TokenType right_pair;
  bool allow_trailing_delim;
} InvocationOp;

static const UnaryOp prefix_ops[] = {
  [TK_PLUS]  = { OP_POSITE, PREC_SIGN },
  [TK_MINUS] = { OP_NEGATE, PREC_SIGN },
  [TK_NOT] =   { OP_NOT,    PREC_NOT  },
};

TNode *prefix_op(Parse *p)
{
  Token op_token = eat(p);
  UnaryOp op = prefix_ops[op_token.type];

  // Parse right operand.
  int r_bp = (int)op.precedence;
  TNode *rhs = expr(p, r_bp);

  return treenode_op_t(AST_UNOP, op.type, op_token.line, 1, &rhs);
}

static TNode *assignage(Parse *p, TNode *lhs, int min_bp, Op op_shorthand)
{
  if (PREC_ASSIGN < min_bp)
    return NULL;

  size_t line = eat(p).line; // op
  // Allows assignment shorthand op:=
  if (op_shorthand != OP_NONE)
    next(p); // :=

  TNode *operands[2];
  operands[0] = lhs;
  const int r_bp = (int)PREC_ASSIGN + (int)ASSOC_RIGHT;
  operands[1] = expr(p, r_bp);

  return treenode_op_t(AST_ASSIGN, op_shorthand, line, 2, operands);
}

static const BinaryOp infix_ops[] = {
  [TK_PLUS]    = { OP_ADD,    PREC_TERM,   ASSOC_LEFT  },
  [TK_MINUS]   = { OP_SUB,    PREC_TERM,   ASSOC_LEFT  },
  [TK_STAR]    = { OP_MUL,    PREC_FACTOR, ASSOC_LEFT  },
  [TK_SLASH]   = { OP_MUL,    PREC_FACTOR, ASSOC_LEFT  },
  [TK_CARET]   = { OP_POW,    PREC_POWER,  ASSOC_RIGHT },
  [TK_PERCENT] = { OP_MODULO, PREC_FACTOR, ASSOC_LEFT  },
  [TK_2PIPE]   = { OP_CONCAT, PREC_CONCAT, ASSOC_LEFT  },
  [TK_AND]     = { OP_AND,    PREC_AND,    ASSOC_LEFT  },
  [TK_OR]      = { OP_OR,     PREC_OR,     ASSOC_LEFT  },
  [TK_IN]      = { OP_IN,     PREC_IN,     ASSOC_LEFT  },
  [TK_NOTIN]   = { OP_NOTIN,  PREC_IN,     ASSOC_LEFT  },
  [TK_MOD]     = { OP_MODULO, PREC_FACTOR, ASSOC_LEFT  },
  [TK_ARROW]   = { OP_I9N,    PREC_I9N,    ASSOC_LEFT  },
};

TNode *infix_op(Parse *p, TNode *lhs, int min_bp)
{
  Token op_token = p->current;
  BinaryOp op = infix_ops[op_token.type];

  // Assignment shorthand
  if (peek(p).type == TK_ASSIGN)
    return assignage(p, lhs, min_bp, op.type);

  int l_bp = (int)op.precedence;
  if (l_bp < min_bp)
    return NULL;

  next(p); // Consume op_token

  // Parse right operand.
  int r_bp = (int)l_bp + (int)op.associativity;
  TNode *rhs = expr(p, r_bp);

  TNode *operands[2] = {lhs, rhs};
  return treenode_op_t(AST_BINOP, op.type, op_token.line, 2, operands);
}

static inline bool is_prefix_and_infix(TokenType op)
{
  // Can be extended later.
  return op == TK_PLUS || op == TK_MINUS;
}

static const UnaryOp postfix_ops[] = {
  [TK_PERCENT] = { OP_PERCENTAGE, PREC_PERCENT   },
  [TK_BANG]    = { OP_FACTORIAL,  PREC_FACTORIAL },
};

TNode *postfix_op(Parse *p, TNode *lhs, int min_bp)
{
  Token op_token = p->current;
  UnaryOp op = postfix_ops[op_token.type];

  int l_bp = (int)op.precedence;
  if (l_bp < min_bp)
    return NULL;

  next(p); // Consume op_token

  return treenode_op_t(AST_UNOP, op.type, op_token.line, 1, &lhs);
}

static bool led_op_is_infix(TokenType op, TokenType next)
{
  const ParseRule *next_rule = parse_rule(next);

  if (next_rule->nud == NULL)
    return false; // Next token is not a valid rhs.

  else if (next_rule->led == NULL || !is_prefix_and_infix(next))
    return true; // Next token is not an infix op.

  else {
    int op_bp = (int)infix_ops[op].precedence +
                (int)infix_ops[op].associativity;

    int next_bp = (int)infix_ops[next].precedence +
                  (int)infix_ops[next].associativity;

    return op_bp < next_bp; // Battle of the binding powers!
  }
}

// LED op tokens of ambiguous fixity.
// 50% + 3
TNode *led_op(Parse *p, TNode *lhs, int min_bp)
{
  if (led_op_is_infix(p->current.type, peek(p).type))
    return infix_op(p, lhs, min_bp);
  else
    return postfix_op(p, lhs, min_bp);
}

static const BinOp cmp_types[] = {
  [TK_EQ]  = OP_EQ,
  [TK_NEQ] = OP_NEQ,
  [TK_LT]  = OP_LT,
  [TK_LEQ] = OP_LEQ,
  [TK_GT]  = OP_GT,
  [TK_GEQ] = OP_GEQ,
};

static TNode *cmp_op_chain(Parse *p, TNode *lhs)
{
  if (is_cmp_token(p->current.type)) {
    Token op_token = eat(p);
    Op op_type = (Op)cmp_types[op_token.type];

    TNode *cmp = treenode_op_t(
        AST_CONJUNCT_CMP, op_type, op_token.line, 1, &lhs);
    return cmp_op_chain(p, cmp);
  }
  else {
    const int r_bp = (int)PREC_CMP + (int)ASSOC_LEFT;
    return expr(p, r_bp);
  }
}

// Comparison operators that can be chained.
// a < b <= c != 0
TNode *cmp_op(Parse *p, TNode *lhs, int min_bp)
{
  if (PREC_CMP < min_bp)
    return NULL;

  // Assignment shorthand.
  if (peek(p).type == TK_ASSIGN)
    return assignage(p, lhs, min_bp, cmp_types[p->current.type]);

  return cmp_op_chain(p, lhs);
}

static TNode *delimited_listing(Parse *p,
    AST_T type,
    const TokenType start, const TokenType delim, const TokenType end,
    bool allow_trailing_delim)
{
  Token start_tok = consume(p, start); // [
  TNode *listing = treenode_list(type, start_tok.line, NodeList_new());

  // Consume listing elements
  do {
    if (match(p, end)) { // ]
      if (listing->list.len == 0 || allow_trailing_delim)
        return listing;
      else
        runtime_error("Invalid trailing %s in listing\n", tok_cstring(delim));
    }

    NodeList_push(&listing->list, expr(p, PREC_NONE));

    if (match(p, end)) // ]
      return listing;
  } while (match(p, delim)); // ,

  invalid_token(p->current);
  unreachable();
}

static const InvocationOp invoke_ops[] = {
  [TK_LPAREN] = { AST_CALL,      TK_RPAREN, false },
  [TK_LBRACK] = { AST_SUBSCRIPT, TK_RBRACK, true  },
};

TNode *invocation(Parse *p, TNode *lhs, int min_bp)
{
  if (PREC_CALL < min_bp)
    return NULL;

  Token left_pair = p->current;
  InvocationOp op = invoke_ops[left_pair.type];

  TNode *listing = delimited_listing(p, AST_LIST,
      left_pair.type, TK_COMMA, op.right_pair,
      op.allow_trailing_delim);

  TNode *operands[2] = {lhs, listing};
  return treenode_op(op.ast_type, left_pair.line, 2, operands);
}

TNode *grouping(Parse *p)
{
  return delimited_listing(p, AST_GROUPING,
      TK_LPAREN, TK_NEVER, TK_RPAREN, false);
}

TNode *stmt(Parse *p)
{
  TokenType tok = p->current.type;

  if (parse_rule(tok)->nud == NULL)
    return NULL;
  else
    return expr(p, PREC_NONE);
}

// A block is a series of statements.
TNode *block(Parse *p)
{
  size_t line = eat(p).line; // {

  // Consume first statement
  TNode *first_stmt = stmt(p);

  if (!first_stmt)
    runtime_error("illegal empty block, expect statement");

  NodeList stmts = NodeList_with_cap(1);
  NodeList_push(&stmts, first_stmt);

  // Consume statements ...;
  while (match(p, TK_SEMICOLON)) {
    if (p->current.type == TK_RCURLY) {
      // Trailing semicolon, no value from block expr.
      NodeList_push(&stmts, treenode_new(AST_NONE, p->current.line));
      break;
    }

    TNode *s = stmt(p);
    if (s == NULL)
      break;

    NodeList_push(&stmts, s);
  }

  consume(p, TK_RCURLY); // }
  return treenode_list(AST_BLOCK, line, stmts);
}

TNode *list(Parse *p)
{
  return delimited_listing(p, AST_LIST,
      TK_LBRACK, TK_COMMA, TK_RBRACK, true);
}

static TNode *ctrl_construct_body(Parse *p)
{
  consume(p, TK_COLON);
  const int r_bp = (int)PREC_BASE + (int)ASSOC_RIGHT;
  return expr(p, r_bp);
}

static TNode *condition(Parse *p, AST_T cond_type, size_t line)
{
  TNode *cond_node = treenode_new(cond_type, line);

  const int r_bp = (int)PREC_BASE + (int)ASSOC_RIGHT;
  cond_node->ctrl_construct.head = expr(p, r_bp);
  cond_node->ctrl_construct.body = ctrl_construct_body(p);

  return cond_node;
}

static const AST_T cond_types[] = {
  [TK_IF]    = AST_IF,
  [TK_WHILE] = AST_WHILE,
};

// if while.
// These are in essence mixfix operators terminated by :
TNode *cond(Parse *p)
{
  Token cond_tok = eat(p);
  return condition(p, cond_types[cond_tok.type], cond_tok.line);
}

// elif is likewise mixfix, else on the other hand binary.
TNode *else_elif(Parse *p, TNode *lhs, int min_bp)
{
  if (PREC_BASE < min_bp)
    return NULL;

  Token tok = eat(p);
  TNode *operands[2];
  operands[0] = lhs;
  operands[1] = tok.type == TK_ELIF ?
    condition(p, AST_IF, tok.line) : ctrl_construct_body(p);

  return treenode_op(AST_ELSE, tok.line, 2, operands);
}

TNode *loop(Parse *p)
{
  TNode *node = treenode_new(AST_LOOP, eat(p).line);
  node->ctrl_construct.body = ctrl_construct_body(p);
  return node;
}

TNode *for_loop(Parse *p)
{
  Token for_tok = eat(p),
        ident_tok = consume(p, TK_WORD);
  consume(p, TK_IN);

  TNode *node = treenode_new(AST_FOR, for_tok.line);
  node->for_loop.ident = ident_tok.slice;

  const int r_bp = (int)PREC_BASE + (int)ASSOC_RIGHT;
  node->for_loop.in = expr(p, r_bp);
  node->for_loop.body = ctrl_construct_body(p);
  return node;
}

// break [... break] [continue]
TNode *loop_break(Parse *p)
{
  int breaks;
  for (breaks = 0; match(p, TK_BREAK); breaks++);
  bool continues = match(p, TK_CONTINUE);

  TNode *node = treenode_new(AST_FLOW_CONTROL, eat(p).line);
  node->flow_ctrl.breaks = breaks;
  node->flow_ctrl.continues = continues;
  return node;
}

// continue
TNode *loop_cont(Parse *p)
{
  TNode *node = treenode_new(AST_FLOW_CONTROL, eat(p).line);
  node->flow_ctrl.breaks = 0;
  node->flow_ctrl.continues = true;
  return node;
}

TNode *boolean(Parse *p)
{
  Token booltok = eat(p);
  int P = booltok.type == TK_TRUE;
  return treenode_constant(value_new(P, boolean), booltok.line);
}

TNode *number(Parse *p)
{
  Token ntok = eat(p);

  // Copying the slice to NUL-terminated so strtod doesn't parse anything extra
  Str nstr = str_from_slice(ntok.slice);
  float64_t n = strtod(nstr.s, NULL);
  free((void *)nstr.s);

  return treenode_constant(value_new(n, number), ntok.line);
}

TNode *metastring(Parse *p)
{
  NodeList metas = NodeList_new();
  size_t line = p->current.line;

  for (bool found_end = false; !found_end;) {
    switch (p->current.type) {
    case TK_STREND:
      found_end = true;
    case TK_STRCONT:
      if (p->current.slice.len == 0)
        next(p); // Skip empty string tokens.
      else {
        TNode *s = string(p);
        NodeList_push(&metas, s);
      }
      break;
    default:
      {
        TNode *interpd_expr = expr(p, PREC_NONE); // \(...)
        NodeList_push(&metas, interpd_expr);
      }
    }
  }

  return treenode_list(AST_METASTRING, line, metas);
}

TNode *string(Parse *p)
{
  Token strtok = eat(p);
  Str str = str_from_slice(strtok.slice);
  return treenode_constant(string_value_new(str), strtok.line);
}

// x
TNode *ident(Parse *p)
{
  Token ident_tok = eat(p);
  TNode *ident_node = treenode_new(AST_IDENT, ident_tok.line);
  ident_node->ident = ident_tok.slice;
  return ident_node;
}

TNode *assign(Parse *p, TNode *lhs, int min_bp)
{
  return assignage(p, lhs, min_bp, OP_NONE);
}

TNode *let(Parse *p)
{
  next(p); // let
  Token ident_tok = consume(p, TK_WORD);

  TNode *node = treenode_new(AST_LET, ident_tok.line);
  node->ident = ident_tok.slice;
  return node;
}

TNode *maplet(Parse *p, TNode *lhs, int min_bp)
{
  size_t line = eat(p).line;
  TNode *operands[2];
  operands[0] = lhs;
  operands[1] = expr(p, (int)PREC_MAPLET + (int)ASSOC_RIGHT);
  return treenode_op(AST_MAPLET, line, 2, operands);
}

// An impl of Pratt parsing.
// Handles prefix, infix, postfix and mixfix expressions
TNode *expr(Parse *p, int min_bp)
{
  Token lhs_token = p->current;
  NudRule lhs_rule = parse_rule(lhs_token.type)->nud;

  if (lhs_rule == NULL) {
    error_out("Expecting expression.\n");
    invalid_token(lhs_token);
  }

  TNode *lhs = lhs_rule(p);

  for (;;) {
    Token op_token = p->current;
    LedRule op_rule = parse_rule(op_token.type)->led;

    if (op_rule == NULL) {
      error_out("Expecting operator.\n");
      invalid_token(op_token);
    }

    TNode *op_result = op_rule(p, lhs, min_bp);
    if (op_result == NULL)
      break; // Precedence is too small or op otherwise cannot be used as a LED

    lhs = op_result;
  }

  return lhs;
}
