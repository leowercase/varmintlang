#ifndef LANG_LEX_H
#define LANG_LEX_H

#include "util.h"
#include "val.h"

#include <ctype.h>

// Lexical analysis splits text into lexically meaningful tokens.

typedef enum {
  TK_EOF,
  TK_ERR,
  TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH,
  TK_CARET,
  TK_PERCENT,
  TK_BANG,
  TK_EQ, TK_NEQ, TK_LT, TK_GT, TK_LEQ, TK_GEQ,
  TK_NOT,
  TK_AND, TK_OR, TK_ARROW,
  TK_LPAREN, TK_RPAREN,
  TK_NUMERAL,
  TK_STRCONT, TK_STREND,
  TK_WORD,
} TokenType;

static inline
bool is_cmp_token(TokenType type)
{
  return TK_EQ <= type && type <= TK_LEQ;
}

static inline
bool is_ident_beginning(char c)
{
  return isalpha(c) || c == '_';
}

static inline
bool is_ident(char c)
{
  return is_ident_beginning(c) || isdigit(c);
}

typedef struct {
  TokenType type;
  Str string;
  size_t line;
} Token;

typedef struct {
  char *start;
  char *current;
  size_t line;
  bool escaping_string;
} Lex;

Lex lex_new(char *source);
Token lex_token(Lex *lex);

void print_token(Token token);

#endif
