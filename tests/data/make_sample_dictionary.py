#!/usr/bin/env python3
"""Writes the small English StarDict dictionary the host tests read:
tests/data/dictionary/en-sample.{ifo,idx,dict.dz}.

The .dict.dz is a dictzip with tiny chunks (64 bytes), so definitions
cross chunk boundaries the way they do in real dictionaries. Run it again
after changing the words below; the output is checked in.
"""
import pathlib
import struct
import zlib

# Headword, definition (HTML, as many StarDict dictionaries have it).
WORDS = [
    ("Alice", "A girl who follows a white rabbit."),
    ("bank", "<b>bank</b><br>1. The land beside a river.<br>2. A place that keeps money."),
    ("book", "A set of printed pages, bound together &amp; read."),
    ("conversation", "Talk between people."),
    ("love", "A deep fondness. <i>verb</i>: to feel it."),
    ("make", "To bring into being."),
    ("rabbit", "A small animal with long ears &lt;Leporidae&gt;."),
    ("run", "To move fast on foot."),
    ("sister", "A girl or woman with the same parents as another person."),
    ("study", "Time spent learning."),
    ("tired", "In need of rest or sleep."),
]
CHUNK = 64

out = pathlib.Path(__file__).resolve().parent / "dictionary"
out.mkdir(exist_ok=True)

# StarDict sorts headwords by ASCII case-insensitive order, then bytes.
WORDS.sort(key=lambda w: (w[0].lower().encode(), w[0].encode()))
data = b""
idx = b""
for word, text in WORDS:
    body = text.encode()
    idx += word.encode() + b"\0" + struct.pack(">II", len(data), len(body))
    data += body

# dictzip: gzip whose "RA" extra field lists each chunk's compressed size;
# every chunk is flushed to a byte boundary so it inflates on its own.
comp = zlib.compressobj(9, zlib.DEFLATED, -15)
chunks = []
for i in range(0, len(data), CHUNK):
    chunks.append(comp.compress(data[i:i + CHUNK]) + comp.flush(zlib.Z_FULL_FLUSH))
chunks[-1] += comp.flush(zlib.Z_FINISH)
ra = struct.pack("<HHH", 1, CHUNK, len(chunks)) + b"".join(struct.pack("<H", len(c)) for c in chunks)
extra = b"RA" + struct.pack("<H", len(ra)) + ra
header = b"\x1f\x8b\x08\x04" + b"\0\0\0\0" + b"\x02\x03" + struct.pack("<H", len(extra)) + extra
trailer = struct.pack("<II", zlib.crc32(data) & 0xFFFFFFFF, len(data))
(out / "en-sample.dict.dz").write_bytes(header + b"".join(chunks) + trailer)
(out / "en-sample.idx").write_bytes(idx)
(out / "en-sample.ifo").write_text(
    "StarDict's dict ifo file\n"
    "version=2.4.2\n"
    f"wordcount={len(WORDS)}\n"
    f"idxfilesize={len(idx)}\n"
    "bookname=Sample English\n"
    "sametypesequence=h\n")
print(f"wrote {out} ({len(WORDS)} words, {len(chunks)} chunks)")
