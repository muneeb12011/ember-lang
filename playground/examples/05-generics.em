import "std/option.em" as option;

struct Pair<A, B> { first: A, second: B }

fn swap<A, B>(p: Pair<A, B>) -> Pair<B, A> {
    return Pair { first: p.second, second: p.first };
}

fn find(xs: [int], target: int) -> option.Option<int> {
    for i in 0..len(xs) {
        if xs[i] == target { return option.some(i); }
    }
    return option.none<int>();
}

fn main() -> int {
    let p = Pair { first: "age", second: 19 };
    let q = swap(p);
    print(q.first);
    print(q.second);

    let xs = [4, 8, 15, 16, 23, 42];
    print(option.unwrap_or(find(xs, 15), -1));
    print(option.unwrap_or(find(xs, 7), -1));
    return 0;
}
