import "std/vec.em" as vec;

fn main() -> int {
    let v = vec.make<int>();
    vec.push(v, 5);
    print(vec.get(v, 0));
    print(vec.get(v, 3));
    print(99);
    return 0;
}
