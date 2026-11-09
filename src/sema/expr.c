#include <ctype.h>
#include <limits.h>
#include <string.h>

#include "diag/diag.h"
#include "sema.h"

Type *check_init(Node **slot, Type *t);

static Type *int_literal_type(const char *text) {
    int u = 0, l = 0;
    for (const char *p = text; *p; p++) {
        if (*p == 'u' || *p == 'U') u = 1;
        if (*p == 'l' || *p == 'L') l++;
    }
    if (l >= 2) return u ? ty_ullong : ty_llong;
    if (l == 1) return u ? ty_ulong : ty_long;
    unsigned long long v = strtoull(text, NULL, 0);
    if (u) return v > UINT_MAX ? ty_ulong : ty_uint;
    if (v > INT_MAX) return ty_long;
    return ty_int;
}

static Type *float_literal_type(const char *text) {
    size_t n = strlen(text);
    char last = n ? text[n - 1] : 0;
    if (last == 'f' || last == 'F') return ty_float;
    if ((last == 'l' || last == 'L') && !strpbrk(text, "xX")) return ty_ldouble;
    return ty_double;
}

static int is_stringish(Type *t) {
    t = type_unqual(t);
    return t && (t->kind == TY_STRING || type_is_char_ptr(t));
}

static Type *check_ident(Node *n) {
    Sym *s = lookup(n->name, n->loc);
    if (s) {
        n->sym = s;
        switch (s->kind) {
        case S_VAR:
            if (!s->type) {
                if (s->decl && s->decl->tref && !s->decl->op) s->type = resolve_type(s->decl->tref);
                else s->type = ty_unknown;
            }
            n->lvalue = 1;
            return s->type;
        case S_FUNC:
            return s->type ? s->type : ty_unknown;
        case S_CONST:
            return s->type;
        case S_TYPE:
            diag_error(n->loc, "'%s' is a type, not a value", n->name);
            if (s->type && type_unqual(s->type)->kind == TY_STRUCT)
                diag_hint("construct it with %s{ ... }", n->name);
            return ty_unknown;
        case S_NS:
            diag_error(n->loc, "'%s' is a namespace, not a value", n->name);
            return ty_unknown;
        }
    }
    const char *sep = strstr(n->name, "::");
    if (sep) {
        const char *last = n->name;
        for (const char *p = n->name; (p = strstr(p, "::")); p += 2) last = p;
        char *prefix = xstrndup(n->name, (size_t)(last - n->name));
        Sym *e = lookup_quiet(prefix);
        free(prefix);
        if (e && e->kind == S_TYPE && e->type && type_unqual(e->type)->kind == TY_ENUM)
            return variant_ctor(n, e, last + 2, NULL);
        diag_error(n->loc, "unknown name '%s'", n->name);
        return ty_unknown;
    }
    if (strcmp(n->name, "NULL") == 0) return ty_null;
    suggest_name(n->name, n->loc);
    return ty_unknown;
}

static Type *check_unary(Node *n) {
    Type *t = check_expr(&n->a, NULL);
    Type *u = type_unqual(t);
    switch (n->op) {
    case P_MINUS: case P_PLUS:
        if (!type_is_arith(u) && !type_is_unknownish(u)) {
            char *s = type_str(t);
            diag_error(n->loc, "'%s' needs a number, found %s", tok_spell((TokKind)n->op), s);
            free(s);
        }
        return type_is_arith(u) && type_rank(u) < 4 ? ty_int : t;
    case P_TILDE:
        if (!type_is_integer(u) && !type_is_unknownish(u)) diag_error(n->loc, "'~' needs an integer");
        return type_is_integer(u) && type_rank(u) < 4 ? ty_int : t;
    case P_BANG:
        check_cond(&n->a);
        return ty_bool;
    case P_STAR:
        if (u->kind == TY_PTR || u->kind == TY_ARRAY) return u->base;
        if (type_is_unknownish(u)) return ty_unknown;
        if (u->kind == TY_FUNC) return u;
        {
            char *s = type_str(t);
            diag_error(n->loc, "cannot dereference a value of type %s", s);
            free(s);
        }
        return ty_unknown;
    case P_AMP: {
        Node *target = n->a;
        while (target->kind == N_CONV) target = target->a;
        if (target->kind == N_IDENT && target->sym) target->sym->addr_taken = 1;
        mark_mutated(target);
        if (target->kind == N_INDEX && type_unqual(target->a->type)->kind == TY_LIST) target->lvalue = 2;
        return type_ptr(t);
    }
    case P_INC: case P_DEC:
        if (!is_lvalue(n->a)) diag_error(n->loc, "'%s' needs a variable", tok_spell((TokKind)n->op));
        mark_mutated(n->a);
        return t;
    default:
        return t;
    }
}

static Type *check_binary(Node *n) {
    Type *a = check_expr(&n->a, NULL);
    Type *b = check_expr(&n->b, NULL);
    Type *ua = type_unqual(a), *ub = type_unqual(b);
    int op = n->op;
    if (op == P_ANDAND || op == P_OROR) {
        check_cond(&n->a);
        check_cond(&n->b);
        return ty_bool;
    }
    if (ua->kind == TY_STRING || ub->kind == TY_STRING) {
        int other_null = ua->kind == TY_NULL || ub->kind == TY_NULL;
        if ((op == P_EQ || op == P_NE) && other_null) return ty_bool;
        if (!is_stringish(ua) || !is_stringish(ub)) {
            Type *other = ua->kind == TY_STRING ? b : a;
            if (op == P_PLUS && type_is_arith(other)) {
                diag_error(n->loc, "cannot add a number to a string");
                diag_hint("use interpolation: \"text \\(value)\"");
                return ty_string;
            }
            if (!type_is_unknownish(other)) {
                char *os = type_str(other);
                diag_error(n->loc, "'%s' between string and %s", tok_spell((TokKind)op), os);
                free(os);
                return ty_unknown;
            }
        }
        coerce(&n->a, ty_string, "string operand");
        coerce(&n->b, ty_string, "string operand");
        switch (op) {
        case P_PLUS: n->builtin = B_STR_CONCAT; return ty_string;
        case P_EQ: case P_NE: n->builtin = B_STR_EQ; return ty_bool;
        case P_LT: case P_GT: case P_LE: case P_GE: n->builtin = B_STR_CMP; return ty_bool;
        default:
            diag_error(n->loc, "'%s' is not defined for strings", tok_spell((TokKind)op));
            return ty_unknown;
        }
    }
    if (ua->kind == TY_LIST || ub->kind == TY_LIST || ua->kind == TY_MAP || ub->kind == TY_MAP ||
        ua->kind == TY_RESULT || ub->kind == TY_RESULT || (ua->kind == TY_ENUM && !ua->simple) ||
        (ub->kind == TY_ENUM && !ub->simple) || (ua->kind == TY_STRUCT && !ua->is_ref) ||
        (ub->kind == TY_STRUCT && !ub->is_ref)) {
        if ((op == P_EQ || op == P_NE) && (ua->kind == TY_NULL || ub->kind == TY_NULL) &&
            (ua->kind == TY_LIST || ua->kind == TY_MAP || ub->kind == TY_LIST || ub->kind == TY_MAP))
            return ty_bool;
        if (op == P_PLUS && ua->kind == TY_LIST && type_eq(ua, ub)) {
            n->builtin = B_LIST_CONCAT;
            return a;
        }
        if ((op == P_EQ || op == P_NE) && type_eq(ua, ub) && ua->kind != TY_STRUCT) {
            n->builtin = B_VAL_EQ;
            return ty_bool;
        }
        char *as = type_str(a), *bs = type_str(b);
        diag_error(n->loc, "'%s' is not defined between %s and %s", tok_spell((TokKind)op), as, bs);
        free(as);
        free(bs);
        return ty_unknown;
    }
    switch (op) {
    case P_EQ: case P_NE: case P_LT: case P_GT: case P_LE: case P_GE:
        return ty_bool;
    case P_PLUS: case P_MINUS:
        if (ua->kind == TY_PTR || ua->kind == TY_ARRAY) {
            if (op == P_MINUS && (ub->kind == TY_PTR || ub->kind == TY_ARRAY)) return ty_long;
            return ua->kind == TY_ARRAY ? type_ptr(ua->base) : a;
        }
        if (ub->kind == TY_PTR || ub->kind == TY_ARRAY) return ub->kind == TY_ARRAY ? type_ptr(ub->base) : b;
        break;
    case P_SHL: case P_SHR:
        if (type_is_integer(ua)) return type_rank(ua) < 4 ? ty_int : a;
        return type_is_unknownish(ua) ? ty_unknown : a;
    default:
        break;
    }
    if ((!type_is_arith(ua) && !type_is_unknownish(ua)) || (!type_is_arith(ub) && !type_is_unknownish(ub))) {
        char *as = type_str(a), *bs = type_str(b);
        diag_error(n->loc, "'%s' is not defined between %s and %s", tok_spell((TokKind)op), as, bs);
        free(as);
        free(bs);
        return ty_unknown;
    }
    return type_common(a, b);
}

static int compound_op_base(int op) {
    switch (op) {
    case P_PLUSEQ: return P_PLUS;
    case P_MINUSEQ: return P_MINUS;
    case P_STAREQ: return P_STAR;
    case P_SLASHEQ: return P_SLASH;
    case P_PERCENTEQ: return P_PERCENT;
    case P_AMPEQ: return P_AMP;
    case P_PIPEEQ: return P_PIPE;
    case P_CARETEQ: return P_CARET;
    case P_SHLEQ: return P_SHL;
    case P_SHREQ: return P_SHR;
    default: return 0;
    }
}

static Type *check_assign(Node *n) {
    Type *t = check_expr(&n->a, NULL);
    Node *target = n->a;
    if (!is_lvalue(target)) {
        if (target->kind == N_INDEX && type_unqual(target->a->type)->kind == TY_STRING)
            diag_error(n->loc, "strings are immutable; build a new string instead");
        else if (target->kind == N_MEMBER && target->builtin)
            diag_error(n->loc, "'%s' is read-only", target->name);
        else
            diag_error(n->loc, "the left side of an assignment must be a variable, field or element");
        return t;
    }
    mark_mutated(target);
    if (target->kind == N_IDENT && target->sym && target->sym->kind == S_VAR && target->sym->decl &&
        (target->sym->decl->flags & F_WEAK)) {
    }
    Type *u = type_unqual(t);
    if (n->op == P_ASSIGN) {
        check_init(&n->b, t);
        coerce(&n->b, t, "assignment");
        return t;
    }
    Type *v = check_expr(&n->b, u->kind == TY_STRING ? ty_string : NULL);
    if (u->kind == TY_STRING) {
        if (n->op != P_PLUSEQ) diag_error(n->loc, "only '+=' works on strings");
        coerce(&n->b, ty_string, "string append");
        n->builtin = B_STR_CONCAT;
        return t;
    }
    if (u->kind == TY_LIST && n->op == P_PLUSEQ) {
        coerce(&n->b, t, "list append");
        n->builtin = B_LIST_CONCAT;
        return t;
    }
    int base = compound_op_base(n->op);
    if ((u->kind == TY_PTR) && (base == P_PLUS || base == P_MINUS)) return t;
    if ((!type_is_arith(u) && !type_is_unknownish(u)) ||
        (!type_is_arith(type_unqual(v)) && !type_is_unknownish(v))) {
        char *ts = type_str(t), *vs = type_str(v);
        diag_error(n->loc, "'%s' is not defined between %s and %s", tok_spell((TokKind)n->op), ts, vs);
        free(ts);
        free(vs);
    }
    return t;
}

static Type *unify(Node *n, Type *a, Type *b) {
    Type *ua = type_unqual(a), *ub = type_unqual(b);
    if (type_eq(ua, ub)) return a;
    if (type_is_arith(ua) && type_is_arith(ub)) return type_common(a, b);
    if (ua->kind == TY_NULL) return b;
    if (ub->kind == TY_NULL) return a;
    if (ua->kind == TY_STRING && type_is_char_ptr(ub)) {
        coerce(&n->c, ty_string, "conditional");
        return a;
    }
    if (ub->kind == TY_STRING && type_is_char_ptr(ua)) {
        coerce(&n->b, ty_string, "conditional");
        return b;
    }
    if (type_is_unknownish(ua)) return b;
    if (type_is_unknownish(ub)) return a;
    if (type_is_ptrlike(ua) && type_is_ptrlike(ub)) return a;
    char *as = type_str(a), *bs = type_str(b);
    diag_error(n->loc, "the branches of '?:' have different types: %s and %s", as, bs);
    free(as);
    free(bs);
    return a;
}

static Type *check_index(Node *n) {
    Type *t = check_expr(&n->a, NULL);
    Type *u = type_unqual(t);
    switch (u->kind) {
    case TY_LIST: case TY_STRING: {
        Type *i = check_expr(&n->b, NULL);
        if (!type_is_integer(i) && !type_is_unknownish(i)) diag_error(n->b->loc, "index must be an integer");
        return u->kind == TY_LIST ? u->base : ty_char;
    }
    case TY_MAP:
        check_expr(&n->b, u->key);
        coerce(&n->b, u->key, "map key");
        return u->base;
    case TY_PTR: case TY_ARRAY:
        check_expr(&n->b, NULL);
        return u->base;
    default:
        check_expr(&n->b, NULL);
        if (type_is_unknownish(u)) return ty_unknown;
        {
            char *s = type_str(t);
            diag_error(n->loc, "cannot index a value of type %s", s);
            free(s);
        }
        return ty_unknown;
    }
}

static Type *check_cast(Node *n) {
    Type *to = resolve_type(n->tref);
    Type *from = check_expr(&n->a, to);
    Type *ut = type_unqual(to), *uf = type_unqual(from);
    if (ut->kind == TY_STRING) {
        if (uf->kind == TY_STRING || uf->kind == TY_NULL) return to;
        if (type_is_char_ptr(uf) || type_is_unknownish(uf)) {
            n->a = wrap_conv(n->a, to, CV_CSTR_TO_STR);
            n->op = 1;
            return to;
        }
        diag_error(n->loc, "cannot cast to string; use string(value) to format a value");
        return to;
    }
    if (type_is_managed(ut) && !type_eq(ut, uf) && uf->kind != TY_NULL && !type_is_unknownish(uf)) {
        char *a = type_str(from), *b = type_str(to);
        diag_error(n->loc, "cannot cast %s to %s", a, b);
        free(a);
        free(b);
    }
    if (type_is_managed(uf) && uf->kind != TY_STRING && !type_is_managed(ut) && !type_is_unknownish(ut) &&
        !(uf->kind == TY_STRUCT && uf->is_ref)) {
        char *a = type_str(from), *b = type_str(to);
        diag_error(n->loc, "cannot cast %s to %s", a, b);
        free(a);
        free(b);
    }
    return to;
}

static Type *check_list_lit(Node *n, Type *expected) {
    Type *ex = type_unqual(expected);
    Type *elem = ex && ex->kind == TY_LIST ? ex->base : NULL;
    if (!elem) {
        if (!n->list.len) {
            diag_error(n->loc, "cannot infer the element type of an empty list");
            diag_hint("give the variable a type, e.g. int[] xs = [];");
            return type_list(ty_unknown);
        }
        elem = check_expr(&n->list.items[0], NULL);
        if (type_unqual(elem)->kind == TY_NULL) elem = ty_unknown;
    }
    for (int i = 0; i < n->list.len; i++) {
        check_init(&n->list.items[i], elem);
        coerce(&n->list.items[i], elem, "list element");
    }
    return ex && ex->kind == TY_LIST ? expected : type_list(elem);
}

static Type *check_result_ctor(Node *n, Type *expected) {
    Type *ex = type_unqual(expected);
    if (!ex || ex->kind != TY_RESULT) {
        if (ex && type_is_unknownish(ex)) return ty_unknown;
        diag_error(n->loc, "%s(...) needs a Result<T> context", n->kind == N_OK ? "Ok" : "Err");
        diag_hint("return it from a function returning Result<T>, or assign it to a Result<T> variable");
        if (n->a) check_expr(&n->a, NULL);
        return ty_unknown;
    }
    if (n->kind == N_OK) {
        if (type_unqual(ex->base)->kind == TY_VOID) {
            if (n->a) diag_error(n->loc, "Ok() of Result<void> takes no value");
        } else if (!n->a) {
            diag_error(n->loc, "Ok(...) needs a value");
        } else {
            check_init(&n->a, ex->base);
            coerce(&n->a, ex->base, "Ok value");
        }
    } else {
        if (!n->a) {
            diag_error(n->loc, "Err(...) needs a message");
        } else {
            check_expr(&n->a, ty_string);
            coerce(&n->a, ty_string, "Err message");
        }
    }
    return expected;
}

static Type *check_interp(Node *n) {
    for (int i = 0; i < n->list.len; i++) {
        Node *part = n->list.items[i];
        if (part->kind == N_STR) {
            part->type = ty_string;
            part->op = 2;
            continue;
        }
        Type *t = check_expr(&n->list.items[i], NULL);
        if (type_unqual(t)->kind == TY_VOID) diag_error(part->loc, "cannot interpolate a void value");
    }
    return ty_string;
}

static Type *check_inner(Node **slot, Type *expected) {
    Node *n = *slot;
    switch (n->kind) {
    case N_INT: return int_literal_type(n->text);
    case N_FLOAT: return float_literal_type(n->text);
    case N_CHAR: return ty_char;
    case N_STR: {
        Type *ex = type_unqual(expected);
        if (ex && ex->kind == TY_ARRAY && type_is_char_ptr(ex)) {
            n->op = 1;
            return expected;
        }
        if (ex && (ex->kind == TY_PTR || ex->kind == TY_NAMED)) n->op = 1;
        return ty_string;
    }
    case N_INTERP: return check_interp(n);
    case N_BOOL: return ty_bool;
    case N_NULL: return ty_null;
    case N_IDENT: return check_ident(n);
    case N_UNARY: return check_unary(n);
    case N_POSTFIX:
        check_expr(&n->a, NULL);
        if (!is_lvalue(n->a)) diag_error(n->loc, "'%s' needs a variable", tok_spell((TokKind)n->op));
        mark_mutated(n->a);
        return n->a->type;
    case N_BINARY: return check_binary(n);
    case N_ASSIGN: return check_assign(n);
    case N_TERNARY: {
        check_cond(&n->a);
        Type *a = check_expr(&n->b, expected);
        Type *b = check_expr(&n->c, expected);
        if (expected && !type_is_unknownish(expected)) {
            coerce(&n->b, expected, "conditional branch");
            coerce(&n->c, expected, "conditional branch");
            return expected;
        }
        return unify(n, a, b);
    }
    case N_COMMA:
        check_expr(&n->a, NULL);
        return check_expr(&n->b, expected);
    case N_CALL: return check_call(slot, expected);
    case N_INDEX: return check_index(n);
    case N_MEMBER: return check_member(slot);
    case N_CAST: return check_cast(n);
    case N_SIZEOF:
        if (n->tref) resolve_type(n->tref);
        else check_expr(&n->a, NULL);
        return ty_ulong;
    case N_ALIGNOF:
        resolve_type(n->tref);
        return ty_ulong;
    case N_COMPOUND: {
        Type *t = resolve_type(n->tref);
        check_init(&n->b, t);
        if (type_unqual(t)->kind == TY_ARRAY && type_unqual(t)->len < 0) {
            Type *fixed = type_array(type_unqual(t)->base, n->b->list.len);
            n->b->type = fixed;
            return fixed;
        }
        return t;
    }
    case N_STRUCTLIT: {
        Type *t = resolve_type(n->tref);
        Type *u = type_unqual(t);
        if (u->kind != TY_STRUCT || !u->decl) {
            diag_error(n->loc, "'%s' is not a struct defined in Moxy", n->tref->name);
            return ty_unknown;
        }
        check_init(&n->b, t);
        return t;
    }
    case N_INITLIST:
        if (!expected) {
            diag_error(n->loc, "cannot infer the type of this initializer");
            diag_hint("use a typed declaration or a literal like Point{ .x = 1 }");
            return ty_unknown;
        }
        return check_init(slot, expected);
    case N_LIST: return check_list_lit(n, expected);
    case N_OK: case N_ERR: return check_result_ctor(n, expected);
    case N_AWAIT: {
        Type *t = check_expr(&n->a, NULL);
        Type *u = type_unqual(t);
        if (u->kind != TY_FUTURE) {
            if (!type_is_unknownish(u)) {
                char *s = type_str(t);
                diag_error(n->loc, "await needs a Future, found %s", s);
                free(s);
            }
            return ty_unknown;
        }
        prog.threads = 1;
        return u->base;
    }
    case N_LAMBDA: return check_lambda(n, expected);
    case N_RANGE:
        diag_error(n->loc, "ranges are only valid in for-in loops");
        return ty_unknown;
    case N_CONV: return n->type;
    case N_TYPEARG:
        resolve_type(n->tref);
        return ty_unknown;
    default:
        diag_error(n->loc, "unexpected expression");
        return ty_unknown;
    }
}

Type *check_expr(Node **slot, Type *expected) {
    Type *t = check_inner(slot, expected);
    if (!t) t = ty_unknown;
    if ((*slot)->kind != N_CONV || !(*slot)->type) (*slot)->type = t;
    return (*slot)->type;
}
