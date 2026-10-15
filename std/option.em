// std/option.em - Option<T>: a value that may be absent. `import "std/option.em" as option;`
//   let o = option.some(5);            let n: option.Option<int> = option.none();
//   match o { Some(x) => { ... } None => { ... } }
pub enum Option<T> { Some(T), None }

pub fn some<T>(x: T) -> Option<T> { return Option.Some(x); }
pub fn none<T>() -> Option<T> { return Option.None; }

pub fn is_some<T>(o: Option<T>) -> bool {
    match o {
        Some(_) => { return true; }
        None => { return false; }
    }
}

pub fn is_none<T>(o: Option<T>) -> bool { return !is_some(o); }

// the value inside, or a runtime error for None
pub fn unwrap<T>(o: Option<T>) -> T {
    match o {
        Some(x) => { return x; }
        None => { panic("unwrap of an empty Option"); }
    }
}

pub fn unwrap_or<T>(o: Option<T>, fallback: T) -> T {
    match o {
        Some(x) => { return x; }
        None => { return fallback; }
    }
}
