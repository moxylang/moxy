#ifndef MOXY_LEXER_H
#define MOXY_LEXER_H

#include "token.h"

/* tokenize a whole file; errors are reported through diag */
TokenVec lex_file(int file, const char *src);

/* tokenize a fragment (string interpolation), positions offset from `at` */
TokenVec lex_fragment(const char *src, Loc at);

#endif
