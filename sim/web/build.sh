#!/usr/bin/env bash
# Builds the browser simulator into a single self-contained HTML page.
# Needs Emscripten (em++) on PATH: https://emscripten.org/docs/getting_started/
#   sim/web/build.sh            -> build/web/vellum-simulator.html
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/build/web"
mkdir -p "$out"

# miniz's inflater is C; compile it on its own, then link it in.
emcc -O2 -w -c "$root/components/vellum_core/third_party/miniz/miniz_tinfl.c" \
  -I"$root/components/vellum_core/third_party/miniz" -o "$out/miniz_tinfl.o"

em++ -O2 -std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror \
  -I"$root/components/vellum_core/include" -I"$root/components/vellum_core/third_party/miniz" \
  "$root/sim/web/web_main.cpp" "$root"/components/vellum_core/src/*.cpp "$root"/components/vellum_core/src/fonts/*.cpp "$root"/components/vellum_core/src/hyphenation/*.cpp "$root"/components/vellum_core/src/books/*.cpp "$out/miniz_tinfl.o" \
  -sMODULARIZE=1 -sEXPORT_NAME=VellumCore -sSINGLE_FILE=1 -sSINGLE_FILE_BINARY_ENCODE=0 \
  -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sEXPORTED_RUNTIME_METHODS=HEAPU8,FS -sFORCE_FILESYSTEM=1 -lidbfs.js \
  -o "$out/vellum-core.js"

python3 - "$root/sim/web/index.html" "$out/vellum-core.js" "$out/vellum-simulator.html" <<'PY'
import sys
page, js, dest = sys.argv[1:]
code = open(js).read()
if "</script" in code.lower():
    sys.exit("compiled JS contains </script; cannot inline it")
open(dest, "w").write(open(page).read().replace("/* VELLUM_CORE_JS */", code))
PY
echo "wrote $out/vellum-simulator.html"
