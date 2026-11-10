# Moxy language reference

Moxy is C11 plus a small set of additions: managed strings, lists and maps, tagged enums with `match`, `Result` error values, closures, futures, namespaces, `defer`, and automatic memory management. Any C you write inside a `.mxy` file means what it means in C. This reference covers what Moxy adds and how it interacts with C.

- [Lexical structure](#lexical-structure)
- [Types](#types)
- [Declarations](#declarations)
- [Expressions](#expressions)
- [Statements](#statements)
- [Pattern matching](#pattern-matching)
- [Errors and Result](#errors-and-result)
- [Built-ins and methods](#built-ins-and-methods)
- [Closures](#closures)
- [Concurrency](#concurrency)
- [Modules](#modules)
- [Working with C](#working-with-c)
- [Standard library](#standard-library)

Memory management has its own chapter: [memory.md](memory.md).

## Lexical structure

**Comments**: `// line` and `/* block */`.

**Reserved words**: every C11 keyword, plus `bool`, `true` and `false` (keywords in C23 too). `string` and `null` are the string type and the null pointer, but they can also name a variable, parameter, field or function, as they can in C.

**Contextual words**: `match`, `in`, `defer`, `await`, `namespace`, `using`, `ref`, `weak`, `fn`, `map`, `Result`, `Future`, `Ok` and `Err` have meaning only in their own syntax. They remain valid identifiers elsewhere, so C code that uses `map`, `in` or `match` as variable names keeps compiling.

**Literals**

| Kind | Examples |
|---|---|
| integer | `42`, `0x2A`, `0b101010`, `052`, `42u`, `42L`, `9000000000` (inferred `long`) |
| float | `3.14` (double), `2.5f` (float), `1e9`, `0x1p4` |
| char | `'a'`, `'\n'`, `'\x41'` |
| string | `"text"`, `"tab\tnewline\n"`; adjacent literals concatenate: `"a" "b"` |
| interpolated string | `"\(name) is \(age) years old"` |
| bool | `true`, `false` |
| null | `null` (also C's `NULL`) |

Inside a string, `\(expr)` evaluates `expr` and formats it the way `print` does. Interpolation nests: `"outer \("inner \(x)")"`.

## Types

### C types

All C types work as written: `int`, `unsigned long`, `char *`, `const char *`, fixed arrays `int buf[16]`, function pointers `int (*cmp)(const void *, const void *)`, `struct`, `union`, C `enum`, and `typedef`. Types from headers (`FILE`, `size_t`, `SDL_Window`) can be used without declaring them.

### `bool`

`bool` with `true` and `false`. Comparisons and logical operators produce `bool`, so `print(1 < 2)` prints `true`.

### `string`

An immutable, reference-counted UTF-8 byte string. In generated C it is a `const char *` to NUL-terminated data, so it can be passed to any C function that reads a string.

```c
string name = "moxy";
string greeting = "hello " + name;      // concatenation
bool same = greeting == "hello moxy";   // content comparison
print(name.len, name.upper(), name[0]); // 4 MOXY m
```

- `==`, `!=`, `<`, `<=`, `>`, `>=` compare contents; comparing with `null` compares the pointer.
- `s[i]` is a bounds-checked `char`.
- A `char *` from C becomes a `string` by copying, which happens automatically on assignment, or explicitly with `string(p)`.
- Adding a number to a string is an error. Use interpolation: `"n = \(n)"`.

### Lists: `T[]`

Growable, bounds-checked arrays with value semantics (copy-on-write).

```c
int[] xs = [1, 2, 3];
xs.push(4);
int[] ys = xs;    // independent copy
ys[0] = 100;      // xs[0] is still 1
Point[] pts = [Point{ .x = 1, .y = 2 }];
int[][] grid = [[1, 2], [3, 4]];
```

`int[] xs = [];` is an empty list. `null` is also an empty list. Lists of any type work, including structs, enums, other lists, maps, strings, closures and ref objects.

### Maps: `map[K, V]`

Hash maps that remember insertion order, with value semantics. Keys can be any type with equality: numbers, chars, bools, strings, enums, or structs of those.

```c
map[string, int] ages = {"ada": 36, "grace": 45};
ages["linus"] = 21;              // insert or replace
int a = ages["ada"];             // panics if the key is missing
int z = ages.get_or("zed", 0);   // default instead of panic
if (ages.has("grace")) { ... }
```

### `Result<T>`

A success value of type `T` or an error message. See [Errors and Result](#errors-and-result).

### `Future<T>`

The handle of a function running on another thread. See [Concurrency](#concurrency).

### Closures: `fn(A, B) -> R`

A callable value that may carry captured variables. `fn(int)` with no arrow returns `void`. See [Closures](#closures).

### Structs

Moxy defines a typedef for every struct you define, so `struct Point { ... }` can be used as `Point`. A trailing `;` after the closing brace is optional when the brace ends the line.

```c
struct Point { double x; double y; }

typedef struct {
    string name;
    int[] scores;          // managed fields are fine
    map[string, string] tags;
} Player;
```

Structs are values. Assigning a struct copies it, and managed fields inside are retained, so the copy is independent.

### `ref struct`

A shared, reference-counted object.

```c
ref struct Node {
    string name;
    Node[] children;
    weak Node parent;      // does not keep the parent alive
}

Node root = Node{ .name = "root" };
Node same = root;          // same object
same.name = "renamed";     // root.name is "renamed" too
```

Use `.` to access fields (Moxy dereferences automatically). `==` compares identity. A ref variable can be `null`.

### Enums

A Moxy enum has no `=` values and no trailing `;`. Variants can carry data:

```c
enum Color { Red, Green, Blue }

enum Shape {
    Circle(double radius),
    Rect(double w, double h),
    Label(string text),
    Empty
}

Color c = Color::Red;
Shape s = Shape::Rect(2.0, 3.0);
```

An enum with an `=` value or a trailing `;` is a plain C enum, and its constants are global: `enum Mode { READ = 1, WRITE = 2 };`.

### Type inference with `auto`

`auto x = expression;` gives `x` the expression's type, as in C23: `auto names = ["a", "b"];` declares a `string[]`.

## Declarations

### Variables

C declarations, including several declarators per line and fixed arrays. A managed local without an initializer starts empty (`null`).

Global variables can have any initializer. Initializers that aren't C constants run once at startup, before `main`.

### Functions

C function syntax. Functions can be used before they're defined, so no forward declarations are needed (but they're allowed). Parameters are passed by value: a list parameter is the caller's list, and modifying it inside the function modifies a copy.

### `main`

```c
void main() { ... }                      // exits 0
int main() { return 3; }                 // exit code 3
void main(string[] args) { ... }         // args[0] is the program
int main(int argc, char **argv) { ... }  // classic C
```

### Namespaces

```c
namespace geo {
    struct Point { double x; double y; }
    double dist2(Point a, Point b) { ... }
    namespace shapes { ... }
}

geo::Point p = geo::Point{ .x = 1.0, .y = 2.0 };
using geo;                         // bring geo's names into scope
Point q = Point{ .x = 0.0, .y = 0.0 };
```

In generated C, namespaced names become `geo__Point` and `geo__dist2`.

## Expressions

### Operators

Precedence, highest first:

| Level | Operators | Associativity |
|---|---|---|
| postfix | `f(x)` `a[i]` `a.b` `a->b` `x++` `x--` | left |
| unary | `-` `+` `!` `~` `*` `&` `++x` `--x` `sizeof` `_Alignof` `(T)x` `await` | right |
| multiplicative | `*` `/` `%` | left |
| additive | `+` `-` | left |
| shift | `<<` `>>` | left |
| relational | `<` `<=` `>` `>=` | left |
| equality | `==` `!=` | left |
| bitwise | `&`, then `^`, then `\|` | left |
| logical | `&&`, then `\|\|` | left |
| pipe | `\|>` | left |
| conditional | `c ? a : b` | right |
| assignment | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | right |
| comma | `,` | left |

Moxy-specific meanings:

- `string + string` concatenates; `s += t` appends.
- `list + list` concatenates; `xs += ys` appends.
- `==` and `!=` compare lists, maps, `Result`s and enums by value.
- `a |> f(b)` means `f(a, b)`, and `a |> f` means `f(a)`.

### Literals for composite values

```c
Point p = Point{ .x = 1, .y = 2 };        // struct literal
Point q = (Point){ 1, 2 };                // C compound literal
Point r = { .y = 5 };                     // initializer, other fields zero
int[] xs = [1, 2, 3];                     // list literal
map[string, int] m = {"a": 1, "b": 2};    // map literal
Shape s = Shape::Circle(1.5);             // enum variant
```

### Lambdas

```c
(int x) => x * 2
(string s, int n) => s.repeat(n)
() => print("hi")
(int x) => {
    int y = x + 1;
    return y * y;
}
```

The return type is inferred from the body.

## Statements

Everything in C works: blocks, `if`/`else` (with or without braces), `while`, `do`/`while`, `for (;;)`, `switch`/`case`/`default` (including GNU `case 1 ... 5:`), `break`, `continue`, `return`, `goto` and labels.

A trailing `;` after an expression statement is optional when the next token begins a new line or is `}`.

### for-in

```c
for i in 0..10 { }        // 0 through 9
for i in 1..=10 { }       // 1 through 10
for x in xs { }           // list elements
for i, x in xs { }        // index and element
for k in m { }            // map keys
for k, v in m { }         // keys and values, in insertion order
for c in "text" { }       // chars
for i, c in "text" { }
for v in fixed_array { }  // C arrays with a known length
for x in make_list() { }  // any expression
```

Range bounds are evaluated once. A collection is held for the duration of the loop, so modifying it in the body changes a copy and doesn't disturb the iteration.

### defer

`defer statement;` runs `statement` when the enclosing block ends, including through `return`, `break` and `continue`. Several defers run in reverse order.

```c
FILE *f = fopen(path, "r");
if (!f) { return Err("cannot open"); }
defer fclose(f);
```

## Pattern matching

`match` branches on enums, `Result`s, integers, chars, strings and bools:

```c
match shape {
    Shape::Circle(r) => print("circle", r),
    Shape::Rect(w, h) => {
        print("rect", w, h);
    }
    Label(text) => print(text),     // the enum name may be left off
    _ => print("something else"),
}

match code {
    0 => print("ok"),
    1 => print("warning"),
    _ => print("error"),
}
```

- A pattern binds each payload value by position. Use `_` to skip a value.
- An arm's body is a statement or a block, followed by `,` or a newline.
- Matching an enum, `Result` or `bool` must cover every case or include `_ =>`; anything else must include `_ =>`. A missing case is a compile error that names it.
- `break` and `continue` inside an arm apply to the enclosing loop.

## Errors and Result

A function that can fail returns `Result<T>`:

```c
Result<int> parse_digit(char c) {
    if (c < '0' || c > '9') { return Err("not a digit: \(c)"); }
    return Ok(c - '0');
}

Result<void> save(string path, string data) {
    if (!store(path, data)) { return Err("cannot write \(path)"); }
    return Ok();
}
```

Using the result:

```c
match parse_digit('7') {
    Ok(n) => print(n),
    Err(e) => print("error:", e),
}

int n = parse_digit('x').unwrap_or(0);
if (r.is_err()) { print(r.error()); }
int v = r.unwrap();                 // panics with the error message on Err
```

`Ok(...)` and `Err(...)` need a known `Result` type from their context: a return, an assignment, an argument, or a typed declaration.

A **panic** stops the program with a message and exit code 101. It happens on `panic("message")`, an out-of-range index, a missing map key, `unwrap()` of an `Err`, or calling a null closure. A failed **assert** exits with code 1.

## Built-ins and methods

### Functions

| Built-in | Meaning |
|---|---|
| `print(a, b, ...)` | prints the values separated by spaces, then a newline |
| `assert(cond)` / `assert(cond, "message")` | stops with the source location if `cond` is false |
| `panic("message")` | stops with the message |
| `string(value)` | formats any value as a string; copies a C `char *` |

`print` and interpolation format values as follows:

- integers in decimal;
- floats in the shortest form that reads back exactly (`0.1`, `3.0`);
- `bool` as `true` or `false`, and `char` as the character;
- lists as `[1, 2]`, maps as `{"a": 1}`, structs as `Point { x: 1, y: 2 }`, enums as `Shape::Circle(1.5)`, and `Result`s as `Ok(1)` or `Err("...")`;
- values of unknown C types through C's `_Generic`.

### string

| Method | Result |
|---|---|
| `s.len` | length in bytes (`long`) |
| `s.is_empty()` | `bool` |
| `s.contains(t)`, `s.starts_with(t)`, `s.ends_with(t)` | `bool` |
| `s.find(t)` | index or `-1` |
| `s.slice(a, b)` | bytes `[a, b)` |
| `s.split(sep)`, `s.lines()` | `string[]` |
| `s.trim()`, `s.upper()`, `s.lower()` | `string` |
| `s.replace(a, b)`, `s.repeat(n)` | `string` |
| `s.chars()` | `char[]` |
| `s.at(i)`, `s[i]` | `char` |
| `s.to_int()` | `Result<long>` |
| `s.to_float()` | `Result<double>` |

### T[]

| Method | Result |
|---|---|
| `xs.len`, `xs.is_empty()` | `long`, `bool` |
| `xs.push(x)`, `xs.insert(i, x)` | add elements |
| `xs.pop()`, `xs.remove(i)` | remove and return an element |
| `xs.clear()`, `xs.reverse()`, `xs.sort()` | modify in place |
| `xs.contains(x)`, `xs.index_of(x)` | `bool`, index or `-1` |
| `xs.first()`, `xs.last()` | element (panics if empty) |
| `xs.slice(a, b)` | `T[]` |
| `xs.join(sep)` | `string` of the formatted elements |
| `xs.map(f)`, `xs.filter(f)`, `xs.each(f)` | new list, new list, `void` |

### map[K, V]

| Method | Result |
|---|---|
| `m.len`, `m.is_empty()` | `long`, `bool` |
| `m.set(k, v)`, `m[k] = v` | insert or replace |
| `m.get(k)`, `m[k]` | value (panics if missing) |
| `m.get_or(k, default)` | value or default |
| `m.has(k)` | `bool` |
| `m.remove(k)` | `bool`: whether it was there |
| `m.keys()`, `m.values()` | lists, in insertion order |
| `m.clear()` | removes everything |

### Result<T>

`is_ok()`, `is_err()`, `unwrap()`, `unwrap_or(default)`, `error()`.

## Closures

```c
fn(int) -> int adder(int n) {
    return (int x) => x + n;       // captures n
}

fn(int) -> int add5 = adder(5);
print(add5(1));                     // 6

int[] doubled = [1, 2, 3].map((int x) => x * 2);
```

- Captures are copies taken when the lambda is created. Assigning to a captured variable inside the lambda is an error. Pass the value as a parameter or keep a local copy instead.
- A named function can be used wherever a closure is expected.
- A C function pointer parameter (`int f(int)` or `int (*f)(int)`) accepts named functions and lambdas that capture nothing. A capturing lambda needs a `fn(...)` parameter.
- Closures can be stored in variables, struct fields, lists and maps.

## Concurrency

A function that returns `Future<T>` runs on its own thread when called. `await` waits for it and returns the result.

```c
Future<long> sum_to(long n) {
    long total = 0;
    for i in 0..n { total += i; }
    return total;
}

Future<long> a = sum_to(1000000);
Future<long> b = sum_to(2000000);
print(await a + await b);
```

- Arguments are copied into the thread.
- Reference counts are atomic, so managed values can be shared safely.
- A future that's never awaited keeps running in the background, but the program doesn't wait for it before exiting.
- Programs that use futures link with `-lpthread` automatically.

## Modules

`#include "file.mxy"` brings another Moxy file into the program. Moxy resolves the path in this order:

1. next to the including file (a file never resolves to itself, so `tests/words.mxy` can include `words.mxy` from `src/`);
2. the project's `src/` and configured include directories;
3. each dependency's directory;
4. the embedded standard library (`std/...`).

Each file is included once, no matter how many times or in what order it's included, so diamonds and cycles are fine. Errors in included files report that file's name and line.

`#include <header.h>` and `#include "header.h"` are C includes, passed to the C compiler unchanged. `#define`, `#pragma` and the other directives also pass through.

A macro defined once is visible to the whole program, so modules can include each other in any order. A macro that is redefined or `#undef`'d follows C: each use sees the definition in effect where it was written. Moxy's runtime comes before your directives, so your macros never change it.

Conditional compilation works anywhere, including around declarations, inside function bodies and inside struct bodies. Two definitions of the same function or global in different branches are allowed:

```c
#ifdef DEBUG
void log(string s) { print(s); }
#else
void log(string s) { }
#endif
```

## Working with C

- **Unknown names are C.** An identifier Moxy can't find is assumed to come from a C header. If it's close to a Moxy name, Moxy warns ("did you mean"). If it doesn't exist at all, the C compiler reports it at your `.mxy` line, through `#line` directives in the generated C.
- **Typed declarations.** `extern char *getenv(const char *name);` or `int[] helper();` tell Moxy the type of a C function or a function in another file.
- **Strings.** Pass a `string` wherever C wants a `const char *`. Turn C strings into Moxy strings with `string(p)` or by assigning to a `string` variable, which copies. String literals passed to C functions stay plain C literals, so format checking keeps working.
- **Values Moxy can't see into** (`FILE *`, `size_t`, `struct stat`) flow through unchanged. Moxy refuses to turn them into lists or maps.
- **@type.** `@type Name` declares a C typedef name so the parser treats it as a type before any header is seen. This is rarely needed: common C typedefs and any `Name name` declaration are recognized automatically.
- **Headers next to your code.** `#include "native.h"` finds headers beside any `.mxy` file in the program; their directories are passed to the C compiler.
- **Types as macro arguments.** C macros that take a type work as you'd expect: `va_arg(ap, int)`, `offsetof(Point, y)`.
- **Generated C** is plain C11 with the GNU builtins `__atomic_*` and `__attribute__`, which gcc and clang support. `moxy emit file.mxy` shows it.

### C syntax Moxy doesn't parse

Moxy parses your C, so it reads only the C it understands. These extensions are not accepted in `.mxy` code. They're fine inside C headers you `#include` and in the body of a `#define`, because Moxy passes both to the C compiler untouched (a macro *used* where a declaration keyword belongs is not understood).

| Construct | Workaround |
|---|---|
| `__attribute__((...))` on declarations | declare it in a `.h` file and `#include` it; the definition can stay in Moxy |
| `_Alignas(n)` | declare the variable or struct in a `.h` file |
| `_Generic(...)` used directly in code | use it inside a `#define` macro |
| statement expressions `({ ... })` | a helper function or a block |
| designator ranges `[0 ... 9] = x` | a loop, or list each element |
| K&R function definitions `int f(a) int a; {}` | prototype-style parameters |
| `bool`, `true`, `false` as names | they're keywords, as in C23; rename the variable |
| inline assembly with operands (`asm("..." : "=r"(x))`) | put it in a C file listed under `sources` in `moxy.yaml` |

## Standard library

Include with `#include "std/<module>.mxy"`.

| Module | Contents |
|---|---|
| `std/string.mxy` | `str_len`, `str_eq`, `str_contains`, `str_starts_with`, `str_ends_with`, `join`, `pad_left`, `pad_right`, `reverse`, `count` |
| `std/math.mxy` | `abs_int`, `min_int`, `max_int`, `clamp_int`, `min_float`, `max_float`, `clamp_float`, `gcd`, `pow_int`, `sum_ints`, plus `<math.h>` |
| `std/io.mxy` | `eprint`, `eprintln`, `readln`, `read_file`, `write_file`, `append_file`, `read_lines` |
| `std/os.mxy` | `args`, `env`, `has_env`, `run` |
| `std/time.mxy` | `now_ms`, `monotonic_ms`, `sleep_ms` |
| `std/test.mxy` | `assert_eq_int`, `assert_eq_float`, `assert_eq_str`, `assert_true`, `assert_false` |
| `std/debug.mxy` | `todo`, `unreachable` |
