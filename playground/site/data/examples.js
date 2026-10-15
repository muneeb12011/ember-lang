const EXAMPLES = [
 {
  "id": "01-hello",
  "title": "Hello",
  "code": "// Welcome to Ember! Press Run (or Ctrl+Enter).\nfn main() -> int {\n    print(\"Hello, World!\");\n\n    let name = \"Ember\";\n    print(\"This program was compiled by \" + name + \"'s own compiler,\");\n    print(\"running in your browser as WebAssembly.\");\n    return 0;\n}\n"
 },
 {
  "id": "02-lists",
  "title": "Lists",
  "code": "import \"std/vec.em\" as vec;\n\nfn main() -> int {\n    // a fixed-size array\n    let fruits = [\"apple\", \"banana\", \"mango\"];\n    print(\"first fruit: \" + fruits[0]);\n    print(\"count: \" + str(len(fruits)));\n\n    // a growable list, like a Python list\n    let names = vec.make<str>();\n    vec.push(names, \"Ali\");\n    vec.push(names, \"Sara\");\n    vec.push(names, \"Muneeb\");\n    for i in 0..vec.size(names) {\n        print(\"Hello \" + vec.get(names, i));\n    }\n\n    let nums = vec.from_array([5, 3, 8, 1]);\n    print(\"sum = \" + str(vec.sum(nums)));\n    return 0;\n}\n"
 },
 {
  "id": "03-structs",
  "title": "Structs",
  "code": "struct Point { x: int, y: int }\n\nfn add(a: Point, b: Point) -> Point {\n    return Point { x: a.x + b.x, y: a.y + b.y };\n}\n\nfn show(p: Point) -> str {\n    return \"(\" + str(p.x) + \", \" + str(p.y) + \")\";\n}\n\nfn main() -> int {\n    let a = Point { x: 1, y: 2 };\n    let b = Point { x: 10, y: 20 };\n    let c = add(a, b);\n    print(show(a) + \" + \" + show(b) + \" = \" + show(c));\n\n    // structs are references: both names see the change\n    let d = c;\n    d.x = 99;\n    print(\"c is now \" + show(c));\n    return 0;\n}\n"
 },
 {
  "id": "04-enums",
  "title": "Enums",
  "code": "enum Shape { Circle(float), Rect(float, float), Triangle(float, float) }\n\nfn area(s: Shape) -> float {\n    match s {\n        Circle(r) => { return 3.14159 * r * r; }\n        Rect(w, h) => { return w * h; }\n        Triangle(b, h) => { return 0.5 * b * h; }\n    }\n}\n\nfn name(s: Shape) -> str {\n    match s {\n        Circle(_) => { return \"circle\"; }\n        Rect(_, _) => { return \"rectangle\"; }\n        _ => { return \"triangle\"; }\n    }\n}\n\nfn main() -> int {\n    let shapes = [Shape.Circle(1.0), Shape.Rect(2.0, 3.0), Shape.Triangle(4.0, 5.0)];\n    for i in 0..len(shapes) {\n        print(name(shapes[i]) + \": \" + str(area(shapes[i])));\n    }\n    // Try deleting the Triangle arm in area(): the compiler\n    // refuses, because match must cover every variant.\n    return 0;\n}\n"
 },
 {
  "id": "05-generics",
  "title": "Generics",
  "code": "import \"std/option.em\" as option;\n\nstruct Pair<A, B> { first: A, second: B }\n\nfn swap<A, B>(p: Pair<A, B>) -> Pair<B, A> {\n    return Pair { first: p.second, second: p.first };\n}\n\nfn find(xs: [int], target: int) -> option.Option<int> {\n    for i in 0..len(xs) {\n        if xs[i] == target { return option.some(i); }\n    }\n    return option.none<int>();\n}\n\nfn main() -> int {\n    let p = Pair { first: \"age\", second: 19 };\n    let q = swap(p);\n    print(q.first);\n    print(q.second);\n\n    let xs = [4, 8, 15, 16, 23, 42];\n    print(option.unwrap_or(find(xs, 15), -1));\n    print(option.unwrap_or(find(xs, 7), -1));\n    return 0;\n}\n"
 },
 {
  "id": "06-closures",
  "title": "Closures",
  "code": "import \"std/iter.em\" as iter;\n\nstruct Counter { n: int }\n\nfn make_counter() -> fn() -> int {\n    let c = Counter { n: 0 };\n    return fn() -> int { c.n += 1; return c.n; };\n}\n\nfn main() -> int {\n    let next = make_counter();\n    next();\n    next();\n    print(\"counter: \" + str(next()));\n\n    let factor = 3;\n    let tripled = iter.map([1, 2, 3, 4], fn(x: int) -> int { return x * factor; });\n    let big = iter.count(tripled, fn(x: int) -> bool { return x > 6; });\n    print(\"values above 6: \" + str(big));\n    return 0;\n}\n"
 },
 {
  "id": "07-optimizer",
  "title": "Optimizer",
  "code": "// Run this, then compare the IR and Optimized stages:\n// the optimizer folds the constant 10 into the comparison,\n// removes the copy of i, and merges the add into total.\n// The x86-64 stage shows total and i living in registers.\nfn main() -> int {\n    let mut total = 0;\n    for i in 0..10 {\n        total += i * i;\n    }\n    print(total);\n    return 0;\n}\n"
 },
 {
  "id": "08-fizzbuzz",
  "title": "Fizzbuzz",
  "code": "fn main() -> int {\n    for i in 1..21 {\n        if i % 15 == 0 {\n            print(\"FizzBuzz\");\n        } else if i % 3 == 0 {\n            print(\"Fizz\");\n        } else if i % 5 == 0 {\n            print(\"Buzz\");\n        } else {\n            print(i);\n        }\n    }\n    return 0;\n}\n"
 },
 {
  "id": "09-primes",
  "title": "Primes",
  "code": "// Sieve of Eratosthenes: every prime below 100\nfn main() -> int {\n    let n = 100;\n    let composite = new [bool](n);\n    let mut line = \"\";\n    for i in 2..n {\n        if !composite[i] {\n            line = line + str(i) + \" \";\n            let mut j = i * i;\n            while j < n {\n                composite[j] = true;\n                j += i;\n            }\n        }\n    }\n    print(line);\n    return 0;\n}\n"
 },
 {
  "id": "10-compile-error",
  "title": "Compile error",
  "code": "// This program has two mistakes. Press Run to see\n// how the compiler reports them, then fix them.\nfn main() -> int {\n    let count = 3;\n    print(cuont);\n    let half: float = count / 2;\n    return 0;\n}\n"
 },
 {
  "id": "11-runtime-error",
  "title": "Runtime error",
  "code": "// Every array access is bounds-checked at run time.\nfn main() -> int {\n    let scores = [90, 75, 88];\n    for i in 0..4 {\n        print(scores[i]);\n    }\n    return 0;\n}\n"
 }
];
