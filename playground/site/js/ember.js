// ember.js - shared by every page of the Ember site: syntax highlighting, the code editor, the client for the
// compiler (which runs in a web worker), and the pipeline rail.
'use strict';

const $ = (id) => document.getElementById(id);
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

// ---------------------------------------------------------------- compile and run, in a worker

// One worker per page. A run that takes longer than its limit is stopped by terminating the worker; a new one
// starts straight away, so the page never freezes on an endless loop.
class Compiler {
  constructor() {
    this.listeners = [];
    this.pending = new Map();
    this.nextId = 0;
    this.start();
  }
  start() {
    this.ready = false;
    this.worker = new Worker('worker.js');
    this.worker.onmessage = (ev) => {
      const m = ev.data;
      if (m.type === 'ready') { this.ready = true; this.listeners.forEach((f) => f('ready')); }
      else if (m.type === 'fatal') this.listeners.forEach((f) => f('fatal', m.message));
      else if (m.type === 'result' && this.pending.has(m.id)) {
        const p = this.pending.get(m.id);
        clearTimeout(p.timer);
        this.pending.delete(m.id);
        p.resolve(m);
      }
    };
  }
  onState(f) { this.listeners.push(f); if (this.ready) f('ready'); }
  // args as on the command line, e.g. ['--interp'] or ['--emit-ir', '-O0']
  run(src, args, { stdin = '', limit = 0 } = {}) {
    const id = ++this.nextId;
    return new Promise((resolve) => {
      const timer = limit ? setTimeout(() => this.stop('timeout'), limit) : 0;
      this.pending.set(id, { resolve, timer });
      this.worker.postMessage({ id, src, args, stdin });
    });
  }
  stop(reason = 'stopped') {
    this.worker.terminate();
    for (const p of this.pending.values()) { clearTimeout(p.timer); p.resolve({ stopped: reason }); }
    this.pending.clear();
    this.start();
  }
}
const RUN_LIMIT_MS = 5000;

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
const quiet = (text) => `<span class="quiet">${text}</span>`;

// Render a program's result into an output element. Returns the set of lines with errors.
function showResult(el, r) {
  if (r.stopped) {
    el.innerHTML = `<span class="stderr">${r.stopped === 'timeout'
      ? `Stopped after ${RUN_LIMIT_MS / 1000} seconds. The program may be stuck in an endless loop.`
      : 'Stopped before it finished.'}</span>`;
    return new Set();
  }
  let html = esc(r.stdout);
  if (r.stderr) html += `<span class="stderr">${linkify(r.stderr)}</span>`;
  if (!r.stdout && !r.stderr) html = quiet('The program finished without printing anything.');
  el.innerHTML = html;
  return errorLinesOf(r.stderr);
}

// ---------------------------------------------------------------- the editor

// A transparent textarea over a highlighted <pre>, with line numbers. new Editor(element, { value, onRun })
class Editor {
  constructor(root, { value = '', onRun = null, onChange = null } = {}) {
    this.onRun = onRun;
    this.onChange = onChange;
    this.errors = new Set();
    root.classList.add('editor');
    root.innerHTML = '<div class="gutter" aria-hidden="true"></div><div class="stack">' +
      '<pre class="hl" aria-hidden="true"></pre><textarea spellcheck="false" autocapitalize="off" ' +
      'autocomplete="off" autocorrect="off" aria-label="Ember source code"></textarea></div>';
    this.root = root;
    this.gutter = root.querySelector('.gutter');
    this.hl = root.querySelector('.hl');
    this.ta = root.querySelector('textarea');
    this.ta.value = value;
    this.ta.addEventListener('input', () => this.edited());
    this.ta.addEventListener('keydown', (e) => this.key(e));
    ['keyup', 'click', 'select'].forEach((ev) => this.ta.addEventListener(ev, () => this.markCaret()));
    this.render();
    if (document.fonts) document.fonts.ready.then(() => this.render());
  }
  get value() { return this.ta.value; }
  set value(v) { this.ta.value = v; this.errors = new Set(); this.root.scrollTop = 0; this.render(); }
  setErrors(lines) { this.errors = lines; this.render(); }
  edited() {
    if (this.errors.size) this.errors = new Set();
    this.render();
    if (this.onChange) this.onChange(this.ta.value);
  }
  render() {
    const lines = highlightEmber(this.ta.value).split('\n');
    this.hl.innerHTML = lines.map((l, i) => `<span class="row${this.errors.has(i + 1) ? ' err' : ''}">${l || ' '}</span>`).join('');
    let g = '';
    for (let i = 1; i <= lines.length; i++) g += `<div${this.errors.has(i) ? ' class="err"' : ''}>${i}</div>`;
    this.gutter.innerHTML = g;
    this.ta.style.height = this.hl.offsetHeight + 'px';
    this.markCaret();
  }
  caret() {
    const before = this.ta.value.slice(0, this.ta.selectionStart).split('\n');
    return { line: before.length, col: before[before.length - 1].length + 1 };
  }
  markCaret() {
    const { line } = this.caret();
    [...this.gutter.children].forEach((d, i) => d.classList.toggle('cur', i + 1 === line && document.activeElement === this.ta));
    if (this.onCaret) this.onCaret(this.caret());
  }
  insert(text) {
    if (!document.execCommand('insertText', false, text)) {
      this.ta.setRangeText(text, this.ta.selectionStart, this.ta.selectionEnd, 'end');
      this.edited();
    }
  }
  lineStart(pos) { return this.ta.value.lastIndexOf('\n', pos - 1) + 1; }
  key(e) {
    const ta = this.ta;
    if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); if (this.onRun) this.onRun(); return; }
    if (e.key === 'Escape') { ta.blur(); return; }
    const v = ta.value, s = ta.selectionStart, end = ta.selectionEnd;
    if (e.key === 'Tab') {
      e.preventDefault();
      if (e.shiftKey) {
        const ls = this.lineStart(s);
        const n = (v.slice(ls).match(/^ {1,4}/) || [''])[0].length;
        if (n) { ta.setSelectionRange(ls, ls + n); this.insert(''); ta.setSelectionRange(Math.max(ls, s - n), Math.max(ls, end - n)); }
      } else this.insert('    ');
    } else if (e.key === 'Enter' && !e.shiftKey && s === end) {
      e.preventDefault();
      const line = v.slice(this.lineStart(s), s);
      let indent = line.match(/^\s*/)[0];
      const opens = /[{(\[]\s*$/.test(line);
      if (opens) indent += '    ';
      if (opens && /^\s*[}\])]/.test(v.slice(s))) {
        this.insert('\n' + indent + '\n' + indent.slice(4));
        ta.setSelectionRange(s + 1 + indent.length, s + 1 + indent.length);
      } else this.insert('\n' + indent);
    } else if (e.key === '}' && s === end && /^ {4,}$/.test(v.slice(this.lineStart(s), s))) {
      e.preventDefault();
      ta.setSelectionRange(s - 4, s);
      this.insert('}');
    }
  }
  goTo(line, col = 1) {
    const lines = this.ta.value.split('\n');
    let pos = 0;
    for (let i = 0; i < line - 1 && i < lines.length; i++) pos += lines[i].length + 1;
    pos += Math.max(0, col - 1);
    this.ta.focus();
    this.ta.setSelectionRange(pos, pos);
    const top = (line - 1) * parseFloat(getComputedStyle(this.hl).lineHeight);
    if (top < this.root.scrollTop || top > this.root.scrollTop + this.root.clientHeight - 60) this.root.scrollTop = Math.max(0, top - 80);
    this.markCaret();
  }
}

// clicking main.em:5:11 or (line 5) in an output jumps to that line in the editor
function wireLocations(outputEl, editor) {
  outputEl.addEventListener('click', (e) => {
    const a = e.target.closest('a.loc');
    if (a) editor.goTo(+a.dataset.line, +a.dataset.col || 1);
  });
}

// ---------------------------------------------------------------- the pipeline rail

// The rail is the site's signature: Source -> IR -> Optimized -> x86-64 -> Output. Heat flows along it as the
// compiler finishes each stage, and cools from white-hot at the source to deep red at the machine code.
const STAGES = ['src', 'ir0', 'ir1', 'asm', 'out'];
class Rail {
  constructor(el) {
    this.el = el;
    this.track = el.querySelector('.rail-track');
    this.heat = el.querySelector('.rail-heat');
    this.at = 0;
    this.err = false;
    this.layout();
    window.addEventListener('resize', () => this.layout());
    if (document.fonts) document.fonts.ready.then(() => this.layout());
  }
  stage(s) { return this.el.querySelector(`.stage[data-stage="${s}"]`); }
  x(s) {
    const n = this.stage(s).querySelector('.node').getBoundingClientRect();
    return n.left - this.el.getBoundingClientRect().left + this.el.scrollLeft + n.width / 2;
  }
  layout() {
    const n = this.stage('src').querySelector('.node').getBoundingClientRect();
    const a = this.x('src'), b = this.x('out');
    this.track.style.left = a + 'px';
    this.track.style.top = (n.top - this.el.getBoundingClientRect().top + n.height / 2 - 1.5) + 'px';
    this.track.style.width = (b - a) + 'px';
    this.heat.style.setProperty('--heat-span', (b - a) + 'px');
    this.setHeat(this.at, this.err);
  }
  setHeat(i, err = false) {
    this.at = i;
    this.err = err;
    this.heat.classList.toggle('err', err);
    this.heat.style.width = i <= 0 ? '0px' : (this.x(STAGES[i]) - this.x('src')) + 'px';
  }
  set(s, state, stat) {
    const el = this.stage(s);
    el.classList.remove('lit', 'err', 'skip');
    if (state) el.classList.add(state);
    if (stat !== undefined) el.querySelector('.stat').innerHTML = stat;
  }
  reset(srcStat) {
    this.el.classList.remove('stale');
    this.setHeat(0);
    this.set('src', 'lit', srcStat);
    ['ir0', 'ir1', 'asm', 'out'].forEach((s) => this.set(s, '', ''));
  }
  stale(on) { this.el.classList.toggle('stale', on); }
}

const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;
const pace = () => new Promise((r) => setTimeout(r, reducedMotion ? 0 : 170));
function lineStat(src) {
  const n = src.replace(/\n+$/, '').split('\n').length;
  return `${n} line${n === 1 ? '' : 's'}`;
}

// Compile one program through every stage, lighting the rail as each finishes.
// views: { ir0, ir1, asm, out } elements (any may be missing). Returns a summary for the caller's status line.
async function runPipeline(compiler, src, rail, views, stdin = '') {
  rail.reset(lineStat(src));
  const put = (k, html) => { if (views[k]) views[k].innerHTML = html; };
  const notReached = quiet('The compiler stopped before this stage.');

  const ir0 = await compiler.run(src, ['--emit-ir', '-O0']);
  if (ir0.stopped) return stoppedAt('ir0', ir0.stopped);
  if (ir0.code !== 0) {
    rail.set('ir0', 'err', 'error');
    rail.setHeat(1, true);
    ['ir1', 'asm', 'out'].forEach((s) => rail.set(s, 'skip', ''));
    put('ir0', quiet('The program has compile errors, so no IR was produced.'));
    put('ir1', notReached);
    put('asm', notReached);
    put('out', `<span class="stderr">${linkify(ir0.stderr)}</span>`);
    return { kind: 'compile-error', errors: errorLinesOf(ir0.stderr) };
  }
  await pace();
  const n0 = countIR(ir0.stdout);
  put('ir0', highlightLow(ir0.stdout, 'ir'));
  rail.set('ir0', 'lit', `${n0} instr`);
  rail.setHeat(1);

  const ir1 = await compiler.run(src, ['--emit-ir']);
  if (ir1.stopped) return stoppedAt('ir1', ir1.stopped);
  await pace();
  const n1 = countIR(ir1.stdout);
  const pct = n0 ? Math.round((100 * (n0 - n1)) / n0) : 0;
  put('ir1', highlightLow(ir1.stdout, 'ir'));
  rail.set('ir1', 'lit', `${n1} instr${pct > 0 ? ` <span class="gain">−${pct}%</span>` : ''}`);
  rail.setHeat(2);

  const asm = await compiler.run(src, ['--emit-asm']);
  if (asm.stopped) return stoppedAt('asm', asm.stopped);
  await pace();
  put('asm', highlightLow(asm.stdout, 'asm'));
  rail.set('asm', 'lit', `${countAsm(asm.stdout)} instr`);
  rail.setHeat(3);

  const r = await compiler.run(src, ['--interp'], { stdin, limit: RUN_LIMIT_MS });
  if (r.stopped) return stoppedAt('out', r.stopped);
  await pace();
  const errors = views.out ? showResult(views.out, r) : errorLinesOf(r.stderr);
  rail.set('out', r.code === 0 ? 'lit' : 'err', `exit ${r.code}`);
  rail.setHeat(4);
  return { kind: r.code === 0 ? 'ok' : 'runtime-error', code: r.code, ms: r.ms, errors, stdout: r.stdout };

  function stoppedAt(stage, reason) {
    rail.set(stage, 'err', reason === 'timeout' ? 'timed out' : 'stopped');
    rail.setHeat(Math.max(0, STAGES.indexOf(stage) - 1), true);
    STAGES.slice(STAGES.indexOf(stage) + 1).forEach((s) => rail.set(s, 'skip', ''));
    if (views.out) showResult(views.out, { stopped: reason });
    return { kind: 'stopped', reason, errors: new Set() };
  }
}

// ---------------------------------------------------------------- page chrome

function setupTheme() {
  const btn = $('theme');
  if (!btn) return;
  btn.addEventListener('click', () => {
    const dark = getComputedStyle(document.documentElement).getPropertyValue('--page').trim() === '#0d1013';
    const next = dark ? 'light' : 'dark';
    document.documentElement.dataset.theme = next;
    try { localStorage.setItem('ember-theme', next); } catch (_) {}
  });
}

// highlight static code blocks: <pre data-lang="ember|ir|asm">
function highlightStatic(root = document) {
  root.querySelectorAll('pre[data-lang]').forEach((pre) => {
    const lang = pre.dataset.lang, text = pre.textContent;
    pre.innerHTML = lang === 'ember' ? highlightEmber(text) : highlightLow(text, lang);
  });
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

setupTheme();
