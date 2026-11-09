#include <string.h>

#include "internal.h"

static int is_brace(const char *s) { return s[0] == '{'; }
static CExpr own(char *s) { return (CExpr){s, 1}; }
static CExpr bor(char *s) { return (CExpr){s, 0}; }

char *gen_borrow(Node *n, Ctx *c) { return borrow(c, gen_expr(n, c), n->type); }
char *gen_consume(Node *n, Ctx *c) { return consume(c, gen_expr(n, c), n->type); }
char *gen_value(Node *n, Ctx *c) { return gen_borrow(n, c); }

static int sub_dirty(Ctx *s) { return s->pre.len > 0 || s->temps.len > 0; }

static void sub_into(Ctx *c, Ctx *s, int extra) {
    (void)extra;
    ctx_flush_pre(s, &c->pre);
}

static const char *fmt_fn(Type *u) {
    switch (u->kind) {
    case TY_BOOL: return "mxy_fmtv_bool";
    case TY_CHAR: return "mxy_fmtv_char";
    case TY_SCHAR: return "mxy_fmtv_schar";
    case TY_UCHAR: return "mxy_fmtv_uchar";
    case TY_SHORT: return "mxy_fmtv_short";
    case TY_USHORT: return "mxy_fmtv_ushort";
    case TY_INT: case TY_CENUM: return "mxy_fmtv_int";
    case TY_UINT: return "mxy_fmtv_uint";
    case TY_LONG: return "mxy_fmtv_long";
    case TY_ULONG: return "mxy_fmtv_ulong";
    case TY_LLONG: return "mxy_fmtv_llong";
    case TY_ULLONG: return "mxy_fmtv_ullong";
    case TY_FLOAT: return "mxy_fmtv_float";
    case TY_DOUBLE: return "mxy_fmtv_double";
    case TY_LDOUBLE: return "mxy_fmtv_ldouble";
    case TY_STRING: return "mxy_fmtv_str";
    case TY_PTR: case TY_ARRAY: case TY_NULL: case TY_FUNC: return NULL;
    default: return NULL;
    }
}

void fmt_into(Ctx *c, const char *buf, Node *arg, int nested) {
    Type *u = type_unqual(arg->type);
    char *v = gen_borrow(arg, c);
    if (type_is_char_ptr(u)) {
        raw_line(&c->pre, c->indent, "mxy_fmtv_cstr(%s, %s);", buf, v);
        return;
    }
    if (u->kind == TY_PTR || u->kind == TY_NULL || u->kind == TY_FUNC) {
        raw_line(&c->pre, c->indent, "mxy_fmtv_ptr(%s, (const void *)%s);", buf, v);
        return;
    }
    if (u->kind == TY_STRING && nested) {
        raw_line(&c->pre, c->indent, "mxy_fmtp_str(%s, &(mxy_str){%s}, 1);", buf, v);
        return;
    }
    const char *f = fmt_fn(u);
    if (f) {
        raw_line(&c->pre, c->indent, "%s(%s, %s);", f, buf, v);
        return;
    }
    if (type_is_unknownish(u) && u->kind != TY_NAMED) {
        raw_line(&c->pre, c->indent, "MXY_FMT_ANY(%s, %s);", buf, v);
        return;
    }
    if (u->kind == TY_NAMED) {
        raw_line(&c->pre, c->indent, "MXY_FMT_ANY(%s, %s);", buf, v);
        return;
    }
    char *tmp = hoist(c, v, arg->type, 0);
    char *vw = vw_of(arg->type);
    raw_line(&c->pre, c->indent, "mxy_vw_fmt(%s, %s, &%s, %d);", vw, buf, tmp, nested);
}

CExpr gen_interp(Node *n, Ctx *c) {
    char *b = fresh("b");
    raw_line(&c->pre, c->indent, "mxy_buf %s = {0};", b);
    char *bp = xfmt("&%s", b);
    for (int i = 0; i < n->list.len; i++) {
        Node *part = n->list.items[i];
        if (part->kind == N_STR && part->op == 2) {
            if (part->text[0])
                raw_line(&c->pre, c->indent, "mxy_buf_put(%s, \"%s\", sizeof(\"%s\") - 1);", bp, part->text, part->text);
            continue;
        }
        fmt_into(c, bp, part, 0);
    }
    return own(xfmt("mxy_str_from_buf(%s)", bp));
}

CExpr gen_conv(Node *n, Ctx *c) {
    switch (n->op) {
    case CV_CSTR_TO_STR: {
        char *e = gen_borrow(n->a, c);
        return own(xfmt("mxy_str_from(%s)", e));
    }
    case CV_TO_STRING: {
        char *b = fresh("b");
        raw_line(&c->pre, c->indent, "mxy_buf %s = {0};", b);
        char *bp = xfmt("&%s", b);
        fmt_into(c, bp, n->a, 0);
        return own(xfmt("mxy_str_from_buf(%s)", bp));
    }
    case CV_FN_TO_CLOSURE: {
        Node *inner = n->a;
        if (inner->kind == N_IDENT && inner->sym && inner->sym->kind == S_FUNC) return bor(fn_closure(inner->sym));
        return bor(xstrdup("NULL"));
    }
    case CV_LAMBDA_FNPTR:
        return bor(xfmt("__mxy_lambda%d", n->a->id));
    default: {
        char *e = gen_borrow(n->a, c);
        char *t = ctype(n->type);
        return bor(xfmt("((%s)%s)", t, e));
    }
    }
}

static CExpr gen_list_lit(NodeList *items, Type *lt, Ctx *c) {
    Type *elem = type_unqual(lt)->base;
    if (!items->len) return own(xstrdup("NULL"));
    Buf b = {0};
    char *et = ctype(elem);
    char *vw = vw_of(elem);
    buf_fmt(&b, "mxy_list_from(%s, %d, (%s[]){", vw, items->len, et);
    for (int i = 0; i < items->len; i++) {
        CExpr e = gen_init(items->items[i], elem, c);
        char *v = consume(c, e, elem);
        buf_fmt(&b, "%s%s", i ? ", " : "", v);
    }
    buf_put(&b, "})");
    return own(buf_take(&b));
}

static CExpr gen_map_lit(Node *list, Type *mt, Ctx *c) {
    Type *u = type_unqual(mt);
    char *m = fresh("m");
    raw_line(&c->pre, c->indent, "mxy_map *%s = NULL;", m);
    char *kvw = vw_of(u->key), *vvw = vw_of(u->base);
    for (int i = 0; i < list->list.len; i++) {
        Node *kv = list->list.items[i];
        char *k = consume(c, gen_init(kv->a, u->key, c), u->key);
        char *v = consume(c, gen_init(kv->b, u->base, c), u->base);
        char *kt = hoist(c, k, u->key, 0);
        char *vt = hoist(c, v, u->base, 0);
        raw_line(&c->pre, c->indent, "mxy_map_set(&%s, %s, %s, &%s, &%s, 1, 1);", m, kvw, vvw, kt, vt);
    }
    return own(m);
}

static void struct_items(Buf *b, Node *list, Type *t, Ctx *c) {
    Type *u = type_unqual(t);
    buf_putc(b, '{');
    int pos = 0;
    for (int i = 0; i < list->list.len; i++) {
        Node *item = list->list.items[i];
        if (i) buf_put(b, ", ");
        if (item->kind == N_DESIG && item->name) {
            Type *ft = item->type ? item->type : ty_unknown;
            char *v = consume(c, gen_init(item->b, ft, c), ft);
            buf_fmt(b, ".%s = %s", item->name, v);
            continue;
        }
        Type *ft = ty_unknown;
        if (u->decl) {
            int k = 0;
            for (int j = 0; j < u->decl->list.len; j++) {
                Node *f = u->decl->list.items[j];
                if (f->kind != N_FIELD) continue;
                if (k++ == pos) {
                    ft = f->type;
                    break;
                }
            }
        }
        pos++;
        char *v = consume(c, gen_init(item, ft, c), ft);
        buf_put(b, v);
    }
    if (!list->list.len) buf_put(b, "0");
    buf_putc(b, '}');
}

static void array_items(Buf *b, Node *list, Type *t, Ctx *c) {
    Type *elem = type_unqual(t)->base;
    buf_putc(b, '{');
    for (int i = 0; i < list->list.len; i++) {
        Node *item = list->list.items[i];
        if (i) buf_put(b, ", ");
        if (item->kind == N_DESIG && item->a) {
            char *idx = gen_value(item->a, c);
            char *v = consume(c, gen_init(item->b, elem, c), elem);
            buf_fmt(b, "[%s] = %s", idx, v);
            continue;
        }
        char *v = consume(c, gen_init(item, elem, c), elem);
        buf_put(b, v);
    }
    if (!list->list.len) buf_put(b, "0");
    buf_putc(b, '}');
}

/* an initializer for a value of type t; brace lists come back as "{...}" */
CExpr gen_init(Node *n, Type *t, Ctx *c) {
    if (!n) return bor(xstrdup("0"));
    if (n->kind != N_INITLIST) return gen_expr(n, c);
    Type *u = type_unqual(t ? t : n->type);
    if (!u) u = ty_unknown;
    Buf b = {0};
    switch (u->kind) {
    case TY_LIST:
        return gen_list_lit(&n->list, u, c);
    case TY_MAP:
        if (!n->list.len) return own(xstrdup("NULL"));
        return gen_map_lit(n, u, c);
    case TY_STRUCT:
        if (u->is_ref) {
            char *o = fresh("o");
            raw_line(&c->pre, c->indent, "%s *%s = (%s *)mxy_obj_new(sizeof(%s), __mxy_dropf_%s, __mxy_fmtf_%s, \"%s\");",
                     u->cname, o, u->cname, u->cname, u->cname, u->cname, u->name);
            int pos = 0;
            for (int i = 0; i < n->list.len; i++) {
                Node *item = n->list.items[i];
                Node *field = NULL;
                Node *val = item;
                if (item->kind == N_DESIG && item->name) {
                    field = type_field(u, item->name, NULL, NULL);
                    val = item->b;
                } else {
                    int k = 0;
                    for (int j = 0; j < u->decl->list.len; j++) {
                        Node *f = u->decl->list.items[j];
                        if (f->kind != N_FIELD) continue;
                        if (k++ == pos) field = f;
                    }
                    pos++;
                }
                if (!field) continue;
                if (field->flags & F_WEAK) {
                    char *v = borrow(c, gen_init(val, field->type, c), field->type);
                    raw_line(&c->pre, c->indent, "%s->%s = mxy_obj_weak_retain(%s);", o, field->name, v);
                } else {
                    char *v = consume(c, gen_init(val, field->type, c), field->type);
                    if (is_brace(v)) v = xfmt("(%s)%s", ctype(field->type), v);
                    raw_line(&c->pre, c->indent, "%s->%s = %s;", o, field->name, v);
                }
            }
            return own(o);
        }
        struct_items(&b, n, u, c);
        return (CExpr){buf_take(&b), managed(u)};
    case TY_ARRAY:
        array_items(&b, n, u, c);
        return (CExpr){buf_take(&b), managed(u)};
    default:
        if (n->list.len == 1) return gen_init(n->list.items[0], t, c);
        if (u->kind == TY_UNKNOWN || u->kind == TY_NAMED) {
            buf_putc(&b, '{');
            for (int i = 0; i < n->list.len; i++) {
                Node *item = n->list.items[i];
                if (i) buf_put(&b, ", ");
                if (item->kind == N_DESIG && item->name) {
                    buf_fmt(&b, ".%s = %s", item->name, gen_value(item->b, c));
                } else if (item->kind == N_DESIG) {
                    char *idx = gen_value(item->a, c);
                    buf_fmt(&b, "[%s] = %s", idx, gen_value(item->b, c));
                } else {
                    CExpr e = gen_init(item, NULL, c);
                    buf_put(&b, borrow(c, e, item->type));
                }
            }
            if (!n->list.len) buf_put(&b, "0");
            buf_putc(&b, '}');
            return bor(buf_take(&b));
        }
        return bor(xstrdup("0"));
    }
}

static CExpr gen_logical(Node *n, Ctx *c) {
    char *a = gen_value(n->a, c);
    Ctx sub;
    ctx_init(&sub, c->indent + 1);
    char *b = gen_value(n->b, &sub);
    const char *op = n->op == P_ANDAND ? "&&" : "||";
    if (!sub_dirty(&sub)) {
        ctx_free(&sub);
        return bor(xfmt("(%s %s %s)", a, op, b));
    }
    char *r = fresh("c");
    raw_line(&c->pre, c->indent, "bool %s = (%s) ? 1 : 0;", r, a);
    raw_line(&c->pre, c->indent, "if (%s%s) {", n->op == P_ANDAND ? "" : "!", r);
    sub_into(c, &sub, 0);
    raw_line(&c->pre, c->indent + 1, "%s = (%s) ? 1 : 0;", r, b);
    ctx_release(&sub, &c->pre);
    raw_line(&c->pre, c->indent, "}");
    ctx_free(&sub);
    return bor(r);
}

static CExpr gen_ternary(Node *n, Ctx *c) {
    char *cond = gen_value(n->a, c);
    Type *t = n->type;
    int m = managed(t);
    Ctx sa, sb;
    ctx_init(&sa, c->indent + 1);
    ctx_init(&sb, c->indent + 1);
    CExpr ea = gen_expr(n->b, &sa);
    CExpr eb = gen_expr(n->c, &sb);
    if (!sub_dirty(&sa) && !sub_dirty(&sb) && (!m || ea.owned == eb.owned)) {
        ctx_free(&sa);
        ctx_free(&sb);
        return (CExpr){xfmt("(%s ? %s : %s)", cond, ea.s, eb.s), m && ea.owned};
    }
    char *r = fresh("v");
    char *d = cdecl(t, r);
    raw_line(&c->pre, c->indent, "%s;", d);
    raw_line(&c->pre, c->indent, "if (%s) {", cond);
    char *va = m ? consume(&sa, ea, t) : ea.s;
    ctx_flush_pre(&sa, &c->pre);
    raw_line(&c->pre, c->indent + 1, "%s = %s;", r, va);
    ctx_release(&sa, &c->pre);
    raw_line(&c->pre, c->indent, "} else {");
    char *vb = m ? consume(&sb, eb, t) : eb.s;
    ctx_flush_pre(&sb, &c->pre);
    raw_line(&c->pre, c->indent + 1, "%s = %s;", r, vb);
    ctx_release(&sb, &c->pre);
    raw_line(&c->pre, c->indent, "}");
    ctx_free(&sa);
    ctx_free(&sb);
    if (m) {
        Temp tmp = {xstrdup(r), t};
        vec_push(c->temps, tmp);
    }
    return bor(r);
}

static const char *cmp_op(int op) {
    switch (op) {
    case P_LT: return "<";
    case P_GT: return ">";
    case P_LE: return "<=";
    case P_GE: return ">=";
    default: return "==";
    }
}

static CExpr gen_binary(Node *n, Ctx *c) {
    switch (n->builtin) {
    case B_STR_CONCAT: {
        char *a = gen_borrow(n->a, c), *b = gen_borrow(n->b, c);
        return own(xfmt("mxy_str_concat(%s, %s)", a, b));
    }
    case B_STR_EQ: {
        char *a = gen_borrow(n->a, c), *b = gen_borrow(n->b, c);
        return bor(xfmt("(%smxy_str_eq(%s, %s))", n->op == P_NE ? "!" : "", a, b));
    }
    case B_STR_CMP: {
        char *a = gen_borrow(n->a, c), *b = gen_borrow(n->b, c);
        return bor(xfmt("(mxy_str_cmp(%s, %s) %s 0)", a, b, cmp_op(n->op)));
    }
    case B_VAL_EQ: {
        char *a = gen_borrow(n->a, c), *b = gen_borrow(n->b, c);
        char *ta = hoist(c, a, n->a->type, 0), *tb = hoist(c, b, n->b->type, 0);
        char *vw = vw_of(n->a->type);
        return bor(xfmt("(%smxy_vw_eq(%s, &%s, &%s))", n->op == P_NE ? "!" : "", vw, ta, tb));
    }
    case B_LIST_CONCAT: {
        char *a = gen_borrow(n->a, c), *b = gen_borrow(n->b, c);
        char *vw = vw_of(type_unqual(n->type)->base);
        return own(xfmt("mxy_list_concat(%s, %s, %s)", a, b, vw));
    }
    default:
        break;
    }
    if (n->op == P_ANDAND || n->op == P_OROR) return gen_logical(n, c);
    char *a = gen_value(n->a, c);
    char *b = gen_value(n->b, c);
    return bor(xfmt("(%s %s %s)", a, tok_spell((TokKind)n->op), b));
}

static CExpr gen_index(Node *n, Ctx *c) {
    Type *u = type_unqual(n->a->type);
    char *loc = loc_lit(n->loc);
    if (u->kind == TY_LIST) {
        char *l = gen_borrow(n->a, c);
        char *i = gen_value(n->b, c);
        char *et = ctype(u->base);
        return bor(xfmt("(*(%s *)mxy_list_at(%s, %s, %s))", et, l, i, loc));
    }
    if (u->kind == TY_MAP) {
        char *m = gen_borrow(n->a, c);
        char *k = gen_consume(n->b, c);
        char *kt = hoist(c, k, u->key, 1);
        char *vt = ctype(u->base);
        char *kvw = vw_of(u->key);
        return bor(xfmt("(*(%s *)mxy_map_get(%s, %s, &%s, %s))", vt, m, kvw, kt, loc));
    }
    if (u->kind == TY_STRING) {
        char *s = gen_borrow(n->a, c);
        char *i = gen_value(n->b, c);
        return bor(xfmt("mxy_str_at(%s, %s, %s)", s, i, loc));
    }
    char *a = gen_value(n->a, c);
    char *i = gen_value(n->b, c);
    return bor(xfmt("%s[%s]", a, i));
}

static CExpr gen_result_ctor(Node *n, Ctx *c) {
    char *T = ctype(n->type);
    Type *inner = type_unqual(type_unqual(n->type)->base);
    if (n->kind == N_OK) {
        if (inner->kind == TY_VOID || !n->a) return own(xfmt("((%s){ .ok = 1 })", T));
        char *v = consume(c, gen_init(n->a, inner, c), inner);
        if (is_brace(v)) v = xfmt("(%s)%s", ctype(inner), v);
        return own(xfmt("((%s){ .ok = 1, .val = %s })", T, v));
    }
    char *e = gen_consume(n->a, c);
    return own(xfmt("((%s){ .ok = 0, .err = %s })", T, e));
}

CExpr gen_enum_ctor(Node *n, Ctx *c) {
    Type *u = type_unqual(n->type);
    char *variant = xfmt("%s_%s", u->cname, n->name2);
    if (u->simple) return bor(variant);
    Node *v = n->d;
    if (!v->params.len) return bor(xfmt("((%s){ .tag = %s })", u->cname, variant));
    Buf b = {0};
    buf_fmt(&b, "((%s){ .tag = %s, .%s = { ", u->cname, variant, n->name2);
    for (int i = 0; i < v->params.len && n->kind == N_CALL && i < n->list.len; i++) {
        Type *pt = v->params.items[i].type->resolved;
        char *val = consume(c, gen_init(n->list.items[i], pt, c), pt);
        if (is_brace(val)) val = xfmt("(%s)%s", ctype(pt), val);
        buf_fmt(&b, "%s.%s = %s", i ? ", " : "", v->params.items[i].name, val);
    }
    buf_put(&b, " } })");
    return (CExpr){buf_take(&b), managed(u)};
}

static CExpr gen_await(Node *n, Ctx *c) {
    char *f = gen_borrow(n->a, c);
    char *loc = loc_lit(n->loc);
    Type *t = n->type;
    if (type_unqual(t)->kind == TY_VOID) {
        raw_line(&c->pre, c->indent, "(void)mxy_future_await(%s, %s);", f, loc);
        return bor(xstrdup("((void)0)"));
    }
    char *v = xfmt("(*(%s *)mxy_future_await(%s, %s))", ctype(t), f, loc);
    if (managed(t)) return own(retain_of(t, v));
    return bor(v);
}

CExpr gen_expr(Node *n, Ctx *c) {
    switch (n->kind) {
    case N_INT: case N_FLOAT:
        return bor(xstrdup(n->text));
    case N_CHAR:
        return bor(xfmt("'%s'", n->text));
    case N_STR:
        if (n->op == 1) return bor(c_string_lit(n->text));
        return bor(lit_ref(n->text));
    case N_INTERP:
        return gen_interp(n, c);
    case N_BOOL:
        return bor(xstrdup(n->op ? "true" : "false"));
    case N_NULL:
        return bor(xstrdup("NULL"));
    case N_IDENT:
        if (n->builtin == B_ENUM_CTOR) return gen_enum_ctor(n, c);
        if (n->sym) return bor(sym_ref(n->sym));
        return bor(xstrdup(n->name));
    case N_UNARY: {
        if (n->op == P_AMP) return bor(xfmt("(&%s)", gen_lvalue(n->a, c)));
        if (n->op == P_INC || n->op == P_DEC) return bor(xfmt("(%s%s)", tok_spell((TokKind)n->op), gen_lvalue(n->a, c)));
        char *a = gen_value(n->a, c);
        return bor(xfmt("(%s%s)", tok_spell((TokKind)n->op), a));
    }
    case N_POSTFIX:
        return bor(xfmt("(%s%s)", gen_lvalue(n->a, c), tok_spell((TokKind)n->op)));
    case N_BINARY:
        return gen_binary(n, c);
    case N_ASSIGN:
        return gen_assign(n, c);
    case N_TERNARY:
        return gen_ternary(n, c);
    case N_COMMA: {
        CExpr a = gen_expr(n->a, c);
        if (managed(n->a->type) && a.owned) {
            char *r = release_expr(n->a->type, a.s);
            raw_line(&c->pre, c->indent, "%s;", r);
            return gen_expr(n->b, c);
        }
        CExpr b = gen_expr(n->b, c);
        return (CExpr){xfmt("(%s, %s)", a.s, b.s), b.owned};
    }
    case N_CALL:
        return gen_call(n, c);
    case N_INDEX:
        return gen_index(n, c);
    case N_MEMBER:
        return gen_member(n, c);
    case N_CAST: {
        if (n->op == 1) return gen_expr(n->a, c);
        char *t = ctype(n->type);
        char *a = gen_value(n->a, c);
        return bor(xfmt("((%s)%s)", t, a));
    }
    case N_SIZEOF: {
        if (n->tref) return bor(xfmt("sizeof(%s)", ctype(n->tref->resolved)));
        Ctx throwaway;
        ctx_init(&throwaway, 0);
        char *a = gen_value(n->a, &throwaway);
        ctx_free(&throwaway);
        return bor(xfmt("sizeof(%s)", a));
    }
    case N_ALIGNOF:
        return bor(xfmt("_Alignof(%s)", ctype(n->tref->resolved)));
    case N_COMPOUND: case N_STRUCTLIT: {
        Type *t = n->type;
        Type *u = type_unqual(t);
        CExpr e = gen_init(n->b, t, c);
        if (u->kind == TY_LIST || u->kind == TY_MAP || (u->kind == TY_STRUCT && u->is_ref)) return e;
        if (is_brace(e.s)) e.s = xfmt("((%s)%s)", ctype(t), e.s);
        return e;
    }
    case N_INITLIST: {
        CExpr e = gen_init(n, n->type, c);
        Type *u = type_unqual(n->type);
        if (is_brace(e.s) && u && u->kind != TY_ARRAY) e.s = xfmt("((%s)%s)", ctype(n->type), e.s);
        return e;
    }
    case N_LIST:
        return gen_list_lit(&n->list, n->type, c);
    case N_OK: case N_ERR:
        return gen_result_ctor(n, c);
    case N_AWAIT:
        return gen_await(n, c);
    case N_LAMBDA: {
        int owned = 0;
        char *v = lambda_value(n, c, &owned);
        return (CExpr){v, owned};
    }
    case N_CONV:
        return gen_conv(n, c);
    case N_TYPEARG:
        return bor(ctype(n->tref->resolved));
    default:
        return bor(xstrdup("0"));
    }
}
