// std/compat/rand.em - DEPRECATED. The prefixed names std/rand.em used before Ember had modules, kept for one
// release. Brought in automatically by `import "std/rand.em";` (without `as`); new code should write
// `import "std/rand.em" as rand;` and call the new names.
import "std/rand.em" as modern;

pub fn rng_new(seed: int) -> modern.Rng { return modern.make(seed); }
pub fn rng_next(r: modern.Rng) -> int { return modern.next(r); }
pub fn rng_range(r: modern.Rng, n: int) -> int { return modern.range(r, n); }
pub fn rng_float(r: modern.Rng) -> float { return modern.next_float(r); }
pub fn rng_shuffle(r: modern.Rng, a: [int]) { modern.shuffle(r, a); }
