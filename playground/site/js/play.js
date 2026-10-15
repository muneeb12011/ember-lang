// play.js - the playground. Loading an example only loads it: nothing compiles or runs until Run is pressed.
'use strict';
(function () {
  const STORE_KEY = 'ember-playground-code';
  const INFO = {
    ir0: ['IR', 'Three-address code on virtual registers, straight from the type-checked program'],
    ir1: ['Optimized IR', 'After propagation, coalescing, dead-code removal and inlining'],
    asm: ['x86-64', 'Intel syntax, registers chosen by the linear-scan allocator'],
    out: ['Output', 'Run on the compiler\'s reference interpreter'],
  };
  const runBtn = $('run'), label = runBtn.querySelector('.label'), statusEl = $('status');
  const views = { ir0: $('view-ir0'), ir1: $('view-ir1'), asm: $('view-asm'), out: $('view-out') };
  const rail = new Rail($('rail'));
  let busy = false, hasRun = false;

  const editor = new Editor($('editor'), {
    onRun: () => run(),
    onChange: (src) => {
      $('stat-src').textContent = lineStat(src);
      if (hasRun && !busy) { rail.stale(true); statusEl.textContent = 'Edited. Press Run to compile the new version.'; }
      try { localStorage.setItem(STORE_KEY, src); } catch (_) {}
    },
  });
  editor.onCaret = ({ line, col }) => { $('cursor').textContent = `Line ${line}, column ${col}`; };
  Object.values(views).forEach((v) => wireLocations(v, editor));

  function select(s) {
    rail.el.querySelectorAll('button.stage').forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.stage === s)));
    Object.entries(views).forEach(([k, v]) => { v.hidden = k !== s; });
    $('view-title').textContent = INFO[s][0];
    $('view-note').textContent = INFO[s][1];
  }
  rail.el.querySelectorAll('button.stage').forEach((b) => b.addEventListener('click', () => select(b.dataset.stage)));

  // waiting state: shown on load and whenever a different program is loaded
  function waiting(message) {
    hasRun = false;
    rail.reset(lineStat(editor.value));
    views.out.innerHTML = quiet(message);
    ['ir0', 'ir1', 'asm'].forEach((k) => { views[k].innerHTML = quiet('Press Run, and this stage fills in as the compiler finishes it.'); });
    editor.setErrors(new Set());
  }

  const compiler = new Compiler();
  compiler.onState((s, msg) => {
    if (s === 'ready') {
      runBtn.disabled = false;
      label.textContent = 'Run';
      statusEl.textContent = 'Compiler loaded. Nothing runs until you press Run.';
    }
    if (s === 'fatal') statusEl.innerHTML = `<span class="bad">The compiler could not load.</span> ${esc(msg)}`;
  });

  async function run() {
    if (busy) { compiler.stop(); return; }
    if (!compiler.ready) return;
    busy = hasRun = true;
    runBtn.classList.add('stop');
    label.textContent = 'Stop';
    statusEl.textContent = 'Compiling…';
    const r = await runPipeline(compiler, editor.value, rail, views, $('stdin').value);
    editor.setErrors(r.errors);
    if (r.kind === 'compile-error') {
      select('out');
      $('view-note').textContent = 'The program has errors, so it never ran';
      statusEl.innerHTML = '<span class="bad">Compile error.</span> Click a location in the output to jump to that line.';
    } else if (r.kind === 'stopped') {
      select('out');
      statusEl.innerHTML = `<span class="bad">${r.reason === 'timeout' ? 'Timed out.' : 'Stopped.'}</span> The compiler restarted and is ready again.`;
    } else if (r.kind === 'runtime-error') {
      statusEl.innerHTML = `<span class="bad">Exit code ${r.code}.</span> The program compiled but failed while running. Click the line number to see where.`;
    } else {
      statusEl.innerHTML = `<span class="ok">Exit code 0.</span> Compiled and ran in ${Math.max(1, Math.round(r.ms))} ms, in your browser.`;
    }
    busy = false;
    runBtn.classList.remove('stop');
    label.textContent = 'Run';
  }
  runBtn.addEventListener('click', run);

  // examples: picking one loads it, and waits for Run
  const picker = $('examples');
  picker.innerHTML = '<option value="" disabled>Load an example</option>' +
    EXAMPLES.map((ex, i) => `<option value="${i}">${esc(ex.title)}</option>`).join('');
  picker.addEventListener('change', () => {
    const ex = EXAMPLES[+picker.value];
    editor.value = ex.code;
    $('stat-src').textContent = lineStat(ex.code);
    history.replaceState(null, '', location.pathname);
    select(ex.id.includes('optimizer') ? 'ir1' : 'out');
    waiting(`Loaded “${esc(ex.title)}”. Press Run when you're ready.`);
    statusEl.textContent = 'Example loaded. Nothing has run yet.';
  });

  $('share').addEventListener('click', async () => {
    const url = location.origin + location.pathname + '#code=' + encodeShare(editor.value);
    history.replaceState(null, '', url);
    const btn = $('share');
    try { await navigator.clipboard.writeText(url); btn.textContent = 'Link copied'; }
    catch (_) { btn.textContent = 'Link in address bar'; }
    setTimeout(() => { btn.textContent = 'Copy link'; }, 2200);
  });

  // start: a shared link, then the last program edited here, then the first example
  let src = null, index = 0;
  const m = location.hash.match(/code=([\w-]+)/);
  if (m) { try { src = decodeShare(m[1]); } catch (_) {} }
  if (src === null) { try { src = localStorage.getItem(STORE_KEY); } catch (_) {} }
  if (src === null || !src.trim()) src = EXAMPLES[0].code;
  else index = EXAMPLES.findIndex((ex) => ex.code === src);
  picker.value = index >= 0 ? String(index) : '';
  editor.value = src;
  $('stat-src').textContent = lineStat(src);
  select('out');
  waiting(m ? 'Someone shared this program with you. Press Run to compile it.' : 'Press Run (or Ctrl+Enter) to compile this program and run it.');
})();
