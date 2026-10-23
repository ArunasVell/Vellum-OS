# Dictionaries

Hold a word while reading and Vellum shows what it means, from dictionaries
on the memory card. Without touch, open Contents › Look up a word and pick
the word with the side buttons.

## What Vellum reads

StarDict dictionaries. A StarDict dictionary is three files with the same
name:

| File | What it is |
| --- | --- |
| `name.ifo` | a short description (name, number of words) |
| `name.idx` | the word list |
| `name.dict` or `name.dict.dz` | the definitions (`.dz` is compressed; both work) |

Put all three in the card's `dictionaries` folder, either directly or in a
folder of their own inside it. Or send them from the phone: open
**Add books** on the reader and choose the three files on the upload page.

Vellum does not read a compressed word list (`name.idx.gz`): unpack it
first (`gunzip name.idx.gz`, or any unzip app). A `name.syn` file, if there
is one, is ignored. Download archives (`.tar.gz`, `.tar.xz`, `.tar.bz2`,
`.zip`) must be unpacked on a computer before the files go on the card.

## Which dictionary for which book

Vellum looks a word up in the dictionaries for the book's language first
(from the EPUB's `dc:language`), then in those for the menu language, then
in any other. It knows a dictionary's language from its folder or file
name: put English ones in `dictionaries/en/` and Lithuanian ones in
`dictionaries/lt/`, or start the file name with the language
(`en-wordnet.ifo`, `lt-en.ifo`). Words such as `eng`, `english`, `lit`
and `lithuanian` in the name work too.

Words are tried as printed, without punctuation, and in lowercase. For
English, common endings are also taken off (`studies` → `study`,
`stopped` → `stop`, `making` → `make`, `Alice’s` → `alice`). Lithuanian
words are tried as printed and in lowercase only, so a dictionary that
lists inflected forms finds more.

The first lookup in a large dictionary takes a few seconds while Vellum
writes a small index of it to `vellum/dictionaries` on the card; later
lookups are quick.

## Ready-made dictionaries

`tools/make_dictionaries.py` builds four dictionaries made for Vellum, from
free word lists:

| Files | What it is | Words |
| --- | --- | --- |
| `english` | English words explained in English (Wiktionary) | 574,000 with forms |
| `english-lithuanian` | Lithuanian words for English ones (FreeDict) | 6,000 |
| `lithuanian-english` | Lithuanian words explained in English, with their forms (Wiktionary) | 7,000 words, 130,000 forms |
| `lithuanian-english-freedict` | Lithuanian words in English, a second list (FreeDict) | 7,000 |

Copy the `dictionaries` folder it makes to the card as it is (about 32 MB;
the English one is 29 MB of it, leave it out if you do not need it).
`SOURCES.txt` in that folder names the sources and licences.

They differ from the downloads below in ways that suit the reader:

- **Word forms are words of their own.** Vellum ignores `.syn` files, where
  most dictionaries keep forms, so here `Afrikoje`, `šuniui` and `ėjo`
  find `Afrika`, `šuo` and `eiti`, and `went` and `mice` find `go` and
  `mouse`. Lithuanian forms are stored without stress marks, as books
  print them.
- **Short, plain text.** Each meaning is numbered and the text stops at a
  whole meaning before the 4 KB the reader shows.

For an English book the English one answers first; the
English–Lithuanian one only when it does not have the word. To get
Lithuanian words first instead, leave `english.*` off the card.

To build them again (needs git and Python 3):

```sh
python3 tools/make_dictionaries.py --fetch sources out
```

## Free dictionaries

None of these are bundled with Vellum; download them yourself. Check each
licence if you pass the files on.

### English

- **Wiktionary, English** (English words explained in English), StarDict
  build from the [Wiktionary-Dictionaries](https://github.com/Vuizur/Wiktionary-Dictionaries)
  project. Large and current. Licence: CC BY-SA 3.0 and GFDL, like
  Wiktionary itself.
- **WordNet 3.0** (Princeton). A thesaurus-like dictionary of English
  words, meanings and examples, often packaged for StarDict as
  "WordNet". Licence: the WordNet licence, which allows free use and
  copying with the copyright notice kept
  ([licence](https://wordnet.princeton.edu/node/10)).
- **FreeDict English–Lithuanian** (`eng-lit`, about 6,000 words), from
  [freedict.org/downloads](https://freedict.org/downloads/) in StarDict
  format. Gives Lithuanian words for English ones. Licence: GPL 2 or
  later.

### Lithuanian

- **FreeDict Lithuanian–English** (`lit-eng`, about 7,000 words), from
  [freedict.org/downloads](https://freedict.org/downloads/). Explains
  Lithuanian words in English. Licence: GPL 2 or later.
- **Wiktionary, Lithuanian** (Lithuanian words explained in English, with
  their inflected forms), from
  [Wiktionary-Dictionaries](https://github.com/Vuizur/Wiktionary-Dictionaries).
  Licence: CC BY-SA 3.0 and GFDL.

There is no free Lithuanian–Lithuanian explanatory dictionary we know of
in StarDict form; the *Dabartinės lietuvių kalbos žodynas* is not free to
copy.

## For developers

The code is in `components/vellum_core/src/dictionary.cpp`. Tests build
small dictionaries of their own; `tests/data/make_sample_dictionary.py`
writes the dictzip sample in `tests/data/dictionary`.
