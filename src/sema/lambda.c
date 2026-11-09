#include "diag/diag.h"
#include "sema.h"

Type *check_init(Node **slot, Type *t);

static void declare_params(ParamList *params, FnCtx *ctx) {
    for (int i = 0; i < params->len; i++) {
        Param *p = &params->items[i];
        if (!p->name) continue;
        Sym *s = sym_new(S_VAR, p->name, p->loc);
        Type *t = resolve_type(p->type);
        Type *u = type_unqual(t);
        if (u->kind == TY_ARRAY) t = type_ptr(u->base);
        else if (u->kind == TY_FUNC) t = type_ptr(u);
        s->type = t;
        s->param = 1;
        s->owner = ctx;
        s->decl = NULL;
        p->sym = s;
        scope_declare(cur_scope, s);
    }
}

Type *check_lambda(Node *n, Type *expected) {
    Type *ex = type_unqual(expected);
    Type *shape = NULL;
    if (ex && ex->kind == TY_CLOSURE) shape = ex;
    else if (ex && ex->kind == TY_PTR && type_unqual(ex->base)->kind == TY_FUNC) shape = type_unqual(ex->base);
    TypeList params = {0};
    for (int i = 0; i < n->params.len; i++) vec_push(params, resolve_type(n->params.items[i].type));
    if (shape) {
        if (shape->params.len != params.len) {
            diag_error(n->loc, "this lambda takes %d parameter%s but %d %s expected", params.len,
                       params.len == 1 ? "" : "s", shape->params.len, shape->params.len == 1 ? "is" : "are");
        } else {
            for (int i = 0; i < params.len; i++) {
                if (type_eq(params.items[i], shape->params.items[i]) ||
                    type_is_unknownish(shape->params.items[i]))
                    continue;
                char *a = type_str(params.items[i]), *b = type_str(shape->params.items[i]);
                diag_error(n->params.items[i].loc, "lambda parameter is %s but %s is expected", a, b);
                free(a);
                free(b);
            }
        }
    }
    FnCtx *ctx = xcalloc(1, sizeof(FnCtx));
    ctx->node = n;
    ctx->outer = cur_fn;
    ctx->is_lambda = 1;
    Type *want = shape ? shape->base : NULL;
    if (want && type_is_unknownish(want)) want = NULL;
    ctx->ret = want;
    ctx->infer_ret = want == NULL;
    FnCtx *saved = cur_fn;
    cur_fn = ctx;
    scope_push(SC_FUNC);
    declare_params(&n->params, ctx);
    if (n->flags & F_EXPRBODY) {
        Type *t = check_init(&n->a, ctx->ret);
        if (ctx->infer_ret) {
            ctx->ret = type_unqual(t)->kind == TY_NULL ? ty_unknown : t;
        } else if (type_unqual(ctx->ret)->kind != TY_VOID) {
            coerce(&n->a, ctx->ret, "lambda result");
        }
    } else {
        check_block_list(&n->a->list);
        if (!ctx->ret) ctx->ret = ty_void;
    }
    scope_pop();
    cur_fn = saved;
    n->id = prog.lambdas.len;
    vec_push(prog.lambdas, n);
    n->flags |= ctx->has_cleanup ? F_BODY : 0;
    return type_closure(ctx->ret, params);
}

void check_function(Node *fn) {
    Sym *sym = fn->sym;
    Type *ft = type_unqual(sym->type);
    FnCtx *ctx = xcalloc(1, sizeof(FnCtx));
    ctx->node = fn;
    ctx->ret = ft ? ft->base : ty_void;
    FnCtx *saved = cur_fn;
    Scope *saved_scope = cur_scope;
    cur_fn = ctx;
    cur_scope = ns_scope(fn->ns);
    scope_push(SC_FUNC);
    declare_params(&fn->params, ctx);
    check_block_list(&fn->a->list);
    if (ctx->has_goto && ctx->has_cleanup)
        diag_warn(fn->loc, "'%s' uses goto together with managed values or defer; goto skips automatic cleanup",
                  fn->name);
    pop_checked();
    cur_scope = saved_scope;
    cur_fn = saved;
}
