# Ember playground

A web page that runs the real Ember compiler in the browser: `src/main.cpp` compiled to WebAssembly with Emscripten.
Programs run on the compiler's reference interpreter (`--interp`); the IR, Optimized and x86-64 stages are `--emit-ir` and
`--emit-asm`. The output is byte-for-byte identical to the native compiler's.

| Path | What it is |
|---|---|
| `src/web_main.cpp` | browser entry point: calls the compiler's `main` with an argument list from JavaScript |
| `src/runner.js` | runs one compile in a fresh wasm instance and captures stdout and stderr |
| `examples/*.em` | the example programs in the menu |
| `site/` | the static site: `index.html`, `style.css`, `app.js`, `worker.js`, self-hosted fonts, and the built `embc.js` + `embc.wasm` |
| `build.sh` | rebuilds `site/embc.js`, `site/embc.wasm` and `site/examples.js` (needs Emscripten's `em++`) |

Run it locally: `cd playground/site && python3 -m http.server 8000`, then open http://localhost:8000.

Deploy: any static host. On Vercel, import the GitHub repository and set **Root Directory** to `playground/site`
(no build command). Every push to `main` redeploys.

Rebuild after changing the compiler: `sudo apt install emscripten`, then `sh playground/build.sh` from the repository root.

Each program runs in a web worker; one that runs longer than 5 seconds is stopped by terminating the worker.
