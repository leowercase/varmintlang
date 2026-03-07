#ifndef LANG_LEX_H
#define LANG_LEX_H

#include "generic/dyn_array.h"
#include "str.h"
#include "util.h"

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
  TK_LET,
  TK_NOT,
  TK_AND, TK_OR,
  TK_IN, TK_NOTIN,
  TK_MOD,
  TK_IF, TK_ELSE, TK_ELIF,
  TK_LOOP, TK_FOR, TK_WHILE,
  TK_BREAK, TK_CONTINUE,
  TK_RETURN,
  TK_USING,
  TK_TRUE, TK_FALSE,
  TK_SOME, TK_NONE,
  TK_ARROW,
  TK_MAPS_TO,
  TK_LPAREN, TK_RPAREN,
  TK_LBRACK, TK_RBRACK,
  TK_LCURLY, TK_RCURLY,
  TK_COLON, TK_SEMICOLON, TK_COMMA,
  TK_NUMERAL,
  TK_STRCONT, TK_STREND,
  TK_WORD,
} TokenType;

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
  const Str keywords[] = {
    [TK_LET]      = str_from("let"),
    [TK_NOT]      = str_from("not"),
    [TK_AND]      = str_from("and"),
    [TK_OR]       = str_from("or"),
    [TK_IN]       = str_from("in"),
    [TK_NOTIN]    = str_from("notin"),
    [TK_MOD]      = str_from("mod"),
    [TK_IF]       = str_from("if"),
    [TK_ELSE]     = str_from("else"),
    [TK_ELIF]     = str_from("elif"),
    [TK_LOOP]     = str_from("loop"),
    [TK_FOR]      = str_from("for"),
    [TK_WHILE]    = str_from("while"),
    [TK_BREAK]    = str_from("break"),
    [TK_CONTINUE] = str_from("continue"),
    [TK_RETURN]   = str_from("return"),
    [TK_USING]    = str_from("using"),
    [TK_TRUE]     = str_from("True"),
    [TK_FALSE]    = str_from("False"),
    [TK_SOME]     = str_from("Some"),
    [TK_NONE]     = str_from("None"),
  };
  // This could be faster with a trie. Still sufficiently fast though.
  for (int i = TK_LET; i <= TK_NONE; i++)
    if (strs_eq(keywords[i], str)) return (TokenType)i;

  return (TokenType)false;
}

typedef struct {
  TokenType type;
  Str slice;
  size_t line;
} Token;

typedef struct {
  int parens, curlies;
} UnmatchedBrackets;

typedef DYN_ARRAY_STRUCT(UnmatchedBrackets) TemplateNesting;
#define T UnmatchedBrackets
#define ARR TemplateNesting
#include "generic/dyn_array.inc"

static inline
TemplateNesting template_nesting_init(void)
{
  TemplateNesting nesting = TemplateNesting_with_cap(1);

  UnmatchedBrackets initial_unmatched = {0, 0};
  TemplateNesting_push(&nesting, initial_unmatched);

  return nesting;
}

typedef struct {
  char *start;
  char *current;
  size_t line;

  bool escaping_string;
  TemplateNesting template_nesting;
} Lex;

static inline
Lex lex_new(char *source)
{
  Lex lex;
  lex.start = lex.current = source;
  lex.line = 1;

  lex.escaping_string = false;
  lex.template_nesting = template_nesting_init();

  return lex;
}

Token lex_token(Lex *lex);

char *const token_cstring(const TokenType type);

#endif
