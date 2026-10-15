fn greet(name: str, times: int) {
    for i in 0..times { print(name); }
}

fn main() -> int {
    print("hello\tworld");
    print("say \"hi\"");
    print("back\\slash");
    print(true);
    print(false);
    greet("ember", 3);
    return 7;
}
