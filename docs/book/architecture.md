# Architecture

`moxy` is one executable: the compiler, the test runner, the formatter, the linter, and the project and package manager. Project management comes from [goose](https://github.com/wess/goose), which is linked in as a library (`libgoose.a`). Moxy registers callbacks with goose's framework API for building, running, testing and installing, and goose handles `moxy.yaml`, dependency fetching, lock files, workspaces and the shared commands.

```
            ┌─────────────── moxy ───────────────┐
 .mxy ───►  load ─► lex ─► parse ─► sema ─► gen  ├──► .c ──► cc ──► binary
            │                                    │
            │  cli: run · build · test · fmt ·   │
            │       lint · check · emit          │
            │  goose: new · init · add · remove ·│
            │         update · workspaces · lock │
            └────────────────────────────────────┘
```

## Pipeline

### 1. Load (`src/load`)

`load_program` reads the entry file and lexes it. For every `#include "x.mxy"`, it resolves the path in this order:

1. relative to the including file (never the including file itself);
2. registered include directories (project `src/`, configured includes, dependency directories);
3. the standard library embedded in the binary.

The included file's tokens are spliced in place. A set of canonical paths makes each file load once, so diamond includes and include cycles are harmless. Every token remembers its file, so diagnostics always name the right file and line.

### 2. Lex (`src/lex`)

The lexer produces tokens with source locations. Notes:

- Preprocessor lines become single `T_DIRECTIVE` tokens.
- String literals keep their escapes verbatim and allow `\( ... )` interpolation with nested strings.
- Only C keywords plus `string`, `bool`, `true`, `false` and `null` are reserved. Moxy's other words (`match`, `in`, `map`, `defer`, ...) are recognized by the parser in context, so C code that uses them as names keeps working.

### 3. Parse (`src/parse`)

The parser is a recursive-descent parser for C11 declarations and statements plus Moxy syntax, producing one AST (`src/ast/ast.h`). Nothing is passed through as raw text: everything is parsed, so typos are reported at the right place.

| File | Contents |
|---|---|
| `state.c` | token cursor, and the type-name table: a prescan registers every struct, union and enum tag, typedef and `@type` name, so declarations parse before their types are defined |
| `types.c` | declaration specifiers, C declarators (pointers, arrays, nested function pointers), Moxy types, struct and enum bodies |
| `expr.c` | precedence climbing, casts and compound literals, lambdas, interpolation (sub-lexed), pipes desugared to calls |
| `stmt.c` | statements, `for`-in, `match` arms and patterns, `defer`, labels |
| `top.c` | declarations, namespaces, `using`, and conditional directives |

Each top-level declaration records the preprocessor condition it sits under, for example `defined(DEBUG) && !(X)`, so code generation can reproduce `#if` blocks around everything it emits, even after reordering.

### 4. Semantic analysis (`src/sema`)

Sema resolves every name and computes a type for every expression.

| File | Contents |
|---|---|
| `scope.c` | scope stack and namespace scopes; captures are recorded on lambdas when a name resolves to an enclosing function's local |
| `resolve.c` | type syntax to `Type`; struct, union and enum registration; typedefs |
| `decl.c` | three passes: declare everything, resolve signatures and fields, then check initializers and bodies |
| `expr.c`, `init.c`, `call.c`, `method.c` | expressions, initializer lists, calls and enum constructors, builtin methods on `string`, lists, maps and `Result` |
| `conv.c` | assignability; inserts `N_CONV` nodes for implicit conversions (C string to `string`, function to closure, capture-free lambda to C function pointer) |
| `stmt.c`, `match.c`, `lambda.c` | statements, exhaustiveness checking, lambda return inference, lint checks |

Identifiers that Moxy can't resolve are treated as C (`TY_UNKNOWN`): they pass through, and the C compiler checks them. Values of unknown type can't become lists, maps or other managed values.

### 5. Code generation (`src/gen`)

Code generation writes one C file in sections, assembled in a fixed order:

```
feature macros, user #include/#define (with their #if conditions)
runtime (embedded from runtime/*.h)
string literals (immortal static objects)
type forward declarations, then definitions in dependency order
value witnesses and copy/release helpers
prototypes and globals
glue: closure thunks, lambda environments, async launchers, main wrapper
program: functions with #line directives
```

Functions are generated first and type definitions last, so every type a function needs (such as `Result<Point>`) is known when the types section is written.

**Expressions** (`expr.c`, `call.c`, `lvalue.c`) return the C text plus whether the value is *owned* (+1) or *borrowed* (+0). Each statement has a context holding a prelude (statements that must run first) and temporaries to release afterwards. `&&`, `||` and `?:` with preludes on one side are lowered to `if` statements to keep short-circuit evaluation.

**Statements** (`stmt.c`, `loop.c`, `match.c`) keep a stack of cleanup scopes (managed locals and `defer` statements):

- leaving a block releases its scope;
- `return` releases all scopes, moving out a returned local;
- `break` and `continue` release only the scopes they leave.

**Types and helpers** (`types.c`, `helpers.c`): user structs, tagged enums and `Result<T>` structs are emitted in dependency order. Every type that is stored in a collection or printed gets a *value witness*, a table of copy, drop, equality, hash, compare and format functions. That lets one runtime implementation of lists and maps serve every element type.

**Functions** (`func.c`):

- Lambdas become functions that take their closure as the first argument; captured variables live in an environment struct after the closure header.
- Named functions used as closures get a thunk and a static, immortal closure object.
- `Future<T>` functions are split into a body, an argument struct, a thread entry, and a launcher that returns the future handle.

**Libraries** (`header.c`): library builds also write a C header exposing plain structs and the functions whose signatures C can express.

### 6. Runtime (`runtime/*.h`)

The runtime is C, embedded into every generated program (`scripts/genembed.sh` turns it into `src/embed/embed.c`). All functions are `static`, so separate translation units never collide.

| File | Contents |
|---|---|
| `core.h` | atomic reference counts, panics, `mxy_buf`, value witnesses |
| `str.h` | strings: header `{rc, len}` before the bytes; literals have rc −1 (immortal) |
| `fmt.h` | printing and value witnesses for builtin types; `MXY_FMT_ANY` uses `_Generic` for C values of unknown type |
| `list.h` | copy-on-write lists, bounds checks, sort, slices |
| `map.h` | insertion-ordered hash maps (an entry array plus an open-addressing index) |
| `obj.h` | ref objects with strong and weak counts |
| `fn.h` | closures |
| `future.h` | thread-per-call futures |
| `process.h` | `args()`, assertions, file reading |

## Command line (`src/cli`, `src/main.c`)

`main.c` handles the commands that work on files (`moxy file.mxy`, `run file.mxy`, `build file.mxy`, `check`, `test`, `fmt`, `lint`, `emit`), and hands everything else to `goose_main`. `-p member` changes into a workspace member first.

| File | Contents |
|---|---|
| `project.c` | the goose callbacks: `on_build` compiles the entry file (`src/main.mxy`, or `src/lib.mxy` for libraries) with include paths from configured includes and dependencies, adds extra C sources and dependency C sources, and links or archives; also `on_run`, `on_test`, `on_install`, the `new` template, and the `build:` keys `cc`, `cflags`, `ldflags` |
| `compile.c` (also) | `source_dirs` lists the directory of every loaded `.mxy` file; builds pass them as `-I` so C headers next to sources resolve |
| `test.c` | finds tests, compiles each in-process, runs it, and compares against `.out`, `.code` or `.err` files |
| `compile.c` | the pipeline in one call; errors jump back with `longjmp` instead of exiting, so one bad file doesn't stop a test run |

## Formatter and linter

- `src/fmt` is token-based: it keeps your line breaks and comments, and normalizes spacing and indentation. It is idempotent.
- The linter runs sema with lint flags (unused variables, shadowing, empty blocks); `moxyfmt.yaml` toggles them.

## Testing the compiler

```sh
make test                                          # build, then run tests/ and examples
make sanitize && ./build/asan/moxy test --sanitize tests
```

CI runs the suite on Linux and macOS, plus an ASan/UBSan build of the compiler running every test with sanitizers (and LeakSanitizer) enabled for the generated programs.
