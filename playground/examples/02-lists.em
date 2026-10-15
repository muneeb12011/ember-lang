import "std/vec.em" as vec;

fn main() -> int {
    // a fixed-size array
    let fruits = ["apple", "banana", "mango"];
    print("first fruit: " + fruits[0]);
    print("count: " + str(len(fruits)));

    // a growable list, like a Python list
    let names = vec.make<str>();
    vec.push(names, "Ali");
    vec.push(names, "Sara");
    vec.push(names, "Muneeb");
    for i in 0..vec.size(names) {
        print("Hello " + vec.get(names, i));
    }

    let nums = vec.from_array([5, 3, 8, 1]);
    print("sum = " + str(vec.sum(nums)));
    return 0;
}
