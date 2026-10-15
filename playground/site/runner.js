// runner.js - run the WebAssembly build of embc once: a fresh instance per run (the compiler keeps global state),
// with the compiled wasm module cached so each run only pays for instantiation.
// Used by the playground's web worker and by the node test (test.cjs).
async function runEmbc(createEmbc, wasmModule, src, args, stdin) {
  const out = [], err = [];
  const input = new TextEncoder().encode(stdin || '');
  let inPos = 0;
  const m = await createEmbc({
    instantiateWasm(imports, done) {
      WebAssembly.instantiate(wasmModule, imports).then(inst => done(inst));
      return {};
    },
    preRun: [mod => mod.FS.init(
      () => (inPos < input.length ? input[inPos++] : null),
      c => { if (c !== null) out.push(c & 255); },
      c => { if (c !== null) err.push(c & 255); })],
    print() {}, printErr() {},
  });
  m.FS.writeFile('main.em', src);
  let code;
  try {
    code = m.ccall('embc_run', 'number', ['string'], [['main.em', ...args].join('\n')]);
  } catch (e) {
    const msg = String(e && e.message || e);
    if (/memory access out of bounds|call stack|Maximum call stack|stack overflow/i.test(msg)) {
      err.push(...new TextEncoder().encode('runtime error: stack overflow\n'));
    } else {
      err.push(...new TextEncoder().encode('internal error: ' + msg + '\n'));
    }
    code = 1;
  }
  const dec = new TextDecoder();
  return { code, stdout: dec.decode(new Uint8Array(out)), stderr: dec.decode(new Uint8Array(err)) };
}
if (typeof module !== 'undefined') module.exports = { runEmbc };
