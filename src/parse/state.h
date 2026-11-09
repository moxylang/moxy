#ifndef MOXY_PARSE_STATE_H
#define MOXY_PARSE_STATE_H

#include "ast/ast.h"
#include "diag/diag.h"
#include "lex/token.h"

typedef struct {
    Token *toks;
    int len;
    int pos;
    int no_struct_lit; /* disables Type{...} literals (match/for-in heads) */
    int in_arm;        /* parsing a match arm body: ',' ends a statement */
    char *ns;          /* current namespace path */
} Parser;

extern Parser P;

Token *peek(void);
Token *peek_at(int ahead);
Token *next(void);
int at(TokKind kind);
int at_word(const char *word);
int word_at(int ahead, const char *word);
int accept(TokKind kind);
Token *expect(TokKind kind, const char *what);
_Noreturn void fail(Loc loc, const char *fmt, ...);
int match_paren(int open);

void types_reset(void);
void types_add(const char *name);
int types_has(const char *name);
void types_prescan(void);

int is_type_name(Token *t);
int is_type_start_at(int ahead);
int is_decl_start(void);

/* types.c */
typedef struct {
    int storage;    /* F_STATIC | F_EXTERN | F_INLINE ... */
    int is_typedef;
    int is_auto;
    TypeRef *base;
} Specs;

int parse_specifiers(Specs *out);
TypeRef *parse_declarator(TypeRef *base, char **name, ParamList *fparams, Loc *nameloc);
TypeRef *parse_type_name(void);
void parse_params(ParamList *out, TypeRefList *types, int *variadic);
Node *parse_struct_body(Node *s);
Node *parse_cenum_body(Node *e);
Node *parse_moxy_enum(void);
int moxy_enum_ahead(void);

/* expr.c */
Node *parse_expr(void);
Node *parse_assign(void);
Node *parse_ternary(void);
Node *parse_initializer(void);
Node *parse_unary(void);

/* stmt.c */
Node *parse_stmt(void);
Node *parse_block(void);
Node *parse_local_decls(void);

/* top.c */
Node *parse_decl_group(int top);

#endif
