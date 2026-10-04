#!/usr/bin/env bash
# Builds the browser simulator into a single self-contained HTML page.
# Needs Emscripten (em++) on PATH: https://emscripten.org/docs/getting_started/
#   sim/web/build.sh            -> build/web/vellum-simulator.html
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/build/web"
mkdir -p "$out"

# Vendored C libraries (miniz's inflater, the QR code generator): compile
# them on their own, then link them in.
emcc -O2 -w -c "$root/components/vellum_core/third_party/miniz/miniz_tinfl.c" \
  -I"$root/components/vellum_core/third_party/miniz" -o "$out/miniz_tinfl.o"
emcc -O2 -w -c "$root/components/vellum_core/third_party/qrcodegen/qrcodegen.c" -o "$out/qrcodegen.o"

em++ -O2 -std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror \
  -I"$root/components/vellum_core/include" -I"$root/components/vellum_core/third_party/miniz" -I"$root/components/vellum_core/third_party/qrcodegen" \
  "$root/sim/web/web_main.cpp" "$root"/components/vellum_core/src/*.cpp "$root"/components/vellum_core/src/fonts/*.cpp "$root"/components/vellum_core/src/hyphenation/*.cpp "$root"/components/vellum_core/src/books/*.cpp "$out/miniz_tinfl.o" "$out/qrcodegen.o" \
  -sMODULARIZE=1 -sEXPORT_NAME=VellumCore -sSINGLE_FILE=1 -sSINGLE_FILE_BINARY_ENCODE=0 \
  -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sEXPORTED_RUNTIME_METHODS=HEAPU8,FS,ccall -sFORCE_FILESYSTEM=1 -lidbfs.js \
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
