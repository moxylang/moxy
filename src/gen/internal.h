#ifndef MOXY_GEN_INTERNAL_H
#define MOXY_GEN_INTERNAL_H

#include "base/map.h"
#include "gen.h"
#include "sema/sema.h"

/* a generated C expression and whether it carries its own reference */
typedef struct {
    char *s;
    int owned;
} CExpr;

typedef struct {
    char *name;
    Type *type;
} Temp;

typedef Vec(Temp) TempList;

/* per-statement state: code that must run first, and temporaries to release */
typedef struct {
    Buf pre;
    TempList temps;
    int indent;
} Ctx;

enum { CL_VAR, CL_DEFER, CL_RAW };

typedef struct {
    int kind;
    char *name;
    Type *type;
    Node *stmt;
    Sym *sym;
} Cleanup;

enum { GS_BLOCK, GS_LOOP, GS_SWITCH, GS_FUNC };

typedef struct {
    int kind;
    Vec(Cleanup) items;
    char *continue_label;
    int dead; /* the block already left through return/break/continue */
} GScope;

typedef struct {
    Buf head;       /* feature macros and standard includes */
    Buf directives; /* user #include / #define */
    Buf fwd;        /* typedef struct X X; */
    Buf types;      /* struct / enum definitions */
    Buf hproto;     /* helper prototypes */
    Buf helpers;    /* helper definitions */
    Buf protos;     /* function prototypes */
    Buf lits;       /* string literals */
    Buf globals;    /* global variables */
    Buf funcs;      /* function definitions (user code with #line) */
    Buf init;       /* runtime initialization of globals */
} Sections;

extern Sections out;
extern const GenOptions *gopts;
extern Node *cur_func;       /* N_FUNC or N_LAMBDA being generated */
extern Type *cur_ret;
typedef Vec(GScope) GScopeVec;
extern GScopeVec gscopes;
extern int tmp_counter;

/* out.c */
char *fresh(const char *prefix);
void line_at(Buf *b, int indent, Loc loc, const char *fmt, ...);
void raw_line(Buf *b, int indent, const char *fmt, ...);
void mark_loc(Buf *b, Loc loc);
char *c_string_lit(const char *raw);
char *lit_ref(const char *raw);
char *loc_lit(Loc loc);
char *sym_ref(Sym *s);

/* own.c: ownership helpers */
int managed(Type *t);
char *retain_of(Type *t, const char *e);
void release_into(Buf *b, int indent, Type *t, const char *e);
char *release_expr(Type *t, const char *e);
char *vw_of(Type *t);
char *cdecl(Type *t, const char *name);
char *ctype(Type *t);
char *borrow(Ctx *c, CExpr e, Type *t);
char *consume(Ctx *c, CExpr e, Type *t);
char *hoist(Ctx *c, const char *e, Type *t, int owned);
void ctx_init(Ctx *c, int indent);
void ctx_flush_pre(Ctx *c, Buf *b);
void ctx_release(Ctx *c, Buf *b);
void ctx_free(Ctx *c);
char *result_name(Type *t);
char *struct_helpers(Type *t);

/* types.c */
void emit_types(void);
void need_type(Type *t);

/* expr.c */
CExpr gen_expr(Node *n, Ctx *c);
char *gen_borrow(Node *n, Ctx *c);
char *gen_consume(Node *n, Ctx *c);
char *gen_value(Node *n, Ctx *c);
CExpr gen_init(Node *n, Type *t, Ctx *c);
CExpr gen_conv(Node *n, Ctx *c);
CExpr gen_interp(Node *n, Ctx *c);
CExpr gen_enum_ctor(Node *n, Ctx *c);
char *closure_invoke(const char *f, Type *ft, char **args, int nargs, Loc loc, Ctx *c);
void fmt_into(Ctx *c, const char *buf, Node *arg, int nested);

/* call.c */
CExpr gen_call(Node *n, Ctx *c);
CExpr gen_member(Node *n, Ctx *c);
CExpr gen_closure_call(const char *fexpr, Type *ft, NodeList *args, int first, Ctx *c, Loc loc);

/* lvalue.c */
char *gen_lvalue(Node *n, Ctx *c);
CExpr gen_assign(Node *n, Ctx *c);

/* stmt.c */
void gen_stmt(Node *n, Buf *b, int indent);
void gen_block_body(NodeList *list, Buf *b, int indent);
void gscope_push(int kind);
void gscope_pop(Buf *b, int indent);
void add_cleanup_var(const char *name, Type *t, Sym *sym);
void emit_cleanups_to(Buf *b, int indent, int depth, const char *skip);
void gen_stmt_ctx(Node *n, Buf *b, int indent, void (*fn)(Node *, Ctx *, Buf *, int));

/* loop.c */
void gen_loop(Node *n, Buf *b, int indent);

/* match.c */
void gen_match(Node *n, Buf *b, int indent);

/* func.c */
void gen_function(Node *fn);
void gen_lambdas(void);
char *lambda_value(Node *lam, Ctx *c, int *owned);
char *fn_closure(Sym *fn);
void gen_globals(void);
void gen_main_wrapper(void);
char *capture_ref(Sym *s);

#endif
