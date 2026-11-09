#ifndef MOXY_PARSE_H
#define MOXY_PARSE_H

#include "ast/ast.h"
#include "lex/token.h"

/* parse a spliced token stream into an N_BLOCK of top-level declarations */
Node *parse_program(TokenVec toks);

/* make an external C typedef name known to the parser */
void parse_register_type(const char *name);

/* parse a type from source text (used by tests and sema helpers) */
Node *parse_expression_tokens(TokenVec toks);

#endif
