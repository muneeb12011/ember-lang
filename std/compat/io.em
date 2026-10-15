// std/compat/io.em - DEPRECATED. The prefixed names std/io.em used before Ember had modules, kept for one
// release. Brought in automatically by `import "std/io.em";` (without `as`); new code should write
// `import "std/io.em" as io;` and call the new names.
import "std/io.em" as modern;

pub fn io_read_ints(n: int) -> [int] { return modern.read_ints(n); }
pub fn io_read_lines() -> [str] { return modern.read_lines(); }
