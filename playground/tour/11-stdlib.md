# The standard library

The standard library is written in Ember itself and ships inside the compiler. Import a module and give it a
short name: `import "std/vec.em" as vec;`.

Some of what's there:

- `vec`: `Vec<T>`, a list that grows (`push`, `pop`, `get`, `set`, `size`)
- `str`: `split`, `join`, `upper`, `replace`, `trim`, `contains`
- `sort`: sorting for ints, floats and strings, plus binary `search`
- `map`: `IntMap`, a hash map from int to int
- `iter`: `map`, `filter`, `fold`, `count` over arrays
- `math`, `rand`, `option`, `io`

**Try it:** count the words in a sentence of your own with `str.split` and `len`.

```ember
import "std/vec.em" as vec;
import "std/str.em" as str;
import "std/sort.em" as sort;
import "std/iter.em" as iter;

fn main() -> int {
    let words = str.split("ember compiles to native code and wasm", " ");
    print(str.join(words, "-"));
    print(str.upper(words[0]));

    let scores = vec.make<int>();
    vec.push(scores, 72);
    vec.push(scores, 95);
    vec.push(scores, 81);
    let arr = vec.to_array(scores);
    sort.ints(arr);
    print(arr[0]);
    print(arr[len(arr) - 1]);

    let passed = iter.count(arr, fn(s: int) -> bool { return s >= 80; });
    print(str(passed) + " of " + str(len(arr)) + " passed");
    return 0;
}
```
