// strength reduction: multiplication and signed division / remainder by constants, at the edges of the int range
fn main() -> int {
    let xs = [0, 1, -1, 7, -7, 8, -8, 9, -9, 1023, -1023, 9223372036854775807, -9223372036854775807 - 1, 123456789, -123456789];
    let mut h = 0;
    for i in 0..len(xs) {
        let x = xs[i];
        print(x / 2); print(x % 2); print(x / 8); print(x % 8); print(x / 1024); print(x % 1024);
        print(x * 2); print(x * 8); print(x * 3); print(x * 5); print(x * 9); print(x / 4611686018427387904); print(x % 4611686018427387904);
    }
    return 0;
}
