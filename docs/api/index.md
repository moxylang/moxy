# Compiler internals

A map of the source tree for contributors. [architecture.md](../book/architecture.md) explains how the pieces fit together; this page says where things live.

## Layout

Every directory under `src/` is a module: `index.mxy` declares its types and functions and includes its implementation files.

```
src/
  main.mxy          command dispatch (file commands, then goose)
  base/             allocation (xmalloc...), Buf string builder, Vec, files, StrMap
  diag/             multi-file diagnostics, recoverable bail (setjmp)
  lex/              tokens and lexer
  load/             include resolution, include-once splicing
  ast/              the AST node and type syntax (TypeRef)
  parse/            parser (state, types, expr, stmt, top)
  types/            semantic types, C spelling, mangling, Moxy-syntax names
  sema/             scopes, resolution, checking, conversions, match, lambdas
  gen/              C generation (expr, call, lvalue, stmt, loop, match, func,
                    types, helpers, own, header, out, codegen)
  embed/            generated: runtime and std sources compiled into moxy
  fmt/              formatter
  conf/             moxyfmt.yaml loader
  cli/              run/build/test/fmt/lint/check/emit and goose callbacks
runtime/            C runtime embedded into generated programs
std/                standard library modules (.mxy)
scripts/genembed.sh regenerates src/embed/data.mxy (make does this)
bootstrap/moxy.c    C generated from src/ by an earlier moxy; make starts here
tests/              test suite, one directory per area
libs/goose/         goose sources (submodule), linked as libgoose.a
```

## Key interfaces

| Function | Where | Purpose |
|---|---|---|
| `TokenVec load_program(const char *path)` | `load/index.mxy` | read, lex and splice includes |
| `void load_add_include_dir(const char *dir)` | `load/index.mxy` | add an include search path |
| `Node *parse_program(TokenVec)` | `parse/index.mxy` | tokens to an `N_BLOCK` of declarations |
| `int sema_check(Node *root, int lint)` | `sema/index.mxy` | check the program; fills the global `prog` |
| `char *codegen(const GenOptions *)` | `gen/index.mxy` | C source for the checked program |
| `char *compile_to_c(path, opts, &threads, &has_main)` | `cli/index.mxy` | the whole pipeline, recoverable on errors |
| `char *fmt_source(const char *, const MoxyConfig *)` | `fmt/index.mxy` | format source text |
| `int project_main(int argc, char **argv)` | `cli/index.mxy` | goose framework entry |

### The AST (`ast/index.mxy`)

One `Node` struct serves every kind. The fields used by each `NodeKind` are listed next to the enum.

Sema annotates nodes with:

- `type`: the expression's type;
- `sym`: the resolved symbol;
- `builtin`: which builtin operation a call or operator is (`B_*` in `sema/index.mxy`);
- `captures`: for lambdas, the captured symbols.

Implicit conversions appear as `N_CONV` nodes; `op` holds the `CV_*` kind.

### Types (`types/index.mxy`)

`Type` covers C types (`TY_INT` ... `TY_FUNC`, `TY_STRUCT`, `TY_CENUM`) and Moxy types (`TY_STRING`, `TY_LIST`, `TY_MAP`, `TY_RESULT`, `TY_FUTURE`, `TY_CLOSURE`, `TY_ENUM`).

- `TY_NAMED` is an opaque C type from a header.
- `TY_UNKNOWN` is a C value Moxy can't see into.
- `type_is_managed` decides whether a type needs reference counting.
- `type_cdecl` spells a C declaration.
- `type_mangle` produces identifier-safe names for helpers.

### Ownership in codegen (`gen/index.mxy`)

`gen_expr` returns `CExpr {s, owned}`. The helpers that move between ownership states:

| Helper | Purpose |
|---|---|
| `borrow` | use without taking a reference; owned values are hoisted into a temporary that is released after the statement |
| `consume` | take a reference, retaining borrowed values |
| `hoist` | store in a temporary |
| `retain_of` / `release_expr` / `release_into` | per-type reference operations |
| `vw_of` | value witness for runtime collections |

Cleanup scopes (`gscope_push`/`gscope_pop`, `add_cleanup_var`, `emit_cleanups_to`) make every exit path release the right values and run `defer`s.

## Adding things

**A builtin method.**

1. Add a `B_*` constant in `sema/index.mxy`.
2. Add a row in the method table in `sema/method.mxy`, with its type rule.
3. Generate it in `gen/call.mxy`, using a runtime function in `runtime/*.h` if needed.
4. Add a test under `tests/`.

**A runtime function.** Add it to the right `runtime/*.h` as `MXY_FN` (static, unused-safe). Make rebuilds `src/embed/data.mxy`. If Moxy code in `std/` needs it, declare it there with an `extern` prototype using Moxy types. Names starting with `mxy_` are never re-declared in generated C.

**A standard library module.** Add `std/<name>.mxy`. It is embedded automatically and available as `#include "std/<name>.mxy"`.

**A diagnostic.** Use `diag_error(loc, fmt, ...)` and optionally `diag_hint(...)`. Add a failure test: `tests/fail/<name>.mxy` with the expected message in `<name>.err`.

## Building and testing

```sh
make                                # build/moxy (bootstraps from bootstrap/moxy.c)
make verify                         # build/moxy reproduces its own C
make seed                           # refresh bootstrap/moxy.c after verify
make sanitize                       # build/asan/moxy with ASan and UBSan
make test                           # tests and examples
./build/asan/moxy test --sanitize tests
./build/moxy fmt --check tests examples std tools
```
