#include "types.h"

#include <string.h>

Type *ty_void, *ty_bool, *ty_char, *ty_schar, *ty_uchar, *ty_short, *ty_ushort, *ty_int, *ty_uint,
    *ty_long, *ty_ulong, *ty_llong, *ty_ullong, *ty_float, *ty_double, *ty_ldouble, *ty_string,
    *ty_unknown, *ty_null, *ty_size;

static int next_id;

Type *type_new(TypeKind kind) {
    Type *t = xcalloc(1, sizeof(Type));
    t->kind = kind;
    t->managed = -1;
    t->len = -1;
    t->id = ++next_id;
    return t;
}

void types_init(void) {
    if (ty_void) return;
    ty_void = type_new(TY_VOID);
    ty_bool = type_new(TY_BOOL);
    ty_char = type_new(TY_CHAR);
    ty_schar = type_new(TY_SCHAR);
    ty_uchar = type_new(TY_UCHAR);
    ty_short = type_new(TY_SHORT);
    ty_ushort = type_new(TY_USHORT);
    ty_int = type_new(TY_INT);
    ty_uint = type_new(TY_UINT);
    ty_long = type_new(TY_LONG);
    ty_ulong = type_new(TY_ULONG);
    ty_llong = type_new(TY_LLONG);
    ty_ullong = type_new(TY_ULLONG);
    ty_float = type_new(TY_FLOAT);
    ty_double = type_new(TY_DOUBLE);
    ty_ldouble = type_new(TY_LDOUBLE);
    ty_string = type_new(TY_STRING);
    ty_unknown = type_new(TY_UNKNOWN);
    ty_null = type_new(TY_NULL);
    ty_size = type_named("size_t");
}

Type *type_ptr(Type *base) {
    Type *t = type_new(TY_PTR);
    t->base = base;
    return t;
}

Type *type_array(Type *base, long len) {
    Type *t = type_new(TY_ARRAY);
    t->base = base;
    t->len = len;
    return t;
}

Type *type_list(Type *elem) {
    Type *t = type_new(TY_LIST);
    t->base = elem;
    return t;
}

Type *type_map(Type *key, Type *val) {
    Type *t = type_new(TY_MAP);
    t->key = key;
    t->base = val;
    return t;
}

Type *type_result(Type *inner) {
    Type *t = type_new(TY_RESULT);
    t->base = inner;
    return t;
}

Type *type_future(Type *inner) {
    Type *t = type_new(TY_FUTURE);
    t->base = inner;
    return t;
}

Type *type_closure(Type *ret, TypeList params) {
    Type *t = type_new(TY_CLOSURE);
    t->base = ret;
    t->params = params;
    return t;
}

Type *type_func(Type *ret, TypeList params, int variadic) {
    Type *t = type_new(TY_FUNC);
    t->base = ret;
    t->params = params;
    t->variadic = variadic;
    return t;
}

Type *type_named(const char *name) {
    Type *t = type_new(TY_NAMED);
    t->name = xstrdup(name);
    t->cname = xstrdup(name);
    return t;
}

Type *type_qual(Type *t, int quals) {
    if (!quals || (t->quals & quals) == quals) return t;
    Type *q = xmalloc(sizeof(Type));
    *q = *t;
    q->quals |= quals;
    q->unqual = t->unqual ? t->unqual : t;
    return q;
}

Type *type_unqual(Type *t) { return t && t->unqual ? t->unqual : t; }

int type_eq(Type *a, Type *b) {
    a = type_unqual(a);
    b = type_unqual(b);
    if (a == b) return 1;
    if (!a || !b || a->kind != b->kind) return 0;
    switch (a->kind) {
    case TY_PTR: case TY_LIST: case TY_RESULT: case TY_FUTURE:
        return type_eq(a->base, b->base);
    case TY_ARRAY:
        return a->len == b->len && type_eq(a->base, b->base);
    case TY_MAP:
        return type_eq(a->key, b->key) && type_eq(a->base, b->base);
    case TY_FUNC: case TY_CLOSURE:
        if (!type_eq(a->base, b->base) || a->params.len != b->params.len || a->variadic != b->variadic) return 0;
        for (int i = 0; i < a->params.len; i++)
            if (!type_eq(a->params.items[i], b->params.items[i])) return 0;
        return 1;
    case TY_NAMED:
        return strcmp(a->name, b->name) == 0;
    case TY_STRUCT: case TY_ENUM: case TY_CENUM:
        return a->decl && a->decl == b->decl;
    default:
        return 1;
    }
}

int type_is_integer(Type *t) {
    t = type_unqual(t);
    return t && ((t->kind >= TY_BOOL && t->kind <= TY_ULLONG) || t->kind == TY_CENUM ||
                 (t->kind == TY_ENUM && t->simple));
}

int type_is_float(Type *t) {
    t = type_unqual(t);
    return t && t->kind >= TY_FLOAT && t->kind <= TY_LDOUBLE;
}

int type_is_arith(Type *t) { return type_is_integer(t) || type_is_float(t); }

int type_is_ptrlike(Type *t) {
    t = type_unqual(t);
    return t && (t->kind == TY_PTR || t->kind == TY_ARRAY || t->kind == TY_NULL || t->kind == TY_FUNC);
}

int type_is_char_ptr(Type *t) {
    t = type_unqual(t);
    if (!t || (t->kind != TY_PTR && t->kind != TY_ARRAY)) return 0;
    Type *b = type_unqual(t->base);
    return b->kind == TY_CHAR || b->kind == TY_SCHAR || b->kind == TY_UCHAR;
}

int type_is_unknownish(Type *t) {
    t = type_unqual(t);
    return !t || t->kind == TY_UNKNOWN || t->kind == TY_NAMED;
}

int type_is_scalar(Type *t) {
    t = type_unqual(t);
    return type_is_arith(t) || type_is_ptrlike(t) || type_is_unknownish(t) || t->kind == TY_STRING ||
           (t->kind == TY_STRUCT && t->is_ref) || t->kind == TY_CLOSURE;
}

/* values that carry a reference count directly */
int type_is_refcounted(Type *t) {
    t = type_unqual(t);
    if (!t) return 0;
    switch (t->kind) {
    case TY_STRING: case TY_LIST: case TY_MAP: case TY_FUTURE: case TY_CLOSURE:
        return 1;
    case TY_STRUCT:
        return t->is_ref;
    default:
        return 0;
    }
}

int type_is_managed(Type *t) {
    t = type_unqual(t);
    if (!t) return 0;
    if (type_is_refcounted(t)) return 1;
    switch (t->kind) {
    case TY_RESULT:
        return 1;
    case TY_ARRAY:
        return type_is_managed(t->base);
    case TY_STRUCT: {
        if (t->is_union || !t->decl) return 0;
        if (t->managed >= 0) return t->managed;
        t->managed = 0;
        for (int i = 0; i < t->decl->list.len; i++) {
            Node *f = t->decl->list.items[i];
            if (f->kind == N_FIELD && f->type && type_is_managed(f->type)) t->managed = 1;
        }
        return t->managed;
    }
    case TY_ENUM: {
        if (t->simple || !t->decl) return 0;
        if (t->managed >= 0) return t->managed;
        t->managed = 0;
        for (int i = 0; i < t->decl->list.len; i++) {
            Node *v = t->decl->list.items[i];
            for (int j = 0; j < v->params.len; j++) {
                TypeRef *r = v->params.items[j].type;
                if (r->resolved && type_is_managed(r->resolved)) t->managed = 1;
            }
        }
        return t->managed;
    }
    default:
        return 0;
    }
}

int type_rank(Type *t) {
    t = type_unqual(t);
    switch (t->kind) {
    case TY_BOOL: return 1;
    case TY_CHAR: case TY_SCHAR: case TY_UCHAR: return 2;
    case TY_SHORT: case TY_USHORT: return 3;
    case TY_INT: case TY_CENUM: case TY_ENUM: return 4;
    case TY_UINT: return 5;
    case TY_LONG: return 6;
    case TY_ULONG: return 7;
    case TY_LLONG: return 8;
    case TY_ULLONG: return 9;
    case TY_FLOAT: return 10;
    case TY_DOUBLE: return 11;
    case TY_LDOUBLE: return 12;
    default: return 0;
    }
}

/* usual arithmetic conversions */
Type *type_common(Type *a, Type *b) {
    a = type_unqual(a);
    b = type_unqual(b);
    if (type_is_unknownish(a)) return type_is_unknownish(b) ? ty_unknown : (type_is_arith(b) ? ty_unknown : b);
    if (type_is_unknownish(b)) return type_is_arith(a) ? ty_unknown : a;
    int ra = type_rank(a), rb = type_rank(b);
    Type *hi = ra >= rb ? a : b;
    if (type_rank(hi) < 4) return ty_int;
    if (hi->kind == TY_CENUM || hi->kind == TY_ENUM) return ty_int;
    return hi;
}

Node *type_field(Type *s, const char *name, Type **ftype, char **path) {
    s = type_unqual(s);
    if (path) *path = NULL;
    if (!s || s->kind != TY_STRUCT || !s->decl) return NULL;
    for (int i = 0; i < s->decl->list.len; i++) {
        Node *f = s->decl->list.items[i];
        if (f->kind != N_FIELD) continue;
        if (f->name && strcmp(f->name, name) == 0) {
            if (ftype) *ftype = f->type;
            return f;
        }
        if (!f->name && f->type) {
            Node *inner = type_field(f->type, name, ftype, NULL);
            if (inner) return inner;
        }
    }
    return NULL;
}
