// A tour of Ember: modules, structs, enums with match, generics, closures, strings and the standard library.
import "std/vec.em" as vec;
import "std/iter.em" as iter;
import "std/str.em" as strings;
import "std/option.em" as option;

enum Shape { Circle(float), Rect(float, float), Triangle(float, float, float) }

struct Named<T> { name: str, value: T }

fn area(s: Shape) -> float {
    match s {
        Circle(r) => { return 3.141592653589793 * r * r; }
        Rect(w, h) => { return w * h; }
        Triangle(a, b, c) => {
            let p = (a + b + c) / 2.0;
            return sqrt(p * (p - a) * (p - b) * (p - c));
        }
    }
}

fn largest<T>(items: [Named<T>], key: fn(T) -> float) -> option.Option<Named<T>> {
    if len(items) == 0 { return option.none(); }
    let mut best = items[0];
    for i in 1..len(items) {
        if key(items[i].value) > key(best.value) { best = items[i]; }
    }
    return option.some(best);
}

fn main() -> int {
    let shapes = [
        Named { name: "coin", value: Shape.Circle(1.2) },
        Named { name: "door", value: Shape.Rect(0.9, 2.1) },
        Named { name: "sail", value: Shape.Triangle(3.0, 4.0, 5.0) }
    ];
    match largest(shapes, area) {
        Some(n) => { print("largest: " + n.name + " (" + str(area(n.value)) + ")"); }
        None => { print("no shapes"); }
    }

    let text = "the quick brown fox jumps over the lazy dog the end";
    let words = strings.split(text, " ");
    let long_words = iter.filter(words, fn(w: str) -> bool { return len(w) > 3; });
    print(strings.join(long_words, ","));
    let threshold = 4;
    let counts = iter.map(words, fn(w: str) -> int { return len(w); });
    print(iter.count(counts, fn(n: int) -> bool { return n >= threshold; }));

    let seen: vec.Vec<str> = vec.make();
    for i in 0..len(words) {
        if !vec.contains(seen, words[i]) { vec.push(seen, words[i]); }
    }
    print(str(vec.size(seen)) + " distinct words");
    print(strings.upper(vec.last(seen)));
    return 0;
}
