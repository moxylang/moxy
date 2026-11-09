#include <string.h>

#include "internal.h"

static StrMap made;

char *result_name(Type *t) {
    char *m = type_mangle(type_unqual(t)->base);
    char *r = xfmt("mxy_res_%s", m);
    free(m);
    return r;
}

static void field_ops(Buf *b, Type *ft, const char *expr, int retain) {
    Type *u = type_unqual(ft);
    if (!managed(u)) return;
    if (u->kind == TY_ARRAY) {
        char *i = fresh("i");
        char *len = u->len >= 0 ? xfmt("%ld", u->len) : xfmt("(long)(sizeof(%s) / sizeof(%s[0]))", expr, expr);
        char *elem = xfmt("%s[%s]", expr, i);
        buf_fmt(b, "    for (long %s = 0; %s < %s; %s++) ", i, i, len, i);
        if (retain) {
            char *r = retain_of(u->base, elem);
            buf_fmt(b, "%s = %s;\n", elem, r);
            free(r);
        } else {
            char *r = release_expr(u->base, elem);
            buf_fmt(b, "%s;\n", r);
            free(r);
        }
        free(i);
        free(len);
        free(elem);
        return;
    }
    if (retain) {
        char *r = retain_of(u, expr);
        buf_fmt(b, "    %s = %s;\n", expr, r);
        free(r);
    } else {
        char *r = release_expr(u, expr);
        buf_fmt(b, "    %s;\n", r);
        free(r);
    }
}

static int simple_field(Node *f) {
    return f->name && !f->a && type_unqual(f->type)->kind != TY_ARRAY;
}

static int plain_struct(Type *u) {
    if (!u->decl || u->is_union) return 1;
    for (int i = 0; i < u->decl->list.len; i++) {
        Node *f = u->decl->list.items[i];
        if (f->kind == N_FIELD && !simple_field(f) && !managed(f->type)) return 1;
    }
    return 0;
}

static void gen_struct_value(Type *u, const char *m, Buf *b) {
    const char *T = u->cname;
    int is_managed = managed(u);
    if (is_managed) {
        buf_fmt(b, "static %s __mxy_cp_%s(%s v) {\n", T, m, T);
        for (int i = 0; i < u->decl->list.len; i++) {
            Node *f = u->decl->list.items[i];
            if (f->kind != N_FIELD || !f->name) continue;
            char *e = xfmt("v.%s", f->name);
            field_ops(b, f->type, e, 1);
            free(e);
        }
        buf_put(b, "    return v;\n}\n");
        buf_fmt(b, "static void __mxy_rl_%s(%s v) {\n", m, T);
        for (int i = 0; i < u->decl->list.len; i++) {
            Node *f = u->decl->list.items[i];
            if (f->kind != N_FIELD || !f->name) continue;
            char *e = xfmt("v.%s", f->name);
            field_ops(b, f->type, e, 0);
            free(e);
        }
        buf_put(b, "}\n");
    }
    int plain = plain_struct(u);
    buf_fmt(b, "static bool __mxy_vweq_%s(const void *a, const void *b) {\n", m);
    if (plain) {
        buf_fmt(b, "    return memcmp(a, b, sizeof(%s)) == 0;\n}\n", T);
    } else {
        buf_fmt(b, "    const %s *x = (const %s *)a, *y = (const %s *)b;\n", T, T, T);
        for (int i = 0; i < u->decl->list.len; i++) {
            Node *f = u->decl->list.items[i];
            if (f->kind != N_FIELD) continue;
            char *vw = vw_of(f->type);
            buf_fmt(b, "    if (!mxy_vw_eq(%s, &x->%s, &y->%s)) return false;\n", vw, f->name, f->name);
            free(vw);
        }
        buf_put(b, "    (void)x; (void)y;\n    return true;\n}\n");
    }
    buf_fmt(b, "static uint64_t __mxy_vwhash_%s(const void *p) {\n", m);
    if (plain) {
        buf_fmt(b, "    return mxy_hash_bytes(p, sizeof(%s));\n}\n", T);
    } else {
        buf_fmt(b, "    const %s *x = (const %s *)p;\n    uint64_t h = 17;\n", T, T);
        for (int i = 0; i < u->decl->list.len; i++) {
            Node *f = u->decl->list.items[i];
            if (f->kind != N_FIELD) continue;
            char *vw = vw_of(f->type);
            buf_fmt(b, "    h = h * 31 + mxy_vw_hash(%s, &x->%s);\n", vw, f->name);
            free(vw);
        }
        buf_put(b, "    (void)x;\n    return h;\n}\n");
    }
    buf_fmt(b, "static void __mxy_vwfmt_%s(mxy_buf *b, const void *p, int nested) {\n", m);
    buf_fmt(b, "    const %s *x = (const %s *)p;\n    (void)nested; (void)x;\n", T, T);
    buf_fmt(b, "    mxy_buf_puts(b, \"%s {\");\n", u->name);
    int first = 1;
    for (int i = 0; u->decl && i < u->decl->list.len; i++) {
        Node *f = u->decl->list.items[i];
        if (f->kind != N_FIELD || !f->name) continue;
        buf_fmt(b, "    mxy_buf_puts(b, \"%s%s: \");\n", first ? " " : ", ", f->name);
        first = 0;
        Type *fu = type_unqual(f->type);
        if (f->a) {
            buf_fmt(b, "    MXY_FMT_ANY(b, x->%s);\n", f->name);
        } else if (fu->kind == TY_ARRAY) {
            buf_put(b, "    mxy_buf_puts(b, \"[...]\");\n");
        } else if (fu->kind == TY_STRUCT && fu->is_ref) {
            buf_fmt(b, "    if (x->%s) mxy_buf_printf(b, \"%s@%%p\", (const void *)x->%s); else mxy_buf_puts(b, \"null\");\n",
                    f->name, fu->name, f->name);
        } else {
            char *vw = vw_of(f->type);
            buf_fmt(b, "    mxy_vw_fmt(%s, b, &x->%s, 1);\n", vw, f->name);
            free(vw);
        }
    }
    buf_fmt(b, "    mxy_buf_puts(b, \"%s}\");\n}\n", first ? "" : " ");
    buf_fmt(&out.hproto, "static const mxy_vw __mxy_vw_%s;\n", m);
    buf_fmt(b, "static const mxy_vw __mxy_vw_%s __attribute__((unused)) = {sizeof(%s), ", m, T);
    if (is_managed) buf_fmt(b, "__mxy_vwcopy_%s, __mxy_vwdrop_%s, ", m, m);
    else buf_put(b, "NULL, NULL, ");
    buf_fmt(b, "__mxy_vweq_%s, __mxy_vwhash_%s, NULL, __mxy_vwfmt_%s, \"%s\"};\n", m, m, m, u->name);
}

static void gen_enum(Type *u, const char *m, Buf *b) {
    const char *T = u->cname;
    Node *decl = u->decl;
    int is_managed = managed(u);
    if (is_managed) {
        buf_fmt(b, "static %s __mxy_cp_%s(%s v) {\n    switch (v.tag) {\n", T, m, T);
        for (int i = 0; i < decl->list.len; i++) {
            Node *v = decl->list.items[i];
            buf_fmt(b, "    case %s_%s:\n", T, v->name);
            for (int j = 0; j < v->params.len; j++) {
                char *e = xfmt("v.%s.%s", v->name, v->params.items[j].name);
                field_ops(b, v->params.items[j].type->resolved, e, 1);
                free(e);
            }
            buf_put(b, "        break;\n");
        }
        buf_put(b, "    }\n    return v;\n}\n");
        buf_fmt(b, "static void __mxy_rl_%s(%s v) {\n    switch (v.tag) {\n", m, T);
        for (int i = 0; i < decl->list.len; i++) {
            Node *v = decl->list.items[i];
            buf_fmt(b, "    case %s_%s:\n", T, v->name);
            for (int j = 0; j < v->params.len; j++) {
                char *e = xfmt("v.%s.%s", v->name, v->params.items[j].name);
                field_ops(b, v->params.items[j].type->resolved, e, 0);
                free(e);
            }
            buf_put(b, "        break;\n");
        }
        buf_put(b, "    }\n}\n");
    }
    const char *tag = u->simple ? "(*x)" : "x->tag";
    buf_fmt(b, "static bool __mxy_vweq_%s(const void *a, const void *b) {\n", m);
    buf_fmt(b, "    const %s *x = (const %s *)a, *y = (const %s *)b;\n", T, T, T);
    if (u->simple) {
        buf_put(b, "    return *x == *y;\n}\n");
    } else {
        buf_put(b, "    if (x->tag != y->tag) return false;\n    switch (x->tag) {\n");
        for (int i = 0; i < decl->list.len; i++) {
            Node *v = decl->list.items[i];
            buf_fmt(b, "    case %s_%s:\n", T, v->name);
            for (int j = 0; j < v->params.len; j++) {
                char *vw = vw_of(v->params.items[j].type->resolved);
                const char *fn = v->params.items[j].name;
                buf_fmt(b, "        if (!mxy_vw_eq(%s, &x->%s.%s, &y->%s.%s)) return false;\n", vw, v->name, fn,
                        v->name, fn);
                free(vw);
            }
            buf_put(b, "        break;\n");
        }
        buf_put(b, "    }\n    return true;\n}\n");
    }
    buf_fmt(b, "static uint64_t __mxy_vwhash_%s(const void *p) {\n", m);
    buf_fmt(b, "    const %s *x = (const %s *)p;\n    return mxy_hash_u64((uint64_t)%s);\n}\n", T, T, tag);
    buf_fmt(b, "static int __mxy_vwcmp_%s(const void *a, const void *b) {\n", m);
    buf_fmt(b, "    int x = (int)(%s), y = (int)(", u->simple ? "*(const int *)a" : "((const struct { int t; } *)a)->t");
    buf_fmt(b, "%s);\n    return (x > y) - (x < y);\n}\n", u->simple ? "*(const int *)b" : "((const struct { int t; } *)b)->t");
    buf_fmt(b, "static void __mxy_vwfmt_%s(mxy_buf *b, const void *p, int nested) {\n", m);
    buf_fmt(b, "    const %s *x = (const %s *)p;\n    (void)nested;\n    switch (%s) {\n", T, T, tag);
    for (int i = 0; i < decl->list.len; i++) {
        Node *v = decl->list.items[i];
        buf_fmt(b, "    case %s_%s:\n        mxy_buf_puts(b, \"%s::%s\");\n", T, v->name, u->name, v->name);
        if (v->params.len) {
            buf_put(b, "        mxy_buf_putc(b, '(');\n");
            for (int j = 0; j < v->params.len; j++) {
                char *vw = vw_of(v->params.items[j].type->resolved);
                if (j) buf_put(b, "        mxy_buf_puts(b, \", \");\n");
                buf_fmt(b, "        mxy_vw_fmt(%s, b, &x->%s.%s, 1);\n", vw, v->name, v->params.items[j].name);
                free(vw);
            }
            buf_put(b, "        mxy_buf_putc(b, ')');\n");
        }
        buf_put(b, "        break;\n");
    }
    buf_put(b, "    }\n}\n");
    buf_fmt(&out.hproto, "static const mxy_vw __mxy_vw_%s;\n", m);
    buf_fmt(b, "static const mxy_vw __mxy_vw_%s __attribute__((unused)) = {sizeof(%s), ", m, T);
    if (is_managed) buf_fmt(b, "__mxy_vwcopy_%s, __mxy_vwdrop_%s, ", m, m);
    else buf_put(b, "NULL, NULL, ");
    buf_fmt(b, "__mxy_vweq_%s, __mxy_vwhash_%s, __mxy_vwcmp_%s, __mxy_vwfmt_%s, \"%s\"};\n", m, m, m, m, u->name);
}

static void gen_result(Type *u, const char *m, Buf *b) {
    char *T = result_name(u);
    Type *inner = type_unqual(u->base);
    int has_val = inner->kind != TY_VOID;
    buf_fmt(b, "static %s __mxy_cp_%s(%s v) {\n", T, m, T);
    if (has_val && managed(inner)) {
        char *r = retain_of(inner, "v.val");
        buf_fmt(b, "    if (v.ok) v.val = %s;\n", r);
        free(r);
    }
    buf_put(b, "    if (!v.ok) v.err = mxy_str_retain(v.err);\n    return v;\n}\n");
    buf_fmt(b, "static void __mxy_rl_%s(%s v) {\n", m, T);
    if (has_val && managed(inner)) {
        char *r = release_expr(inner, "v.val");
        buf_fmt(b, "    if (v.ok) %s;\n", r);
        free(r);
    }
    buf_put(b, "    if (!v.ok) mxy_str_release(v.err);\n}\n");
    char *vw = has_val ? vw_of(inner) : NULL;
    buf_fmt(b, "static bool __mxy_vweq_%s(const void *a, const void *b) {\n", m);
    buf_fmt(b, "    const %s *x = (const %s *)a, *y = (const %s *)b;\n", T, T, T);
    buf_put(b, "    if (x->ok != y->ok) return false;\n");
    if (has_val) buf_fmt(b, "    if (x->ok) return mxy_vw_eq(%s, &x->val, &y->val);\n", vw);
    else buf_put(b, "    if (x->ok) return true;\n");
    buf_put(b, "    return mxy_str_eq(x->err, y->err);\n}\n");
    buf_fmt(b, "static uint64_t __mxy_vwhash_%s(const void *p) {\n    const %s *x = (const %s *)p;\n", m, T, T);
    if (has_val) buf_fmt(b, "    return x->ok ? mxy_vw_hash(%s, &x->val) : mxy_hash_str(&x->err);\n}\n", vw);
    else buf_put(b, "    return x->ok ? 1 : mxy_hash_str(&x->err);\n}\n");
    buf_fmt(b, "static void __mxy_vwfmt_%s(mxy_buf *b, const void *p, int nested) {\n", m);
    buf_fmt(b, "    const %s *x = (const %s *)p;\n    (void)nested;\n", T, T);
    if (has_val)
        buf_fmt(b, "    if (x->ok) { mxy_buf_puts(b, \"Ok(\"); mxy_vw_fmt(%s, b, &x->val, 1); mxy_buf_putc(b, ')'); }\n", vw);
    else
        buf_put(b, "    if (x->ok) mxy_buf_puts(b, \"Ok()\");\n");
    buf_put(b, "    else { mxy_buf_puts(b, \"Err(\"); mxy_fmtp_str(b, &x->err, 1); mxy_buf_putc(b, ')'); }\n}\n");
    buf_fmt(&out.hproto, "static const mxy_vw __mxy_vw_%s;\n", m);
    buf_fmt(b, "static const mxy_vw __mxy_vw_%s __attribute__((unused)) = {sizeof(%s), __mxy_vwcopy_%s, "
               "__mxy_vwdrop_%s, __mxy_vweq_%s, __mxy_vwhash_%s, NULL, __mxy_vwfmt_%s, \"Result\"};\n",
            m, T, m, m, m, m, m);
    free(vw);
    free(T);
}

static void gen_opaque(Type *u, const char *m, Buf *b) {
    char *T = type_cdecl(u, "");
    char *name = type_str(u);
    buf_fmt(b, "static void __mxy_vwfmt_%s(mxy_buf *b, const void *p, int nested) {\n", m);
    buf_fmt(b, "    (void)p; (void)nested;\n    mxy_buf_puts(b, \"<%s>\");\n}\n", name);
    buf_fmt(&out.hproto, "static const mxy_vw __mxy_vw_%s;\n", m);
    buf_fmt(b, "static const mxy_vw __mxy_vw_%s __attribute__((unused)) = {sizeof(%s), NULL, NULL, NULL, NULL, NULL, "
               "__mxy_vwfmt_%s, \"%s\"};\n",
            m, T, m, name);
    free(T);
    free(name);
}

/* ensure the copy/release/witness helpers exist for a value type; returns its mangled name */
char *struct_helpers(Type *t) {
    Type *u = type_unqual(t);
    char *m = type_mangle(u);
    if (strmap_get(&made, m)) return m;
    strmap_put(&made, m, (void *)1);
    need_type(u);
    Buf b = {0};
    char *T = type_cdecl(u, "");
    int is_managed = managed(u);
    if (is_managed) {
        buf_fmt(&out.hproto, "static %s __mxy_cp_%s(%s v);\nstatic void __mxy_rl_%s(%s v);\n", T, m, T, m, T);
    }
    if (u->kind == TY_STRUCT && u->decl && !u->is_ref) gen_struct_value(u, m, &b);
    else if (u->kind == TY_ENUM && u->decl) gen_enum(u, m, &b);
    else if (u->kind == TY_RESULT) gen_result(u, m, &b);
    else gen_opaque(u, m, &b);
    if (is_managed) {
        buf_fmt(&b, "static void __mxy_vwcopy_%s(void *d, const void *s) { *(%s *)d = __mxy_cp_%s(*(const %s *)s); }\n",
                m, T, m, T);
        buf_fmt(&b, "static void __mxy_vwdrop_%s(void *p) { __mxy_rl_%s(*(%s *)p); }\n", m, m, T);
        buf_fmt(&out.hproto, "static void __mxy_vwcopy_%s(void *d, const void *s);\nstatic void __mxy_vwdrop_%s(void *p);\n",
                m, m);
    }
    buf_put(&out.helpers, buf_str(&b));
    buf_free(&b);
    free(T);
    return m;
}

void helpers_reset(void) { strmap_free(&made); }
