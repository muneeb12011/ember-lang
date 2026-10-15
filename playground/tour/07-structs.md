# Structs

A struct groups named fields. Build one by naming every field: `Point { x: 1, y: 2 }`. Read and change fields
with a dot.

Structs are references, like objects in JavaScript or Python. Assigning one to another name doesn't copy it:
both names point at the same struct, so a change through one shows through the other. The garbage collector
frees structs once nothing points at them.

**Try it:** add a field `name: str` to `Account`, give each account a name, and print it in `report`.

```ember
struct Account { balance: int, deposits: int }

fn deposit(a: Account, amount: int) {
    a.balance += amount;
    a.deposits += 1;
}

fn report(a: Account) {
    print(str(a.balance) + " after " + str(a.deposits) + " deposits");
}

fn main() -> int {
    let mine = Account { balance: 0, deposits: 0 };
    deposit(mine, 500);
    deposit(mine, 250);
    report(mine);

    let same = mine;
    deposit(same, 100);
    report(mine);
    return 0;
}
```
