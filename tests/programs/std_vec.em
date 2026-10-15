import "std/vec.em" as vec;

fn main() -> int {
    let v: vec.Vec<int> = vec.make();
    for i in 0..20 { vec.push(v, i * i); }
    print(vec.size(v));
    print(vec.get(v, 7));
    vec.set(v, 7, -1);
    print(vec.get(v, 7));
    print(vec.pop(v));
    print(vec.size(v));
    print(vec.sum(v));
    print(vec.index_of(v, 16));
    print(vec.index_of(v, 17));
    print(vec.contains(v, 100));
    vec.reverse(v);
    print(vec.get(v, 0));
    print(vec.get(v, 18));
    let a = vec.to_array(v);
    print(len(a));
    let w = vec.from_array(a);
    print(vec.sum(w));
    vec.clear(v);
    print(vec.size(v));
    let s = vec.make<str>();
    vec.push(s, "a");
    vec.push(s, "bb");
    for i in 0..9 { vec.push(s, str(i)); }
    print(vec.size(s));
    print(vec.get(s, 1));
    print(vec.get(s, 10));
    print(len(vec.to_array(s)));
    return 0;
}
