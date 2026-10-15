import "std/vec.em" as vec;

fn main() -> int {
    print("Hello, World!");

    // a fixed list (array): its size never changes
    let fruits = ["apple", "banana", "mango"];
    print(fruits[0]);
    print(len(fruits));
    for i in 0..len(fruits) {
        print(fruits[i]);
    }

    // a growable list (like a Python list)
    let names = vec.make<str>();
    vec.push(names, "Ali");
    vec.push(names, "Sara");
    vec.push(names, "Muneeb");
    print(vec.size(names));
    print(vec.get(names, 2));
    for i in 0..vec.size(names) {
        print("Hello " + vec.get(names, i));
    }
    return 0;
}