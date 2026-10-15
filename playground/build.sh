#!/bin/sh
# Builds the playground into playground/site/: the compiler as WebAssembly (needs Emscripten's em++)
# and examples.js from playground/examples/*.em. Run from the repository root: sh playground/build.sh
set -e
cd "$(dirname "$0")/.."
em++ -std=c++17 -O2 -fexceptions playground/src/web_main.cpp -o playground/site/embc.js \
  -sMODULARIZE=1 -sEXPORT_NAME=createEmbc -sEXPORTED_FUNCTIONS=_embc_run -sEXPORTED_RUNTIME_METHODS=ccall,FS \
  -sALLOW_MEMORY_GROWTH=1 -sTOTAL_STACK=67108864 -sINITIAL_MEMORY=134217728 -sENVIRONMENT=web,worker,node
cp playground/src/runner.js playground/site/runner.js
python3 - <<'PY'
import json, os, re
d = 'playground/examples'
out = []
for f in sorted(os.listdir(d)):
    if not f.endswith('.em'): continue
    title = re.sub(r'^\d+-', '', f[:-3]).replace('-', ' ').capitalize()
    out.append({'id': f[:-3], 'title': title, 'code': open(os.path.join(d, f)).read()})
open('playground/site/examples.js', 'w').write('const EXAMPLES = ' + json.dumps(out, indent=1) + ';\n')
print(len(out), 'examples')
PY
echo "built playground/site"
