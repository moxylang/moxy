# Moxy

Moxy is C with the conveniences you reach for every day: real strings, growable lists, hash maps, tagged enums with `match`, `Result` errors, closures, futures, namespaces, and automatic memory management. It compiles to plain, readable C11, and every line of existing C still works.

```c
#include "std/io.mxy"

enum Shape {
    Circle(double r),
    Rect(double w, double h)
}

double area(Shape s) {
    match s {
        Shape::Circle(r) => return 3.14159 * r * r,
        Shape::Rect(w, h) => return w * h,
    }
}

void main(string[] args) {
    Shape[] shapes = [Shape::Circle(1.0), Shape::Rect(2.0, 3.0)];
    double total = 0;
    for s in shapes { total += area(s); }
    print("total area: \(total)");

    map[string, int] counts = {};
    for word in "the cat and the hat".split(" ") {
        counts[word] = counts.get_or(word, 0) + 1;
    }
    print(counts);   // {"the": 2, "cat": 1, "and": 1, "hat": 1}

    match read_file("config.txt") {
        Ok(text) => print(text.lines().len, "lines"),
        Err(e) => print("no config:", e),
    }
}
```

No `malloc`, no `free`, no leaks: strings, lists, maps and closures are reference counted and freed the moment the last user is done with them. See [Memory management](docs/book/memory.md).

## Why Moxy

C is fast, portable and everywhere, but daily work in it means hand-rolled dynamic arrays, `printf` format strings, manual tagged unions, and tracking every allocation. Moxy keeps C's types, control flow and mental model, and removes that busywork:

| You write | Instead of |
|---|---|
| `string s = "hi " + name;` | `malloc`, `strcpy`, `strcat`, and remembering to `free` |
| `print(x, y, list)` | `printf` with the right format codes |
| `int[] xs = [1, 2, 3]; xs.push(4);` | a struct with `data`/`len`/`cap` and `realloc` |
| `map[string, int] m = {"a": 1};` | a hash table library |
| `enum Shape { Circle(double r) }` + `match` | a tag enum, a union, and a `switch` |
| `Result<int> parse(string s)` | out-parameters and error codes |
| `(int x) => x + k` | a function plus a context struct |
| `Future<T>` / `await` | pthread boilerplate |
| `defer fclose(f);` | `goto cleanup` |

The generated C is clean enough to read and debug, and `moxy emit file.mxy` shows it.

## Install

```sh
# macOS / Linux
curl -fsSL https://github.com/wess/moxy/releases/latest/download/install.sh | sh

# Homebrew
brew tap wess/moxy https://github.com/wess/moxy
brew install moxy

# asdf
asdf plugin add moxy https://github.com/wess/moxy.git
asdf install moxy latest
```

Debian, Fedora and Arch packages are attached to each [release](https://github.com/wess/moxy/releases).

### From source

You need a C compiler and `make`. [goose](https://github.com/wess/goose), the build and package engine, is a submodule that compiles straight into the `moxy` binary. `moxy` is the only executable you need.

```sh
git clone --recursive https://github.com/wess/moxy.git
cd moxy
make            # builds build/moxy
make test       # runs the test suite
make install    # installs to /usr/local/bin (PREFIX=... to change)
```

## Quick start

```sh
moxy run hello.mxy              # compile and run one file
moxy build hello.mxy -o hello   # build a binary
moxy hello.mxy                  # print the generated C
moxy check src                  # type-check without building
```

Projects:

```sh
moxy new app && cd app          # src/main.mxy, tests/, moxy.yaml
moxy run                        # build and run
moxy test                       # run everything in tests/
moxy build --release
moxy install --prefix ~/.local
```

## A tour

### Values and printing

```c
int age = 30;
double height = 1.82;
string name = "moxy";
bool ok = true;
auto tags = ["fast", "small"];      // inferred: string[]

print(name, age, height, ok, tags); // moxy 30 1.82 true ["fast", "small"]
print("\(name) is \(age)");         // interpolation
```

### Strings

```c
string s = "  Hello, World  ".trim();
print(s.len, s.upper(), s.contains("World"), s.find(","));
string[] parts = "a,b,c".split(",");
string joined = parts.join(" | ");
if (s == "Hello, World") { ... }    // compares contents
Result<long> n = "42".to_int();
```

### Lists and maps

```c
int[] xs = [5, 3, 8];
xs.push(1);
xs.sort();
int[] evens = xs.filter((int x) => x % 2 == 0);
string[] labels = xs.map((int x) => "#\(x)");

map[string, int] stock = {"apples": 3};
stock["pears"] = 7;
for item, qty in stock { print(item, qty); }
```

Lists and maps are values: `int[] b = a;` gives `b` its own copy, made lazily the first time either side changes.

### Structs, enums and match

```c
struct Point { int x; int y; }

enum Event {
    Click(Point at),
    Key(char code),
    Quit
}

void handle(Event e) {
    match e {
        Event::Click(p) => print("click at", p.x, p.y),
        Event::Key(c) => print("key", c),
        Event::Quit => print("bye"),
    }
}
```

`match` must cover every variant, or include `_ =>`, or the compiler reports which variant is missing.

### Errors

```c
Result<Config> load(string path) {
    Result<string> text = read_file(path);
    if (text.is_err()) { return Err("cannot load \(path): \(text.error())"); }
    return Ok(parse(text.unwrap()));
}
```

### Closures

```c
fn(int) -> int scaler(int factor) {
    return (int x) => x * factor;
}

fn(int) -> int triple = scaler(3);
print(triple(5));   // 15
```

### Shared objects

```c
ref struct Node {
    string name;
    Node[] children;
    weak Node parent;
}

Node root = Node{ .name = "root" };
Node leaf = Node{ .name = "leaf", .parent = root };
root.children.push(leaf);
```

`ref struct` objects are shared and freed when the last reference goes. `weak` references break cycles.

### Cleanup and concurrency

```c
FILE *f = fopen(path, "r");
defer fclose(f);              // runs however the scope exits

Future<int> work(int n) { ... }
Future<int> a = work(1);
Future<int> b = work(2);
print(await a + await b);
```

### Modules

```c
#include "geometry.mxy"       // each file is included once
#include "std/io.mxy"         // standard library
#include <sqlite3.h>          // any C header

namespace geo {
    double dist(Point a, Point b) { ... }
}
geo::dist(p, q);
```

## Projects and packages

A project is a directory with a `moxy.yaml`:

```yaml
project:
  name: "app"
  version: "0.1.0"
  type: "bin"                 # or "lib"

build:
  cc: "cc"
  cflags: "-Wall"
  ldflags: "-lsqlite3"
  includes:
    - "src"
  sources:
    - "src/native.c"          # extra C files to compile in

dependencies:
  geom:
    path: "../geom"
  http:
    git: "https://github.com/user/http.mxy"
    version: "v1.2.0"
```

| Command | What it does |
|---|---|
| `moxy add <git-url> [--name n] [--version v]` | fetch a dependency into `packages/`, pinned in `moxy.lock` |
| `moxy remove <name>`, `moxy update` | remove or refresh dependencies |
| `moxy build [--release]` | build `src/main.mxy` (bin) or `src/lib.mxy` (lib) |
| `moxy run [--release] [-- args]` | build and run |
| `moxy test` | run `tests/` |
| `moxy clean`, `moxy install [--prefix dir]`, `moxy task <name>` | housekeeping |

A dependency's `.mxy` files can be included by name (`#include "lib.mxy"`). Libraries build to `build/<mode>/lib<name>.a`, along with a C header (`<name>.h`) so plain C programs can call them too.

**Workspaces.** A root `moxy.yaml` with `workspace: members: [...]` builds every member in dependency order. `moxy build -p member` builds one member.

## Testing

`moxy test` runs every `.mxy` file under `tests/` (and any `*.test.mxy` file) that has a `main`:

| Next to `name.mxy` | Meaning |
|---|---|
| nothing | the test passes if it exits 0 (use `assert`) |
| `name.out` | standard output must match exactly |
| `name.code` | expected exit code (for example `101` for a panic) |
| `name.err` | compilation must fail with each listed message |

`moxy test --sanitize` runs the tests under AddressSanitizer and UndefinedBehaviorSanitizer. On Linux it also runs LeakSanitizer.

## Tooling

| Command | |
|---|---|
| `moxy fmt [paths] [--check]` | format source (configure with `moxyfmt.yaml`) |
| `moxy lint [paths]` | unused variables, shadowing, empty blocks |
| `moxy check [paths]` | type-check only |
| `moxy emit file.mxy [-o out.c]` | the generated C |
| `tools/moxylsp` | language server (diagnostics, hover, completion, symbols) |
| `tools/editors/zed` | Zed extension |

Errors look like this, and C compiler errors in generated code point back to your `.mxy` lines:

```
error: match is not exhaustive; missing Color::Blue
  --> src/main.mxy:4:5
   |
 4 |     match c {
   |     ^^^^^
  = help: add the missing arms or a '_ =>' arm
```

## Documentation

| | |
|---|---|
| [Tutorial](docs/tutorial/start.md) | learn Moxy step by step |
| [Language reference](docs/book/reference.md) | every feature in detail |
| [Memory management](docs/book/memory.md) | the ownership model, and how it compares to Swift, Rust, Go and others |
| [Architecture](docs/book/architecture.md) | how the compiler works |
| [Errors](docs/errors.md) | diagnostics and what they mean |
| [Grammar](docs/grammar.ebnf) | formal syntax |
| [Compiler internals](docs/api/index.md) | source layout for contributors |
| [Changes](docs/changes.md) | what changed between releases |

## License

MIT
