#ifndef MOXY_LOAD_H
#define MOXY_LOAD_H

#include "lex/token.h"

/* search directories for `#include "x.mxy"` beyond the including file's dir */
void load_add_include_dir(const char *dir);
void load_reset(void);

/* read, lex and splice includes (each .mxy file at most once) */
TokenVec load_program(const char *path);

/* lex a source string as a program (used by fmt/lint/tests) */
TokenVec load_source(const char *display, const char *src);

#endif
