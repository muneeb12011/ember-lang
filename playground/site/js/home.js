// home.js - the live demo in the hero. Nothing runs until the visitor presses Run.
'use strict';
(function () {
  const DEMO = `enum Shape { Circle(float), Square(float) }

fn area(s: Shape) -> float {
    match s {
        Circle(r) => { return 3.14159 * r * r; }
        Square(w) => { return w * w; }
    }
}

fn main() -> int {
    let shapes = [Shape.Circle(2.0), Shape.Square(3.0)];
    let mut total = 0.0;
    for i in 0..len(shapes) {
        total += area(shapes[i]);
    }
    print(total);
    return 0;
}
`;
  const runBtn = $('run'), label = runBtn.querySelector('.label');
  const out = $('out');
  const rail = new Rail($('rail'));
  let busy = false, hasRun = false;
  const editor = new Editor($('editor'), {
    value: DEMO,
    onRun: () => run(),
    onChange: () => { if (hasRun && !busy) rail.stale(true); },
  });
  wireLocations(out, editor);
  rail.set('src', 'lit', lineStat(DEMO));
  highlightStatic();

  const compiler = new Compiler();
  compiler.onState((s, msg) => {
    if (s === 'ready') { runBtn.disabled = false; label.textContent = 'Run'; }
    if (s === 'fatal') { out.innerHTML = quiet('The compiler could not load in this browser: ' + esc(msg)); }
  });

  async function run() {
    if (busy) { compiler.stop(); return; }
    if (!compiler.ready) return;
    busy = hasRun = true;
    runBtn.classList.add('stop');
    label.textContent = 'Stop';
    out.innerHTML = quiet('Compiling…');
    const r = await runPipeline(compiler, editor.value, rail, { out });
    editor.setErrors(r.errors);
    if (r.kind === 'ok') $('demo-note').textContent = `Compiled and ran in ${Math.max(1, Math.round(r.ms))} ms, in your browser`;
    else if (r.kind === 'compile-error') $('demo-note').textContent = 'The compiler found a problem';
    busy = false;
    runBtn.classList.remove('stop');
    label.textContent = 'Run';
  }
  runBtn.addEventListener('click', run);
})();
