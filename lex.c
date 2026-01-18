#include "lex.h"

#include <ctype.h>
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
  StrSlice slice;
  slice.s = lex->start;
  slice.len = (size_t)(lex->current - lex->start);

  Token tok = {type, slice, lex->line};
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
  char *c;

  switch (escape_character) {
    // https://en.wikipedia.org/wiki/Escape_sequences_in_C#Escape_sequences
  case 'a': c = "\a"; break;
  case 'b': c = "\b"; break;
  case 'e': c = "\x1b"; break;
  case 'f': c = "\f"; break;
  case 'n': c = "\n"; break;
  case 'r': c = "\r"; break;
  case 't': c = "\t"; break;
  case 'v': c = "\v"; break;
  case '\\': c = "\\"; break;
  case '"': c = "\""; break;
  case '0': c = "\0"; break;

    // https://en.wikipedia.org/wiki/String_interpolation
    // \(...)
  case '(':
    lex->start = lex->current;
    next(lex);
    lex->escaping_string = false;
    lex->template_nesting++;
    return token(lex, TK_LPAREN);
    // \{...}
  case '{':
    lex->start = lex->current;
    next(lex);
    lex->escaping_string = false;
    lex->template_nesting++;
    return token(lex, TK_LCURLY);

  case '\0':
    return error_token(lex, "unterminated string");
  default:
    return error_token(lex, "invalid escape sequence");
  }

  Str str = {c, 1};
  next(lex);

  Token tok = {TK_STRCONT, str, lex->line};
  return tok;
}

static Token word(Lex *lex)
{
  while (is_ident(*lex->current))
    next(lex);

  Token word = token(lex, TK_WORD);

  TokenType keyword = is_keyword(word.slice);
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

  if (isdigit(c))
    return number(lex);

  if (is_ident_beginning(c))
    return word(lex);

  if (c == '"')
    return string(lex);

  // One or two character tokens
  next(lex);
  switch (c) {
  case '\0':
    {
      Token eof = token(lex, TK_EOF);
      lex->current--; // Don't go past EOF
      return eof;
    }

  case '(':
    lex->unmatched_parens++;
    return token(lex, TK_LPAREN);

  case ')':
    if (lex->template_nesting > 0 && lex->unmatched_parens == 0) {
      // We're ending \(...)
      lex->template_nesting--;
      lex->escaping_string = true;
    }
    else
      lex->unmatched_parens--;
    return token(lex, TK_RPAREN);

  case '{':
    lex->unmatched_curlies++;
    return token(lex, TK_LCURLY);

  case '}':
    if (lex->template_nesting > 0 && lex->unmatched_curlies == 0) {
      // Ending \{...}.
      lex->template_nesting--;
      lex->escaping_string = true;
    }
    else
      lex->unmatched_curlies--;
    return token(lex, TK_RCURLY);

  case '*': return token(lex, TK_STAR);
  case '/': return token(lex, TK_SLASH);
  case '^': return token(lex, TK_CARET);
  case '%': return token(lex, TK_PERCENT);
  case '=': return token(lex, TK_EQ);
  case '[': return token(lex, TK_LBRACK);
  case ']': return token(lex, TK_RBRACK);
  case ';': return token(lex, TK_SEMICOLON);
  case ',': return token(lex, TK_COMMA);

  case '-':
    switch (*lex->current) {
    case '>':
      next(lex); return token(lex, TK_ARROW);
    case '-':
      next(lex); return token(lex, TK_2MINUS);
    default:
      return token(lex, TK_MINUS);
    }

  case '+':
    return token(lex,
        match(lex, '+') ? TK_2PLUS : TK_PLUS);

  case '!':
    return token(lex,
        match(lex, '=') ? TK_NEQ : TK_BANG);

  case '<':
    return token(lex,
        match(lex, '=') ? TK_LEQ : TK_LT);

  case '>':
    return token(lex,
        match(lex, '=') ? TK_GEQ : TK_GT);

  case ':':
    return token(lex,
        match(lex, '=') ? TK_ASSIGN : TK_COLON);

  case '|':
    if (match(lex, '|'))
      return token(lex, TK_2PIPE);
  }

  return error_token(lex, "illegal token");
}

const char *tok_cstring(const TokenType type)
{
#define case_(name) case TK_##name: return #name;

  switch (type) {
  case_(EOF)
  case_(ERR)
  case_(PLUS) case_(MINUS) case_(STAR) case_(SLASH)
  case_(CARET)
  case_(PERCENT)
  case_(BANG)
  case_(2PIPE)
  case_(EQ) case_(NEQ) case_(LT) case_(GT) case_(LEQ) case_(GEQ)
  case_(ASSIGN)
  case_(2PLUS) case_(2MINUS)
  case_(LET)
  case_(NOT)
  case_(AND) case_(OR)
  case_(TRUE) case_(FALSE)
  case_(IF) case_(ELSE)
  case_(LOOP) case_(FOR) case_(WHILE)
  case_(BREAK) case_(CONTINUE)
  case_(ARROW)
  case_(LPAREN) case_(RPAREN)
  case_(LBRACK) case_(RBRACK)
  case_(LCURLY) case_(RCURLY)
  case_(COLON) case_(SEMICOLON) case_(COMMA)
  case_(NUMERAL)
  case_(STRCONT) case_(STREND)
  case_(WORD)
  }

#undef case_
}
