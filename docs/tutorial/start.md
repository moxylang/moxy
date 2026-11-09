# Getting started with Moxy

This tutorial takes you from "hello, world" to a small multi-file project with tests. It assumes you know some C (or any curly-brace language). Each section builds on the last.

## 1. Hello

```c
// hello.mxy
void main() {
    print("hello, world");
}
```

```sh
moxy run hello.mxy
```

`moxy run` compiles the file to C, compiles the C, and runs the program. To see the C it produces, run `moxy hello.mxy`.

## 2. Values

```c
void main() {
    int count = 3;
    double price = 2.5;
    string item = "apples";
    bool fresh = true;

    print(count, item, "at", price, fresh);   // 3 apples at 2.5 true
    print("\(count) \(item) cost \(count * price)");
}
```

`print` takes any number of values and knows how to show each one, so there are no format codes. `"\(expr)"` drops a value into a string.

`auto` infers a type from its initializer: `auto total = count * price;` declares a `double`.

## 3. Strings

`string` is a real string type. You can concatenate it, compare it with `==`, and call methods on it.

```c
void main() {
    string name = "  Ada Lovelace ";
    string clean = name.trim();
    print(clean.len, clean.upper());           // 12 ADA LOVELACE

    string[] parts = clean.split(" ");
    string initials = string(parts[0][0]) + string(parts[1][0]);
    print(initials);                           // AL

    if (clean.starts_with("Ada")) {
        print("hello, " + parts[0]);
    }
}
```

Strings are freed automatically. You never `malloc` or `free` them.

## 4. Lists

```c
void main() {
    int[] scores = [72, 95, 88];
    scores.push(61);
    scores.sort();
    print(scores, scores.len);                 // [61, 72, 88, 95] 4

    int best = scores.last();
    int[] passing = scores.filter((int s) => s >= 70);
    print(best, passing);                      // 95 [72, 88, 95]

    for i, s in scores {
        print("#\(i + 1): \(s)");
    }
}
```

Indexing is checked. `scores[10]` stops the program with a clear message instead of reading garbage.

Lists are values. After `int[] copy = scores;`, changing `copy` never changes `scores`.

## 5. Maps

```c
void main() {
    map[string, int] stock = {"apples": 3, "pears": 0};
    stock["plums"] = 12;
    stock["apples"] += 2;

    for fruit, n in stock {
        print(fruit, n);
    }
    print(stock.get_or("kiwis", 0));           // 0
    print(stock.has("pears"));                 // true
}
```

Maps keep their insertion order, so output is predictable.

## 6. Functions and structs

```c
struct Point { double x; double y; }

double dist2(Point a, Point b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

Point midpoint(Point a, Point b) {
    return Point{ .x = (a.x + b.x) / 2, .y = (a.y + b.y) / 2 };
}

void main() {
    Point a = Point{ .x = 0, .y = 0 };
    Point b = Point{ .x = 6, .y = 8 };
    print(dist2(a, b), midpoint(a, b));       // 100.0 Point { x: 3.0, y: 4.0 }
}
```

Functions can be called before they're defined. Structs can contain strings, lists and maps. Copying a struct copies them too.

## 7. Enums and match

```c
enum Command {
    Move(int dx, int dy),
    Say(string text),
    Quit
}

void run(Command c) {
    match c {
        Command::Move(dx, dy) => print("moving", dx, dy),
        Command::Say(text) => print("saying", text),
        Command::Quit => print("bye"),
    }
}

void main() {
    Command[] script = [Command::Say("hi"), Command::Move(1, 2), Command::Quit];
    for c in script { run(c); }
}
```

Delete the `Command::Quit` arm and the compiler tells you exactly what's missing:

```
error: match is not exhaustive; missing Command::Quit
```

`match` also works on numbers, chars and strings, with `_ =>` as the catch-all.

## 8. Errors without exceptions

Functions that can fail return `Result<T>`:

```c
Result<int> parse_age(string s) {
    Result<long> n = s.to_int();
    if (n.is_err()) { return Err("'\(s)' is not a number"); }
    if (n.unwrap() < 0) { return Err("age can't be negative"); }
    return Ok((int)n.unwrap());
}

void main() {
    for input in ["42", "-1", "old"] {
        match parse_age(input) {
            Ok(age) => print("age", age),
            Err(e) => print("error:", e),
        }
    }
}
```

`unwrap_or(default)` gives a fallback, and `unwrap()` stops the program if the value is an error.

## 9. Closures

Lambdas can use variables from where they're written:

```c
fn(int) -> int multiplier(int factor) {
    return (int x) => x * factor;
}

void main() {
    fn(int) -> int double_it = multiplier(2);
    int[] xs = [1, 2, 3].map(double_it);
    string unit = "cm";
    string[] labels = xs.map((int x) => "\(x)\(unit)");
    print(labels);   // ["2cm", "4cm", "6cm"]
}
```

## 10. Shared objects and cleanup

When several parts of a program need to share and change one thing, make it a `ref struct`:

```c
ref struct Account {
    string owner;
    double balance;
}

void deposit(Account a, double amount) {
    a.balance += amount;       // changes the shared account
}

void main() {
    Account acct = Account{ .owner = "ada", .balance = 10 };
    deposit(acct, 5);
    print(acct.balance);       // 15.0
}
```

For C resources like files, `defer` runs cleanup when the scope ends:

```c
#include <stdio.h>

void main() {
    FILE *f = fopen("notes.txt", "w");
    if (!f) { return; }
    defer fclose(f);
    fprintf(f, "written by moxy\n");
}
```

## 11. A project with tests

```sh
moxy new notes
cd notes
```

You get:

```
notes/
  moxy.yaml
  src/main.mxy
  tests/basic.mxy
```

Split code across files with `#include`:

```c
// src/words.mxy
int word_count(string text) {
    int n = 0;
    for w in text.split(" ") {
        if (!w.is_empty()) { n += 1; }
    }
    return n;
}
```

```c
// src/main.mxy
#include "words.mxy"

void main(string[] args) {
    print(word_count("the quick  brown fox"));
}
```

```c
// tests/words.mxy
#include "words.mxy"

void main() {
    assert(word_count("") == 0);
    assert(word_count("a b c") == 3);
}
```

```sh
moxy run      # 4
moxy test     # runs everything in tests/
```

A test can also check its exact output: put the expected text in `tests/words.out`.

## 12. Using C

Everything in C still works, so you can include any header and call it:

```c
#include <math.h>
#include <time.h>

void main() {
    print(sqrt(2.0));
    time_t now = time(null);
    string when = string(ctime(&now)).trim();
    print("it is", when);
}
```

`string(ptr)` copies a C string into a Moxy string. Moxy strings go straight into C functions that expect `const char *`.

## Where next

- [Language reference](../book/reference.md): everything in detail
- [Memory management](../book/memory.md): how automatic memory works
- `examples/`: larger programs, including an HTTP server and an NES emulator
