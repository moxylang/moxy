#include <string.h>

#include "internal.h"

static CExpr bor(char *s) { return (CExpr){s, 0}; }

static int is_ref_or_ptr(Type *t) {
    Type *u = type_unqual(t);
    return u && (u->kind == TY_PTR || (u->kind == TY_STRUCT && u->is_ref));
}

static char *key_temp(Node *a, Type *t, Ctx *c) {
    CExpr e = gen_init(a, t, c);
    return hoist(c, e.s, t, e.owned && managed(t));
}

char *gen_lvalue(Node *n, Ctx *c) {
    switch (n->kind) {
    case N_IDENT:
        if (n->sym) return sym_ref(n->sym);
        return xstrdup(n->name);
    case N_UNARY:
        if (n->op == P_STAR) return xfmt("(*%s)", gen_value(n->a, c));
        break;
    case N_INDEX: {
        Type *u = type_unqual(n->a->type);
        char *loc = loc_lit(n->loc);
        if (u->kind == TY_LIST) {
            char *l = gen_lvalue(n->a, c);
            char *i = gen_value(n->b, c);
            return xfmt("(*(%s *)mxy_list_at_mut(&%s, %s, %s, %s))", ctype(u->base), l, vw_of(u->base), i, loc);
        }
        if (u->kind == TY_MAP) {
            char *m = gen_lvalue(n->a, c);
            char *k = key_temp(n->b, u->key, c);
            return xfmt("(*(%s *)mxy_map_get_mut(&%s, %s, %s, &%s, %s))", ctype(u->base), m, vw_of(u->key),
                        vw_of(u->base), k, loc);
        }
        return xfmt("%s[%s]", gen_value(n->a, c), gen_value(n->b, c));
    }
    case N_MEMBER: {
        if (is_ref_or_ptr(n->a->type) || n->op == P_ARROW) return xfmt("%s->%s", gen_value(n->a, c), n->name);
        Type *u = type_unqual(n->a->type);
        if (u && type_is_unknownish(u) && n->a->kind != N_IDENT) return xfmt("%s.%s", gen_value(n->a, c), n->name);
        return xfmt("%s.%s", gen_lvalue(n->a, c), n->name);
    }
    default:
        break;
    }
    return gen_value(n, c);
}

static int side_effect_free(Node *n) {
    return n->kind == N_IDENT || (n->kind == N_MEMBER && side_effect_free(n->a) && !is_ref_or_ptr(n->a->type));
}

/* stores an owned value into a managed slot, releasing what was there */
static CExpr store_managed(Node *target, CExpr val, Type *t, Ctx *c) {
    char *v = consume(c, val, t);
    if (v[0] == '{') v = xfmt("(%s)%s", ctype(t), v);
    char *nv = fresh("n");
    raw_line(&c->pre, c->indent, "%s = %s;", cdecl(t, nv), v);
    if (side_effect_free(target)) {
        char *lv = gen_lvalue(target, c);
        raw_line(&c->pre, c->indent, "%s;", release_expr(t, lv));
        raw_line(&c->pre, c->indent, "%s = %s;", lv, nv);
        return bor(lv);
    }
    char *p = fresh("p");
    char *lv = gen_lvalue(target, c);
    raw_line(&c->pre, c->indent, "%s = &%s;", cdecl(type_ptr(t), p), lv);
    raw_line(&c->pre, c->indent, "%s;", release_expr(t, xfmt("*%s", p)));
    raw_line(&c->pre, c->indent, "*%s = %s;", p, nv);
    return bor(xfmt("(*%s)", p));
}

static CExpr assign_index(Node *n, Ctx *c) {
    Node *target = n->a;
    Type *u = type_unqual(target->a->type);
    char *loc = loc_lit(n->loc);
    if (u->kind == TY_MAP) {
        char *m = gen_lvalue(target->a, c);
        CExpr k = gen_init(target->b, u->key, c);
        int km = k.owned && managed(u->key);
        char *kt = hoist(c, k.s, u->key, 0);
        CExpr v = gen_init(n->b, u->base, c);
        if (v.s[0] == '{') v.s = xfmt("(%s)%s", ctype(u->base), v.s);
        int vm = v.owned && managed(u->base);
        char *vt = hoist(c, v.s, u->base, 0);
        raw_line(&c->pre, c->indent, "mxy_map_set(&%s, %s, %s, &%s, &%s, %d, %d);", m, vw_of(u->key), vw_of(u->base),
                 kt, vt, km, vm);
        return bor(vt);
    }
    Type *elem = u->base;
    char *l = gen_lvalue(target->a, c);
    char *i = gen_value(target->b, c);
    CExpr v = gen_init(n->b, elem, c);
    if (v.s[0] == '{') v.s = xfmt("(%s)%s", ctype(elem), v.s);
    if (!managed(elem)) {
        return bor(xfmt("((*(%s *)mxy_list_at_mut(&%s, %s, %s, %s)) = %s)", ctype(elem), l, vw_of(elem), i, loc, v.s));
    }
    int move = v.owned;
    char *vt = hoist(c, v.s, elem, 0);
    raw_line(&c->pre, c->indent, "mxy_list_set(&%s, %s, %s, &%s, %d, %s);", l, vw_of(elem), i, vt, move, loc);
    return bor(vt);
}

CExpr gen_assign(Node *n, Ctx *c) {
    Node *target = n->a;
    Type *t = target->type;
    if (n->op == P_ASSIGN) {
        if (target->kind == N_INDEX) {
            Type *u = type_unqual(target->a->type);
            if (u->kind == TY_MAP || (u->kind == TY_LIST && managed(u->base))) return assign_index(n, c);
        }
        if (target->flags & F_WEAK) {
            char *v = gen_borrow(n->b, c);
            char *nv = hoist(c, v, t, 0);
            char *lv = gen_lvalue(target, c);
            raw_line(&c->pre, c->indent, "mxy_obj_weak_retain(%s);", nv);
            raw_line(&c->pre, c->indent, "mxy_obj_weak_release(%s);", lv);
            raw_line(&c->pre, c->indent, "%s = %s;", lv, nv);
            return bor(nv);
        }
        if (managed(t)) {
            CExpr v = gen_init(n->b, t, c);
            return store_managed(target, v, t, c);
        }
        CExpr v = gen_init(n->b, t, c);
        char *val = v.s;
        if (val[0] == '{') val = xfmt("(%s)%s", ctype(t), val);
        char *lv = gen_lvalue(target, c);
        return bor(xfmt("(%s = %s)", lv, val));
    }
    if (n->builtin == B_STR_CONCAT || n->builtin == B_LIST_CONCAT) {
        char *lv = gen_lvalue(target, c);
        char *p = fresh("p");
        raw_line(&c->pre, c->indent, "%s = &%s;", cdecl(type_ptr(t), p), lv);
        char *rhs = gen_borrow(n->b, c);
        char *nv = fresh("n");
        if (n->builtin == B_STR_CONCAT)
            raw_line(&c->pre, c->indent, "mxy_str %s = mxy_str_concat(*%s, %s);", nv, p, rhs);
        else
            raw_line(&c->pre, c->indent, "mxy_list *%s = mxy_list_concat(*%s, %s, %s);", nv, p, rhs,
                     vw_of(type_unqual(t)->base));
        raw_line(&c->pre, c->indent, "%s;", release_expr(t, xfmt("*%s", p)));
        raw_line(&c->pre, c->indent, "*%s = %s;", p, nv);
        return bor(xfmt("(*%s)", p));
    }
    char *lv = gen_lvalue(target, c);
    char *v = gen_value(n->b, c);
    return bor(xfmt("(%s %s %s)", lv, tok_spell((TokKind)n->op), v));
}
