fn main() -> int {
    let x = 1;
    {
        let x = x + 10;
        print(x);
        {
            let x = x * 2;
            print(x);
        }
        print(x);
    }
    print(x);
    let mut total = 0;
    for i in 0..3 {
        let x = i * 100;
        total = total + x;
    }
    print(total);
    print(x);
    return 0;
}
