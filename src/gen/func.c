#include <string.h>

#include "internal.h"

Buf glue;
static StrMap thunks;

char *capture_ref(Sym *s) {
    if (!cur_func || cur_func->kind != N_LAMBDA) return NULL;
    for (int i = 0; i < cur_func->captures.len; i++)
        if (cur_func->captures.items[i] == s) return xfmt("((struct __mxy_env%d *)__self)->%s", cur_func->id, s->cname);
    return NULL;
}

static char *fn_name(Node *fn) {
    if (fn == prog.main) return xstrdup("__mxy_main");
    return xstrdup(fn->sym->cname);
}

static char *params_decl(ParamList *params, TypeList *types, int variadic, const char *first) {
    Buf b = {0};
    if (first) buf_put(&b, first);
    for (int i = 0; i < params->len; i++) {
        if (b.len) buf_put(&b, ", ");
        Param *p = &params->items[i];
        char *name = p->sym ? xstrdup(p->sym->cname) : p->name ? xstrdup(p->name) : xfmt("__arg%d", i);
        buf_put(&b, cdecl(types->items[i], name));
    }
    if (variadic) buf_put(&b, b.len ? ", ..." : "...");
    if (!b.len) buf_put(&b, "void");
    return buf_take(&b);
}

static const char *storage(Node *fn) {
    int nr = (fn->flags & F_NORETURN) != 0;
    if (fn == prog.main) return "static ";
    if (fn->flags & F_INLINE) return nr ? "static inline _Noreturn " : "static inline ";
    if (fn->flags & F_STATIC) return nr ? "static _Noreturn " : "static ";
    return nr ? "_Noreturn " : "";
}

static void retain_params(ParamList *params, Buf *b, int indent) {
    for (int i = 0; i < params->len; i++) {
        Sym *s = params->items[i].sym;
        if (!s || !managed(s->type) || !(s->mutated || s->addr_taken)) continue;
        raw_line(b, indent, "%s = %s;", s->cname, retain_of(s->type, s->cname));
        add_cleanup_var(s->cname, s->type, s);
    }
}

static void prototype(Node *fn) {
    Sym *s = fn->sym;
    if (!s || str_starts(s->cname, "mxy_")) return;
    Type *ft = type_unqual(s->type);
    if (!ft || ft->kind != TY_FUNC) return;
    char *params = params_decl(&fn->params, &ft->params, ft->variadic, NULL);
    if (s->is_async) {
        buf_fmt(&out.protos, "mxy_future *%s(%s);\n", s->cname, params);
        buf_fmt(&out.protos, "static %s;\n", cdecl(ft->base, xfmt("__mxy_async_%s(%s)", s->cname, params)));
        return;
    }
    char *name = fn == prog.main ? xstrdup("__mxy_main") : xstrdup(s->cname);
    buf_fmt(&out.protos, "%s%s;\n", storage(s->decl ? s->decl : fn), cdecl(ft->base, xfmt("%s(%s)", name, params)));
}

static void gen_async_glue(Node *fn) {
    Sym *s = fn->sym;
    Type *ft = type_unqual(s->type);
    Type *ret = ft->base;
    const char *N = s->cname;
    Buf *b = &glue;
    buf_fmt(&out.protos, "struct __mxy_args_%s {", N);
    for (int i = 0; i < fn->params.len; i++) buf_fmt(&out.protos, " %s;", cdecl(ft->params.items[i], xfmt("p%d", i)));
    buf_put(&out.protos, " char __pad; };\n");
    buf_fmt(b, "static void __mxy_run_%s(void *a, void *out) {\n    struct __mxy_args_%s *x = (struct __mxy_args_%s *)a;\n    (void)x;\n", N, N, N);
    Buf args = {0};
    for (int i = 0; i < fn->params.len; i++) buf_fmt(&args, "%sx->p%d", i ? ", " : "", i);
    if (type_unqual(ret)->kind == TY_VOID) {
        buf_fmt(b, "    (void)out;\n    __mxy_async_%s(%s);\n}\n", N, buf_str(&args));
    } else {
        buf_fmt(b, "    %s = __mxy_async_%s(%s);\n    memcpy(out, &r, sizeof r);\n}\n", cdecl(ret, "r"), N, buf_str(&args));
    }
    buf_fmt(b, "static void __mxy_dropargs_%s(void *a) {\n    struct __mxy_args_%s *x = (struct __mxy_args_%s *)a;\n    (void)x;\n", N, N, N);
    for (int i = 0; i < fn->params.len; i++) {
        if (!managed(ft->params.items[i])) continue;
        buf_fmt(b, "    %s;\n", release_expr(ft->params.items[i], xfmt("x->p%d", i)));
    }
    buf_put(b, "}\n");
    char *params = params_decl(&fn->params, &ft->params, 0, NULL);
    buf_fmt(b, "mxy_future *%s(%s) {\n", N, params);
    buf_fmt(b, "    struct __mxy_args_%s *__args = (struct __mxy_args_%s *)mxy_zalloc(sizeof *__args);\n", N, N);
    for (int i = 0; i < fn->params.len; i++) {
        Param *p = &fn->params.items[i];
        char *pn = p->sym ? p->sym->cname : p->name;
        buf_fmt(b, "    __args->p%d = %s;\n", i, retain_of(ft->params.items[i], pn));
    }
    buf_fmt(b, "    return mxy_future_spawn(%s, __args, __mxy_run_%s, __mxy_dropargs_%s);\n}\n", vw_of(ret), N, N);
}

void gen_function(Node *fn) {
    Sym *s = fn->sym;
    Type *ft = type_unqual(s->type);
    cur_func = fn;
    cur_ret = ft->base;
    char *name = s->is_async ? xfmt("__mxy_async_%s", s->cname) : fn_name(fn);
    char *params = params_decl(&fn->params, &ft->params, ft->variadic, NULL);
    const char *st = s->is_async ? "static " : storage(fn);
    Buf *b = &out.funcs;
    mark_loc(b, fn->loc);
    buf_fmt(b, "%s%s {\n", st, cdecl(ft->base, xfmt("%s(%s)", name, params)));
    gscopes.len = 0;
    gscope_push(GS_FUNC);
    retain_params(&fn->params, b, 1);
    gen_block_body(&fn->a->list, b, 1);
    gscope_pop(b, 1);
    if (fn == prog.main && type_unqual(ft->base)->kind != TY_VOID) raw_line(b, 1, "return 0;");
    buf_put(b, "}\n\n");
    if (s->is_async) gen_async_glue(fn);
    cur_func = NULL;
}

char *fn_closure(Sym *fn) {
    const char *N = fn->cname;
    if (!strmap_get(&thunks, N)) {
        strmap_put(&thunks, N, (void *)1);
        Type *ft = type_unqual(fn->type);
        Buf params = {0}, args = {0};
        buf_put(&params, "mxy_fn *self");
        for (int i = 0; i < ft->params.len; i++) {
            char *pn = xfmt("a%d", i);
            buf_fmt(&params, ", %s", cdecl(ft->params.items[i], pn));
            buf_fmt(&args, "%s%s", i ? ", " : "", pn);
        }
        char *sig = cdecl(ft->base, xfmt("__mxy_thunk_%s(%s)", N, buf_str(&params)));
        buf_fmt(&out.protos, "static %s;\n", sig);
        buf_fmt(&out.protos, "static mxy_fn __mxy_fnobj_%s = {-1, (void *)__mxy_thunk_%s, NULL};\n", N, N);
        const char *ret = type_unqual(ft->base)->kind == TY_VOID ? "" : "return ";
        buf_fmt(&glue, "static %s {\n    (void)self;\n    %s%s(%s);\n}\n", sig, ret, N, buf_str(&args));
    }
    return xfmt("(&__mxy_fnobj_%s)", N);
}

char *lambda_value(Node *lam, Ctx *c, int *owned) {
    *owned = 0;
    if (lam->builtin == 1) return xfmt("__mxy_lambda%d", lam->id);
    if (!lam->captures.len) return xfmt("(&__mxy_fnobj%d)", lam->id);
    Buf b = {0};
    buf_fmt(&b, "__mxy_mk%d(", lam->id);
    for (int i = 0; i < lam->captures.len; i++) buf_fmt(&b, "%s%s", i ? ", " : "", sym_ref(lam->captures.items[i]));
    buf_put(&b, ")");
    (void)c;
    *owned = 1;
    return buf_take(&b);
}

static void gen_lambda(Node *lam) {
    Type *ct = type_unqual(lam->type);
    Type *ret = ct->base;
    int id = lam->id;
    int as_fnptr = lam->builtin == 1;
    char *params = params_decl(&lam->params, &ct->params, 0, as_fnptr ? NULL : "mxy_fn *__self");
    char *sig = cdecl(ret, xfmt("__mxy_lambda%d(%s)", id, params));
    buf_fmt(&out.protos, "static %s;\n", sig);
    if (lam->captures.len) {
        buf_fmt(&out.protos, "struct __mxy_env%d { mxy_fn hdr;", id);
        for (int i = 0; i < lam->captures.len; i++) {
            Sym *s = lam->captures.items[i];
            buf_fmt(&out.protos, " %s;", cdecl(s->type, s->cname));
        }
        buf_put(&out.protos, " };\n");
        Buf mkp = {0};
        for (int i = 0; i < lam->captures.len; i++) {
            Sym *s = lam->captures.items[i];
            buf_fmt(&mkp, "%s%s", i ? ", " : "", cdecl(s->type, s->cname));
        }
        buf_fmt(&out.protos, "static mxy_fn *__mxy_mk%d(%s);\n", id, buf_str(&mkp));
        buf_fmt(&glue, "static void __mxy_envdrop%d(mxy_fn *f) {\n    struct __mxy_env%d *e = (struct __mxy_env%d *)f;\n    (void)e;\n",
                id, id, id);
        for (int i = 0; i < lam->captures.len; i++) {
            Sym *s = lam->captures.items[i];
            if (managed(s->type)) buf_fmt(&glue, "    %s;\n", release_expr(s->type, xfmt("e->%s", s->cname)));
        }
        buf_put(&glue, "}\n");
        buf_fmt(&glue, "static mxy_fn *__mxy_mk%d(%s) {\n", id, buf_str(&mkp));
        buf_fmt(&glue, "    struct __mxy_env%d *e = (struct __mxy_env%d *)mxy_alloc(sizeof *e);\n", id, id);
        buf_fmt(&glue, "    e->hdr.rc = 1;\n    e->hdr.fn = (void *)__mxy_lambda%d;\n    e->hdr.drop = __mxy_envdrop%d;\n", id, id);
        for (int i = 0; i < lam->captures.len; i++) {
            Sym *s = lam->captures.items[i];
            buf_fmt(&glue, "    e->%s = %s;\n", s->cname, retain_of(s->type, s->cname));
        }
        buf_put(&glue, "    return &e->hdr;\n}\n");
    } else if (!as_fnptr) {
        buf_fmt(&out.protos, "static mxy_fn __mxy_fnobj%d = {-1, (void *)__mxy_lambda%d, NULL};\n", id, id);
    }
    Node *saved = cur_func;
    Type *saved_ret = cur_ret;
    cur_func = lam;
    cur_ret = ret;
    Buf *b = &out.funcs;
    mark_loc(b, lam->loc);
    buf_fmt(b, "static %s {\n", sig);
    if (!as_fnptr) raw_line(b, 1, "(void)__self;");
    gscopes.len = 0;
    gscope_push(GS_FUNC);
    retain_params(&lam->params, b, 1);
    if (lam->flags & F_EXPRBODY) {
        Node ret_stmt = {0};
        ret_stmt.kind = type_unqual(ret)->kind == TY_VOID ? N_EXPR : N_RETURN;
        ret_stmt.loc = lam->a->loc;
        ret_stmt.a = lam->a;
        gen_stmt(&ret_stmt, b, 1);
    } else {
        gen_block_body(&lam->a->list, b, 1);
    }
    gscope_pop(b, 1);
    buf_put(b, "}\n\n");
    cur_func = saved;
    cur_ret = saved_ret;
}

void gen_lambdas(void) {
    for (int i = 0; i < prog.lambdas.len; i++) gen_lambda(prog.lambdas.items[i]);
}

static int const_init(Node *n) {
    if (!n) return 1;
    switch (n->kind) {
    case N_INT: case N_FLOAT: case N_CHAR: case N_BOOL: case N_NULL: case N_SIZEOF: case N_ALIGNOF:
        return 1;
    case N_STR:
        return 1;
    case N_IDENT:
        return !n->sym || n->sym->kind == S_CONST || n->sym->kind == S_FUNC ||
               (n->builtin == B_ENUM_CTOR && type_unqual(n->type)->simple);
    case N_UNARY:
        if (n->op == P_AMP) return n->a->kind == N_IDENT;
        return const_init(n->a);
    case N_BINARY:
        return !n->builtin && const_init(n->a) && const_init(n->b);
    case N_TERNARY:
        return const_init(n->a) && const_init(n->b) && const_init(n->c);
    case N_CAST:
        return n->op != 1 && const_init(n->a);
    case N_CONV:
        return n->op == CV_FN_TO_CLOSURE || n->op == CV_LAMBDA_FNPTR;
    case N_INITLIST: case N_COMPOUND: {
        Node *list = n->kind == N_COMPOUND ? n->b : n;
        Type *u = type_unqual(n->type);
        if (u && (u->kind == TY_LIST || u->kind == TY_MAP || (u->kind == TY_STRUCT && u->is_ref))) return 0;
        for (int i = 0; i < list->list.len; i++) {
            Node *item = list->list.items[i];
            if (item->kind == N_DESIG) {
                if (!const_init(item->b)) return 0;
            } else if (!const_init(item)) {
                return 0;
            }
        }
        return 1;
    }
    case N_LAMBDA:
        return n->captures.len == 0;
    default:
        return 0;
    }
}

void gen_globals(void) {
    Buf *g = &out.globals;
    for (int i = 0; i < prog.globals.len; i++) {
        Node *v = prog.globals.items[i];
        Sym *s = v->sym;
        if (!s || str_starts(s->cname, "mxy_")) continue;
        if (s->decl != v && (v->flags & F_EXTERN)) continue;
        if (s->decl != v && !v->cond && !v->a) continue;
        Type *t = s->type;
        const char *st = (v->flags & F_EXTERN) && !v->a ? "extern " : (v->flags & F_STATIC) ? "static " : "";
        const char *tl = (v->flags & F_THREADLOCAL) ? "_Thread_local " : "";
        if (v->cond) buf_fmt(g, "#if %s\n", v->cond);
        char *d = cdecl(t, s->cname);
        if (!v->a) {
            buf_fmt(g, "%s%s%s;\n", st, tl, d);
        } else if (const_init(v->a)) {
            Ctx c;
            ctx_init(&c, 0);
            CExpr e = gen_init(v->a, t, &c);
            char *val = e.s;
            Type *u = type_unqual(t);
            if (val[0] == '{' && u->kind != TY_ARRAY && u->kind != TY_STRUCT && !type_is_unknownish(u))
                val = xfmt("(%s)%s", ctype(t), val);
            buf_fmt(g, "%s%s%s = %s;\n", st, tl, d, val);
            ctx_free(&c);
        } else {
            buf_fmt(g, "%s%s%s = {0};\n", st, tl, d);
            Ctx c;
            ctx_init(&c, 1);
            CExpr e = gen_init(v->a, t, &c);
            char *val = managed(t) ? consume(&c, e, t) : borrow(&c, e, t);
            if (val[0] == '{') val = xfmt("(%s)%s", ctype(t), val);
            if (v->cond) buf_fmt(&out.init, "#if %s\n", v->cond);
            buf_put(&out.init, buf_str(&c.pre));
            buf_fmt(&out.init, "    %s = %s;\n", s->cname, val);
            ctx_release(&c, &out.init);
            if (v->cond) buf_put(&out.init, "#endif\n");
            ctx_free(&c);
        }
        if (v->cond) buf_put(g, "#endif\n");
    }
}

void gen_prototypes(void) {
    StrMap done = {0};
    for (int i = 0; i < prog.funcs.len; i++) {
        Node *fn = prog.funcs.items[i];
        if (!fn->sym) continue;
        char *key = xfmt("%s|%s", fn->sym->cname, fn->cond ? fn->cond : "");
        if (strmap_get(&done, key)) continue;
        strmap_put(&done, key, (void *)1);
        if (fn->cond) buf_fmt(&out.protos, "#if %s\n", fn->cond);
        prototype(fn->a ? fn : fn->sym->decl ? fn->sym->decl : fn);
        if (fn->cond) buf_put(&out.protos, "#endif\n");
    }
    strmap_free(&done);
}

void gen_main_wrapper(void) {
    Node *m = prog.main;
    if (!m) return;
    Type *ft = type_unqual(m->sym->type);
    int returns_int = type_unqual(ft->base)->kind != TY_VOID;
    Buf *b = &glue;
    buf_put(b, "int main(int argc, char **argv) {\n    mxy_argc = argc;\n    mxy_argv = argv;\n");
    if (ft->params.len == 2) {
        buf_fmt(b, "    %s__mxy_main(argc, argv);\n", returns_int ? "return " : "");
    } else if (ft->params.len == 1) {
        buf_put(b, "    mxy_list *args = mxy_args();\n");
        buf_fmt(b, "    int code = %s__mxy_main(args);\n", returns_int ? "" : "0;\n    ");
        buf_put(b, "    mxy_list_release(args);\n    return code;\n");
        buf_put(b, "}\n");
        return;
    } else {
        buf_fmt(b, "    %s__mxy_main();\n", returns_int ? "return " : "");
    }
    if (!returns_int) buf_put(b, "    return 0;\n");
    buf_put(b, "}\n");
}

void func_reset(void) {
    buf_free(&glue);
    strmap_free(&thunks);
}
