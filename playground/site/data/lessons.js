const LESSONS = [
 {
  "id": "hello",
  "title": "Your first program",
  "html": "<p>Every Ember program starts at <code>main</code>. It takes nothing and returns an <code>int</code>: that number becomes the program's\nexit code, and <code>0</code> means everything went well.</p>\n<p><code>print</code> writes one value and a line break. It accepts an <code>int</code>, a <code>float</code>, a <code>bool</code> or a <code>str</code>.</p>\n<p>When you press <strong>Run</strong>, the compiler checks the program, turns it into IR, optimizes it, and runs it on its\ninterpreter, all inside this page.</p>\n<p class=\"try\"><strong>Try it:</strong> change <code>return 0;</code> to <code>return 3;</code> and run again. The exit code under the output changes to 3.</p>",
  "code": "fn main() -> int {\n    print(\"Hello from Ember!\");\n    print(42);\n    print(true);\n    return 0;\n}\n"
 },
 {
  "id": "let",
  "title": "let and let mut",
  "html": "<p><code>let</code> gives a value a name. Once set, it can't change. If you want to change it later, say so up front with\n<code>let mut</code>.</p>\n<p>This is deliberate: when you read <code>let</code>, you know that name means the same thing on every line below it. The\ncompiler enforces it, so it's a guarantee, not a convention.</p>\n<p>Changing a variable has shortcuts: <code>x += 1</code> means <code>x = x + 1</code>, and the same works for <code>-= *= /= %=</code>.</p>\n<p class=\"try\"><strong>Try it:</strong> delete the word <code>mut</code> and run. The compiler stops you at the line that tries to change <code>score</code>.</p>",
  "code": "fn main() -> int {\n    let name = \"Ember\";\n    let mut score = 10;\n    score += 5;\n    score *= 2;\n    print(name);\n    print(score);\n    return 0;\n}\n"
 },
 {
  "id": "types",
  "title": "Types never mix by accident",
  "html": "<p>Ember has four basic types: <code>int</code> (64-bit whole numbers), <code>float</code> (decimals), <code>bool</code> and <code>str</code>.</p>\n<p>Ints and floats never convert silently. If you want <code>7 / 2</code> as a decimal, convert first with <code>float(...)</code>. To\ngo back, <code>int(...)</code> cuts off the fraction. Turning anything into text is <code>str(...)</code>.</p>\n<p>Integer division drops the remainder: <code>7 / 2</code> is <code>3</code>, and <code>7 % 2</code> is the remainder, <code>1</code>.</p>\n<p class=\"try\"><strong>Try it:</strong> change <code>float(a) / float(b)</code> to <code>a / b</code> and run. The answer is now an int, and the compiler won't\nlet you store it in a <code>float</code>.</p>",
  "code": "fn main() -> int {\n    let a = 7;\n    let b = 2;\n    print(a / b);\n    print(a % b);\n    let exact: float = float(a) / float(b);\n    print(exact);\n    print(\"a is \" + str(a));\n    print(int(9.99));\n    return 0;\n}\n"
 },
 {
  "id": "functions",
  "title": "Functions",
  "html": "<p>A function names its parameters with their types, and says what it returns after the arrow. Leave out the arrow\nif it returns nothing.</p>\n<p>The compiler checks every path: if a function promises an <code>int</code>, every way through it must end in a <code>return</code>.</p>\n<p>Functions can call themselves. <code>fib</code> below is the classic recursive version, which is slow on purpose; it's\none of Ember's benchmarks.</p>\n<p class=\"try\"><strong>Try it:</strong> delete the line <code>return b;</code> in <code>bigger</code> and run. The compiler points out that when <code>a</code> isn't\nbigger, the function never returns.</p>",
  "code": "fn bigger(a: int, b: int) -> int {\n    if a > b {\n        return a;\n    }\n    return b;\n}\n\nfn fib(n: int) -> int {\n    if n < 2 { return n; }\n    return fib(n - 1) + fib(n - 2);\n}\n\nfn greet(who: str) {\n    print(\"Hello, \" + who);\n}\n\nfn main() -> int {\n    print(bigger(3, 8));\n    print(fib(20));\n    greet(\"Lahore\");\n    return 0;\n}\n"
 },
 {
  "id": "control",
  "title": "Loops and decisions",
  "html": "<p><code>if</code> and <code>while</code> need no parentheses, but always use braces. <code>for i in 0..n</code> counts from <code>0</code> up to <code>n - 1</code>,\nlike Python's <code>range(n)</code>.</p>\n<p><code>break</code> leaves a loop early and <code>continue</code> skips to the next round.</p>\n<p>Ember also has <code>&amp;&amp;</code>, <code>||</code> and <code>!</code> for combining conditions. They short-circuit: the right side only runs when\nit's needed.</p>\n<p class=\"try\"><strong>Try it:</strong> change the loop to count to 30 and stop at the first number divisible by both 4 and 6.</p>",
  "code": "fn main() -> int {\n    for i in 1..16 {\n        if i % 2 == 0 {\n            continue;\n        }\n        if i > 11 {\n            break;\n        }\n        print(i);\n    }\n\n    let mut n = 27;\n    let mut steps = 0;\n    while n != 1 {\n        if n % 2 == 0 { n = n / 2; } else { n = 3 * n + 1; }\n        steps += 1;\n    }\n    print(\"27 reaches 1 in \" + str(steps) + \" steps\");\n    return 0;\n}\n"
 },
 {
  "id": "arrays",
  "title": "Arrays",
  "html": "<p>Write an array with square brackets: <code>[3, 1, 4]</code>. Its type is <code>[int]</code>. <code>len(a)</code> is its length, and <code>a[i]</code>\nreads or writes element <code>i</code>, counting from 0.</p>\n<p><code>new [int](n)</code> makes an array of <code>n</code> zeros. An array's length is fixed; for a list that grows, see lesson 11.</p>\n<p>Every access is checked. Reading past the end doesn't return garbage, it stops the program with a clear error\nand the line number. The optimizer removes the check when it can prove the index is in range, as in the loop\nbelow.</p>\n<p class=\"try\"><strong>Try it:</strong> change <code>0..len(temps)</code> to <code>0..len(temps) + 1</code> and run.</p>",
  "code": "fn main() -> int {\n    let temps = [31, 35, 29, 38, 33];\n    let mut hottest = temps[0];\n    let mut total = 0;\n    for i in 0..len(temps) {\n        total += temps[i];\n        if temps[i] > hottest { hottest = temps[i]; }\n    }\n    print(\"hottest: \" + str(hottest));\n    print(\"average: \" + str(total / len(temps)));\n\n    let squares = new [int](5);\n    for i in 0..5 { squares[i] = i * i; }\n    print(squares[4]);\n    return 0;\n}\n"
 },
 {
  "id": "structs",
  "title": "Structs",
  "html": "<p>A struct groups named fields. Build one by naming every field: <code>Point { x: 1, y: 2 }</code>. Read and change fields\nwith a dot.</p>\n<p>Structs are references, like objects in JavaScript or Python. Assigning one to another name doesn't copy it:\nboth names point at the same struct, so a change through one shows through the other. The garbage collector\nfrees structs once nothing points at them.</p>\n<p class=\"try\"><strong>Try it:</strong> add a field <code>name: str</code> to <code>Account</code>, give each account a name, and print it in <code>report</code>.</p>",
  "code": "struct Account { balance: int, deposits: int }\n\nfn deposit(a: Account, amount: int) {\n    a.balance += amount;\n    a.deposits += 1;\n}\n\nfn report(a: Account) {\n    print(str(a.balance) + \" after \" + str(a.deposits) + \" deposits\");\n}\n\nfn main() -> int {\n    let mine = Account { balance: 0, deposits: 0 };\n    deposit(mine, 500);\n    deposit(mine, 250);\n    report(mine);\n\n    let same = mine;\n    deposit(same, 100);\n    report(mine);\n    return 0;\n}\n"
 },
 {
  "id": "enums",
  "title": "Enums and match",
  "html": "<p>An enum is a value that is exactly one of several shapes, and each shape can carry its own data. <code>match</code> takes\nit apart and runs the arm for the shape it has.</p>\n<p>The compiler checks that a <code>match</code> handles every variant. Add a new variant and forget one <code>match</code>\nsomewhere, and the program won't compile until you handle it. That turns a whole class of bugs into compile\nerrors. A <code>_</code> arm means \"anything else\".</p>\n<p class=\"try\"><strong>Try it:</strong> add <code>Refund(int)</code> to <code>Event</code> and run. The compiler names the variant <code>describe</code> is missing.</p>",
  "code": "enum Event { Login(str), Purchase(str, int), Logout }\n\nfn describe(e: Event) -> str {\n    match e {\n        Login(user) => { return user + \" signed in\"; }\n        Purchase(item, price) => { return \"bought \" + item + \" for \" + str(price); }\n        Logout => { return \"signed out\"; }\n    }\n}\n\nfn main() -> int {\n    let events = [Event.Login(\"sara\"), Event.Purchase(\"keyboard\", 45), Event.Logout];\n    for i in 0..len(events) {\n        print(describe(events[i]));\n    }\n    return 0;\n}\n"
 },
 {
  "id": "generics",
  "title": "Generics",
  "html": "<p>Write a function or a struct once and use it with any type. Type parameters go in angle brackets: <code>&lt;T&gt;</code>.</p>\n<p>You rarely write the types at the call: the compiler works them out from the arguments. Behind the scenes it\nmakes one copy of <code>largest</code> for each type you use it with, so generic code runs exactly as fast as code\nwritten for one type.</p>\n<p>Inside generic code, a <code>T</code> can be stored, passed, returned and compared with <code>==</code>, but not printed or added,\nbecause not every type supports that. That's why <code>largest</code> takes the comparison as a parameter.</p>\n<p class=\"try\"><strong>Try it:</strong> add <code>print(items[0]);</code> inside <code>largest</code>. The compiler explains why a <code>T</code> can't be printed.</p>",
  "code": "struct Pair<A, B> { first: A, second: B }\n\nfn largest<T>(items: [T], greater: fn(T, T) -> bool) -> T {\n    let mut best = items[0];\n    for i in 1..len(items) {\n        if greater(items[i], best) { best = items[i]; }\n    }\n    return best;\n}\n\nfn int_gt(a: int, b: int) -> bool { return a > b; }\nfn longer(a: str, b: str) -> bool { return len(a) > len(b); }\n\nfn main() -> int {\n    print(largest([4, 19, 7], int_gt));\n    print(largest([\"fig\", \"banana\", \"kiwi\"], longer));\n    let p = Pair { first: \"year\", second: 2026 };\n    print(p.first + \" \" + str(p.second));\n    return 0;\n}\n"
 },
 {
  "id": "closures",
  "title": "Functions as values",
  "html": "<p>Functions are values too: store them in variables, pass them to other functions, return them. The type of a\nfunction that takes an <code>int</code> and returns an <code>int</code> is written <code>fn(int) -&gt; int</code>.</p>\n<p>An anonymous function, <code>fn(x: int) -&gt; int { ... }</code>, can use variables from around it. Ember captures them by\nvalue: the closure gets a copy when it's created. To share something that changes, capture a struct and change\nits fields, as <code>make_counter</code> does.</p>\n<p class=\"try\"><strong>Try it:</strong> write <code>make_adder(n: int) -&gt; fn(int) -&gt; int</code> that returns a function adding <code>n</code>, and use it\nwith <code>apply_twice</code>.</p>",
  "code": "struct Count { n: int }\n\nfn apply_twice(f: fn(int) -> int, x: int) -> int {\n    return f(f(x));\n}\n\nfn make_counter() -> fn() -> int {\n    let c = Count { n: 0 };\n    return fn() -> int { c.n += 1; return c.n; };\n}\n\nfn main() -> int {\n    let rate = 3;\n    let triple = fn(x: int) -> int { return x * rate; };\n    print(apply_twice(triple, 2));\n\n    let next = make_counter();\n    next();\n    next();\n    print(next());\n    return 0;\n}\n"
 },
 {
  "id": "stdlib",
  "title": "The standard library",
  "html": "<p>The standard library is written in Ember itself and ships inside the compiler. Import a module and give it a\nshort name: <code>import \"std/vec.em\" as vec;</code>.</p>\n<p>Some of what's there:</p>\n<ul>\n<li><code>vec</code>: <code>Vec&lt;T&gt;</code>, a list that grows (<code>push</code>, <code>pop</code>, <code>get</code>, <code>set</code>, <code>size</code>)</li>\n<li><code>str</code>: <code>split</code>, <code>join</code>, <code>upper</code>, <code>replace</code>, <code>trim</code>, <code>contains</code></li>\n<li><code>sort</code>: sorting for ints, floats and strings, plus binary <code>search</code></li>\n<li><code>map</code>: <code>IntMap</code>, a hash map from int to int</li>\n<li><code>iter</code>: <code>map</code>, <code>filter</code>, <code>fold</code>, <code>count</code> over arrays</li>\n<li><code>math</code>, <code>rand</code>, <code>option</code>, <code>io</code></li>\n</ul>\n<p class=\"try\"><strong>Try it:</strong> count the words in a sentence of your own with <code>str.split</code> and <code>len</code>.</p>",
  "code": "import \"std/vec.em\" as vec;\nimport \"std/str.em\" as str;\nimport \"std/sort.em\" as sort;\nimport \"std/iter.em\" as iter;\n\nfn main() -> int {\n    let words = str.split(\"ember compiles to native code and wasm\", \" \");\n    print(str.join(words, \"-\"));\n    print(str.upper(words[0]));\n\n    let scores = vec.make<int>();\n    vec.push(scores, 72);\n    vec.push(scores, 95);\n    vec.push(scores, 81);\n    let arr = vec.to_array(scores);\n    sort.ints(arr);\n    print(arr[0]);\n    print(arr[len(arr) - 1]);\n\n    let passed = iter.count(arr, fn(s: int) -> bool { return s >= 80; });\n    print(str(passed) + \" of \" + str(len(arr)) + \" passed\");\n    return 0;\n}\n"
 },
 {
  "id": "errors",
  "title": "When things go wrong",
  "html": "<p>Ember splits mistakes into two kinds.</p>\n<p><strong>Compile errors</strong> are found before anything runs: a misspelled name, the wrong type, a missing <code>match</code> arm.\nThe compiler reports all of them at once, each with the line and a caret under the exact spot. Click a location\nin the output to jump to it.</p>\n<p><strong>Runtime errors</strong> can only happen while the program runs: an index past the end of an array, a division by\nzero, a failed <code>assert</code>. The program stops with the reason and the line, instead of carrying on with garbage.</p>\n<p>You can stop a program on purpose with <code>panic(\"message\")</code>, and check your assumptions with <code>assert(condition)</code>.</p>\n<p>That's the tour. Open the <a href=\"play.html\">playground</a> to write your own programs, and look at the IR and\nx86-64 stages to see what the compiler made of them.</p>\n<p class=\"try\"><strong>Try it:</strong> call <code>safe_divide(10, 0)</code> and see which kind of error that is.</p>",
  "code": "fn safe_divide(a: int, b: int) -> int {\n    assert(b != 0);\n    return a / b;\n}\n\nfn main() -> int {\n    print(safe_divide(10, 2));\n    let marks = [70, 85, 90];\n    let i = 3;\n    print(marks[i]);\n    print(\"this line never runs\");\n    return 0;\n}\n"
 }
];
