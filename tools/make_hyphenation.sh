#!/usr/bin/env bash
# Regenerates the hyphenation tables in components/vellum_core/src/hyphenation.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/assets/hyphenation"
out="$root/components/vellum_core/src/hyphenation"

python3 "$root/tools/hyphconvert.py" "$src/hyph-en-us.tex" --name en_us --left 2 --right 3 -o "$out/en_us.cpp"
python3 "$root/tools/hyphconvert.py" "$src/hyph-lt.tex" --name lt --left 2 --right 2 -o "$out/lt.cpp"
