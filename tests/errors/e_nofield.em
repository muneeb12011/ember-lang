// expect: has no field `z`
struct P { x: int }
fn main() -> int {
    let p = P { x: 1 };
    print(p.z);
    return 0;
}
