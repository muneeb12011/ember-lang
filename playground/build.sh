#!/bin/sh
# Builds the Ember website into playground/site/. Run from the repository root, after `make`:
#   sh playground/build.sh          rebuild the compiler for the browser too (needs Emscripten's em++)
#   sh playground/build.sh --pages  only rebuild the pages, examples and lessons
set -e
cd "$(dirname "$0")/.."
if [ "$1" != "--pages" ]; then
  em++ -std=c++17 -O2 -fexceptions playground/src/web_main.cpp -o playground/site/embc.js \
    -sMODULARIZE=1 -sEXPORT_NAME=createEmbc -sEXPORTED_FUNCTIONS=_embc_run -sEXPORTED_RUNTIME_METHODS=ccall,FS \
    -sALLOW_MEMORY_GROWTH=1 -sTOTAL_STACK=67108864 -sINITIAL_MEMORY=134217728 -sENVIRONMENT=web,worker,node
  cp playground/src/runner.js playground/site/runner.js
fi
python3 playground/build_site.py
