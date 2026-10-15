#!/usr/bin/env python3
"""Assembles the Ember website in playground/site/ from playground/pages/, tour/, examples/ and content/.

Fills the shared head, header and footer into every page, generates the IR and assembly on the Inside page with
the native compiler (./embc), and writes data/examples.js and data/lessons.js. Run from the repository root,
after `make`:  python3 playground/build_site.py
"""
import html, json, os, re, shutil, subprocess, tempfile
import markdown

ROOT = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(ROOT)
SITE = os.path.join(ROOT, 'site')
GITHUB = 'https://github.com/muneeb12011/ember-lang'

NAV = [('tour', 'tour.html', 'Tour'), ('play', 'play.html', 'Playground'),
       ('inside', 'inside.html', 'Inside the compiler'), ('reference', 'reference.html', 'Reference')]

def head(title, desc):
    return f'''<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{html.escape(title)}</title>
  <meta name="description" content="{html.escape(desc)}">
  <meta property="og:title" content="{html.escape(title)}">
  <meta property="og:description" content="{html.escape(desc)}">
  <meta name="color-scheme" content="light dark">
  <link rel="icon" href="favicon.svg" type="image/svg+xml">
  <link rel="preload" href="fonts/archivo.woff2" as="font" type="font/woff2" crossorigin>
  <link rel="stylesheet" href="css/site.css">
  <script>try {{ const t = localStorage.getItem('ember-theme'); if (t) document.documentElement.dataset.theme = t; }} catch (_) {{}}</script>
</head>'''

def header(current):
    links = ''.join(f'<a href="{href}"{" aria-current=\"page\"" if key == current else ""}>{label}</a>'
                    for key, href, label in NAV)
    return f'''<header class="site">
  <a class="brand" href="index.html" aria-label="Ember home"><img src="logo.svg" alt="" width="34" height="34"><span class="word">Ember</span></a>
  <nav class="nav" aria-label="Site">{links}<a href="{GITHUB}">GitHub</a></nav>
  <button id="theme" class="icon-btn" type="button" aria-label="Switch between light and dark">
    <svg viewBox="0 0 24 24" width="17" height="17" aria-hidden="true"><path d="M12 3a9 9 0 1 0 9 9 7 7 0 0 1-9-9z" fill="currentColor"/></svg>
  </button>
</header>'''

FOOTER = f'''<footer class="foot">
  <p>Ember is a personal project by Muneeb: the language, its compiler, runtime, garbage collector and standard
    library, written from scratch to understand what happens underneath the tools I use every day.</p>
  <nav aria-label="Footer"><a href="tour.html">Tour</a><a href="play.html">Playground</a><a href="inside.html">Inside</a><a href="reference.html">Reference</a><a href="{GITHUB}">Source on GitHub</a></nav>
</footer>'''

def embc(src, *args):
    """Run the native compiler on src as main.em; returns stdout + stderr."""
    with tempfile.TemporaryDirectory() as d:
        open(os.path.join(d, 'main.em'), 'w').write(src)
        r = subprocess.run([os.path.join(REPO, 'embc'), 'main.em', *args], cwd=d, capture_output=True, text=True)
        return r.stdout + r.stderr

def count(text, asm=False):
    return sum(1 for l in text.split('\n') if re.match(r'^  [a-z]' if asm else r'^  \S', l))

def md(text):
    out = markdown.markdown(text, extensions=['tables', 'fenced_code'])
    # ember code blocks get highlighted in the browser
    out = re.sub(r'<pre><code class="language-ember">(.*?)</code></pre>', r'<pre data-lang="ember">\1</pre>', out, flags=re.S)
    out = re.sub(r'<pre><code>(.*?)</code></pre>', r'<pre>\1</pre>', out, flags=re.S)
    out = out.replace('<p><strong>Try it:</strong>', '<p class="try"><strong>Try it:</strong>')
    return out

def slug(s):
    return re.sub(r'[^a-z0-9]+', '-', s.lower()).strip('-')

INSIDE_SRC = '''fn main() -> int {
    let mut total = 0;
    for i in 0..10 {
        total += i * i;
    }
    print(total);
    return 0;
}
'''

def build_pages():
    extra = {}
    ir0 = embc(INSIDE_SRC, '--emit-ir', '-O0').rstrip()
    ir1 = embc(INSIDE_SRC, '--emit-ir').rstrip()
    asm = embc(INSIDE_SRC, '--emit-asm').rstrip()
    err = embc(INSIDE_SRC.replace('total += i * i;', 'total += 1.5;'), '--interp').rstrip()
    extra['inside'] = {'SRC': html.escape(INSIDE_SRC.rstrip()), 'IR0': html.escape(ir0), 'IR1': html.escape(ir1),
                       'ASM': html.escape(asm), 'ERR': html.escape(err),
                       'N0': str(count(ir0)), 'N1': str(count(ir1))}
    ref = md(open(os.path.join(ROOT, 'content', 'reference.md')).read())
    heads = re.findall(r'<h2>(.*?)</h2>', ref)
    ref = re.sub(r'<h2>(.*?)</h2>', lambda m: f'<h2 id="{slug(m.group(1))}">{m.group(1)}</h2>', ref)
    toc = ''.join(f'<a href="#{slug(h)}">{h}</a>' for h in heads)
    extra['reference'] = {'REFERENCE': ref, 'REFTOC': toc}

    for name in os.listdir(os.path.join(ROOT, 'pages')):
        if not name.endswith('.html'):
            continue
        page = name[:-5]
        s = open(os.path.join(ROOT, 'pages', name)).read()
        s = re.sub(r'\{\{HEAD (.*?) \| (.*?)\}\}', lambda m: head(m.group(1), m.group(2)), s)
        s = re.sub(r'\{\{HEADER (\w+)\}\}', lambda m: header(m.group(1)), s)
        s = s.replace('{{FOOTER}}', FOOTER)
        for k, v in extra.get(page, {}).items():
            s = s.replace('{{' + k + '}}', v)
        left = re.findall(r'\{\{[A-Z0-9]+[^}]*\}\}', s)
        assert not left, f'{name}: unfilled {left}'
        open(os.path.join(SITE, name), 'w').write(s)
    return extra['inside']

def build_data():
    os.makedirs(os.path.join(SITE, 'data'), exist_ok=True)
    ex = []
    d = os.path.join(ROOT, 'examples')
    for f in sorted(os.listdir(d)):
        if f.endswith('.em'):
            title = re.sub(r'^\d+-', '', f[:-3]).replace('-', ' ').capitalize()
            ex.append({'id': f[:-3], 'title': title, 'code': open(os.path.join(d, f)).read()})
    open(os.path.join(SITE, 'data', 'examples.js'), 'w').write('const EXAMPLES = ' + json.dumps(ex, indent=1) + ';\n')

    lessons = []
    d = os.path.join(ROOT, 'tour')
    for f in sorted(os.listdir(d)):
        if not f.endswith('.md'):
            continue
        text = open(os.path.join(d, f)).read()
        title = re.match(r'# (.*)\n', text).group(1)
        code = re.findall(r'```ember\n(.*?)```', text, re.S)[-1]
        body = text.split('\n', 1)[1]
        body = body[:body.rfind('```ember')]  # the program goes in the editor, not the text
        lessons.append({'id': re.sub(r'^\d+-', '', f[:-3]), 'title': title, 'html': md(body).strip(), 'code': code})
    open(os.path.join(SITE, 'data', 'lessons.js'), 'w').write('const LESSONS = ' + json.dumps(lessons, indent=1) + ';\n')
    return ex, lessons

if __name__ == '__main__':
    inside = build_pages()
    ex, lessons = build_data()
    print(f'pages built; {len(ex)} examples, {len(lessons)} lessons; inside: IR {inside["N0"]} -> {inside["N1"]} instructions')
