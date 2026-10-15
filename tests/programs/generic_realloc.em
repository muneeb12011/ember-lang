// regression: instantiating a generic struct while inferring its own literal read the type-parameter list through a
// reference into the type table, which the instantiation itself reallocated (found by the fuzzer: compiler crash)
struct Box<T> { v: T, n: int }
fn boxed<T>(x: T) -> Box<T> { return Box { v: x, n: 1 }; }

fn main() -> int {
    print(boxed(41).v + 1);
    print(boxed("s").v);
    print(boxed(boxed(true)).v.v);
    return 0;
}
