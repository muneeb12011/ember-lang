// app.js - the Ember playground: editor, running the compiler in a worker, and the four output views.
'use strict';

const $ = (id) => document.getElementById(id);
const code = $('code'), hl = $('hl'), gutter = $('gutter'), scroller = $('scroller');
const runBtn = $('run'), runLabel = runBtn.querySelector('.run-label');
const statusEl = $('status');
const RUN_LIMIT_MS = 5000;
const STORE_KEY = 'ember-playground-code';

// ---------------------------------------------------------------- syntax highlighting

const KEYWORDS = new Set(['fn', 'let', 'mut', 'if', 'else', 'while', 'for', 'in', 'return', 'break', 'continue',
  'struct', 'enum', 'match', 'import', 'pub', 'as', 'const', 'new', 'true', 'false', 'null']);
const TYPES = new Set(['int', 'float', 'bool', 'str', 'byte']);
const BUILTINS = new Set(['print', 'put', 'len', 'str', 'int', 'float', 'chr', 'substr', 'parse_int', 'str_from_bytes',
  'read_int', 'read_line', 'at_eof', 'sqrt', 'sin', 'cos', 'tan', 'atan', 'atan2', 'exp', 'log', 'pow', 'floor', 'ceil',
  'read_file', 'write_file', 'append_file', 'file_exists', 'arg_count', 'arg', 'env', 'has_env', 'exit', 'clock_ns',
  'panic', 'assert']);
const TOKEN = /(\/\/[^\n]*)|(\/\*[\s\S]*?(?:\*\/|$))|("(?:\\.|[^"\\\n])*"?)|('(?:\\.|[^'\\\n])*'?)|(\b0x[0-9a-fA-F_]+\b|\b0b[01_]+\b|\b\d[\d_]*(?:\.\d[\d_]*)?(?:[eE][+-]?\d+)?\b)|([A-Za-z_]\w*)|([^\sA-Za-z_0-9"'/]+|\/)/g;

const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const span = (cls, s) => `<span class="${cls}">${esc(s)}</span>`;

function highlightEmber(src) {
  let out = '', last = 0, prevWord = '';
  TOKEN.lastIndex = 0;
  for (let m; (m = TOKEN.exec(src));) {
    out += esc(src.slice(last, m.index));
    last = TOKEN.lastIndex;
    const [t, lineCom, blockCom, s, ch, num, word, op] = m;
    if (lineCom || blockCom) out += span('t-com', t);
    else if (s || ch) out += span('t-str', t);
    else if (num) out += span('t-num', t);
    else if (word) {
      const next = src.slice(last).match(/^\s*(<[^<>()\n]*>)?\s*\(/);
      if (KEYWORDS.has(word)) out += span('t-kw', t);
      else if (prevWord === 'fn' || prevWord === 'struct' || prevWord === 'enum') out += span(prevWord === 'fn' ? 't-fn' : 't-type', t);
      else if (next && BUILTINS.has(word)) out += span('t-bi', t);
      else if (TYPES.has(word) || /^[A-Z]/.test(word)) out += span('t-type', t);
      else if (next) out += span('t-fn', t);
      else out += esc(t);
      prevWord = word;
      continue;
    } else if (op) out += span('t-op', t);
    else out += esc(t);
    if (!word) prevWord = prevWord && /^\s*$/.test(t) ? prevWord : '';
  }
  return out + esc(src.slice(last));
}

// IR and assembly: labels, registers, numbers, comments
function highlightLow(text, kind) {
  return text.split('\n').map((line) => {
    if (/^\s*$/.test(line)) return '';
    if (kind === 'ir' && line.startsWith('func ')) return span('t-kw', 'func') + esc(line.slice(4));
    if (/^\S.*:$/.test(line)) return span('t-label', line);
    if (kind === 'asm' && /^\s+\./.test(line)) return span('t-com', line);
    let out = '', last = 0;
    const re = kind === 'ir'
      ? /\b(r\d+)\b|\b(L\d+)\b|(-?\b\d+(?:\.\d+)?\b)|^(\s+)([a-z_]+)(?=\s|$)|(=\s*)([a-z_]+)/g
      : /\b(r[a-z0-9]+|e[a-z]{2}|xmm\d+|[re]?[sb]p|[re]?[sd]i|[abcd]l)\b|(\.L\w+)|(-?\b\d+\b)|^(\s+)([a-z]+)/g;
    for (let m; (m = re.exec(line));) {
      out += esc(line.slice(last, m.index));
      last = re.lastIndex;
      if (m[1]) out += span('t-reg', m[1]);
      else if (m[2]) out += span('t-label', m[2]);
      else if (m[3]) out += span('t-num', m[3]);
      else if (m[5]) out += esc(m[4]) + span('t-kw', m[5]);
      else if (m[7]) out += esc(m[6]) + span('t-kw', m[7]);
      else out += esc(m[0]);
    }
    return out + esc(line.slice(last));
  }).join('\n');
}

// ---------------------------------------------------------------- editor

let errorLines = new Set();

function render() {
  const src = code.value;
  const lines = highlightEmber(src).split('\n');
  hl.innerHTML = lines.map((l, i) => `<span class="row${errorLines.has(i + 1) ? ' err' : ''}">${l || ' '}</span>`).join('');
  const caretLine = src.slice(0, code.selectionStart).split('\n').length;
  let g = '';
  for (let i = 1; i <= lines.length; i++) g += `<div class="${errorLines.has(i) ? 'err' : i === caretLine ? 'cur' : ''}">${i}</div>`;
  gutter.innerHTML = g;
  // the textarea must cover the whole highlighted area, including long lines
  code.style.height = hl.offsetHeight + 'px';
}

function updateCursor() {
  const before = code.value.slice(0, code.selectionStart).split('\n');
  $('cursor').textContent = `Ln ${before.length}, Col ${before[before.length - 1].length + 1}`;
  [...gutter.children].forEach((d, i) => d.classList.toggle('cur', i + 1 === before.length && !errorLines.has(i + 1)));
}

function insert(text) {
  // execCommand keeps the browser's undo history working; fall back where it is unavailable
  if (!document.execCommand('insertText', false, text)) {
    code.setRangeText(text, code.selectionStart, code.selectionEnd, 'end');
    onEdit();
  }
}

function lineStart(pos) { return code.value.lastIndexOf('\n', pos - 1) + 1; }

code.addEventListener('keydown', (e) => {
  if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); run(); return; }
  if (e.key === 'Escape') { code.blur(); return; }
  const v = code.value, s = code.selectionStart, end = code.selectionEnd;
  if (e.key === 'Tab') {
    e.preventDefault();
    if (e.shiftKey) {
      const ls = lineStart(s);
      const n = (v.slice(ls).match(/^ {1,4}/) || [''])[0].length;
      if (n) { code.setSelectionRange(ls, ls + n); insert(''); code.setSelectionRange(Math.max(ls, s - n), Math.max(ls, end - n)); }
    } else insert('    ');
  } else if (e.key === 'Enter' && !e.shiftKey && s === end) {
    e.preventDefault();
    const line = v.slice(lineStart(s), s);
    let indent = line.match(/^\s*/)[0];
    if (/[{(\[]\s*$/.test(line)) indent += '    ';
    const closing = /^\s*[}\])]/.test(v.slice(s));
    if (closing && /[{(\[]\s*$/.test(line)) {
      insert('\n' + indent + '\n' + indent.slice(4));
      code.setSelectionRange(s + 1 + indent.length, s + 1 + indent.length);
    } else insert('\n' + indent);
  } else if (e.key === '}' && s === end) {
    const ls = lineStart(s);
    if (/^ {4,}$/.test(v.slice(ls, s))) { e.preventDefault(); code.setSelectionRange(s - 4, s); insert('}'); }
  }
});

let saveTimer = 0;
function onEdit() {
  if (errorLines.size) errorLines = new Set();
  render();
  updateCursor();
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => { try { localStorage.setItem(STORE_KEY, code.value); } catch (_) {} }, 400);
}
code.addEventListener('input', onEdit);
['keyup', 'click', 'select'].forEach((ev) => code.addEventListener(ev, updateCursor));

function goToLine(line, col) {
  const lines = code.value.split('\n');
  let pos = 0;
  for (let i = 0; i < line - 1 && i < lines.length; i++) pos += lines[i].length + 1;
  pos += Math.max(0, (col || 1) - 1);
  code.focus();
  code.setSelectionRange(pos, pos);
  const top = (line - 1) * parseFloat(getComputedStyle(hl).lineHeight);
  if (top < scroller.scrollTop || top > scroller.scrollTop + scroller.clientHeight - 60) scroller.scrollTop = Math.max(0, top - 80);
  updateCursor();
}

// ---------------------------------------------------------------- the compiler worker

let worker, ready = false, jobId = 0;
const pending = new Map();

function startWorker() {
  ready = false;
  worker = new Worker('worker.js');
  worker.onmessage = (ev) => {
    const msg = ev.data;
    if (msg.type === 'ready') {
      ready = true;
      setRunState('idle');
      if (firstRunPending) { firstRunPending = false; run(); }
    } else if (msg.type === 'fatal') {
      statusEl.innerHTML = `<span class="bad">The compiler could not load.</span> ${esc(msg.message)}`;
    } else if (msg.type === 'result' && pending.has(msg.id)) {
      const { resolve, timer } = pending.get(msg.id);
      clearTimeout(timer);
      pending.delete(msg.id);
      resolve(msg);
    }
  };
}

function compile(args, { limit = 0 } = {}) {
  const id = ++jobId;
  return new Promise((resolve) => {
    let timer = 0;
    if (limit) timer = setTimeout(() => stopWorker('timeout'), limit);
    pending.set(id, { resolve, timer });
    worker.postMessage({ id, src: code.value, args, stdin: $('stdin').value });
  });
}

function stopWorker(reason) {
  worker.terminate();
  for (const { resolve, timer } of pending.values()) {
    clearTimeout(timer);
    resolve({ stopped: reason });
  }
  pending.clear();
  startWorker();
}

// ---------------------------------------------------------------- the pipeline rail

const STAGES = ['src', 'ir0', 'ir1', 'asm', 'out'];
const INFO = {
  ir0: ['IR', 'Three-address code with virtual registers, before optimization'],
  ir1: ['Optimized IR', 'After constant and copy propagation, coalescing, dead-code removal and inlining'],
  asm: ['x86-64', 'Intel syntax, with registers chosen by the linear-scan allocator'],
  out: ['Output', "Run on the compiler's reference interpreter"],
};
const rail = $('rail'), track = document.querySelector('.rail-track'), heat = $('heat');
const stageEl = (s) => document.querySelector(`.stage[data-stage="${s}"]`);
const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;
const pace = () => new Promise((r) => setTimeout(r, reducedMotion ? 0 : 170));
let heatAt = 0, heatErr = false, hasRun = false;

function nodeX(s) {
  const n = stageEl(s).querySelector('.node').getBoundingClientRect();
  return n.left - rail.getBoundingClientRect().left + rail.scrollLeft + n.width / 2;
}
function layoutRail() {
  const n = stageEl('src').querySelector('.node').getBoundingClientRect();
  const a = nodeX('src'), b = nodeX('out');
  track.style.left = a + 'px';
  track.style.top = (n.top - rail.getBoundingClientRect().top + n.height / 2 - 1.5) + 'px';
  track.style.width = (b - a) + 'px';
  heat.style.setProperty('--heat-span', (b - a) + 'px');
  setHeat(heatAt, heatErr);
}
function setHeat(i, err = false) {
  heatAt = i;
  heatErr = err;
  heat.classList.toggle('err', err);
  heat.style.width = i <= 0 ? '0px' : (nodeX(STAGES[i]) - nodeX('src')) + 'px';
}
function setStage(s, state, stat) {
  const el = stageEl(s);
  el.classList.remove('lit', 'err', 'skip');
  if (state) el.classList.add(state);
  if (stat !== undefined) $('stat-' + s).innerHTML = stat;
}
function select(s) {
  if (s === 'src') { code.focus(); return; }
  STAGES.forEach((t) => { if (t !== 'src') stageEl(t).setAttribute('aria-pressed', String(t === s)); });
  document.querySelectorAll('.view').forEach((v) => { v.hidden = v.id !== 'view-' + s; });
  $('view-title').textContent = INFO[s][0];
  $('view-note').textContent = INFO[s][1];
}
document.querySelectorAll('.stage').forEach((b) => b.addEventListener('click', () => select(b.dataset.stage)));

function lineStat() {
  const n = code.value.replace(/\n+$/, '').split('\n').length;
  return `${n} line${n === 1 ? '' : 's'}`;
}
code.addEventListener('input', () => {
  $('stat-src').textContent = lineStat();
  if (hasRun && !running) {
    rail.classList.add('stale');
    statusEl.textContent = 'Edited. Run again to recompile.';
  }
});

// ---------------------------------------------------------------- running

let running = false, firstRunPending = true;

function setRunState(state) {
  running = state === 'running';
  runBtn.disabled = !ready && !running;
  runBtn.classList.toggle('stop', running);
  runLabel.textContent = running ? 'Stop' : ready ? 'Run' : 'Loading';
}

function linkify(stderr) {
  return esc(stderr)
    .replace(/main\.em:(\d+):(\d+)/g, '<a class="loc" data-line="$1" data-col="$2">main.em:$1:$2</a>')
    .replace(/\(line (\d+)\)/g, '(<a class="loc" data-line="$1">line $1</a>)');
}
function errorLinesOf(stderr) {
  const set = new Set();
  for (const m of stderr.matchAll(/main\.em:(\d+):\d+: error/g)) set.add(+m[1]);
  for (const m of stderr.matchAll(/runtime error: .*\(line (\d+)\)/g)) set.add(+m[1]);
  return set;
}
const countIR = (t) => t.split('\n').filter((l) => /^ {2}\S/.test(l)).length;
const countAsm = (t) => t.split('\n').filter((l) => /^ {2}[a-z]/.test(l)).length;
const quiet = (text) => `<span class="quiet">${esc(text)}</span>`;

function resetRun() {
  setHeat(0);
  rail.classList.remove('stale');
  setStage('src', 'lit', lineStat());
  ['ir0', 'ir1', 'asm', 'out'].forEach((s) => setStage(s, '', ''));
  errorLines = new Set();
  render();
}

function skipRest(from, message) {
  STAGES.slice(STAGES.indexOf(from)).forEach((s) => {
    setStage(s, 'skip', '');
    if (s !== 'out') $('view-' + s).innerHTML = quiet(message);
  });
}

async function run() {
  if (running) { stopWorker('stopped'); return; }
  if (!ready) { firstRunPending = true; return; }
  hasRun = true;
  setRunState('running');
  statusEl.textContent = 'Compiling';
  resetRun();
  const out = $('view-out');

  // 1. front end + lowering: if this fails, the program has compile errors
  const ir0 = await compile(['--emit-ir', '-O0']);
  if (ir0.stopped) return finishStopped(ir0.stopped, 'ir0');
  if (ir0.code !== 0) {
    errorLines = errorLinesOf(ir0.stderr);
    render();
    setStage('ir0', 'err', 'error');
    setHeat(1, true);
    skipRest('ir1', 'The compiler stopped at the errors shown in Output, so it never got this far.');
    $('view-ir0').innerHTML = quiet('The program has compile errors, so no IR was produced. They are listed in Output.');
    out.innerHTML = `<span class="stderr">${linkify(ir0.stderr)}</span>`;
    select('out');
    $('view-note').textContent = 'Compile errors: the program never ran';
    statusEl.innerHTML = '<span class="bad">Compile error.</span> Click a location to jump to that line.';
    setRunState('idle');
    return;
  }
  await pace();
  const n0 = countIR(ir0.stdout);
  $('view-ir0').innerHTML = highlightLow(ir0.stdout, 'ir');
  setStage('ir0', 'lit', `${n0} instr`);
  setHeat(1);

  // 2. the optimizer
  const ir1 = await compile(['--emit-ir']);
  if (ir1.stopped) return finishStopped(ir1.stopped, 'ir1');
  await pace();
  const n1 = countIR(ir1.stdout);
  $('view-ir1').innerHTML = highlightLow(ir1.stdout, 'ir');
  const pct = n0 ? Math.round((100 * (n0 - n1)) / n0) : 0;
  setStage('ir1', 'lit', `${n1} instr${pct > 0 ? ` <span class="gain">−${pct}%</span>` : ''}`);
  setHeat(2);

  // 3. the x86-64 back end
  const asm = await compile(['--emit-asm']);
  if (asm.stopped) return finishStopped(asm.stopped, 'asm');
  await pace();
  $('view-asm').innerHTML = highlightLow(asm.stdout, 'asm');
  setStage('asm', 'lit', `${countAsm(asm.stdout)} instr`);
  setHeat(3);

  // 4. run it
  statusEl.textContent = 'Running';
  const r = await compile(['--interp'], { limit: RUN_LIMIT_MS });
  if (r.stopped) return finishStopped(r.stopped, 'out');
  await pace();
  errorLines = errorLinesOf(r.stderr);
  render();
  let html = esc(r.stdout);
  if (r.stderr) html += `<span class="stderr">${linkify(r.stderr)}</span>`;
  if (!r.stdout && !r.stderr) html = quiet('The program finished without printing anything.');
  out.innerHTML = html;
  setStage('out', r.code === 0 ? 'lit' : 'err', `exit ${r.code}`);
  setHeat(4);
  statusEl.innerHTML = `<span class="${r.code === 0 ? 'ok' : 'bad'}">Exit code ${r.code}.</span> ` +
    (r.code === 0 ? `Compiled and ran in ${Math.max(1, Math.round(r.ms))} ms.` : 'Click the line number to jump to it.');
  setRunState('idle');
}

function finishStopped(reason, stage) {
  const timeout = reason === 'timeout';
  setStage(stage, 'err', timeout ? 'timed out' : 'stopped');
  setHeat(Math.max(0, STAGES.indexOf(stage) - 1), true);
  STAGES.slice(STAGES.indexOf(stage) + 1).forEach((s) => setStage(s, 'skip', ''));
  $('view-out').innerHTML = `<span class="stderr">${timeout
    ? `Stopped after ${RUN_LIMIT_MS / 1000} seconds. The program may be stuck in an endless loop.`
    : 'Stopped.'}</span>`;
  select('out');
  statusEl.innerHTML = `<span class="bad">${timeout ? 'Timed out.' : 'Stopped.'}</span>`;
  setRunState('idle');
}

document.querySelector('.views').addEventListener('click', (e) => {
  const a = e.target.closest('a.loc');
  if (a) goToLine(+a.dataset.line, +a.dataset.col || 1);
});

// ---------------------------------------------------------------- examples, sharing, theme

const picker = $('examples');
picker.innerHTML = '<option value="" disabled>Examples</option>' +
  EXAMPLES.map((ex, i) => `<option value="${i}">${esc(ex.title)}</option>`).join('');
picker.addEventListener('change', () => {
  const ex = EXAMPLES[+picker.value];
  setCode(ex.code);
  history.replaceState(null, '', location.pathname);
  select(ex.id.includes('optimizer') ? 'ir1' : 'out');
  run();
});

function setCode(src) {
  code.value = src;
  errorLines = new Set();
  scroller.scrollTop = 0;
  onEdit();
  $('stat-src').textContent = lineStat();
}

function encodeShare(src) {
  const bytes = new TextEncoder().encode(src);
  let bin = '';
  bytes.forEach((b) => { bin += String.fromCharCode(b); });
  return btoa(bin).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}
function decodeShare(s) {
  const bin = atob(s.replace(/-/g, '+').replace(/_/g, '/'));
  return new TextDecoder().decode(Uint8Array.from(bin, (c) => c.charCodeAt(0)));
}
$('share').addEventListener('click', async () => {
  const url = location.origin + location.pathname + '#code=' + encodeShare(code.value);
  history.replaceState(null, '', url);
  const btn = $('share');
  try { await navigator.clipboard.writeText(url); btn.textContent = 'Link copied'; }
  catch (_) { btn.textContent = 'Link in address bar'; }
  setTimeout(() => { btn.textContent = 'Copy link'; }, 2200);
});

$('theme').addEventListener('click', () => {
  const dark = getComputedStyle(document.documentElement).getPropertyValue('--page').trim() === '#0d1013';
  const next = dark ? 'light' : 'dark';
  document.documentElement.dataset.theme = next;
  try { localStorage.setItem('ember-theme', next); } catch (_) {}
});

// ---------------------------------------------------------------- start

(function init() {
  let src = null, exampleIndex = 0;
  const m = location.hash.match(/code=([\w-]+)/);
  if (m) { try { src = decodeShare(m[1]); } catch (_) {} }
  if (src === null) { try { src = localStorage.getItem(STORE_KEY); } catch (_) {} }
  if (src === null) src = EXAMPLES[0].code;
  else exampleIndex = EXAMPLES.findIndex((ex) => ex.code === src);
  picker.value = exampleIndex >= 0 ? String(exampleIndex) : '';
  setCode(src);
  ['ir0', 'ir1', 'asm'].forEach((s) => { $('view-' + s).innerHTML = quiet('Press Run to compile.'); });
  select('out');
  setStage('src', 'lit', lineStat());
  setRunState('loading');
  startWorker();
  runBtn.addEventListener('click', run);
  layoutRail();
  if (document.fonts) document.fonts.ready.then(() => { render(); layoutRail(); });
  window.addEventListener('resize', () => { render(); layoutRail(); });
})();
