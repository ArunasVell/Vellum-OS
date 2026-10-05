#!/usr/bin/env python3
"""Writes the small FictionBook and plain-text books the host tests read.

sample.fb2 is UTF-8 with what real FB2 files have: an author in parts, an
annotation, a PNG cover as base64, a body title, nested sections (one
without a title), a poem, emphasis, an empty line, a footnote reference and
a notes body. sample.fb2.zip is the same book zipped. sample-1257.txt is
Lithuanian in Windows-1257 with "N skyrius" headings, wrapped at a fixed
width like old text files. Needs Pillow.
"""
import base64
import io
import zipfile

from PIL import Image, ImageDraw


def cover():
    """A test card: white top half, black bottom half."""
    img = Image.new("L", (60, 90), 255)
    ImageDraw.Draw(img).rectangle([0, 45, 60, 90], fill=0)
    buf = io.BytesIO()
    img.save(buf, "PNG")
    return buf.getvalue()


def wrap(b64, width=76):
    return "\n".join(b64[i:i + width] for i in range(0, len(b64), width))


FB2 = """<?xml version="1.0" encoding="UTF-8"?>
<FictionBook xmlns="http://www.gribuser.ru/xml/fictionbook/2.0" xmlns:l="http://www.w3.org/1999/xlink">
<description>
 <title-info>
  <genre>prose</genre>
  <author><first-name>Arūnas</first-name><middle-name>T.</middle-name><last-name>Testeris</last-name></author>
  <author><first-name>Second</first-name><last-name>Author</last-name></author>
  <book-title>Vellum &amp; FB2</book-title>
  <annotation><p>Trumpas <emphasis>aprašymas</emphasis>.</p><p>Antra pastraipa.</p></annotation>
  <coverpage><image l:href="#cover.png"/></coverpage>
  <lang>lt</lang>
 </title-info>
 <src-title-info><book-title>Original</book-title><lang>en</lang></src-title-info>
</description>
<body>
 <title><p>Vellum &amp; FB2</p></title>
 <section>
  <title><p>Pirmas skyrius</p><p>Kelionė</p></title>
  <p>Labas, <strong>pasauli</strong>!<a l:href="#n1" type="note">1</a></p>
  <empty-line/>
  <section>
   <title><p>Eilėraštis</p></title>
   <poem><stanza><v>pirma eilė</v><v>antra eilė</v></stanza></poem>
  </section>
 </section>
 <section>
  <p>Be pavadinimo.</p>
 </section>
 <section>
  <title><p>Antras skyrius</p></title>
  <subtitle>* * *</subtitle>
  <p>Paskutinis „sakinys“.</p>
 </section>
</body>
<body name="notes">
 <title><p>Pastabos</p></title>
 <section id="n1"><title><p>1</p></title><p>Pastaba.</p></section>
</body>
<binary id="cover.png" content-type="image/png">
COVER
</binary>
</FictionBook>
"""

TEXT_1257 = """Vellum bandymas

1 skyrius
Kelionė

Tai buvo graži diena, ir žmonės ėjo
į turgų pirkti šviežios duonos.

    Antra pastraipa prasideda įtrauka
ir tęsiasi čia.

2 skyrius

Ąžuolas žaliavo prie upės, o vėjas
šiureno lapus.
"""


def main():
    fb2 = FB2.replace("COVER", wrap(base64.b64encode(cover()).decode()))
    with open("sample.fb2", "w", encoding="utf-8") as f:
        f.write(fb2)
    with zipfile.ZipFile("sample.fb2.zip", "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("sample.fb2", fb2.encode("utf-8"))
    with open("sample-1257.txt", "wb") as f:
        f.write(TEXT_1257.replace("\n", "\r\n").encode("cp1257"))


if __name__ == "__main__":
    main()
