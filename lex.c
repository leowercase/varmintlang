#include "lex.h"

#include <ctype.h>
#include <string.h>

static inline void descend_template_nesting(Lex *lex)
{
  UnmatchedBrackets new_unmatched = {0, 0};
  TemplateNesting_push(&lex->template_nesting, new_unmatched);
}

static inline void ascend_template_nesting(Lex *lex)
{
  TemplateNesting_pop(&lex->template_nesting);
}

static inline UnmatchedBrackets *current_unmatched(Lex *lex)
{
  return TemplateNesting_top(&lex->template_nesting);
}

static inline size_t current_template_nesting(Lex *lex)
{
  return lex->template_nesting.len - 1;
}

static inline void end_lex(Lex *lex)
{
  free(lex->template_nesting.data);
}

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
  Str slice;
  slice.s = lex->start;
  slice.len = (size_t)(lex->current - lex->start);

  Token tok = {type, slice, lex->line};
  return tok;
}

static Token error_token(Lex *lex, const char *msg)
{
  Str err_str = str_new(msg, strlen(msg));
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
    descend_template_nesting(lex);
    return token(lex, TK_LPAREN);
    // \{...}
  case '{':
    lex->start = lex->current;
    next(lex);
    lex->escaping_string = false;
    descend_template_nesting(lex);
    return token(lex, TK_LCURLY);

  case '\0':
    end_lex(lex);
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

static Token label(Lex *lex)
{
  Token label = word(lex);
  label.type = TK_LABEL;
  return label;
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

static void block_comment(Lex *lex)
{
  next(lex); next(lex); // "#["
  size_t comment_nesting = 1;

  do {
    switch (*lex->current) {
    case '\0': return;
    case '\n': next(lex); lex->line++; continue;
    }

    if (match(lex, ']') && match(lex, '#'))
      comment_nesting--;
    else if (match(lex, '#') && match(lex, '['))
      comment_nesting++;

    next(lex);
  } while (comment_nesting > 0);

  if (*lex->current == '\n') lex->line++;
}

static void skip_redundant_space(Lex *lex)
{
  for (;;) {
    char c = *lex->current;

    if (c == '\n')
      lex->line++;

    else if (c == '#') {
      // Comment.
      if (peek(lex) == '[')
        block_comment(lex);
      else
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

  if (c == '\0') {
    end_lex(lex);
    return token(lex, TK_EOF);
  }

  // One or two character tokens
  next(lex);
  switch (c) {
  case '\'':
    if (is_ident_beginning(*lex->current))
      return label(lex);
    break;

  case '(':
    current_unmatched(lex)->parens++;
    return token(lex, TK_LPAREN);

  case ')':
    if (current_template_nesting(lex) > 0
        && current_unmatched(lex)->parens == 0) {
      // We're ending \(...)
      ascend_template_nesting(lex);
      lex->escaping_string = true;
    }
    else
      current_unmatched(lex)->parens--;
    return token(lex, TK_RPAREN);

  case '{':
    current_unmatched(lex)->curlies++;
    return token(lex, TK_LCURLY);

  case '}':
    if (current_template_nesting(lex) > 0
        && current_unmatched(lex)->curlies == 0) {
      // We're ending \{...}
      ascend_template_nesting(lex);
      lex->escaping_string = true;
    }
    else
      current_unmatched(lex)->curlies--;
    return token(lex, TK_RCURLY);

  case '+': return token(lex, TK_PLUS);
  case '*': return token(lex, TK_STAR);
  case '^': return token(lex, TK_CARET);
  case '%': return token(lex, TK_PERCENT);
  case '!': return token(lex, TK_BANG);
  case '[': return token(lex, TK_LBRACK);
  case ']': return token(lex, TK_RBRACK);
  case ';': return token(lex, TK_SEMICOLON);
  case ',': return token(lex, TK_COMMA);

  case '-':
    return token(lex,
        match(lex, '>') ? TK_ARROW : TK_MINUS);

  case '=':
    return token(lex,
        match(lex, '>') ? TK_MAPS_TO : TK_EQ);

  case '/':
    return token(lex,
        match(lex, '=') ? TK_NEQ : TK_SLASH);

  case '<':
    return token(lex,
        match(lex, '=') ? TK_LEQ : TK_LT);

  case '>':
    return token(lex,
        match(lex, '=') ? TK_GEQ : TK_GT);

  case ':':
    return token(lex,
        match(lex, '=') ? TK_ASSIGN : TK_COLON);

  case '.':
    return token(lex,
      match(lex, '.')
        ? (match(lex, '=') ? TK_DOTDOTEQ : TK_DOTDOT)
        : TK_DOT);

  case '|':
    if (match(lex, '|'))
      return token(lex, TK_2PIPE);
    break;
  }

  return error_token(lex, "illegal token");
}

char *const token_cstring(const TokenType type)
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
  case_(LET)
  case_(NOT)
  case_(AND) case_(OR)
  case_(IN) case_(NOTIN)
  case_(MOD)
  case_(UNWRAPPED)
  case_(IF) case_(ELSE) case_(ELIF)
  case_(LOOP) case_(FOR) case_(WHILE)
  case_(BREAK) case_(CONTINUE)
  case_(RETURN)
  case_(USING)
  case_(TRUE) case_(FALSE)
  case_(SOME) case_(NONE)
  case_(ARROW)
  case_(MAPS_TO)
  case_(LPAREN) case_(RPAREN)
  case_(LBRACK) case_(RBRACK)
  case_(LCURLY) case_(RCURLY)
  case_(COLON) case_(SEMICOLON) case_(COMMA)
  case_(DOT)
  case_(DOTDOT) case_(DOTDOTEQ)
  case_(NUMERAL)
  case_(STRCONT) case_(STREND)
  case_(WORD) case_(LABEL)
  }

#undef case_
}
