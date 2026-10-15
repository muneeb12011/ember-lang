// std/vec.em - Vec<T>: a growable array of any element type. Capacity doubles, so push is amortised O(1).
//   import "std/vec.em" as vec;
//   let v: vec.Vec<int> = vec.make();      (or vec.make<int>())
//   vec.push(v, 3); vec.get(v, 0); vec.size(v)
// [bool] storage stays one byte per element inside a Vec<bool>.
import "std/option.em" as option;

pub struct Vec<T> { data: [T], len: int }

pub fn make<T>() -> Vec<T> {
    return Vec<T> { data: new [T](4), len: 0 };
}

pub fn with_capacity<T>(n: int) -> Vec<T> {
    let mut cap = 4;
    while cap < n { cap *= 2; }
    return Vec<T> { data: new [T](cap), len: 0 };
}

pub fn from_array<T>(a: [T]) -> Vec<T> {
    let v: Vec<T> = with_capacity(len(a));
    for i in 0..len(a) { push(v, a[i]); }
    return v;
}

pub fn size<T>(v: Vec<T>) -> int { return v.len; }
pub fn is_empty<T>(v: Vec<T>) -> bool { return v.len == 0; }

pub fn push<T>(v: Vec<T>, x: T) {
    if v.len == len(v.data) {
        let bigger = new [T](len(v.data) * 2);
        for i in 0..v.len { bigger[i] = v.data[i]; }
        v.data = bigger;
    }
    v.data[v.len] = x;
    v.len += 1;
}

pub fn get<T>(v: Vec<T>, i: int) -> T {
    if i < 0 || i >= v.len { panic("vec.get: index " + str(i) + " out of range 0.." + str(v.len)); }
    return v.data[i];
}

// None instead of a runtime error when i is out of range
pub fn get_opt<T>(v: Vec<T>, i: int) -> option.Option<T> {
    if i < 0 || i >= v.len { return option.Option.None; }
    return option.Option.Some(v.data[i]);
}

pub fn set<T>(v: Vec<T>, i: int, x: T) {
    if i < 0 || i >= v.len { panic("vec.set: index " + str(i) + " out of range 0.." + str(v.len)); }
    v.data[i] = x;
}

pub fn pop<T>(v: Vec<T>) -> T {
    if v.len == 0 { panic("vec.pop: the vector is empty"); }
    v.len -= 1;
    return v.data[v.len];
}

pub fn last<T>(v: Vec<T>) -> T {
    if v.len == 0 { panic("vec.last: the vector is empty"); }
    return v.data[v.len - 1];
}

pub fn clear<T>(v: Vec<T>) { v.len = 0; }

pub fn to_array<T>(v: Vec<T>) -> [T] {
    let out = new [T](v.len);
    for i in 0..v.len { out[i] = v.data[i]; }
    return out;
}

// the first index holding x (compared with ==: by value for scalars and strs, by identity for references), or -1
pub fn index_of<T>(v: Vec<T>, x: T) -> int {
    for i in 0..v.len { if v.data[i] == x { return i; } }
    return -1;
}

pub fn contains<T>(v: Vec<T>, x: T) -> bool { return index_of(v, x) >= 0; }

pub fn reverse<T>(v: Vec<T>) {
    let mut i = 0;
    let mut j = v.len - 1;
    while i < j {
        let t = v.data[i];
        v.data[i] = v.data[j];
        v.data[j] = t;
        i += 1;
        j -= 1;
    }
}

pub fn sum(v: Vec<int>) -> int {
    let mut s = 0;
    for i in 0..v.len { s += v.data[i]; }
    return s;
}
