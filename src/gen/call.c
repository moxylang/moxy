#include <string.h>

#include "diag/diag.h"
#include "internal.h"

static CExpr own(char *s) { return (CExpr){s, 1}; }
static CExpr bor(char *s) { return (CExpr){s, 0}; }

static int simple_name(const char *s) {
    for (const char *p = s; *p; p++)
        if (!(*p == '_' || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')))
            return 0;
    return 1;
}

/* an argument value: borrowed for managed types, compound literal for braces */
static char *arg_value(Node *a, Type *pt, Ctx *c) {
    CExpr e = gen_init(a, pt ? pt : a->type, c);
    Type *t = pt ? pt : a->type;
    if (e.s[0] == '{' && t && !type_is_unknownish(t)) e.s = xfmt("(%s)%s", ctype(t), e.s);
    return borrow(c, e, t);
}

static char *arg_list(NodeList *args, TypeList *params, int first, Ctx *c) {
    Buf b = {0};
    for (int i = first; i < args->len; i++) {
        Type *pt = params && i - first < params->len ? params->items[i - first] : NULL;
        if (b.len) buf_put(&b, ", ");
        buf_put(&b, arg_value(args->items[i], pt, c));
    }
    return buf_take(&b);
}

char *closure_invoke(const char *f, Type *ft, char **args, int nargs, Loc loc, Ctx *c) {
    Type *u = type_unqual(ft);
    char *fv = simple_name(f) ? xstrdup(f) : hoist(c, f, ft, 0);
    Buf sig = {0};
    char *ret = ctype(u->base);
    buf_fmt(&sig, "%s (*)(mxy_fn *", ret);
    for (int i = 0; i < u->params.len; i++) buf_fmt(&sig, ", %s", ctype(u->params.items[i]));
    buf_put(&sig, ")");
    Buf call = {0};
    buf_fmt(&call, "((%s)mxy_fn_check(%s, %s)->fn)(%s", buf_str(&sig), fv, loc_lit(loc), fv);
    for (int i = 0; i < nargs; i++) buf_fmt(&call, ", %s", args[i]);
    buf_put(&call, ")");
    buf_free(&sig);
    return buf_take(&call);
}

CExpr gen_closure_call(const char *fexpr, Type *ft, NodeList *args, int first, Ctx *c, Loc loc) {
    Type *u = type_unqual(ft);
    int n = args->len - first;
    char **vals = xcalloc((size_t)n + 1, sizeof(char *));
    for (int i = 0; i < n; i++) {
        Type *pt = i < u->params.len ? u->params.items[i] : NULL;
        vals[i] = arg_value(args->items[first + i], pt, c);
    }
    char *call = closure_invoke(fexpr, ft, vals, n, loc, c);
    return (CExpr){call, managed(u->base)};
}

static char *source_line(Loc loc) {
    const SrcFile *f = diag_file(loc.file);
    if (!f || !f->text) return xstrdup("assert");
    const char *p = f->text;
    for (int line = 1; *p && line < loc.line; p++)
        if (*p == '\n') line++;
    while (*p == ' ' || *p == '\t') p++;
    const char *e = p;
    while (*e && *e != '\n') e++;
    while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == ';' || e[-1] == '\r')) e--;
    Buf b = {0};
    for (const char *q = p; q < e; q++) {
        if (*q == '"' || *q == '\\') buf_putc(&b, '\\');
        buf_putc(&b, *q);
    }
    return buf_take(&b);
}

static CExpr gen_print(Node *n, Ctx *c) {
    char *b = fresh("p");
    raw_line(&c->pre, c->indent, "mxy_buf %s = {0};", b);
    char *bp = xfmt("&%s", b);
    for (int i = 0; i < n->list.len; i++) {
        if (i) raw_line(&c->pre, c->indent, "mxy_buf_putc(%s, ' ');", bp);
        fmt_into(c, bp, n->list.items[i], 0);
    }
    raw_line(&c->pre, c->indent, "mxy_buf_putc(%s, '\\n');", bp);
    raw_line(&c->pre, c->indent, "mxy_buf_flush(%s, stdout);", bp);
    return bor(xstrdup("((void)0)"));
}

static CExpr gen_assert(Node *n, Ctx *c) {
    char *cond = gen_value(n->list.items[0], c);
    char *msg = n->list.len > 1 ? gen_borrow(n->list.items[1], c) : xstrdup("NULL");
    raw_line(&c->pre, c->indent, "if (!(%s)) mxy_assert_fail(%s, \"%s\", %s);", cond, loc_lit(n->loc),
             source_line(n->loc), msg);
    return bor(xstrdup("((void)0)"));
}

static CExpr gen_panic(Node *n, Ctx *c) {
    char *msg = gen_borrow(n->list.items[0], c);
    raw_line(&c->pre, c->indent, "mxy_panic(%s, \"%%s\", %s ? %s : \"\");", loc_lit(n->loc), msg, msg);
    return bor(xstrdup("((void)0)"));
}

/* a temporary holding a value whose address is passed to the runtime */
static char *addr_of_value(Node *a, Type *t, Ctx *c, int *move) {
    CExpr e = gen_init(a, t, c);
    if (e.s[0] == '{') e.s = xfmt("(%s)%s", ctype(t), e.s);
    *move = e.owned && managed(t);
    return hoist(c, e.s, t, 0);
}

static char *key_temp(Node *a, Type *t, Ctx *c) {
    CExpr e = gen_init(a, t, c);
    if (e.s[0] == '{') e.s = xfmt("(%s)%s", ctype(t), e.s);
    return hoist(c, e.s, t, e.owned && managed(t));
}

static CExpr str_method(Node *n, Node *recv, Ctx *c) {
    char *s = gen_borrow(recv, c);
    char *loc = loc_lit(n->loc);
    NodeList *a = &n->list;
    switch (n->builtin) {
    case B_STR_LEN: return bor(xfmt("mxy_str_len(%s)", s));
    case B_STR_IS_EMPTY: return bor(xfmt("(mxy_str_len(%s) == 0)", s));
    case B_STR_CONTAINS: return bor(xfmt("mxy_str_contains(%s, %s)", s, gen_borrow(a->items[0], c)));
    case B_STR_STARTS: return bor(xfmt("mxy_str_starts_with(%s, %s)", s, gen_borrow(a->items[0], c)));
    case B_STR_ENDS: return bor(xfmt("mxy_str_ends_with(%s, %s)", s, gen_borrow(a->items[0], c)));
    case B_STR_FIND: return bor(xfmt("mxy_str_find(%s, %s)", s, gen_borrow(a->items[0], c)));
    case B_STR_SLICE:
        return own(xfmt("mxy_str_slice(%s, %s, %s, %s)", s, gen_value(a->items[0], c), gen_value(a->items[1], c), loc));
    case B_STR_SPLIT: return own(xfmt("mxy_str_split(%s, %s)", s, gen_borrow(a->items[0], c)));
    case B_STR_LINES: return own(xfmt("mxy_str_lines(%s)", s));
    case B_STR_TRIM: return own(xfmt("mxy_str_trim(%s)", s));
    case B_STR_UPPER: return own(xfmt("mxy_str_upper(%s)", s));
    case B_STR_LOWER: return own(xfmt("mxy_str_lower(%s)", s));
    case B_STR_REPLACE:
        return own(xfmt("mxy_str_replace(%s, %s, %s)", s, gen_borrow(a->items[0], c), gen_borrow(a->items[1], c)));
    case B_STR_REPEAT: return own(xfmt("mxy_str_repeat(%s, %s)", s, gen_value(a->items[0], c)));
    case B_STR_CHARS: return own(xfmt("mxy_str_chars(%s)", s));
    case B_STR_AT: return bor(xfmt("mxy_str_at(%s, %s, %s)", s, gen_value(a->items[0], c), loc));
    case B_STR_TO_INT: case B_STR_TO_FLOAT: {
        int is_int = n->builtin == B_STR_TO_INT;
        char *R = ctype(n->type);
        char *v = fresh("v"), *r = fresh("r");
        raw_line(&c->pre, c->indent, "%s %s = 0;", is_int ? "long" : "double", v);
        raw_line(&c->pre, c->indent, "%s %s = mxy_str_parse_%s(%s, &%s) ? (%s){ .ok = 1, .val = %s } : (%s){ .ok = 0, .err = %s };",
                 R, r, is_int ? "long" : "double", s, v, R, v, R, lit_ref(is_int ? "not a valid integer" : "not a valid number"));
        return own(r);
    }
    default: return bor(xstrdup("0"));
    }
}

static CExpr list_method(Node *n, Node *recv, Ctx *c) {
    Type *lt = type_unqual(recv->type);
    Type *elem = lt->base;
    char *vw = vw_of(elem);
    char *et = ctype(elem);
    char *loc = loc_lit(n->loc);
    NodeList *a = &n->list;
    int move = 0;
    switch (n->builtin) {
    case B_LIST_LEN: return bor(xfmt("mxy_list_len(%s)", gen_borrow(recv, c)));
    case B_LIST_IS_EMPTY: return bor(xfmt("(mxy_list_len(%s) == 0)", gen_borrow(recv, c)));
    case B_LIST_PUSH: {
        char *l = gen_lvalue(recv, c);
        char *t = addr_of_value(a->items[0], elem, c, &move);
        return bor(xfmt("mxy_list_push(&%s, %s, &%s, %d)", l, vw, t, move));
    }
    case B_LIST_INSERT: {
        char *l = gen_lvalue(recv, c);
        char *i = gen_value(a->items[0], c);
        char *t = addr_of_value(a->items[1], elem, c, &move);
        return bor(xfmt("mxy_list_insert(&%s, %s, %s, &%s, %d, %s)", l, vw, i, t, move, loc));
    }
    case B_LIST_POP: case B_LIST_REMOVE: {
        char *l = gen_lvalue(recv, c);
        char *r = fresh("t");
        char *d = cdecl(elem, r);
        raw_line(&c->pre, c->indent, "%s;", d);
        if (n->builtin == B_LIST_POP)
            raw_line(&c->pre, c->indent, "mxy_list_pop(&%s, %s, &%s, %s);", l, vw, r, loc);
        else
            raw_line(&c->pre, c->indent, "mxy_list_remove(&%s, %s, %s, &%s, %s);", l, vw, gen_value(a->items[0], c), r,
                     loc);
        return own(r);
    }
    case B_LIST_CLEAR: return bor(xfmt("mxy_list_clear(&%s)", gen_lvalue(recv, c)));
    case B_LIST_REVERSE: return bor(xfmt("mxy_list_reverse(&%s, %s)", gen_lvalue(recv, c), vw));
    case B_LIST_SORT: return bor(xfmt("mxy_list_sort(&%s, %s)", gen_lvalue(recv, c), vw));
    case B_LIST_CONTAINS: case B_LIST_INDEX_OF: {
        char *l = gen_borrow(recv, c);
        char *t = key_temp(a->items[0], elem, c);
        if (n->builtin == B_LIST_CONTAINS) return bor(xfmt("(mxy_list_index_of(%s, %s, &%s) >= 0)", l, vw, t));
        return bor(xfmt("mxy_list_index_of(%s, %s, &%s)", l, vw, t));
    }
    case B_LIST_SLICE: {
        char *l = gen_borrow(recv, c);
        return own(xfmt("mxy_list_slice(%s, %s, %s, %s, %s)", l, vw, gen_value(a->items[0], c), gen_value(a->items[1], c), loc));
    }
    case B_LIST_FIRST: return bor(xfmt("(*(%s *)mxy_list_at(%s, 0, %s))", et, gen_borrow(recv, c), loc));
    case B_LIST_LAST: {
        char *l = hoist(c, gen_borrow(recv, c), lt, 0);
        return bor(xfmt("(*(%s *)mxy_list_at(%s, mxy_list_len(%s) - 1, %s))", et, l, l, loc));
    }
    case B_LIST_JOIN: return own(xfmt("mxy_list_join(%s, %s)", gen_borrow(recv, c), gen_borrow(a->items[0], c)));
    case B_LIST_MAP: case B_LIST_FILTER: case B_LIST_EACH: {
        char *l = hoist(c, gen_borrow(recv, c), lt, 0);
        Node *fn = a->items[0];
        char *f = hoist(c, gen_borrow(fn, c), fn->type, 0);
        Type *ft = type_unqual(fn->type);
        Type *rt = n->builtin == B_LIST_MAP ? type_unqual(n->type)->base : NULL;
        char *r = fresh("r"), *i = fresh("i"), *e = fresh("e");
        if (n->builtin != B_LIST_EACH) raw_line(&c->pre, c->indent, "mxy_list *%s = NULL;", r);
        raw_line(&c->pre, c->indent, "for (int64_t %s = 0; %s < mxy_list_len(%s); %s++) {", i, i, l, i);
        raw_line(&c->pre, c->indent + 1, "%s = *(%s *)MXY_AT(%s, %s);", cdecl(elem, e), et, l, i);
        Ctx inner;
        ctx_init(&inner, c->indent + 1);
        char *call = closure_invoke(f, ft, &e, 1, n->loc, &inner);
        ctx_flush_pre(&inner, &c->pre);
        if (n->builtin == B_LIST_MAP) {
            char *u = fresh("u");
            raw_line(&c->pre, c->indent + 1, "%s = %s;", cdecl(rt, u), call);
            raw_line(&c->pre, c->indent + 1, "mxy_list_push(&%s, %s, &%s, 1);", r, vw_of(rt), u);
        } else if (n->builtin == B_LIST_FILTER) {
            raw_line(&c->pre, c->indent + 1, "if (%s) mxy_list_push(&%s, %s, &%s, 0);", call, r, vw, e);
        } else {
            Type *ret = type_unqual(ft)->base;
            if (managed(ret)) raw_line(&c->pre, c->indent + 1, "%s;", release_expr(ret, call));
            else raw_line(&c->pre, c->indent + 1, "(void)%s;", call);
        }
        ctx_release(&inner, &c->pre);
        ctx_free(&inner);
        raw_line(&c->pre, c->indent, "}");
        if (n->builtin == B_LIST_EACH) return bor(xstrdup("((void)0)"));
        return own(r);
    }
    default: return bor(xstrdup("0"));
    }
}

static CExpr map_method(Node *n, Node *recv, Ctx *c) {
    Type *mt = type_unqual(recv->type);
    char *kvw = vw_of(mt->key), *vvw = vw_of(mt->base);
    char *vt = ctype(mt->base);
    char *loc = loc_lit(n->loc);
    NodeList *a = &n->list;
    switch (n->builtin) {
    case B_MAP_LEN: return bor(xfmt("mxy_map_len(%s)", gen_borrow(recv, c)));
    case B_MAP_IS_EMPTY: return bor(xfmt("(mxy_map_len(%s) == 0)", gen_borrow(recv, c)));
    case B_MAP_SET: {
        char *m = gen_lvalue(recv, c);
        int km = 0, vm = 0;
        char *k = addr_of_value(a->items[0], mt->key, c, &km);
        char *v = addr_of_value(a->items[1], mt->base, c, &vm);
        return bor(xfmt("mxy_map_set(&%s, %s, %s, &%s, &%s, %d, %d)", m, kvw, vvw, k, v, km, vm));
    }
    case B_MAP_GET: {
        char *m = gen_borrow(recv, c);
        char *k = key_temp(a->items[0], mt->key, c);
        return bor(xfmt("(*(%s *)mxy_map_get(%s, %s, &%s, %s))", vt, m, kvw, k, loc));
    }
    case B_MAP_GET_OR: {
        char *m = gen_borrow(recv, c);
        char *k = key_temp(a->items[0], mt->key, c);
        char *p = fresh("p");
        raw_line(&c->pre, c->indent, "void *%s = mxy_map_lookup(%s, %s, &%s);", p, m, kvw, k);
        char *found = xfmt("(*(%s *)%s)", vt, p);
        if (managed(mt->base)) {
            char *d = gen_consume(a->items[1], c);
            return own(xfmt("(%s ? %s : %s)", p, retain_of(mt->base, found), d));
        }
        return bor(xfmt("(%s ? %s : %s)", p, found, gen_value(a->items[1], c)));
    }
    case B_MAP_HAS: {
        char *m = gen_borrow(recv, c);
        char *k = key_temp(a->items[0], mt->key, c);
        return bor(xfmt("mxy_map_has(%s, %s, &%s)", m, kvw, k));
    }
    case B_MAP_REMOVE: {
        char *m = gen_lvalue(recv, c);
        char *k = key_temp(a->items[0], mt->key, c);
        return bor(xfmt("mxy_map_remove(&%s, %s, %s, &%s)", m, kvw, vvw, k));
    }
    case B_MAP_KEYS: return own(xfmt("mxy_map_keys(%s, %s)", gen_borrow(recv, c), kvw));
    case B_MAP_VALUES: return own(xfmt("mxy_map_values(%s, %s)", gen_borrow(recv, c), vvw));
    case B_MAP_CLEAR: return bor(xfmt("mxy_map_clear(&%s)", gen_lvalue(recv, c)));
    default: return bor(xstrdup("0"));
    }
}

static CExpr res_method(Node *n, Node *recv, Ctx *c) {
    Type *rt = type_unqual(recv->type);
    char *r = hoist(c, gen_borrow(recv, c), rt, 0);
    switch (n->builtin) {
    case B_RES_IS_OK: return bor(xfmt("(%s.ok)", r));
    case B_RES_IS_ERR: return bor(xfmt("(!%s.ok)", r));
    case B_RES_UNWRAP:
        raw_line(&c->pre, c->indent, "if (!%s.ok) mxy_panic(%s, \"unwrap of Err: %%s\", %s.err ? %s.err : \"\");", r,
                 loc_lit(n->loc), r, r);
        if (type_unqual(rt->base)->kind == TY_VOID) return bor(xstrdup("((void)0)"));
        return bor(xfmt("%s.val", r));
    case B_RES_UNWRAP_OR: {
        char *val = xfmt("%s.val", r);
        if (managed(rt->base)) {
            char *d = gen_consume(n->list.items[0], c);
            return own(xfmt("(%s.ok ? %s : %s)", r, retain_of(rt->base, val), d));
        }
        return bor(xfmt("(%s.ok ? %s : %s)", r, val, gen_value(n->list.items[0], c)));
    }
    case B_RES_ERROR: return bor(xfmt("(%s.ok ? %s : %s.err)", r, lit_ref(""), r));
    default: return bor(xstrdup("0"));
    }
}

CExpr gen_call(Node *n, Ctx *c) {
    Node *callee = n->a;
    switch (n->builtin) {
    case B_PRINT: return gen_print(n, c);
    case B_ASSERT: return gen_assert(n, c);
    case B_PANIC: return gen_panic(n, c);
    case B_ENUM_CTOR: return gen_enum_ctor(n, c);
    case B_FUNC: case B_ASYNC_CALL: {
        Sym *s = callee->sym;
        Type *ft = type_unqual(s->type);
        char *args = arg_list(&n->list, &ft->params, 0, c);
        Type *ret = n->type;
        return (CExpr){xfmt("%s(%s)", s->cname, args), managed(ret)};
    }
    case B_CLOSURE_CALL: {
        char *f = gen_borrow(callee, c);
        return gen_closure_call(f, callee->type, &n->list, 0, c, n->loc);
    }
    case B_FNPTR_CALL: {
        char *f = gen_value(callee, c);
        Type *ct = type_unqual(callee->type);
        Type *fn = ct->kind == TY_FUNC ? ct : type_unqual(ct->base);
        char *args = arg_list(&n->list, &fn->params, 0, c);
        return (CExpr){xfmt("(%s)(%s)", f, args), managed(n->type)};
    }
    case B_UNKNOWN_CALL: {
        char *f = callee->kind == N_IDENT && !callee->sym ? xstrdup(callee->name) : gen_value(callee, c);
        char *args = arg_list(&n->list, NULL, 0, c);
        return bor(xfmt("%s(%s)", f, args));
    }
    default:
        break;
    }
    if (callee->kind == N_MEMBER) {
        Node *recv = callee->a;
        Type *rt = type_unqual(recv->type);
        switch (rt->kind) {
        case TY_STRING: return str_method(n, recv, c);
        case TY_LIST: return list_method(n, recv, c);
        case TY_MAP: return map_method(n, recv, c);
        case TY_RESULT: return res_method(n, recv, c);
        default: break;
        }
    }
    char *f = gen_value(callee, c);
    char *args = arg_list(&n->list, NULL, 0, c);
    return bor(xfmt("%s(%s)", f, args));
}

CExpr gen_member(Node *n, Ctx *c) {
    if (n->builtin == B_STR_LEN) return bor(xfmt("mxy_str_len(%s)", gen_borrow(n->a, c)));
    if (n->builtin == B_LIST_LEN) return bor(xfmt("mxy_list_len(%s)", gen_borrow(n->a, c)));
    if (n->builtin == B_MAP_LEN) return bor(xfmt("mxy_map_len(%s)", gen_borrow(n->a, c)));
    Type *ot = type_unqual(n->a->type);
    char *o = gen_borrow(n->a, c);
    int arrow = n->op == P_ARROW || (ot && ((ot->kind == TY_STRUCT && ot->is_ref) || ot->kind == TY_PTR));
    if (ot && ot->kind == TY_PTR && n->op == P_DOT) arrow = 0;
    char *access = xfmt("%s%s%s", o, arrow ? "->" : ".", n->name);
    if (n->flags & F_WEAK) {
        Type *ft = type_unqual(n->type);
        return own(xfmt("((%s *)mxy_obj_weak_load(%s))", ft->cname, access));
    }
    return bor(access);
}
