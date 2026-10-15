fn sum(a: [int]) -> int {
    let mut s = 0;
    for i in 0..len(a) { s = s + a[i]; }
    return s;
}

fn fill(a: [int]) {
    for i in 0..len(a) { a[i] = i * i; }
}

fn main() -> int {
    let a = new_array(1000);
    fill(a);
    print(sum(a));
    print(len(a));
    return 0;
}
