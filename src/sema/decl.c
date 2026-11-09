#include <string.h>

#include "diag/diag.h"
#include "sema.h"

Program prog;
int lint_mode;

Type *check_init(Node **slot, Type *t);

static Scope *scope_of(Node *n) { return ns_scope(n->ns); }

/* definitions in different #if branches may share a name */
static int alt_branch(Node *a, Node *b) {
    return a && b && (a->cond || b->cond) && !str_eq(a->cond, b->cond);
}

static void declare_enum(Node *n) {
    Scope *s = scope_of(n);
    if (scope_get(s, n->name)) {
        diag_error(n->loc, "'%s' is already declared", n->name);
        return;
    }
    Type *t = type_new(TY_ENUM);
    t->name = xstrdup(n->name);
    t->cname = cname_for(n->ns, n->name);
    t->decl = n;
    t->simple = 1;
    for (int i = 0; i < n->list.len; i++)
        if (n->list.items[i]->params.len) t->simple = 0;
    n->type = t;
    Sym *sym = sym_new(S_TYPE, n->name, n->loc);
    sym->type = t;
    sym->cname = xstrdup(t->cname);
    sym->decl = n;
    scope_declare(s, sym);
    vec_push(prog.enums, n);
}

static void declare_func(Node *fn) {
    Scope *s = scope_of(fn);
    Sym *existing = scope_get(s, fn->name);
    vec_push(prog.funcs, fn);
    if (existing && existing->kind == S_FUNC) {
        if (fn->a) {
            if (existing->has_body && alt_branch(existing->decl, fn)) {
                fn->sym = existing;
                return;
            }
            if (existing->has_body) {
                diag_error(fn->loc, "function '%s' is defined twice", fn->name);
                diag_note(existing->loc, "first definition is here");
            }
            existing->decl = fn;
            existing->has_body = 1;
            existing->loc = fn->loc;
        }
        fn->sym = existing;
        return;
    }
    if (existing && !(existing->kind == S_TYPE && existing->decl && existing->decl->kind == N_STRUCT)) {
        diag_error(fn->loc, "'%s' is already declared", fn->name);
        return;
    }
    Sym *sym = sym_new(S_FUNC, fn->name, fn->loc);
    sym->global = 1;
    sym->decl = fn;
    sym->has_body = fn->a != NULL;
    sym->cname = cname_for(fn->ns, fn->name);
    scope_declare(s, sym);
    fn->sym = sym;
}

static void declare_global(Node *v) {
    Scope *s = scope_of(v);
    Sym *existing = scope_get(s, v->name);
    vec_push(prog.globals, v);
    if (existing && existing->kind == S_VAR) {
        if (alt_branch(existing->decl, v)) {
            v->sym = existing;
            return;
        }
        if (v->a && existing->decl && existing->decl->a) {
            diag_error(v->loc, "global '%s' is defined twice", v->name);
        } else if (v->a || !(v->flags & F_EXTERN)) {
            existing->decl = v;
        }
        v->sym = existing;
        return;
    }
    if (existing) {
        diag_error(v->loc, "'%s' is already declared", v->name);
        return;
    }
    Sym *sym = sym_new(S_VAR, v->name, v->loc);
    sym->global = 1;
    sym->decl = v;
    sym->cname = cname_for(v->ns, v->name);
    scope_declare(s, sym);
    v->sym = sym;
}

static void declare_typedef(Node *td) {
    Scope *s = scope_of(td);
    vec_push(prog.typedefs, td);
    Sym *existing = scope_get(s, td->name);
    TypeRef *tr = td->tref;
    if (tr->kind == TR_TAG && tr->def && !tr->def->name) {
        tr->def->ns = td->ns;
        tr->def->cond = td->cond;
        Type *t = tr->def->kind == N_CENUM ? define_cenum(tr->def) : define_struct(tr->def, td->name);
        if (tr->def->kind == N_CENUM && !t->cname) t->cname = cname_for(td->ns, td->name), td->op = 1;
        tr->resolved = t;
    }
    if (existing && existing->kind == S_TYPE && existing->decl && existing->decl->kind == N_TYPEDEF) {
        /* same typedef in different #if branches, or a harmless repeat */
        return;
    }
    if (existing && existing->kind != S_TYPE) {
        diag_error(td->loc, "'%s' is already declared", td->name);
        return;
    }
    Sym *sym = existing ? existing : sym_new(S_TYPE, td->name, td->loc);
    sym->decl = td;
    sym->type = NULL;
    sym->cname = cname_for(td->ns, td->name);
    if (!existing) scope_declare(s, sym);
}

static void pass_declare(Node *root) {
    for (int i = 0; i < root->list.len; i++) {
        Node *n = root->list.items[i];
        if (n->kind == N_TYPEDEF && n->tref->kind == TR_TAG && n->tref->def && !n->tref->def->name) {
            n->tref->def->ns = n->ns;
            n->tref->def->cond = n->cond;
            if (n->tref->def->kind == N_STRUCT) define_struct(n->tref->def, n->name);
        }
    }
    for (int i = 0; i < root->list.len; i++) {
        Node *n = root->list.items[i];
        switch (n->kind) {
        case N_STRUCT: define_struct(n, NULL); break;
        case N_CENUM: define_cenum(n); break;
        case N_ENUM: declare_enum(n); break;
        case N_FUNC: declare_func(n); break;
        case N_VAR: declare_global(n); break;
        case N_TYPEDEF: declare_typedef(n); break;
        case N_DIRECTIVE: vec_push(prog.directives, n); break;
        case N_STATIC_ASSERT: vec_push(prog.asserts, n); break;
        default: break;
        }
    }
    for (int i = 0; i < root->list.len; i++) {
        Node *n = root->list.items[i];
        if (n->kind != N_USING) continue;
        Sym *target = NULL;
        Scope *saved = cur_scope;
        cur_scope = scope_of(n);
        target = lookup_quiet(n->name);
        cur_scope = saved;
        if (!target || target->kind != S_NS) {
            diag_error(n->loc, "unknown namespace '%s'", n->name);
            continue;
        }
        vec_push(scope_of(n)->imports, target->members);
    }
}

static void complete_struct(Node *decl) {
    Type *t = decl->type;
    for (int i = 0; i < decl->list.len; i++) {
        Node *f = decl->list.items[i];
        if (f->kind != N_FIELD) continue;
        if (f->tref->kind == TR_TAG && f->tref->def) {
            f->tref->def->ns = decl->ns;
            f->tref->def->cond = decl->cond;
        }
        f->type = resolve_type(f->tref);
        Type *u = type_unqual(f->type);
        if (u->kind == TY_STRUCT && u->decl && !u->is_ref && u != t && !u->decl->name && f->name == NULL) {
            u->decl->flags |= F_INLINE;
        }
        if (f->flags & F_WEAK) {
            if (u->kind != TY_STRUCT || !u->is_ref)
                diag_error(f->loc, "weak fields must hold a ref struct");
        }
        if (u == t && !t->is_ref) diag_error(f->loc, "struct '%s' cannot contain itself", t->name);
    }
    if (t->is_union) {
        for (int i = 0; i < decl->list.len; i++) {
            Node *f = decl->list.items[i];
            if (f->kind == N_FIELD && f->type && type_is_managed(f->type))
                diag_error(f->loc, "unions cannot hold managed values like strings, lists or maps");
        }
        if (t->is_ref) diag_error(decl->loc, "ref unions are not supported");
    }
}

static void complete_enum(Node *n) {
    for (int i = 0; i < n->list.len; i++) {
        Node *v = n->list.items[i];
        for (int j = 0; j < v->params.len; j++) resolve_type(v->params.items[j].type);
    }
}

static void resolve_signature(Node *fn) {
    Sym *s = fn->sym;
    if (!s) return;
    Scope *saved = cur_scope;
    cur_scope = scope_of(fn);
    Type *ret = resolve_type(fn->tref);
    TypeList params = {0};
    for (int i = 0; i < fn->params.len; i++) {
        Type *p = resolve_type(fn->params.items[i].type);
        Type *u = type_unqual(p);
        if (u->kind == TY_ARRAY) p = type_ptr(u->base);
        else if (u->kind == TY_FUNC) p = type_ptr(u);
        vec_push(params, p);
    }
    cur_scope = saved;
    Type *ur = type_unqual(ret);
    if (ur->kind == TY_FUTURE) {
        if (fn->a) s->is_async = 1;
        if (s->is_async || fn->a) {
            ret = ur->base;
            prog.threads = 1;
        }
    } else if (s->is_async && fn->a == NULL) {
        return;
    }
    Type *ft = type_func(ret, params, fn->op);
    if (s->type && fn->a == NULL) return;
    s->type = ft;
    fn->type = ft;
    if (strcmp(fn->name, "main") == 0 && !*fn->ns && fn->a) {
        prog.main = fn;
        if (ur->kind != TY_VOID && ur->kind != TY_INT)
            diag_error(fn->loc, "main must return void or int");
        int ok = params.len == 0;
        if (params.len == 1) {
            Type *p = type_unqual(params.items[0]);
            ok = p->kind == TY_LIST && type_unqual(p->base)->kind == TY_STRING;
        }
        if (params.len == 2) ok = type_is_integer(params.items[0]);
        if (!ok) diag_error(fn->loc, "main takes no parameters, (string[] args), or (int argc, char **argv)");
    }
}

static void resolve_global(Node *v) {
    Sym *s = v->sym;
    if (!s || v->op == 1) return;
    Scope *saved = cur_scope;
    cur_scope = scope_of(v);
    Type *t = resolve_type(v->tref);
    cur_scope = saved;
    if (!s->type || v->a) s->type = t;
    v->type = t;
}

static void check_global(Node *v) {
    Sym *s = v->sym;
    if (!s || (s->decl != v && !v->a)) return;
    Scope *saved = cur_scope;
    FnCtx *saved_fn = cur_fn;
    FnCtx ctx = {0};
    ctx.ret = ty_void;
    cur_fn = &ctx;
    cur_scope = scope_of(v);
    if (v->op == 1) {
        Type *t = check_expr(&v->a, NULL);
        if (type_unqual(t)->kind == TY_NULL || type_unqual(t)->kind == TY_VOID) {
            diag_error(v->loc, "cannot infer a type for '%s'", v->name);
            t = ty_unknown;
        }
        s->type = t;
        v->type = t;
    } else if (v->a) {
        check_init(&v->a, s->type);
        coerce(&v->a, s->type, "initializer");
        Type *u = type_unqual(s->type);
        if (u->kind == TY_ARRAY && u->len < 0 && !u->cname && v->a->kind == N_INITLIST) {
            s->type = type_array(u->base, v->a->list.len);
            v->type = s->type;
        }
    }
    if (v->flags & F_WEAK) diag_error(v->loc, "'weak' is only allowed on struct fields");
    if (type_unqual(s->type)->kind == TY_VOID) diag_error(v->loc, "variables cannot have type void");
    cur_scope = saved;
    cur_fn = saved_fn;
}

void tags_reset(void);

static void reset(void) {
    tags_reset();
    memset(&prog, 0, sizeof(prog));
    global_scope = NULL;
    cur_scope = NULL;
    cur_fn = NULL;
    global_scope = scope_push(SC_GLOBAL);
    global_scope->ns = xstrdup("");
}

int sema_check(Node *root, int lint) {
    types_init();
    reset();
    lint_mode = lint;
    prog.prog = root;
    int before = diag_errors();
    pass_declare(root);
    for (int i = 0; i < prog.structs.len; i++) {
        Scope *saved = cur_scope;
        cur_scope = scope_of(prog.structs.items[i]);
        complete_struct(prog.structs.items[i]);
        cur_scope = saved;
    }
    for (int i = 0; i < prog.enums.len; i++) {
        Scope *saved = cur_scope;
        cur_scope = scope_of(prog.enums.items[i]);
        complete_enum(prog.enums.items[i]);
        cur_scope = saved;
    }
    for (int i = 0; i < prog.typedefs.len; i++) {
        Node *td = prog.typedefs.items[i];
        Scope *saved = cur_scope;
        cur_scope = scope_of(td);
        Sym *s = lookup_quiet(td->name);
        if (s && s->kind == S_TYPE && !s->type && s->decl == td) s->type = resolve_type(td->tref);
        else resolve_type(td->tref);
        cur_scope = saved;
    }
    for (int i = 0; i < prog.funcs.len; i++) {
        Node *fn = prog.funcs.items[i];
        if (fn->a) resolve_signature(fn);
    }
    for (int i = 0; i < prog.funcs.len; i++) {
        Node *fn = prog.funcs.items[i];
        if (!fn->a) resolve_signature(fn);
    }
    for (int i = 0; i < prog.globals.len; i++) resolve_global(prog.globals.items[i]);
    for (int i = 0; i < prog.globals.len; i++) check_global(prog.globals.items[i]);
    for (int i = 0; i < prog.funcs.len; i++) {
        Node *fn = prog.funcs.items[i];
        if (fn->a && fn->sym && (fn->sym->decl == fn || alt_branch(fn->sym->decl, fn))) check_function(fn);
    }
    for (int i = 0; i < prog.asserts.len; i++) check_expr(&prog.asserts.items[i]->a, NULL);
    return diag_errors() - before;
}
