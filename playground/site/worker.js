// worker.js - runs the Ember compiler off the main thread, so a program with an infinite loop cannot freeze the
// page: the page simply terminates this worker and starts a new one.
importScripts('embc.js', 'runner.js');

// compileStreaming needs the server to send application/wasm; fall back to a plain download if it does not
const compileSlow = () => fetch('embc.wasm').then(r => r.arrayBuffer()).then(b => WebAssembly.compile(b));
const wasmReady = (WebAssembly.compileStreaming
  ? WebAssembly.compileStreaming(fetch('embc.wasm')).catch(compileSlow)
  : compileSlow());

wasmReady.then(() => postMessage({ type: 'ready' }), e => postMessage({ type: 'fatal', message: String(e) }));

onmessage = async (ev) => {
  const { id, src, args, stdin } = ev.data;
  try {
    const wasm = await wasmReady;
    const t0 = performance.now();
    const r = await runEmbc(createEmbc, wasm, src, args, stdin);
    postMessage({ type: 'result', id, ...r, ms: performance.now() - t0 });
  } catch (e) {
    postMessage({ type: 'result', id, code: 70, stdout: '', stderr: 'internal error: ' + e + '\n', ms: 0 });
  }
};
