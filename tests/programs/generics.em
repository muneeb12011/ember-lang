// generic structs, enums and functions (monomorphized); inference from arguments and from the expected type
import "std/vec.em" as vec;
import "std/option.em" as option;

struct Pair<A, B> { first: A, second: B }
struct List<T> { head: T, tail: List<T> }
struct Point { x: int, y: int }
enum Tree<T> { Leaf, Node(Tree<T>, T, Tree<T>) }

fn id<T>(x: T) -> T { return x; }
fn swap<A, B>(p: Pair<A, B>) -> Pair<B, A> { return Pair { first: p.second, second: p.first }; }
fn pair<A, B>(a: A, b: B) -> Pair<A, B> { return Pair<A, B> { first: a, second: b }; }
fn cons<T>(h: T, t: List<T>) -> List<T> { return List { head: h, tail: t }; }
fn length<T>(l: List<T>) -> int {
    if l == null { return 0; }
    return 1 + length(l.tail);
}
fn fill<T>(n: int, x: T) -> [T] {
    let a = new [T](n);
    for i in 0..n { a[i] = x; }
    return a;
}
fn count_eq<T>(a: [T], x: T) -> int {
    let mut c = 0;
    for i in 0..len(a) { if a[i] == x { c += 1; } }
    return c;
}
fn insert(t: Tree<int>, x: int) -> Tree<int> {
    match t {
        Leaf => { return Tree.Node(Tree.Leaf, x, Tree.Leaf); }
        Node(l, v, r) => {
            if x < v { return Tree.Node(insert(l, x), v, r); }
            return Tree.Node(l, v, insert(r, x));
        }
    }
}
fn size<T>(t: Tree<T>) -> int {
    match t {
        Leaf => { return 0; }
        Node(l, _, r) => { return size(l) + 1 + size(r); }
    }
}
fn inorder(t: Tree<int>, out: vec.Vec<int>) {
    match t {
        Leaf => { }
        Node(l, v, r) => {
            inorder(l, out);
            vec.push(out, v);
            inorder(r, out);
        }
    }
}
fn depth<T>(t: Tree<T>) -> int {
    match t {
        Leaf => { return 0; }
        Node(l, _, r) => {
            let a = depth(l);
            let b = depth(r);
            if a > b { return a + 1; }
            return b + 1;
        }
    }
}

fn main() -> int {
    print(id(5));
    print(id("five"));
    print(id<float>(5.5));
    let q = swap(pair(1, "one"));
    print(q.first);
    print(q.second);
    let nested = pair(pair(true, 2), [1.5][0]);
    print(nested.first.second);
    let mut l: List<str> = null;
    for i in 0..5 { l = cons(str(i), l); }
    print(length(l));
    print(l.head);
    print(count_eq(fill(7, "x"), "x"));
    print(count_eq(fill(4, 3.0), 3.0));
    let bools = fill(10, false);
    bools[3] = true;
    print(count_eq(bools, true));
    let p = Point { x: 1, y: 2 };
    let pts = fill(3, p);
    print(count_eq(pts, p));
    print(count_eq(pts, Point { x: 1, y: 2 }));
    let mut t: Tree<int> = Tree.Leaf;
    let keys = [50, 30, 70, 20, 40, 60, 80, 35, 45, 65];
    for i in 0..len(keys) { t = insert(t, keys[i]); }
    print(size(t));
    print(depth(t));
    let out = vec.make<int>();
    inorder(t, out);
    print(vec.size(out));
    print(vec.get(out, 0));
    print(vec.get(out, 9));
    let vv: vec.Vec<vec.Vec<int>> = vec.make();
    for i in 0..4 {
        let row = vec.make<int>();
        for j in 0..i + 1 { vec.push(row, i * j); }
        vec.push(vv, row);
    }
    print(vec.size(vec.get(vv, 3)));
    print(vec.sum(vec.get(vv, 3)));
    let flags = vec.make<bool>();
    for i in 0..100 { vec.push(flags, i % 3 == 0); }
    print(vec.contains(flags, true));
    print(vec.index_of(flags, false));
    print(len(vec.to_array(flags)));
    let words = vec.from_array(["b", "a", "c"]);
    vec.reverse(words);
    print(vec.get(words, 0) + vec.get(words, 2));
    print(option.unwrap_or(vec.get_opt(words, 5), "none"));
    print(option.unwrap(vec.get_opt(words, 1)));
    print(option.is_some(option.some(3)));
    let nothing: option.Option<Point> = option.none();
    print(option.is_none(nothing));
    return 0;
}
