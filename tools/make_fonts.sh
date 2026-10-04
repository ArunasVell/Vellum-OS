#!/usr/bin/env bash
# Regenerates the built-in fonts in components/vellum_core/src/fonts.
# Needs: pip install freetype-py uharfbuzz
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/assets/fonts/literata"
out="$root/components/vellum_core/src/fonts"

make_font() {  # name file size
  python3 "$root/tools/fontconvert.py" "$src/$2" --size "$3" --name "$1" --axis wght=400 -o "$out/$1.cpp"
}

make_font literata_26 'Literata[opsz,wght].ttf' 26
make_font literata_italic_26 'Literata-Italic[opsz,wght].ttf' 26
make_font literata_20 'Literata[opsz,wght].ttf' 20
make_font literata_italic_20 'Literata-Italic[opsz,wght].ttf' 20
make_font literata_16 'Literata[opsz,wght].ttf' 16
make_font literata_italic_40 'Literata-Italic[opsz,wght].ttf' 40
