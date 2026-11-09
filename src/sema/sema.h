#ifndef MOXY_SEMA_H
#define MOXY_SEMA_H

#include "ast/ast.h"
#include "base/map.h"
#include "types/types.h"

typedef enum { S_VAR, S_FUNC, S_TYPE, S_CONST, S_NS } SymKind;

typedef struct Scope Scope;
typedef struct FnCtx FnCtx;

struct Sym {
    SymKind kind;
    char *name;
    char *cname;
    Type *type;
    Node *decl;
    Loc loc;
    int global;
    int param;
    int mutated;
    int addr_taken;
    int used;
    int captured;
    int is_async;
    int has_body;
    int resolving;
    FnCtx *owner;
    Scope *members;
};

enum { SC_GLOBAL, SC_NS, SC_FUNC, SC_BLOCK };

typedef Vec(Scope *) ScopeList;

struct Scope {
    Scope *parent;
    StrMap names;
    int kind;
    char *ns;        /* namespace path for SC_NS/SC_GLOBAL ("" for global) */
    ScopeList imports;
};

struct FnCtx {
    Node *node;      /* N_FUNC or N_LAMBDA */
    Type *ret;
    FnCtx *outer;
    int is_lambda;
    int infer_ret;
    int loops;
    int switches;
    int in_defer;
    int has_cleanup;
    int has_goto;
};

/* conversions recorded on N_CONV (node->op) */
enum {
    CV_CSTR_TO_STR = 1, /* copy a C string into a managed string */
    CV_FN_TO_CLOSURE,   /* named function used as a closure value */
    CV_LAMBDA_FNPTR,    /* capture-free lambda used as a C function pointer */
    CV_TO_STRING,       /* string(x): format any value */
    CV_CAST,            /* plain C conversion */
};

/* builtin operations recorded on node->builtin */
enum {
    B_NONE,
    B_PRINT, B_ASSERT, B_PANIC,
    B_FUNC, B_UNKNOWN_CALL, B_CLOSURE_CALL, B_FNPTR_CALL, B_ENUM_CTOR, B_ASYNC_CALL,
    B_STR_LEN, B_STR_IS_EMPTY, B_STR_CONTAINS, B_STR_STARTS, B_STR_ENDS, B_STR_FIND,
    B_STR_SLICE, B_STR_SPLIT, B_STR_LINES, B_STR_TRIM, B_STR_UPPER, B_STR_LOWER,
    B_STR_REPLACE, B_STR_REPEAT, B_STR_TO_INT, B_STR_TO_FLOAT, B_STR_CHARS, B_STR_AT,
    B_LIST_LEN, B_LIST_IS_EMPTY, B_LIST_PUSH, B_LIST_POP, B_LIST_INSERT, B_LIST_REMOVE,
    B_LIST_CLEAR, B_LIST_CONTAINS, B_LIST_INDEX_OF, B_LIST_REVERSE, B_LIST_SORT,
    B_LIST_SLICE, B_LIST_FIRST, B_LIST_LAST, B_LIST_JOIN, B_LIST_MAP, B_LIST_FILTER,
    B_LIST_EACH, B_LIST_CONCAT,
    B_MAP_LEN, B_MAP_IS_EMPTY, B_MAP_SET, B_MAP_GET, B_MAP_GET_OR, B_MAP_HAS,
    B_MAP_REMOVE, B_MAP_KEYS, B_MAP_VALUES, B_MAP_CLEAR,
    B_RES_IS_OK, B_RES_IS_ERR, B_RES_UNWRAP, B_RES_UNWRAP_OR, B_RES_ERROR,
    B_STR_CONCAT, B_STR_EQ, B_STR_CMP, B_VAL_EQ,
    B_WEAK_LOAD,
};

typedef struct {
    Node *prog;
    NodeList structs;    /* N_STRUCT with bodies, in declaration order */
    NodeList enums;      /* N_ENUM */
    NodeList cenums;     /* N_CENUM with bodies */
    NodeList typedefs;   /* N_TYPEDEF */
    NodeList funcs;      /* N_FUNC (all, including prototypes) */
    NodeList globals;    /* N_VAR at top level */
    NodeList lambdas;    /* N_LAMBDA, ids assigned */
    NodeList directives; /* top-level N_DIRECTIVE */
    NodeList asserts;    /* top-level N_STATIC_ASSERT */
    Node *main;
    int threads;
} Program;

extern Program prog;

/* lint checks (sema_check's lint argument) */
enum { LINT_ON = 1, LINT_UNUSED = 2, LINT_SHADOW = 4, LINT_EMPTY = 8 };

/* entry point: returns number of errors */
int sema_check(Node *root, int lint);

/* shared internals */
extern Scope *global_scope;
extern Scope *cur_scope;
extern FnCtx *cur_fn;
extern int lint_mode;

Scope *scope_push(int kind);
void scope_pop(void);
void pop_checked(void);
Scope *ns_scope(const char *path);
Sym *sym_new(SymKind kind, const char *name, Loc loc);
Sym *scope_declare(Scope *s, Sym *sym);
Sym *scope_get(Scope *s, const char *name);
Sym *lookup(const char *name, Loc loc);
Sym *lookup_quiet(const char *name);
Sym *lookup_tag(const char *name);
void declare_tag(const char *name, Sym *sym);
char *cname_for(const char *ns, const char *name);
void suggest_name(const char *name, Loc loc);

Type *resolve_type(TypeRef *tr);
Type *define_struct(Node *decl, const char *typedef_name);
Type *define_cenum(Node *decl);
char *const_text(Node *e);

Type *check_expr(Node **slot, Type *expected);
Type *check_cond(Node **slot);
int coerce(Node **slot, Type *to, const char *what);
int assignable(Type *to, Type *from);
Node *wrap_conv(Node *n, Type *to, int kind);
void mark_mutated(Node *target);
int is_lvalue(Node *n);

Type *check_call(Node **slot, Type *expected);
Type *check_member(Node **slot);
Type *check_lambda(Node *n, Type *expected);
Type *variant_ctor(Node *n, Sym *enum_sym, const char *variant, Type *expected);
Node *find_variant(Type *e, const char *name);

void check_stmt(Node *n);
void check_block_list(NodeList *list);
void check_function(Node *fn);

#endif
