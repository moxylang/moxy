#include <string.h>

#include "internal.h"

int managed(Type *t) { return t && type_is_managed(t); }

char *ctype(Type *t) {
    need_type(t);
    return type_cdecl(t, "");
}

char *cdecl(Type *t, const char *name) {
    need_type(t);
    return type_cdecl(t, name);
}

char *retain_of(Type *t, const char *e) {
    Type *u = type_unqual(t);
    if (!managed(u)) return xstrdup(e);
    switch (u->kind) {
    case TY_STRING: return xfmt("mxy_str_retain(%s)", e);
    case TY_LIST: return xfmt("mxy_list_retain(%s)", e);
    case TY_MAP: return xfmt("mxy_map_retain(%s)", e);
    case TY_CLOSURE: return xfmt("mxy_fn_retain(%s)", e);
    case TY_FUTURE: return xfmt("mxy_future_retain(%s)", e);
    case TY_STRUCT:
        if (u->is_ref) return xfmt("((%s *)mxy_obj_retain(%s))", u->cname, e);
        /* fallthrough */
    default: {
        char *h = struct_helpers(u);
        char *r = xfmt("__mxy_cp_%s(%s)", h, e);
        free(h);
        return r;
    }
    }
}

char *release_expr(Type *t, const char *e) {
    Type *u = type_unqual(t);
    switch (u->kind) {
    case TY_STRING: return xfmt("mxy_str_release(%s)", e);
    case TY_LIST: return xfmt("mxy_list_release(%s)", e);
    case TY_MAP: return xfmt("mxy_map_release(%s)", e);
    case TY_CLOSURE: return xfmt("mxy_fn_release(%s)", e);
    case TY_FUTURE: return xfmt("mxy_future_release(%s)", e);
    case TY_STRUCT:
        if (u->is_ref) return xfmt("mxy_obj_release(%s)", e);
        /* fallthrough */
    default: {
        char *h = struct_helpers(u);
        char *r = xfmt("__mxy_rl_%s(%s)", h, e);
        free(h);
        return r;
    }
    }
}

void release_into(Buf *b, int indent, Type *t, const char *e) {
    Type *u = type_unqual(t);
    if (!managed(u)) return;
    if (u->kind == TY_ARRAY) {
        char *i = fresh("i");
        char *len = u->len >= 0 ? xfmt("%ld", u->len) : xfmt("(long)(sizeof(%s) / sizeof(%s[0]))", e, e);
        char *elem = xfmt("%s[%s]", e, i);
        char *rel = release_expr(u->base, elem);
        raw_line(b, indent, "for (long %s = 0; %s < %s; %s++) %s;", i, i, len, i, rel);
        free(i);
        free(len);
        free(elem);
        free(rel);
        return;
    }
    char *r = release_expr(u, e);
    raw_line(b, indent, "%s;", r);
    free(r);
}

void ctx_init(Ctx *c, int indent) {
    memset(c, 0, sizeof(*c));
    c->indent = indent;
}

/* store an expression in a fresh temporary; owned temporaries are released
   when the statement finishes */
char *hoist(Ctx *c, const char *e, Type *t, int owned) {
    char *name = fresh("t");
    char *decl = cdecl(t, name);
    raw_line(&c->pre, c->indent, "%s = %s;", decl, e);
    free(decl);
    if (owned && managed(t)) {
        Temp tmp = {xstrdup(name), t};
        vec_push(c->temps, tmp);
    }
    return name;
}

char *borrow(Ctx *c, CExpr e, Type *t) {
    if (e.owned && managed(t)) {
        char *n = hoist(c, e.s, t, 1);
        free(e.s);
        return n;
    }
    return e.s;
}

char *consume(Ctx *c, CExpr e, Type *t) {
    (void)c;
    if (!managed(t) || e.owned) return e.s;
    if (strcmp(e.s, "NULL") == 0) return e.s;
    char *r = retain_of(t, e.s);
    free(e.s);
    return r;
}

void ctx_flush_pre(Ctx *c, Buf *b) {
    if (c->pre.len) buf_put(b, buf_str(&c->pre));
    c->pre.len = 0;
    if (c->pre.data) c->pre.data[0] = '\0';
}

void ctx_release(Ctx *c, Buf *b) {
    for (int i = c->temps.len - 1; i >= 0; i--) release_into(b, c->indent, c->temps.items[i].type, c->temps.items[i].name);
    c->temps.len = 0;
}

void ctx_free(Ctx *c) {
    buf_free(&c->pre);
    for (int i = 0; i < c->temps.len; i++) free(c->temps.items[i].name);
    vec_free(c->temps);
}

static const char *prim_vw(TypeKind k) {
    switch (k) {
    case TY_BOOL: return "&mxy_vw_bool";
    case TY_CHAR: return "&mxy_vw_char";
    case TY_SCHAR: return "&mxy_vw_schar";
    case TY_UCHAR: return "&mxy_vw_uchar";
    case TY_SHORT: return "&mxy_vw_short";
    case TY_USHORT: return "&mxy_vw_ushort";
    case TY_INT: case TY_CENUM: return "&mxy_vw_int";
    case TY_UINT: return "&mxy_vw_uint";
    case TY_LONG: return "&mxy_vw_long";
    case TY_ULONG: return "&mxy_vw_ulong";
    case TY_LLONG: return "&mxy_vw_llong";
    case TY_ULLONG: return "&mxy_vw_ullong";
    case TY_FLOAT: return "&mxy_vw_float";
    case TY_DOUBLE: return "&mxy_vw_double";
    case TY_LDOUBLE: return "&mxy_vw_ldouble";
    case TY_STRING: return "&mxy_vw_str";
    case TY_LIST: return "&mxy_vw_list";
    case TY_MAP: return "&mxy_vw_map";
    case TY_CLOSURE: return "&mxy_vw_fn";
    case TY_FUTURE: return "&mxy_vw_future";
    case TY_VOID: return "&mxy_vw_void";
    case TY_PTR: case TY_NULL: case TY_FUNC: return "&mxy_vw_ptr";
    default: return NULL;
    }
}

char *vw_of(Type *t) {
    Type *u = type_unqual(t);
    const char *p = prim_vw(u->kind);
    if (p) return xstrdup(p);
    if (u->kind == TY_STRUCT && u->is_ref) return xstrdup("&mxy_vw_obj");
    char *h = struct_helpers(u);
    char *r = xfmt("&__mxy_vw_%s", h);
    free(h);
    return r;
}
