// tour.js - the twelve lessons. Each lesson's program runs only when Run is pressed; edits are kept per lesson.
'use strict';
(function () {
  const runBtn = $('run'), label = runBtn.querySelector('.label'), out = $('out'), statusEl = $('status');
  const editor = new Editor($('editor'), { onRun: () => run(), onChange: (src) => save(src) });
  wireLocations(out, editor);
  let current = 0, busy = false;

  const key = (i) => 'ember-tour-' + LESSONS[i].id;
  function save(src) {
    try {
      if (src === LESSONS[current].code) localStorage.removeItem(key(current));
      else localStorage.setItem(key(current), src);
    } catch (_) {}
  }
  function saved(i) { try { return localStorage.getItem(key(i)); } catch (_) { return null; } }

  $('lesson-list').innerHTML = LESSONS.map((l, i) => `<li><a href="#${l.id}" data-i="${i}">${esc(l.title)}</a></li>`).join('');
  $('lesson-pick').innerHTML = LESSONS.map((l, i) => `<option value="${l.id}">${i + 1}. ${esc(l.title)}</option>`).join('');
  $('lesson-pick').addEventListener('change', (e) => { location.hash = e.target.value; });

  function show(i) {
    current = i;
    const l = LESSONS[i];
    document.title = `${l.title}: a tour of Ember`;
    $('step').textContent = `Lesson ${i + 1} of ${LESSONS.length}`;
    $('lesson-title').textContent = l.title;
    $('lesson-body').innerHTML = l.html;
    highlightStatic($('lesson-body'));
    $('lesson-file').textContent = `lesson-${String(i + 1).padStart(2, '0')}.em`;
    $('lesson-pick').value = l.id;
    document.querySelectorAll('#lesson-list a').forEach((a) => {
      if (+a.dataset.i === i) a.setAttribute('aria-current', 'step'); else a.removeAttribute('aria-current');
    });
    const prev = $('prev'), next = $('next');
    prev.style.visibility = i === 0 ? 'hidden' : 'visible';
    prev.href = i > 0 ? '#' + LESSONS[i - 1].id : '#';
    if (i < LESSONS.length - 1) { next.href = '#' + LESSONS[i + 1].id; next.textContent = `Next: ${LESSONS[i + 1].title}`; }
    else { next.href = 'play.html'; next.textContent = 'Open the playground'; }
    editor.value = saved(i) ?? l.code;
    $('reset').hidden = editor.value === l.code;
    out.innerHTML = quiet('Read the lesson, then press Run. Change the program as much as you like; Reset brings the original back.');
    statusEl.textContent = compiler.ready ? 'Ready. Nothing runs until you press Run.' : 'Loading the compiler.';
    window.scrollTo({ top: 0 });
  }

  function fromHash() {
    const id = location.hash.slice(1);
    const i = LESSONS.findIndex((l) => l.id === id);
    show(i >= 0 ? i : 0);
  }
  window.addEventListener('hashchange', fromHash);

  $('reset').addEventListener('click', () => {
    editor.value = LESSONS[current].code;
    save(editor.value);
    $('reset').hidden = true;
    out.innerHTML = quiet('Back to the original program.');
  });
  editor.ta.addEventListener('input', () => { $('reset').hidden = editor.value === LESSONS[current].code; });

  $('open-play').addEventListener('click', (e) => {
    e.preventDefault();
    location.href = 'play.html#code=' + encodeShare(editor.value);
  });

  const compiler = new Compiler();
  compiler.onState((s, msg) => {
    if (s === 'ready') { runBtn.disabled = false; label.textContent = 'Run'; statusEl.textContent = 'Ready. Nothing runs until you press Run.'; }
    if (s === 'fatal') statusEl.innerHTML = `<span class="bad">The compiler could not load.</span> ${esc(msg)}`;
  });

  async function run() {
    if (busy) { compiler.stop(); return; }
    if (!compiler.ready) return;
    busy = true;
    runBtn.classList.add('stop');
    label.textContent = 'Stop';
    statusEl.textContent = 'Compiling and running…';
    const r = await compiler.run(editor.value, ['--interp'], { limit: RUN_LIMIT_MS });
    editor.setErrors(showResult(out, r));
    if (r.stopped) statusEl.innerHTML = `<span class="bad">${r.stopped === 'timeout' ? 'Timed out.' : 'Stopped.'}</span>`;
    else if (r.code !== 0 && /: error: /.test(r.stderr)) statusEl.innerHTML = '<span class="bad">Compile error.</span> Click a location to jump to that line.';
    else if (r.code !== 0) statusEl.innerHTML = `<span class="bad">Exit code ${r.code}.</span> The program stopped with an error.`;
    else statusEl.innerHTML = `<span class="ok">Exit code 0.</span> Compiled and ran in ${Math.max(1, Math.round(r.ms))} ms.`;
    busy = false;
    runBtn.classList.remove('stop');
    label.textContent = 'Run';
  }
  runBtn.addEventListener('click', run);
  fromHash();
})();
