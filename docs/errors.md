# Diagnostics

Moxy checks your program before any C is generated, and reports problems like this:

```
error: match is not exhaustive; missing Color::Blue
  --> src/main.mxy:4:5
   |
 4 |     match c {
   |     ^^^^^
  = help: add the missing arms or a '_ =>' arm
```

- **error** stops compilation with exit code 1. A syntax error stops at the first one; type errors are all reported, followed by a count.
- **warning** is printed and compilation continues.
- **note** points at a related location, such as the first definition of a duplicate.
- **help** lines suggest a fix.

The path is relative to the current directory, and errors inside included files name that file and its line. Colors are used when stderr is a terminal (set `NO_COLOR` to disable them).

**Errors from the C compiler.** Names Moxy doesn't know are assumed to come from C headers. When one of them is wrong (a misspelled C function, a wrong argument to a C API), the C compiler reports it. The generated code carries `#line` directives, so those messages also point at your `.mxy` file and line.

**Runtime errors.** Bounds and key checks happen at run time and stop the program with a *panic* (exit code 101). A failed `assert` exits with code 1. See [Runtime panics](#runtime-panics).

In the messages below, `<name>`, `<type>` and `<n>` stand for the actual values. Types are written in Moxy syntax: `int[]`, `map[string, int]`, `Result<int>`, `fn(int) -> int`.

## Lexing and includes

| Message | Cause |
|---|---|
| `unterminated string literal` | a string has no closing `"` on its line; strings can't span lines (use `"a\n"` or adjacent literals) |
| `unterminated character literal` | a `'` without its closing `'` |
| `unterminated block comment` | `/*` without `*/` |
| `unexpected character '<c>'` | a byte that isn't part of the language, often a stray Unicode character |
| `unterminated '\(' in string interpolation` | `"\(x"` is missing the closing `)` |
| `unexpected <token> in interpolation` | `"\(a b)"`: the interpolated part must be one expression |
| `cannot find '<file>'` | `#include "x.mxy"` wasn't found next to the including file, in an include directory or dependency, or in the standard library; a file never resolves to itself |
| `cannot read '<file>'` | the file exists but couldn't be read |

## Syntax

| Message | Cause |
|---|---|
| `expected <thing>, found <token>` | the general parse error; the most common case is `expected ';'`, which points just after the previous token and suggests adding `;` |
| `unclosed '{' — reached end of file` | a block was never closed |
| `unexpected '<token>' in expression` | an expression was expected, for example `int x = ;` |
| `unexpected end of file in expression` | the file ended mid-expression |
| `expected a declaration` | something at file level that isn't a declaration, function or directive |
| `expected a name in declaration, found '<token>'` | e.g. `int = 5;`, or a reserved word used as a name: `char *string;` |
| `expected a name or '{' after 'struct'` | `struct` with neither a tag nor a body |
| `expected a type`, `expected a parameter type`, `expected a field declaration` | a type was required here |
| `functions cannot be defined inside other functions` | nested function definitions; use a lambda |
| `lambda parameters need names` | `(int) => ...` should be `(int x) => ...` |
| `expected a function call after '\|>'` | the right side of a pipe must be a call, a name or a member |
| `expected 'in' in for-in loop` | `for x of xs` or a missing `in` |
| `'string' is a type; did you mean string(value)?` | `string` used where a value is expected |
| `unclosed match` | a `match` block was never closed |
| `#else without #if`, `#elif without #if`, `#endif without #if`, `unterminated #if block` | unbalanced conditional directives |

Unsupported C extensions such as `__attribute__((...))` on a declaration also show up as syntax errors; see [C syntax Moxy doesn't parse](book/reference.md#c-syntax-moxy-doesnt-parse) for workarounds.

A `;` can be left off an expression statement when the next token starts a new line. Declarations always need their `;`:

```c
int x = 5          // error: expected ';'
print(x)           // fine
```

## Declarations and types

| Message | Cause |
|---|---|
| `'<name>' is already declared` | two different things share a name in the same namespace |
| `function '<name>' is defined twice` (+ note) | two bodies for one function; definitions in different `#if` branches are allowed |
| `global '<name>' is defined twice` | two initialized definitions of one global |
| `redefinition of struct '<name>'` | two bodies for one struct outside of different `#if` branches |
| `struct '<name>' cannot contain itself` | a struct field of its own type by value; use a pointer or a `ref struct` |
| `typedef '<name>' refers to itself` | a cyclic typedef |
| `unknown type '<ns::name>'` | a namespaced type that doesn't exist; plain unknown names are treated as C types |
| `unknown namespace '<name>'` | `using x;` where `x` isn't a namespace |
| `array size must be a constant expression` | `int a[n];` with a runtime `n`; use a list |
| `variables cannot have type void`, `cannot store a void value` | |
| `'auto' declarations need an initializer` | `auto x;` |
| `cannot infer a type from null`, `cannot infer a type for '<name>'` | `auto x = null;` |
| `'weak' is only allowed on struct fields` | |
| `weak fields must hold a ref struct` | `weak int x;` or `weak` on a value struct |
| `unions cannot hold managed values like strings, lists or maps` | use a Moxy `enum` with payloads instead |
| `ref unions are not supported` | |
| `static locals of managed types need a literal or null initializer` | `static string s = make();` |
| `main must return void or int` | |
| `main takes no parameters, (string[] args), or (int argc, char **argv)` | |

## Expressions

| Message | Cause |
|---|---|
| `<context>: expected <type>, found <type>` | a value of the wrong type in an initializer, assignment, argument, return or element; `<context>` says which |
| `<context>: cannot use a C value of unknown type as <type>` | the result of an undeclared C function can't become a list, map or other managed value; declare the function with an `extern` prototype so Moxy knows its type |
| `cannot add a number to a string` | `"n=" + 5`; use `"n=\(5)"` or `string(5)` |
| `'<op>' between string and <type>` | another operator mixing strings and other types |
| `'<op>' is not defined for strings` | e.g. `s * 2`; use `s.repeat(2)` |
| `'<op>' is not defined between <type> and <type>` | e.g. adding a list to an int, or `==` on structs |
| `only '+=' works on strings` | `s -= "x"` |
| `strings are immutable; build a new string instead` | `s[0] = 'x'` |
| `the left side of an assignment must be a variable, field or element` | |
| `'<name>' is read-only` | assigning to a property such as `xs.len` |
| `'<op>' needs a variable` | `5++` |
| `'-' needs a number, found <type>`, `'~' needs an integer` | |
| `cannot dereference a value of type <type>` | `*x` on a non-pointer |
| `cannot index a value of type <type>`, `index must be an integer` | |
| `cannot cast <type> to <type>` | casts between managed types, or from a managed type to a plain one |
| `cannot cast to string; use string(value) to format a value` | `(string)42` |
| `the branches of '?:' have different types: <type> and <type>` | |
| `a void value cannot be used as a condition`, `a list cannot be used as a condition` (+ help: use `.is_empty()`), `a Result cannot be used as a condition`, `a struct value cannot be used as a condition` | |
| `cannot infer the element type of an empty list` | `auto xs = [];`; write `int[] xs = [];` |
| `cannot infer the type of this initializer` | a `{...}` where no type is known |
| `map literals need 'key: value' entries` | `map[string, int] m = {1, 2};` |
| `too many values for <struct>`, `too many values in initializer for <type>` | |
| `unexpected initializer for a struct / an array / a list` | e.g. `"k": v` entries in a struct initializer |
| `'{}' cannot initialize <type>` | `Result<int> r = {};` |
| `<Type> has no field '<name>'` | |
| `'->' needs a pointer; use '.' on struct values`, `'.' on a pointer; use '->'` | `ref struct` values accept either |
| `'<name>' is a type, not a value` (+ help: construct it with `Name{ ... }`) | |
| `'<name>' is a namespace, not a value` | |
| `unknown name 'A::B'`, `unknown function 'A::B'` | a path that resolves to nothing |
| `cannot interpolate a void value`, `cannot print a void value`, `cannot convert a void value to a string` | |
| `ranges are only valid in for-in loops` | `int[] r = 0..5;` |

**Warning** `'<name>' is not declared in Moxy code; assuming it comes from C` (+ help: `did you mean '<other>'?`) appears when an unknown name is one or two edits away from a Moxy name. If it really is a C name you can ignore it; otherwise it's almost always a typo.

## Calls and methods

| Message | Cause |
|---|---|
| `<function> takes <n> argument(s), got <n>` | |
| `<method>() takes <n> argument(s), got <n>` | builtin methods on strings, lists, maps and Results |
| `<Type> has no method '<name>'`, `<Type> has no field or method '<name>'` | |
| `'<name>' is a method; call it as <name>()` | `xs.push` without parentheses (`.len` is the exception: it's a property) |
| `cannot call <method>() on a temporary value` (+ help: store it in a variable first) | `make().push(1)`: mutating methods need a variable, field or element |
| `<name> is not callable (type <type>)`, `field '<name>' is not a function` | |
| `construct <T> with <T>{ ... } instead of calling it`, `'<T>' is a type, not a function` | `Point(1, 2)` |
| `cannot sort a list of <type>` | `sort()` needs numbers or strings |
| `map needs a function that returns a value`, `filter needs a function that returns bool`, `<method> expects a function like (x) => ...`, `<method> expects a function taking <type>` | |
| `await needs a Future, found <type>` | |
| `assert takes a condition and an optional message`, `panic takes one message`, `string(...) takes one value` | |
| `'<T>' is not a struct defined in Moxy` | `T{...}` literals need a Moxy struct |

### Enums and Result

| Message | Cause |
|---|---|
| `<Enum> has no variant '<name>'` | |
| `<Enum>::<V> takes <n> value(s), got <n>` | constructor arity |
| `<Enum>::<V> carries <n> value(s); pass them in parentheses` | `Shape::Circle` without its payload |
| `Ok(...) needs a Result<T> context` (+ help) | `auto r = Ok(5);`; give `r` a type or return it from a `Result` function |
| `Ok(...) needs a value`, `Ok() of Result<void> takes no value`, `Err(...) needs a message` | |
| `this function must return a value` | `return;` in a non-void function |
| `a void function cannot return a value` | |

## match

| Message | Cause |
|---|---|
| `match is not exhaustive; missing <Enum>::<V>, ...` (+ help) | add the missing arms or `_ =>` |
| `match is not exhaustive; missing Err(...)` / `Ok(...)` / `Ok and Err` | |
| `match on strings needs a '_ =>' arm`, `match on this value needs a '_ =>' arm` | numbers, chars and strings can't be exhausted |
| `cannot match on a value of type <type>` | e.g. structs or lists |
| `pattern '<path>' does not belong to <Enum>` | `Color::Red` used to match a `Shape` |
| `<Enum>::<V> carries <n> value(s), the pattern binds <n>` | too many bindings |
| `patterns for a Result are Ok(value), Err(message) and _` | |
| `Ok binds one value`, `Err binds one message` | |
| `pattern of type <type> cannot match <type>`, `this pattern does not fit a value of this type` | |
| warning `<Enum>::<V> is matched more than once` | |
| warning `unreachable match arm after '_'` | |

## Control flow

| Message | Cause |
|---|---|
| `break outside of a loop or switch`, `continue outside of a loop` | |
| `return is not allowed inside defer`, `break is not allowed inside defer`, `continue is not allowed inside defer` | deferred code runs during cleanup and can't jump |
| `switch needs an integer, found <type>` (+ help for strings: use `match`) | |
| `cannot iterate over a value of type <type>` | for-in works on lists, maps, strings, C arrays and ranges |
| `cannot iterate over an array of unknown length` | |
| `range bounds must be integers`, `a range loop has one variable` | |
| `function declarations inside functions are not supported` | |

## Memory and ownership rules

These protect the automatic memory management (see [memory.md](book/memory.md)):

| Message | Cause and fix |
|---|---|
| `cannot modify '<name>' inside a lambda; captured values are copies` | the lambda received a copy of `<name>`, so assigning to it would change nothing outside; pass the value as a parameter, or use a `ref struct` for shared state |
| `<context>: this lambda captures '<name>' and cannot become a plain C function pointer` (+ help) | a C function pointer has nowhere to keep the captured value; declare the parameter as a closure, `fn(int) -> int` |
| `'<name>' needs its own block inside a switch` (+ help) | a `case` label can jump past the variable's initialization; write `case 1: { string s = ...; break; }` |
| `unions cannot hold managed values like strings, lists or maps` | a union can't know which member to release; use an `enum` with payloads |
| warning `'<function>' uses goto together with managed values or defer; goto skips automatic cleanup` | `goto` jumps don't release values or run `defer` |

## Lint warnings

`moxy lint` (and the matching `moxyfmt.yaml` settings) adds:

| Warning | Setting |
|---|---|
| `unused variable '<name>'` (names starting with `_` are exempt) | `lint: unused_vars` |
| `'<name>' shadows a variable declared at line <n>` | `lint: shadow_vars` |
| `empty if body`, `empty while body`, `empty for body` | `lint: empty_blocks` |

## Runtime panics

A panic prints its message and the source location, then exits with code 101:

```
panic: index 10 out of range for list of length 3
  at src/main.mxy:5:13
```

| Message | Cause |
|---|---|
| `index <i> out of range for list of length <n>` | `xs[i]`, `insert`, `remove`, `first`, `last` |
| `slice [<a>, <b>) out of range for list of length <n>` | `xs.slice(a, b)` |
| `pop from an empty list` | |
| `key <key> not found in map` | `m[k]` or `m.get(k)` with a missing key; use `get_or` or `has` |
| `string index <i> out of range (length <n>)`, `string slice [<a>, <b>) out of range (length <n>)` | |
| `unwrap of Err: <message>` | `r.unwrap()` on an error |
| `call of a null closure` | calling an unset `fn(...)` variable |
| `await on a null future` | |
| `values of type <type> cannot be sorted` | |
| `out of memory` | allocation failed |
| `<message>` | `panic("message")` |

A failed assertion prints the condition (or your message) and exits with code 1:

```
assertion failed: assert(total == 6)
  at tests/sum.mxy:9:5
```
