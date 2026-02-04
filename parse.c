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
    [TK_EQ]        = { NULL,       infix_op   },
    [TK_NEQ]       = { NULL,       infix_op   },
    [TK_LT]        = { NULL,       infix_op   },
    [TK_GT]        = { NULL,       infix_op   },
    [TK_LEQ]       = { NULL,       infix_op   },
    [TK_GEQ]       = { NULL,       infix_op   },

    [TK_ASSIGN]    = { NULL,       assign     },
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
    [TK_MAPS_TO]   = { NULL,       infix_op   },

    [TK_LPAREN]    = { grouping,   invocation },
    [TK_RPAREN]    = { NULL,       no_op      },

    [TK_LCURLY]    = { block,      NULL       },
    [TK_RCURLY]    = { NULL,       no_op      },

    [TK_LBRACK]    = { list,       subscript  },
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
  AST_T type;
  Precedence precedence;
} UnaryOp;

typedef struct {
  AST_T type;
  Precedence precedence;
  Associativity associativity;
} BinaryOp;

static const UnaryOp prefix_ops[] = {
  [TK_MINUS] = { (Op)OP_NEGATE, PREC_SIGN },
  [TK_NOT] =   { (Op)OP_NOT,    PREC_NOT  },
};

Tnode *prefix_op(Parse *p)
{
  Token op_token = eat(p);
  UnaryOp op = prefix_ops[op_token.type];

  // Parse right operand.
  int r_bp = (int)op.precedence;
  Tnode *rhs = expr(p, r_bp);

  return treenode_op(op.type, op_token.line, 1, &rhs);
}

// Allows assignment shorthand +:=
static Tnode *assignage(Parse *p, Tnode *lhs, int min_bp, Op op_shorthand)
{
  if (PREC_ASSIGN < min_bp)
    return NULL;

  if (!lhs->assignable)
    runtime_error("lhs is not assignable\n");

  size_t line = eat(p).line; // op
  AST_T type;
  if (op_shorthand != OP_NONE) {
    next(p); // :=
    type = AST_COMPOUND_ASSIGN;
  }
  else type = AST_ASSIGN;

  Tnode *operands[2];
  operands[0] = lhs;
  const int r_bp = (int)PREC_ASSIGN + (int)ASSOC_RIGHT;
  operands[1] = expr(p, r_bp);

  Tnode *node = treenode_op(type, line, 2, operands);
  node->compound_assign_op = op_shorthand;
  return node;
}

Tnode *assign(Parse *p, Tnode *lhs, int min_bp)
{
  return assignage(p, lhs, min_bp, OP_NONE);
}

static const BinaryOp infix_ops[] = {
  [TK_PLUS]    = { (Op)OP_ADD,     PREC_TERM,   ASSOC_LEFT  },
  [TK_MINUS]   = { (Op)OP_SUB,     PREC_TERM,   ASSOC_LEFT  },
  [TK_STAR]    = { (Op)OP_MUL,     PREC_FACTOR, ASSOC_LEFT  },
  [TK_SLASH]   = { (Op)OP_MUL,     PREC_FACTOR, ASSOC_LEFT  },
  [TK_CARET]   = { (Op)OP_POW,     PREC_POWER,  ASSOC_RIGHT },
  [TK_PERCENT] = { (Op)OP_MODULO,  PREC_FACTOR, ASSOC_LEFT  },
  [TK_2PIPE]   = { (Op)OP_CONCAT,  PREC_CONCAT, ASSOC_LEFT  },

  [TK_EQ]      = { (Op)OP_EQ,      PREC_CMP,    ASSOC_LEFT  },
  [TK_NEQ]     = { (Op)OP_NEQ,     PREC_CMP,    ASSOC_LEFT  },
  [TK_LT]      = { (Op)OP_LT,      PREC_CMP,    ASSOC_LEFT  },
  [TK_LEQ]     = { (Op)OP_LEQ,     PREC_CMP,    ASSOC_LEFT  },
  [TK_GT]      = { (Op)OP_GT,      PREC_CMP,    ASSOC_LEFT  },
  [TK_GEQ]     = { (Op)OP_GEQ,     PREC_CMP,    ASSOC_LEFT  },

  [TK_AND]     = { (Op)OP_AND,     PREC_AND,    ASSOC_LEFT  },
  [TK_OR]      = { (Op)OP_OR,      PREC_OR,     ASSOC_LEFT  },
  [TK_IN]      = { (Op)OP_IN,      PREC_IN,     ASSOC_LEFT  },
  [TK_NOTIN]   = { (Op)OP_NOTIN,   PREC_IN,     ASSOC_LEFT  },
  [TK_MOD]     = { (Op)OP_MODULO,  PREC_FACTOR, ASSOC_LEFT  },
  [TK_ARROW]   = { (Op)OP_I9N,     PREC_I9N,    ASSOC_LEFT  },

  [TK_MAPS_TO] = { AST_MAPLET,     PREC_MAPLET, ASSOC_RIGHT },
};

Tnode *infix_op(Parse *p, Tnode *lhs, int min_bp)
{
  Token op_token = p->current;
  BinaryOp op = infix_ops[op_token.type];

  // Assignment shorthand
  if (peek(p).type == TK_ASSIGN) {
    if (!is_binary_op((Op)op.type))
      invalid_token(op_token);
    return assignage(p, lhs, min_bp, (Op)op.type);
  }

  int l_bp = (int)op.precedence;
  if (l_bp < min_bp)
    return NULL;

  next(p); // Consume op_token

  // Parse right operand.
  int r_bp = (int)l_bp + (int)op.associativity;
  Tnode *rhs = expr(p, r_bp);

  Tnode *operands[2] = {lhs, rhs};
  return treenode_op(op.type, op_token.line, 2, operands);
}

static inline bool is_prefix_and_infix(TokenType op)
{
  // Can be extended later.
  return op == TK_PLUS || op == TK_MINUS;
}

static const UnaryOp postfix_ops[] = {
  [TK_PERCENT] = { (Op)OP_PERCENTAGE, PREC_PERCENT   },
  [TK_BANG]    = { (Op)OP_FACTORIAL,  PREC_FACTORIAL },
};

Tnode *postfix_op(Parse *p, Tnode *lhs, int min_bp)
{
  Token op_token = p->current;
  UnaryOp op = postfix_ops[op_token.type];

  int l_bp = (int)op.precedence;
  if (l_bp < min_bp)
    return NULL;

  next(p); // Consume op_token
  return treenode_op(op.type, op_token.line, 1, &lhs);
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
Tnode *led_op(Parse *p, Tnode *lhs, int min_bp)
{
  if (led_op_is_infix(p->current.type, peek(p).type))
    return infix_op(p, lhs, min_bp);
  else
    return postfix_op(p, lhs, min_bp);
}

static Tnode *delimited_listing(Parse *p,
    AST_T type,
    const TokenType start, const TokenType delim, const TokenType end,
    bool allow_trailing_delim)
{
  Token start_tok = consume(p, start); // [
  Tnode *listing = treenode_list(type, start_tok.line, NodeList_new());

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

// list[i]
Tnode *subscript(Parse *p, Tnode *lhs, int min_bp)
{
  if (PREC_CALL < min_bp)
    return NULL;
  size_t line = eat(p).line; // [

  Tnode *operands[2];
  operands[0] = lhs;
  operands[1] = expr(p, PREC_NONE);

  consume(p, TK_RBRACK); // ]

  Tnode *op = treenode_op(AST_SUBSCRIPT, line, 2, operands);
  op->assignable = lhs->assignable;
  return op;
}

// (...)
Tnode *grouping(Parse *p)
{
  return delimited_listing(p, AST_GROUPING,
      TK_LPAREN, TK_COMMA, TK_RPAREN, false);
}

// f(...)
Tnode *invocation(Parse *p, Tnode *lhs, int min_bp)
{
  if (PREC_CALL < min_bp)
    return NULL;
  size_t line = p->current.line;

  Tnode *operands[2];
  operands[0] = lhs;
  operands[1] = grouping(p); // Parse argument list.

  return treenode_op(AST_CALL, line, 2, operands);
}

Tnode *stmt(Parse *p)
{
  TokenType tok = p->current.type;

  if (parse_rule(tok)->nud == NULL)
    return NULL;
  else
    return expr(p, PREC_NONE);
}

// A block is a series of statements.
Tnode *block(Parse *p)
{
  size_t line = eat(p).line; // {

  // Consume first statement
  Tnode *first_stmt = stmt(p);

  if (!first_stmt)
    runtime_error("illegal empty block, expect statement");

  NodeList stmts = NodeList_with_cap(1);
  NodeList_push(&stmts, first_stmt);

  AST_T type = AST_BLOCK;
  // Consume statements ...;
  while (match(p, TK_SEMICOLON)) {
    if (p->current.type == TK_RCURLY) {
      // Trailing semicolon, no value from block expr.
      type = AST_CLOSED_BLOCK;
      break;
    }

    Tnode *s = stmt(p);
    if (s == NULL)
      break;

    NodeList_push(&stmts, s);
  }

  consume(p, TK_RCURLY); // }
  return treenode_list(type, line, stmts);
}

// [x, y, z]
Tnode *list(Parse *p)
{
  return delimited_listing(p, AST_LIST,
      TK_LBRACK, TK_COMMA, TK_RBRACK, true);
}

static Tnode *construct_body(Parse *p)
{
  consume(p, TK_COLON);
  const int r_bp = (int)PREC_BASE + (int)ASSOC_RIGHT;
  return expr(p, r_bp);
}

static Tnode *condition(Parse *p, AST_T cond_type, size_t line)
{
  Tnode *cond_node = treenode_new(cond_type, line);

  const int r_bp = (int)PREC_BASE + (int)ASSOC_RIGHT;
  cond_node->construct.head = expr(p, r_bp);
  cond_node->construct.body = construct_body(p);

  return cond_node;
}

static const AST_T cond_types[] = {
  [TK_IF]    = AST_IF,
  [TK_WHILE] = AST_WHILE,
};

// if while.
// These are in essence mixfix operators terminated by :
Tnode *cond(Parse *p)
{
  Token cond_tok = eat(p);
  return condition(p, cond_types[cond_tok.type], cond_tok.line);
}

// elif is likewise mixfix, else on the other hand binary.
Tnode *else_elif(Parse *p, Tnode *lhs, int min_bp)
{
  if (PREC_BASE < min_bp)
    return NULL;

  Token tok = eat(p);
  Tnode *operands[2];
  operands[0] = lhs;
  operands[1] = tok.type == TK_ELIF ?
    condition(p, AST_IF, tok.line) : construct_body(p);

  return treenode_op(AST_ELSE, tok.line, 2, operands);
}

Tnode *loop(Parse *p)
{
  Tnode *node = treenode_new(AST_LOOP, eat(p).line);
  node->construct.body = construct_body(p);
  return node;
}

Tnode *for_loop(Parse *p)
{
  Token for_tok = eat(p),
        ident_tok = consume(p, TK_WORD);
  consume(p, TK_IN);

  Tnode *node = treenode_new(AST_FOR, for_tok.line);
  node->for_loop.ident = ident_tok.slice;

  const int r_bp = (int)PREC_BASE + (int)ASSOC_RIGHT;
  node->for_loop.in = expr(p, r_bp);
  node->for_loop.body = construct_body(p);
  return node;
}

// break [... break] [continue]
Tnode *loop_break(Parse *p)
{
  int breaks;
  for (breaks = 0; match(p, TK_BREAK); breaks++);
  bool continues = match(p, TK_CONTINUE);

  Tnode *node = treenode_new(AST_FLOW_CONTROL, eat(p).line);
  node->flow.breaks = breaks;
  node->flow.continues = continues;
  return node;
}

// continue
Tnode *loop_cont(Parse *p)
{
  Tnode *node = treenode_new(AST_FLOW_CONTROL, eat(p).line);
  node->flow.breaks = 0;
  node->flow.continues = true;
  return node;
}

Tnode *boolean(Parse *p)
{
  Token booltok = eat(p);
  int P = booltok.type == TK_TRUE;
  return treenode_constant(value_new(P, boolean), booltok.line);
}

Tnode *number(Parse *p)
{
  Token ntok = eat(p);

  // Copying the slice to NUL-terminated so strtod doesn't parse anything extra
  Str nstr = str_from_slice(ntok.slice);
  float64_t n = strtod(nstr.s, NULL);
  free((void *)nstr.s);

  return treenode_constant(value_new(n, number), ntok.line);
}

Tnode *metastring(Parse *p)
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
        Tnode *s = string(p);
        NodeList_push(&metas, s);
      }
      break;
    default:
      {
        Tnode *interpd_expr = expr(p, PREC_NONE); // \(...)
        NodeList_push(&metas, interpd_expr);
      }
    }
  }

  return treenode_list(AST_METASTRING, line, metas);
}

Tnode *string(Parse *p)
{
  Token strtok = eat(p);
  Str str = str_from_slice(strtok.slice);
  return treenode_constant(string_value_new(str), strtok.line);
}

// x
Tnode *ident(Parse *p)
{
  Token ident_tok = eat(p);

  Tnode *ident_node = treenode_new(AST_IDENT, ident_tok.line);
  ident_node->assignable = true;
  ident_node->ident = ident_tok.slice;

  return ident_node;
}

Tnode *let(Parse *p)
{
  next(p); // let
  Token ident_tok = consume(p, TK_WORD);

  Tnode *node = treenode_new(AST_LET, ident_tok.line);
  node->ident = ident_tok.slice;
  return node;
}

// An impl of Pratt parsing.
// Handles prefix, infix, postfix and mixfix expressions
Tnode *expr(Parse *p, int min_bp)
{
  Token lhs_token = p->current;
  NudRule lhs_rule = parse_rule(lhs_token.type)->nud;

  if (lhs_rule == NULL) {
    error_out("Expecting expression.\n");
    invalid_token(lhs_token);
  }

  Tnode *lhs = lhs_rule(p);

  for (;;) {
    Token op_token = p->current;
    LedRule op_rule = parse_rule(op_token.type)->led;

    if (op_rule == NULL) {
      error_out("Expecting operator.\n");
      invalid_token(op_token);
    }

    Tnode *op_result = op_rule(p, lhs, min_bp);
    if (op_result == NULL)
      break; // Precedence is too small or op otherwise cannot be used as a LED

    lhs = op_result;
  }

  return lhs;
}
