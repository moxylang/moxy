# Changes

## 0.3.1

### Compiler

- The compiler is written in Moxy and compiles itself. `make` bootstraps from `bootstrap/moxy.c`, so building from source still needs only a C compiler; `make verify` checks that the result reproduces itself exactly.
- 0.3.0's release binaries failed to build against an outdated goose; goose is now pinned to a version that also builds with gcc 14.

### C compatibility

- A macro that is redefined or `#undef`'d means what it meant where it is used, as in C; before, the last definition applied everywhere. Macros defined once stay visible to the whole program.
- Your macros no longer reach Moxy's runtime, so a name like `#define len 3` is safe.
- A variable or function can share a struct's name: `static struct config config;`, or `struct timer` beside `int timer()`.
- Structs, unions and typedefs defined inside a function belong to that function, so two functions can each define their own `struct item`. Defining a struct inside a function no longer crashes the compiler.
- A struct that is only declared (`struct handle;`) and used through pointers compiles; its name was missing from the generated C.
- `string` and `null` can be used as names (`char *string`, a field called `null`); they keep their Moxy meaning everywhere else.
- `p->field` works when `p` points to a type from a C header, as it does for values of those types.
- C headers next to any `.mxy` file are found, in single-file builds, tests and projects.
- An include never resolves to the including file, so a test can share its module's name (`tests/words.mxy` including `src/words.mxy`).
- `_Noreturn` is kept in the generated C, and assignments used as conditions keep their parentheses, so the generated C compiles without warnings under `-Wall -Wextra`.

### Tooling

- The formatter keeps the spaces in closure types and designated initializers: `fn(int) -> int`, `Point{ .x = 1, .y = 2 }`.

## 0.3.0

A rewrite of the compiler and runtime. Most 0.2 programs compile unchanged; the behavior changes below are deliberate.

### Memory management

- Automatic memory management is always on; `--enable-arc` and `--enable-async` are accepted and ignored. Strings, lists, maps, closures and futures are reference counted and freed when no longer used. See [memory.md](book/memory.md).
- **Lists and maps are values** (copy-on-write). In 0.2 without ARC, `b = a` shared a buffer that could be freed under you; with ARC, `b = a` aliased. Now `b` is an independent copy.
- `string` is a managed, immutable string that is still a `const char *` to C. `==` compares contents, and `+` concatenates.
- New `ref struct` (shared objects) with `weak` fields, and new `defer`.

### Language

- Full C parsing, with no raw passthrough: struct and union definitions, declarators, `switch`, `goto`, compound literals, designated initializers. Typos are reported at the right place.
- A real semantic pass: every expression has a type, and names are scoped per function and block.
- `Result<T>` works as a return type: `return Ok(x)`, `return Err("...")`, and `Result<void>`, plus `unwrap`, `unwrap_or`, `is_ok`, `is_err` and `error()`.
- Lists of structs, structs with string, list and map fields, and methods on fields (`user.tags.push(x)`).
- `for x in <any expression>`, `for i, x in list`, ranges `a..b` and `a..=b`, iteration over strings and C arrays.
- `match`:
  - binds every payload field (`Rect(w, h)`), and `_` skips one;
  - accepts shorthand patterns (`Circle(r)`) and literal patterns (numbers, chars, strings, bools);
  - matches any expression;
  - is checked for exhaustiveness at compile time, naming the missing variants.
- Lambdas capture variables (by value), and closure types are written `fn(A) -> R`. `map`, `filter` and `each` on lists.
- Reassigning collections: `xs = [..]`, `m = {..}`, `s += "..."`, `xs += ys`.
- Map literals `{"k": v}`, `m[k]` indexing and assignment, `get_or`, `remove`, `keys`, `values`.
- String methods: `split`, `trim`, `upper`, `lower`, `replace`, `find`, `slice`, `lines`, `to_int`, `to_float`, `repeat`, `chars`.
- `print` accepts several values. It prints `bool` as `true`/`false`, floats in shortest round-trip form (`3.14`, not `3.140000`), and lists, maps, structs and enums readably.
- String interpolation `"\(expr)"`, `string(x)` formatting, and `auto` type inference.
- `main(string[] args)` and `int main(int argc, char **argv)`.
- `return` anywhere inside `Future<T>` functions, futures of any type, and future handles that can be stored and awaited later.
- `namespace`, nested namespaces, and `using`.

### Modules and C interop

- Each `.mxy` file is included once, so diamond includes and include cycles work.
- Errors report the right file and line after includes, and C compiler errors map back to `.mxy` lines through `#line`.
- `#if`/`#ifdef`/`#else` are honored around declarations and inside functions, so each branch can define the same function.
- String literals passed to C functions stay plain C literals (format checking works).
- Types can be passed to C macros (`va_arg(ap, int)`, `offsetof(T, f)`).

### Safety

- No fixed-size limits in the compiler: programs of any size, any number of declarations, statements, list items, enum variants, parameters, or string length.
- List indexing is bounds-checked, and missing map keys panic with the key, instead of reading garbage.
- A managed local declared directly under a `case` label, or a managed field in a union, is a compile error.

### Tooling

- `moxy` is the only executable: goose is linked in as a library. Build moxy with `make`.
- `moxy test` runs `tests/**/*.mxy` and `*.test.mxy`, with `.out`, `.code` and `.err` expectations and `--sanitize`. The `_test.mxy` naming still works.
- `moxy check`, `moxy emit`, `moxy run -- args`, and `-p member` for workspaces.
- Library projects build `lib<name>.a` plus a C header.
- The token-based formatter is idempotent and keeps `Result<T>` intact; the linter reports unused variables, shadowing and empty blocks.
- CI runs the suite on Linux and macOS, plus a sanitizer job.

### Behavior changes to check when upgrading

| 0.2 | 0.3 |
|---|---|
| `print(true)` printed `1` | prints `true` |
| `print(3.14)` printed `3.140000` | prints `3.14` |
| `m.get(missing)` returned 0 | panics; use `get_or` |
| list aliasing (with `--enable-arc`) | lists are values |
| `string` + pointer arithmetic (`s + 3`) | not allowed on `string`; cast to `const char *` first |
| tests named `*_test.mxy` | still found; `tests/<area>/<name>.mxy` preferred |
