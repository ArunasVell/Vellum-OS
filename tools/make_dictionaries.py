#!/usr/bin/env python3
"""Builds the ready-made StarDict dictionaries Vellum offers for download.

    python3 tools/make_dictionaries.py SOURCES OUT

SOURCES holds the free word lists (fetch them with --fetch, which needs git):
  SOURCES/wiktionary/  Vuizur/Wiktionary-Dictionaries: the English-English
                       and Lithuanian-English .tsv files (CC BY-SA 3.0, GFDL)
  SOURCES/freedict/    freedict/fd-dictionaries: eng-lit/ and lit-eng/ (GPL 2+)

OUT gets a "dictionaries" folder to copy to the memory card as it is:
  english                       English words explained in English
  english-lithuanian            Lithuanian words for English ones
  lithuanian-english            Lithuanian words explained in English
  lithuanian-english-freedict   the same, from a second, smaller list
each as .ifo, .idx and .dict.dz, plus SOURCES.txt with the licences.

What the reader needs shapes the files:
- It shows up to 4 KB of a definition as plain text, so the text is stored
  plain ("m"), each sense numbered, cut at a sense once it would not fit.
- It ignores .syn files, so other forms of a word (Lithuanian "Afrikoje",
  English "went") are entries of their own in the word list, sharing the
  text of the word they come from; the text names that word first. Stress
  marks are taken off Lithuanian forms, as books print them without.
- English forms its own endings rule already finds ("books" -> "book") are
  left out to keep the word list small; it is read whole once per card.
"""
import argparse
import collections
import pathlib
import re
import struct
import subprocess
import sys
import unicodedata
import xml.etree.ElementTree as ET
import zlib

TEXT_BYTES = 4000  # the reader shows 4096
CHUNK = 32768  # dictzip chunk; the reader takes up to 65536
MAX_WORD = 63  # the reader looks words up in 64 bytes

WIKTIONARY_GIT = "https://github.com/Vuizur/Wiktionary-Dictionaries"
FREEDICT_GIT = "https://github.com/freedict/fd-dictionaries"
WIKTIONARY_FILES = ["English-English Wiktionary dictionary.tsv", "Lithuanian-English Wiktionary dictionary.tsv"]


def fetch(sources):
    def sparse(url, dest, paths):
        if dest.exists():
            return
        subprocess.run(["git", "clone", "-q", "--depth", "1", "--filter=blob:none", "--sparse", url, str(dest)],
                       check=True)
        subprocess.run(["git", "-C", str(dest), "sparse-checkout", "set", "--no-cone", *paths], check=True)

    sparse(WIKTIONARY_GIT, sources / "wiktionary", ["/" + f for f in WIKTIONARY_FILES])
    sparse(FREEDICT_GIT, sources / "freedict", ["/eng-lit/", "/lit-eng/"])


def commit_of(folder):
    try:
        return subprocess.run(["git", "-C", str(folder), "log", "-1", "--format=%h, %cs"], capture_output=True,
                              text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown version"


# Lithuanian stress marks: grave, acute and tilde. Ogonek, caron, dot and
# macron are letters of their own (ą, š, ė, ū) and stay.
STRESS = {"̀", "́", "̃"}


def unstressed(word):
    return unicodedata.normalize("NFC", "".join(c for c in unicodedata.normalize("NFD", word) if c not in STRESS))


def english_stems(w):
    """The reader's English ending rule (dictionary.cpp english_stems), in
    the order it tries them."""
    out = []

    def push(s):
        if s not in out:
            out.append(s)

    def stem(cut, add=""):
        if len(w) > cut + 1:
            push(w[:len(w) - cut] + add)

    def undouble(cut):
        if len(w) >= cut + 3:
            keep = len(w) - cut
            a = w[keep - 1]
            if a == w[keep - 2] and a not in "aeiouy" and a not in "ls":
                push(w[:keep - 1])

    if w.endswith("'s") or w.endswith("’s"):
        stem(2)
    if w.endswith("ies"):
        stem(3, "y")
    if w.endswith("es"):
        stem(2)
    if w.endswith("s") and not w.endswith("ss"):
        stem(1)
    if w.endswith("ied"):
        stem(3, "y")
    if w.endswith("ed"):
        stem(2)
        stem(1)
        undouble(2)
    if w.endswith("ing"):
        stem(3)
        stem(3, "e")
        undouble(3)
    return out


# --- Wiktionary (Vuizur's .tsv: "word|form|form<TAB>html") ------------------

POS_RE = re.compile(r"<i>(.*?)</i><br><ol>(.*?)</ol>", re.S)
LI_RE = re.compile(r"<li>(.*?)</li>", re.S)
TAG_RE = re.compile(r"<[^>]*>")


def plain(html):
    text = TAG_RE.sub("", html)
    text = text.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", '"').replace("&amp;", "&")
    return " ".join(text.split())


def wiktionary_senses(html):
    """[(part of speech, [sense, ...]), ...] from one definition."""
    return [(plain(pos), [plain(s) for s in LI_RE.findall(body)]) for pos, body in POS_RE.findall(html)]


def read_wiktionary(path):
    """Words with their senses, and every form with the words it is a form of."""
    words = {}
    forms = collections.defaultdict(list)
    with open(path, encoding="utf-8") as f:
        for line in f:
            head, _, html = line.rstrip("\n").partition("\t")
            names = head.split("|")
            word = names[0]
            words.setdefault(word, []).extend(wiktionary_senses(html))
            for form in names[1:]:
                if form != word and word not in forms[form]:
                    forms[form].append(word)
    return words, forms


# --- FreeDict (TEI) ---------------------------------------------------------

TEI = "{http://www.tei-c.org/ns/1.0}"
POS_NAMES = {"n": "noun", "v": "verb", "adj": "adjective", "adv": "adverb", "prep": "preposition",
             "pron": "pronoun", "conj": "conjunction", "int": "interjection", "num": "numeral",
             "art": "article", "pn": "name", "abbr": "abbreviation", "ptcl": "particle"}


def read_freedict(path):
    words = {}
    root = ET.parse(path).getroot()
    for entry in root.iter(TEI + "entry"):
        orth = entry.findtext(f"{TEI}form/{TEI}orth")
        if not orth:
            continue
        pos = entry.findtext(f"{TEI}gramGrp/{TEI}pos") or ""
        pron = entry.findtext(f"{TEI}form/{TEI}pron")
        label = POS_NAMES.get(pos, pos)
        if pron:
            label = f"[{pron}]" + (f" {label}" if label else "")
        senses = []
        for sense in entry.iter(TEI + "sense"):
            quotes = [" ".join(q.text.split()) for q in sense.iter(TEI + "quote") if q.text and q.text.strip()]
            usage = [u.text.strip() for u in sense.iter(TEI + "usg") if u.text and u.text.strip()]
            refs = [r.text.strip() for r in sense.iter(TEI + "ref") if r.text and r.text.strip()]
            text = ", ".join(dict.fromkeys(quotes))
            if usage:
                text = f"({', '.join(usage)}) {text}".strip()
            if refs:
                text = (text + "; " if text else "") + "see " + ", ".join(refs)
            if text:
                senses.append(text)
        if senses:
            words.setdefault(orth.strip(), []).append((label, senses))
    return words


# --- Text and StarDict files ------------------------------------------------


def section(word, groups, room):
    """One word's text, at most `room` bytes, cut after a whole sense."""
    lines = []
    used = 0
    cut = False
    for pos, senses in groups:
        head = f"{word} · {pos}" if pos else word
        if used + len(head.encode()) + 1 > room:
            cut = True
            break
        lines.append(head)
        used += len(head.encode()) + 1
        for n, sense in enumerate(senses, 1):
            line = f"{n}. {sense}" if len(senses) > 1 else sense
            size = len(line.encode()) + 1
            if used + size > room:
                if not lines[-1:] or lines[-1] == head:
                    # Not one sense fits: as much of the first as does.
                    keep = line.encode()[:max(0, room - used - 4)].decode("utf-8", "ignore")
                    if keep:
                        lines.append(keep + "…")
                cut = True
                break
            lines.append(line)
            used += size
        if cut:
            break
    if cut and lines and not lines[-1].endswith("…") and used + 4 <= room:
        lines.append("…")
    return "\n".join(lines)


def text_for(names, words):
    """The text for one or more words, the first given most room."""
    parts = []
    room = TEXT_BYTES
    for i, name in enumerate(names):
        share = room if i == len(names) - 1 else max(room // 2, room - 400 * (len(names) - 1 - i))
        part = section(name, words[name], share)
        if not part:
            break
        parts.append(part)
        room -= len(part.encode()) + 1
        if room < 80:
            break
    return "\n".join(parts)


def sort_key(word):
    # StarDict's order, as the reader searches it: ASCII letters without
    # case, then byte by byte.
    raw = word.encode()
    return (bytes(c + 32 if 65 <= c <= 90 else c for c in raw), raw)


def write_stardict(folder, name, bookname, description, entries):
    """`entries` maps each headword to its text; equal texts are stored once."""
    data = bytearray()
    placed = {}
    idx = bytearray()
    count = 0
    for word in sorted(entries, key=sort_key):
        body = entries[word].encode()
        if body not in placed:
            placed[body] = (len(data), len(body))
            data += body
        offset, size = placed[body]
        idx += word.encode() + b"\0" + struct.pack(">II", offset, size)
        count += 1
    # dictzip: gzip whose "RA" field lists each chunk's compressed size;
    # each chunk is flushed to a byte boundary so it inflates on its own.
    comp = zlib.compressobj(9, zlib.DEFLATED, -15)
    chunks = [comp.compress(bytes(data[i:i + CHUNK])) + comp.flush(zlib.Z_FULL_FLUSH)
              for i in range(0, len(data), CHUNK)]
    chunks[-1] += comp.flush(zlib.Z_FINISH)
    if len(chunks) > 65535 or max(len(c) for c in chunks) > 65535:
        sys.exit(f"{name}: too big for one dictzip")
    ra = struct.pack("<HHH", 1, CHUNK, len(chunks)) + b"".join(struct.pack("<H", len(c)) for c in chunks)
    extra = b"RA" + struct.pack("<H", len(ra)) + ra
    header = b"\x1f\x8b\x08\x04\0\0\0\0\x02\x03" + struct.pack("<H", len(extra)) + extra
    trailer = struct.pack("<II", zlib.crc32(data) & 0xFFFFFFFF, len(data) & 0xFFFFFFFF)
    (folder / f"{name}.dict.dz").write_bytes(header + b"".join(chunks) + trailer)
    (folder / f"{name}.idx").write_bytes(idx)
    (folder / f"{name}.ifo").write_text(
        "StarDict's dict ifo file\n"
        "version=2.4.2\n"
        f"bookname={bookname}\n"
        f"wordcount={count}\n"
        f"idxfilesize={len(idx)}\n"
        "sametypesequence=m\n"
        f"description={description}\n", encoding="utf-8")
    print(f"{name}: {count} entries, idx {len(idx) // 1024} KB, text {len(data) // 1024} KB, "
          f"dict.dz {(folder / f'{name}.dict.dz').stat().st_size // 1024} KB")


def fits(word):
    return 0 < len(word.encode()) <= MAX_WORD and "\n" not in word


FORM_OF = re.compile(r"\b(participle|form|inflection|tense|plural|singular|case) of\b")


def only_a_form(groups):
    senses = [s for _, ss in groups for s in ss]
    return bool(senses) and all(FORM_OF.search(s) for s in senses)


def wiktionary_entries(words, forms, lithuanian):
    entries = {}
    for word in words:
        if fits(word) and words[word]:
            entries[word] = text_for([word], words)
    if lithuanian:
        # Unstressed spellings of the words themselves.
        for word in list(words):
            plainword = unstressed(word)
            if plainword != word and plainword not in words and fits(plainword) and words[word]:
                forms[plainword].append(word)
    by_form = collections.defaultdict(list)
    for form, of in forms.items():
        key = unstressed(form) if lithuanian else form
        for word in of:
            if words.get(word) and word not in by_form[key]:
                by_form[key].append(word)
    lower = {w.lower() for w in words}
    for form, of in by_form.items():
        if form in entries or not fits(form):
            continue
        if not lithuanian:
            # Left out when the reader's ending rule reaches the same word.
            stems = english_stems(form.lower())
            first = next((s for s in stems if s in lower), None)
            if first is not None and first in {w.lower() for w in of}:
                continue
        # Words of their own before those only another one's form, such as
        # a participle: "einu" is "eiti" before "eitas".
        of = sorted(of, key=lambda w: only_a_form(words[w]))
        entries[form] = text_for(of[:4], words)
    return entries


def freedict_entries(words):
    return {w: text_for([w], words) for w in words if fits(w)}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sources", type=pathlib.Path)
    ap.add_argument("out", type=pathlib.Path)
    ap.add_argument("--fetch", action="store_true", help="download the sources first (git)")
    args = ap.parse_args()
    if args.fetch:
        fetch(args.sources)
    wikt = args.sources / "wiktionary"
    free = args.sources / "freedict"
    out = args.out / "dictionaries"
    out.mkdir(parents=True, exist_ok=True)
    wikt_ver = commit_of(wikt)
    free_ver = commit_of(free)

    words, forms = read_wiktionary(wikt / WIKTIONARY_FILES[0])
    write_stardict(out, "english", "Wiktionary EN",
                   f"English words explained in English, from Wiktionary (via Vuizur/Wiktionary-Dictionaries, "
                   f"{wikt_ver}). CC BY-SA 3.0 and GFDL.", wiktionary_entries(words, forms, False))
    words, forms = read_wiktionary(wikt / WIKTIONARY_FILES[1])
    write_stardict(out, "lithuanian-english", "Wiktionary LT-EN",
                   f"Lithuanian words explained in English, with their forms, from Wiktionary (via "
                   f"Vuizur/Wiktionary-Dictionaries, {wikt_ver}). CC BY-SA 3.0 and GFDL.",
                   wiktionary_entries(words, forms, True))
    write_stardict(out, "english-lithuanian", "FreeDict EN-LT",
                   f"English-Lithuanian FreeDict dictionary (freedict/fd-dictionaries eng-lit, {free_ver}). "
                   "GPL 2 or later.", freedict_entries(read_freedict(free / "eng-lit" / "eng-lit.tei")))
    write_stardict(out, "lithuanian-english-freedict", "FreeDict LT-EN",
                   f"Lithuanian-English FreeDict dictionary (freedict/fd-dictionaries lit-eng, {free_ver}). "
                   "GPL 2 or later.", freedict_entries(read_freedict(free / "lit-eng" / "lit-eng.tei")))
    (out / "SOURCES.txt").write_text(f"""Dictionaries for Vellum, made with tools/make_dictionaries.py
from the Vellum OS source (https://github.com/ArunasVell/Vellum-OS).

english.*, lithuanian-english.*
  From Wiktionary (https://en.wiktionary.org), by its contributors, as
  extracted by the Wiktionary-Dictionaries project
  ({WIKTIONARY_GIT}, {wikt_ver}).
  Changed: shortened to fit the reader's screen, set as plain text, word
  forms listed as words of their own.
  Licence: Creative Commons Attribution-ShareAlike 3.0
  (https://creativecommons.org/licenses/by-sa/3.0/) and GNU FDL.
  You may share and change them under the same licence.

english-lithuanian.*, lithuanian-english-freedict.*
  FreeDict English-Lithuanian and Lithuanian-English dictionaries,
  Copyright (C) 2007 Atviras Kodas Lietuvai (AKL), from
  {FREEDICT_GIT} ({free_ver}), folders eng-lit and lit-eng
  (the source these files are made from).
  Licence: GNU General Public License version 2 or any later version
  (https://www.gnu.org/licenses/gpl-2.0.html).
""", encoding="utf-8")


if __name__ == "__main__":
    main()
