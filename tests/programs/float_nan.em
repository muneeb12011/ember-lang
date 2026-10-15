// NaN signs must match on every target: an invalid operation gives the processor's default NaN (printed "-nan" by
// glibc), a NaN argument is passed through, and inf prints as inf (regression: the wasm target's math functions,
// which come from JavaScript, returned a positive NaN; found by the fuzzer)
fn main() -> int {
    let zero = 0.0;
    print(pow(-1.0, 0.5));
    print(sqrt(-1.0));
    print(zero / zero);
    print(log(-1.0));
    print(-(zero / zero));
    print(sin(-(zero / zero)));
    print(atan2(1.0, -(zero / zero)));
    print(1.0 / zero);
    print(-1.0 / zero);
    print(exp(1000.0));
    print(str(pow(-8.0, 1.0 / 3.0)));
    print(pow(-(zero / zero), 0.0));
    return 0;
}
