// This is a comment (like # in Python)

/* This is a
   block comment */

struct Person { name: str, age: int }

fn greet(p: Person) -> str {
    return "Hi " + p.name + ", you are " + str(p.age);
}

fn main() -> int {
    print("Hello, World!");

    let name = "Muneeb";
    print("My name is " + name);

    let p = Person { name: "Muneeb", age: 19 };
    print(greet(p));

    for i in 1..6 {
        if i % 2 == 0 {
            print(str(i) + " is even");
        } else {
            print(str(i) + " is odd");
        }
    }

    let nums = [5, 3, 8, 1];
    let mut sum = 0;
    for i in 0..len(nums) {
        sum += nums[i];
    }
    print("sum = " + str(sum));
    print(3.5 * 2.0);
    print(10 > 3);
    return 0;
}