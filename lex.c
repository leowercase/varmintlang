#include "lex.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// 1 character of lookahead.
static inline char peek(Lex *lex)
{
  if (*lex->current == '\0') return '\0';
  return lex->current[1];
}

static inline char next(Lex *lex)
{
  lex->current++;
  return *lex->current;
}

static bool match(Lex *lex, const char expected)
{
  char c = *lex->current;
  if (c != '\0' && c == expected) {
    next(lex);
    return true;
  }
  else return false;
}

static Token token(Lex *lex, TokenType type)
{
  Str string;
  string.s = lex->start;
  string.len = lex->current - lex->start;

  Token tok = {type, string, lex->line};
  return tok;
}

static Token error_token(Lex *lex, const char *msg)
{
  Str err_str = str_from(msg);
  Token tok = {TK_ERR, err_str, lex->line};
  return tok;
}

static Token number(Lex *lex)
{
  while (isdigit(*lex->current))
    next(lex);

  if (*lex->current == '.' && isdigit(peek(lex))) {
    next(lex);
    while(isdigit(*lex->current))
      next(lex);
  }

  return token(lex, TK_NUMERAL);
}

static Token metastring(Lex *lex)
{
  lex->start = lex->current;

  for (bool found_end = false; !found_end; next(lex)) {
    switch (*lex->current) {
    case '"':
      found_end = true;
      lex->current--;
      break;
    case '\\':
      lex->escaping_string = true;
      return token(lex, TK_STRCONT);
    case '\0':
      return error_token(lex, "unterminated string");
    case '\n':
      lex->line++;
      break;
    }
  }

  Token str_tok = token(lex, TK_STREND);
  next(lex); // "

  lex->escaping_string = false;

  return str_tok;
}

static Token string(Lex *lex)
{
  next(lex); // "
  return metastring(lex);
}

static Token escape_sequence(Lex *lex)
{
  char escape_character = next(lex);

  // https://en.wikipedia.org/wiki/Escape_sequences_in_C#Escape_sequences
  char *s;
  switch (escape_character) {
  case 'a': s = "\a"; break;
  case 'b': s = "\b"; break;
  case 'e': s = "\x1b"; break;
  case 'f': s = "\f"; break;
  case 'n': s = "\n"; break;
  case 'r': s = "\r"; break;
  case 't': s = "\t"; break;
  case 'v': s = "\v"; break;
  case '\\': s = "\\"; break;
  case '"': s = "\""; break;
  case '0': s = "\0"; break;

  case '\0':
    return error_token(lex, "unterminated string");
  default:
    return error_token(lex, "invalid escape sequence");
  }
  Str string = str_from(s);
  next(lex);

  Token tok = {TK_STRCONT, string, lex->line};
  return tok;
}

static TokenType is_keyword(Str string)
{
  const char *keywords[] = {
    [TK_NOT] = "not",
    [TK_AND] = "and",
    [TK_OR]  = "or",
  };

  for (TokenType i = TK_NOT; i < TK_OR + 1; i++) {
    if (strncmp(keywords[i], string.s, string.len) == 0)
      return i;
  }

  return false;
}

static Token word(Lex *lex)
{
  while (is_ident(*lex->current))
    next(lex);

  Token word = token(lex, TK_WORD);

  TokenType keyword = is_keyword(word.string);
  if (keyword)
    word.type = keyword;

  return word;
}

static void skip_rest_line(Lex *lex)
{
  char c;
  do {
    c = next(lex);
    if (c == '\0')
      return;
  } while (c != '\n');

  next(lex); // '\n'
  lex->line++;
}

static void skip_redundant_space(Lex *lex)
{
  for (;;) {
    char c = *lex->current;

    if (c == '\n')
      lex->line++;

    else if (c == '#') {
      // Comment.
      skip_rest_line(lex);
      continue;
    }

    else if (!isspace(c))
      return;

    next(lex);
  }
}

Token lex_token(Lex *lex)
{
  if (lex->escaping_string) {
    if (*lex->current == '\\')
      return escape_sequence(lex);
    else
      return metastring(lex);
  }

  skip_redundant_space(lex);

  lex->start = lex->current;

  char c = *lex->current;

  if (isdigit(c)) return number(lex);

  if (is_ident_beginning(c)) return word(lex);

  if (c == '"') return string(lex);

  next(lex);
  switch (c) {
  case '\0':
    {
      Token eof = token(lex, TK_EOF);
      lex->current--; // Don't go past EOF
      return eof;
    }

  case '(': return token(lex, TK_LPAREN);
  case ')': return token(lex, TK_RPAREN);

  case '+': return token(lex, TK_PLUS);
  case '*': return token(lex, TK_STAR);
  case '/': return token(lex, TK_SLASH);
  case '^': return token(lex, TK_CARET);
  case '%': return token(lex, TK_PERCENT);
  case '=': return token(lex, TK_EQ);

  case '-':
    return token(lex,
      match(lex, '>') ? TK_ARROW : TK_MINUS);

  case '!':
    return token(lex,
      match(lex, '=') ? TK_NEQ : TK_BANG);

  case '<':
    return token(lex,
      match(lex, '=') ? TK_LEQ : TK_LT);

  case '>':
    return token(lex,
      match(lex, '=') ? TK_GEQ : TK_GT);
  }

  return error_token(lex, "illegal token");
}

Lex lex_new(char *source)
{
  Lex lex;
  lex.start = lex.current = source;
  lex.line = 1;
  lex.escaping_string = false;

  return lex;
}

void print_token(Token token)
{
#define CASE(name) \
  case TK_##name: \
    printf("<" #name "> `%.*s`", (int)token.string.len, token.string.s); \
    break;

  switch (token.type) {
  case TK_EOF:
    printf("<EOF>");
    break;
  case TK_ERR:
    error_out("<lex error: %.*s>", (int)token.string.len, token.string.s, token.line);
    break;
  CASE(PLUS)
  CASE(MINUS)
  CASE(STAR)
  CASE(SLASH)
  CASE(CARET)
  CASE(PERCENT)
  CASE(BANG)
  CASE(EQ)
  CASE(NEQ)
  CASE(LT)
  CASE(GT)
  CASE(LEQ)
  CASE(GEQ)
  CASE(NOT)
  CASE(AND)
  CASE(OR)
  CASE(ARROW)
  CASE(LPAREN)
  CASE(RPAREN)
  CASE(NUMERAL)
  CASE(STRCONT)
  CASE(STREND)
  CASE(WORD)
  }

#undef CASE
}

