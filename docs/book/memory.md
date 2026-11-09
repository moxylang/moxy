# Memory management

Moxy manages memory automatically. You never call `malloc` or `free` for Moxy values, nothing leaks, and nothing is freed while you can still reach it. This page explains the model, why it was chosen over the alternatives, and the exact rules the compiler follows.

## The short version

- `string`, `T[]` (lists) and `map[K, V]` are **values**. Assigning or passing one gives the other side its own copy, the way an `int` works. Copies are cheap because they share storage until one side writes (copy-on-write).
- `ref struct` types are **shared objects**. Every variable that holds one points at the same object, and the object is freed when the last reference disappears.
- `weak` fields point at a ref object without keeping it alive. Use them for back-pointers (parent, owner, delegate) so cycles can't keep objects alive forever.
- Closures, futures and `Result<T>` are managed the same way.
- `defer` runs a statement when the current scope ends (however it ends), for resources Moxy doesn't own, such as files, sockets and locks.
- Plain C is untouched: pointers, `malloc`, arrays and structs of C types behave exactly as they do in C.

```c
ref struct Node {
    string name;
    Node[] children;
    weak Node parent;
}

void main() {
    int[] a = [1, 2, 3];
    int[] b = a;          // shares storage with a
    b.push(4);            // b gets its own copy here; a is still [1, 2, 3]

    Node root = Node{ .name = "root" };
    Node child = Node{ .name = "child", .parent = root };
    root.children.push(child);   // root keeps child alive; child.parent does not keep root alive
}                                // everything above is released here
```

## How other languages do it

Moxy is a C superset, so whatever it does has to work with code that passes raw pointers around and calls C libraries. That rules out some designs. Here's how the main families compare.

| Approach | Examples | Pauses | Deterministic cleanup | C interop | Cost to the programmer |
|---|---|---|---|---|---|
| Manual | C, Zig, Odin | none | yes | native | high: leaks, double frees, use-after-free |
| Tracing GC | Go, Java, C#, JS, D | yes (short in modern collectors) | no (finalizers run "eventually") | awkward: GC must see every pointer, C can't hold GC memory safely | low |
| Ownership and borrowing | Rust | none | yes | good, through `unsafe` | high: lifetimes, borrow checker |
| Reference counting with value semantics | Swift, Objective-C ARC | none | yes | good | low; cycles need `weak` |
| RC plus cycle collector | Python, PHP, Nim ORC | small, for the cycle collector | mostly | good | low |
| RC with compile-time optimizations | Lean and Koka (Perceus), Lobster | none | yes | good | low |
| Generational references | Vale | none | yes | good | low; checks at dereference |

**Manual memory (C, Zig).** Full control and zero runtime cost, but every allocation is a liability. Zig improves the ergonomics with `defer` and explicit allocators, and Moxy borrows `defer`. Moxy keeps manual memory available for C code, but doesn't make it the default.

**Tracing garbage collection (Go, Java).** Easiest for the programmer. But a tracing collector has to find every live pointer, which means stack maps or conservative scanning, and C code holding Moxy pointers would be invisible to it. Memory is reclaimed late, finalizers are unreliable for closing files, and the runtime is large. A conservative collector (Boehm) would work, but it gives up on determinism and makes "when is this file closed?" unanswerable.

**Ownership and borrowing (Rust).** Zero-cost and safe, but it changes how every function signature is written, and it rejects many ordinary C patterns. Moxy is meant to be C with conveniences, not a new discipline.

**Swift's ARC.** The compiler inserts retain and release calls, so the program frees memory deterministically, with no collector and no pauses. Two ideas make it pleasant:

1. Collections and strings are *values* with copy-on-write, so aliasing bugs ("I changed my list and yours changed too") can't happen, and copies cost almost nothing until someone writes.
2. Shared mutable state is opt-in (`class`), and cycles are broken with `weak`/`unowned`.

**Nim's ORC, Python.** ARC plus a cycle collector, so cycles are reclaimed even without `weak`, at the cost of an occasional collection pass.

**Perceus (Koka, Lean), Lobster.** Reference counting where the compiler removes most retain/release pairs and reuses memory in place when a value is uniquely owned. These are optimizations on top of RC, not a different user-facing model.

## What Moxy chose and why

Moxy follows Swift's model: **automatic reference counting with value semantics for collections, plus opt-in shared objects with weak references.**

- **It works with C.** Memory is freed at known points and nothing scans the stack. A `string` is still a `const char *` that any C function can read. C code can keep a Moxy object alive by calling its retain function.
- **It's predictable.** Memory is released at the end of the scope that owns it, in reverse declaration order, with no pauses.
- **It's hard to misuse.** Values copy like `int`, so passing a list to a function can't change it behind your back. Bounds are checked, and a missing map key is a clear error, not garbage.
- **Cycles only happen where you asked for sharing.** Values can't form cycles: a list containing itself is impossible because putting it in copies it. Only `ref struct` objects can form cycles, and `weak` fields are the tool for that.
- **It's thread-safe.** Reference counts are atomic, so values can be passed to `Future` functions freely.

The trade-offs are honest ones:

- Retain and release cost an atomic increment and decrement. The compiler already skips them for borrowed values (parameters that aren't modified, loop variables, temporaries consumed in place), and the remaining cost is small next to the allocation it protects.
- A strong reference cycle between `ref struct` objects leaks unless one side is `weak`. There's no cycle collector yet (see [Future work](#future-work)).
- Writing to a shared list copies it once. That's the price of value semantics, and you can avoid it by keeping one owner.

## The rules

### Managed types

These types are reference counted:

- `string`
- `T[]` (lists)
- `map[K, V]`
- `fn(A) -> R` closures
- `Future<T>`
- `ref struct` objects

These are value types whose *contents* are managed:

- `Result<T>`
- structs with managed fields
- Moxy enums with managed payloads
- fixed C arrays of managed types

The compiler generates copy and release functions for them.

Everything else (numbers, pointers, C structs of C types, C enums) is plain C.

### Ownership conventions

The compiler tracks whether each expression *owns* a reference (+1) or *borrows* one (+0):

| Expression | Ownership |
|---|---|
| variable, field, list element, map value | borrowed |
| function call result | owned |
| literal list, map, struct with managed fields, `Ok`/`Err`, enum with payload | owned |
| string concatenation, interpolation, `string(x)` | owned |
| string literal | borrowed (literals are immortal static objects) |
| `await f` | owned (a copy of the future's result) |
| reading a `weak` field | owned (a strong reference, or `null` if the object is gone) |

From that:

- **Storing** a value (into a variable, field, list, map, or a returned value) needs an owned reference. Borrowed values are retained first. Owned values are moved in.
- **Passing** an argument lends it (+0). The callee retains a parameter only if it modifies, reassigns or takes the address of it, so ordinary calls cost nothing.
- **Temporaries** that are owned but only read (`print(make_list())`, `f(a + b)`) are released at the end of the statement.
- **Returning** a local moves it out without a retain/release pair.
- **Leaving a scope** releases its locals in reverse order. `return`, `break` and `continue` release exactly the scopes they leave, then run `defer` statements in reverse order.
- **Iterating** with `for x in xs` holds a reference to the collection for the duration of the loop. Changing `xs` inside the loop modifies a copy, so the iteration stays consistent.

### Copy-on-write

Every list and map carries a reference count. A mutating operation (`push`, `insert`, `remove`, `set`, `xs[i] = v`, `m[k] = v`, `sort`, `clear`) first checks it:

- count of 1: the operation mutates in place;
- count above 1: the operation copies the storage, releases the shared one, then mutates the copy.

Strings are immutable. Operations that "change" a string return a new one.

`NULL` is a valid empty list or map, so zero-initialized structs need no special setup.

### Ref structs and weak references

`ref struct T { ... }` defines a heap object with a header holding a strong count and a weak count. `T{ .field = value }` allocates one with a strong count of 1.

- Assigning or storing a `T` retains it. Releasing the last strong reference releases its fields and then frees the memory, unless weak references remain.
- A `weak T` field adds to the weak count only. Reading it yields a strong reference if the object is still alive, or `null` if it's gone. Memory is reclaimed once both counts reach zero.
- `==` on ref objects compares identity.

### Closures

A lambda that refers to local variables captures them **by value** when it is created, retaining managed captures. The captured values are released when the closure is released.

A lambda that captures nothing compiles to a static, immortal object with no allocation. A lambda that captures nothing can also be passed where C expects a plain function pointer.

### Futures

`Future<T>` functions run on their own thread. The arguments are retained for the thread. The future handle is reference counted:

- Awaiting returns a copy of the result.
- Dropping a future without awaiting it detaches the thread, which cleans up after itself when it finishes.

### `defer`

```c
FILE *f = fopen(path, "r");
defer fclose(f);
```

A deferred statement runs when the enclosing scope ends: at the closing brace, or at any `return`, `break` or `continue` that leaves the scope. Deferred statements run in reverse order, interleaved correctly with releases of managed locals. They can't themselves `return`, `break` or `continue`.

### Where C rules still apply

- Raw pointers aren't tracked. `string *p = &s;` is fine, but `p` must not outlive `s`.
- A struct that contains managed fields must be zero-initialized before Moxy assigns to those fields. Use `calloc`, a Moxy local, or a `ref struct`, never uninitialized `malloc` memory.
- `goto` jumps don't run cleanup. The compiler warns when a function mixes `goto` with managed locals or `defer`.
- A managed local declared directly under a `case` label is an error. Give the case body its own block.
- Unions can't hold managed values. Use a Moxy `enum` with payloads instead.

## Checking it

The test suite includes memory tests (`tests/memory/`), and CI runs every test under AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer for both the compiler and the generated programs. Run the same locally with:

```sh
moxy test --sanitize          # programs under ASan/UBSan (LeakSanitizer on Linux)
make sanitize && ./build/asan/moxy test --sanitize tests
```

On macOS, `leaks --atExit -- ./program` gives the same answer as LeakSanitizer.

## Future work

- **Fewer retain/release pairs.** Borrow analysis across calls, Perceus-style reuse of uniquely owned storage, and non-atomic counts for values that never cross a thread boundary.
- **Optional cycle collection** for `ref struct` objects (as in Nim's ORC), for programs that would rather not think about `weak`.
- **`unowned` references** for back-pointers that are known to outlive their holder, with no weak-count bookkeeping.
- **Custom allocators and arenas** for C-heavy code that wants bulk freeing.
