import "std/iter.em" as iter;

struct P { name: str, age: int }

fn main() -> int {
    let xs = iter.range(10);
    let sq = iter.map(xs, fn(x: int) -> int { return x * x; });
    print(sq[9]);
    let evens = iter.filter(sq, fn(x: int) -> bool { return x % 2 == 0; });
    print(len(evens));
    print(iter.fold(evens, 0, fn(acc: int, x: int) -> int { return acc + x; }));
    let limit = 50;
    print(iter.any(sq, fn(x: int) -> bool { return x > limit; }));
    print(iter.all(sq, fn(x: int) -> bool { return x < limit; }));
    print(iter.count(sq, fn(x: int) -> bool { return x > limit; }));
    print(iter.find_index(sq, fn(x: int) -> bool { return x > limit; }));
    let words = iter.map(xs, fn(x: int) -> str { return "w" + str(x); });
    print(iter.fold(words, "", fn(acc: str, w: str) -> str { return acc + w; }));
    let lens = iter.map(words, fn(w: str) -> float { return float(len(w)) / 2.0; });
    print(lens[9]);
    let people = [P { name: "ann", age: 31 }, P { name: "bob", age: 17 }, P { name: "cy", age: 45 }];
    let adults = iter.filter(people, fn(p: P) -> bool { return p.age >= 18; });
    print(len(adults));
    print(iter.fold(adults, "", fn(acc: str, p: P) -> str { return acc + p.name + ";"; }));
    let total = new [int](1);
    iter.each(people, fn(p: P) { total[0] += p.age; });
    print(total[0]);
    let table = iter.tabulate(5, fn(i: int) -> [int] { return iter.range(i); });
    print(len(table[4]));
    let sums = iter.zip_with(xs, sq, fn(a: int, b: int) -> int { return a + b; });
    print(sums[3]);
    let flags = iter.map(xs, fn(x: int) -> bool { return x > 6; });
    print(iter.count(flags, fn(b: bool) -> bool { return b; }));
    let empty: [int] = [];
    print(iter.all(empty, fn(x: int) -> bool { return false; }));
    print(len(iter.map(empty, fn(x: int) -> str { return ""; })));
    return 0;
}
