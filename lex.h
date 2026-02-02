#ifndef LANG_LEX_H
#define LANG_LEX_H

#include "util.h"
#include "val.h"

#include <ctype.h>
#include <string.h>

/*
 * Lexical analysis splits text into lexically meaningful tokens.
 * https://en.wikipedia.org/wiki/Lexical_analysis
 *
 * This is what LLMs basically do as well!
 */

typedef enum {
  TK_EOF,
  TK_ERR,
  TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH,
  TK_CARET,
  TK_PERCENT,
  TK_BANG,
  TK_2PIPE,
  TK_EQ, TK_NEQ, TK_LT, TK_GT, TK_LEQ, TK_GEQ,
  TK_ASSIGN,
  TK_2PLUS, TK_2MINUS, 
  TK_LET,
  TK_NOT,
  TK_AND, TK_OR,
  TK_IF, TK_ELSE,
  TK_LOOP, TK_FOR, TK_WHILE,
  TK_BREAK, TK_CONTINUE,
  TK_TRUE, TK_FALSE,
  TK_ARROW,
  TK_LPAREN, TK_RPAREN,
  TK_LBRACK, TK_RBRACK,
  TK_LCURLY, TK_RCURLY,
  TK_COLON, TK_SEMICOLON, TK_COMMA,
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
bool is_infix_op_token(TokenType type)
{
  return (TK_PLUS <= type && type <= TK_SLASH)
    || (TK_CARET <= type && type <= TK_2PIPE)
    || type == TK_AND || type == TK_OR || type == TK_ARROW;
}

// Increment/decrement
static inline
bool is_crement_op(TokenType type)
{
  return type == TK_2PLUS || type == TK_2MINUS;
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

static inline
TokenType is_keyword(Str str)
{
  const char *keywords[] = {
    [TK_LET]      = "let",
    [TK_NOT]      = "not",
    [TK_AND]      = "and",
    [TK_OR]       = "or",
    [TK_IF]       = "if",
    [TK_ELSE]     = "else",
    [TK_LOOP]     = "loop",
    [TK_FOR]      = "for",
    [TK_WHILE]    = "while",
    [TK_BREAK]    = "break",
    [TK_CONTINUE] = "continue",
    [TK_TRUE]     = "True",
    [TK_FALSE]    = "False",
  };
  // This could be faster with a trie. Still sufficiently fast though.

  for (int i = TK_LET; i < TK_FALSE + 1; i++) {
    if (strncmp(keywords[i], str.s, strlen(keywords[i])) == 0)
      return (TokenType)i;
  }

  return (TokenType)false;
}

typedef struct {
  TokenType type;
  StrSlice slice; // Not a C string!
  size_t line;
} Token;

typedef struct {
  char *start;
  char *current;
  size_t line;

  bool escaping_string;
  int template_nesting;
  int unmatched_parens, unmatched_curlies;
} Lex;

static inline
Lex lex_new(char *source)
{
  Lex lex;
  lex.start = lex.current = source;
  lex.line = 1;

  lex.escaping_string = false;
  lex.template_nesting = 0;
  lex.unmatched_parens = lex.unmatched_curlies = 0;

  return lex;
}

Token lex_token(Lex *lex);

const char *tok_cstring(const TokenType type);

#endif
