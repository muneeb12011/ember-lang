# Enums and match

An enum is a value that is exactly one of several shapes, and each shape can carry its own data. `match` takes
it apart and runs the arm for the shape it has.

The compiler checks that a `match` handles every variant. Add a new variant and forget one `match`
somewhere, and the program won't compile until you handle it. That turns a whole class of bugs into compile
errors. A `_` arm means "anything else".

**Try it:** add `Refund(int)` to `Event` and run. The compiler names the variant `describe` is missing.

```ember
enum Event { Login(str), Purchase(str, int), Logout }

fn describe(e: Event) -> str {
    match e {
        Login(user) => { return user + " signed in"; }
        Purchase(item, price) => { return "bought " + item + " for " + str(price); }
        Logout => { return "signed out"; }
    }
}

fn main() -> int {
    let events = [Event.Login("sara"), Event.Purchase("keyboard", 45), Event.Logout];
    for i in 0..len(events) {
        print(describe(events[i]));
    }
    return 0;
}
```
