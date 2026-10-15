// std/iter.em - higher-order helpers over arrays. `import "std/iter.em" as iter;`
//   iter.map([1, 2, 3], fn(x: int) -> int { return x * x; })          [1, 4, 9]
//   iter.fold(a, 0, fn(acc: int, x: int) -> int { return acc + x; })
// Every function takes the array first and the function last; none of them changes its input.

pub fn map<T, U>(a: [T], f: fn(T) -> U) -> [U] {
    let out = new [U](len(a));
    for i in 0..len(a) { out[i] = f(a[i]); }
    return out;
}

pub fn filter<T>(a: [T], keep: fn(T) -> bool) -> [T] {
    let mut n = 0;
    let marks = new [bool](len(a));
    for i in 0..len(a) {
        if keep(a[i]) {
            marks[i] = true;
            n += 1;
        }
    }
    let out = new [T](n);
    let mut k = 0;
    for i in 0..len(a) {
        if marks[i] {
            out[k] = a[i];
            k += 1;
        }
    }
    return out;
}

pub fn fold<T, A>(a: [T], init: A, f: fn(A, T) -> A) -> A {
    let mut acc = init;
    for i in 0..len(a) { acc = f(acc, a[i]); }
    return acc;
}

pub fn any<T>(a: [T], p: fn(T) -> bool) -> bool {
    for i in 0..len(a) { if p(a[i]) { return true; } }
    return false;
}

pub fn all<T>(a: [T], p: fn(T) -> bool) -> bool {
    for i in 0..len(a) { if !p(a[i]) { return false; } }
    return true;
}

pub fn count<T>(a: [T], p: fn(T) -> bool) -> int {
    let mut n = 0;
    for i in 0..len(a) { if p(a[i]) { n += 1; } }
    return n;
}

pub fn each<T>(a: [T], f: fn(T)) {
    for i in 0..len(a) { f(a[i]); }
}

// the index of the first element that satisfies p, or -1
pub fn find_index<T>(a: [T], p: fn(T) -> bool) -> int {
    for i in 0..len(a) { if p(a[i]) { return i; } }
    return -1;
}

// [0, 1, ..., n-1]
pub fn range(n: int) -> [int] {
    let out = new [int](n);
    for i in 0..n { out[i] = i; }
    return out;
}

// [f(0), f(1), ..., f(n-1)]
pub fn tabulate<T>(n: int, f: fn(int) -> T) -> [T] {
    let out = new [T](n);
    for i in 0..n { out[i] = f(i); }
    return out;
}

pub fn zip_with<A, B, C>(a: [A], b: [B], f: fn(A, B) -> C) -> [C] {
    let mut n = len(a);
    if len(b) < n { n = len(b); }
    let out = new [C](n);
    for i in 0..n { out[i] = f(a[i], b[i]); }
    return out;
}
